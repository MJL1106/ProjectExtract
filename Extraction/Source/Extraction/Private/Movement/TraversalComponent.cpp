// Copyright Epic Games, Inc. All Rights Reserved.

#include "TraversalComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/CompanionAnimInstance.h"
#include "Animation/ExtractionAnimInstance.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY(LogTraversal);

static FString OwnerTag(const ACharacter* Char)
{
	return Char ? Char->GetName() : TEXT("(null)");
}

namespace TraversalConstants
{
	static constexpr float VaultLedgeTraceForwardOffset = 10.0f;
	static constexpr float TraversalHeightTraceBuffer = 50.0f;
	static constexpr float VaultWallFacingDotThreshold = 0.5f;
	static constexpr float VaultSnapDuration = 0.15f;
	static constexpr float ClearanceBufferOffset = 5.f;
	static constexpr float SurfaceSkinOffset = 2.f;
	static constexpr float DropThreshold = 30.f;
}

UTraversalComponent::UTraversalComponent()
	: ActiveTraversalType(ETraversalType::None)
	, bWasSprintingAtTraversalEntry(false)
	, VaultTargetLocation(FVector::ZeroVector)
	, VaultSurfaceLocation(FVector::ZeroVector)
	, VaultWallNormal(FVector::ZeroVector)
	, VaultWallImpactPoint(FVector::ZeroVector)
	, VaultSurfaceHeight(0.f)
	, VaultSnapTarget(FVector::ZeroVector)
	, bIsSnappingToVault(false)
	, VaultSnapTimeRemaining(0.f)
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	SetIsReplicatedByDefault(true);
}

void UTraversalComponent::BeginPlay()
{
	Super::BeginPlay();

	OwningCharacter = Cast<ACharacter>(GetOwner());
	if (!IsValid(OwningCharacter))
	{
		UE_LOG(LogTraversal, Warning, TEXT("TraversalComponent on '%s' — owner is not ACharacter."), *GetNameSafe(GetOwner()));
		return;
	}

	CachedMovement = OwningCharacter->GetCharacterMovement();
	CachedCapsule = OwningCharacter->GetCapsuleComponent();
}

void UTraversalComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
		World->GetTimerManager().ClearTimer(WorstCaseTraversalEndHandle);

	Super::EndPlay(EndPlayReason);
}

void UTraversalComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!IsValid(OwningCharacter)) return;
	if (!OwningCharacter->IsLocallyControlled() && !OwningCharacter->HasAuthority()) return;

	if (IsInTraversal())
		UpdateTraversal(DeltaTime);
}

void UTraversalComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION(UTraversalComponent, ActiveTraversalType, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(UTraversalComponent, bWasSprintingAtTraversalEntry, COND_SkipOwner);
}

// ---- Public API ----

bool UTraversalComponent::TryStartTraversal(bool bWasSprinting)
{
	UE_LOG(LogTraversal, Verbose, TEXT("[TryStartTraversal] %s - start. Sprint=%d, IsInTraversal=%d, IsFalling=%d"),
		*OwnerTag(OwningCharacter), bWasSprinting ? 1 : 0,
		IsInTraversal() ? 1 : 0,
		(CachedMovement && CachedMovement->IsFalling()) ? 1 : 0);

	if (IsInTraversal()) return false;

	const ETraversalType DetectedType = PerformTraversalDetection();

	if (DetectedType == ETraversalType::None)
	{
		UE_LOG(LogTraversal, Log, TEXT("[TryStartTraversal] %s - no traversal possible"), *OwnerTag(OwningCharacter));
		return false;
	}

	UE_LOG(LogTraversal, Log, TEXT("[TryStartTraversal] %s - selected type=%d, starting"),
		*OwnerTag(OwningCharacter), (int32)DetectedType);
	bWasSprintingAtTraversalEntry = bWasSprinting;
	ExecuteByType(DetectedType, bWasSprinting);
	return true;
}

bool UTraversalComponent::DetectTraversalAhead(FVector& OutSnapTarget, ETraversalType& OutType)
{
	OutType = PerformTraversalDetection();
	if (OutType == ETraversalType::None) return false;

	OutSnapTarget = VaultTargetLocation;
	return true;
}

void UTraversalComponent::CancelTraversal()
{
	if (IsInTraversal())
		EndTraversal();
}

bool UTraversalComponent::TryStartTraversalFromNavLink(ETraversalType Type, const FVector& Start, const FVector& End, float PlayRate)
{
	if (Type == ETraversalType::None)
	{
		UE_LOG(LogTraversal, Warning, TEXT("[NavLinkTraversal] %s - Type=None rejected"), *OwnerTag(OwningCharacter));
		return false;
	}

	if (!IsValid(OwningCharacter) || !IsValid(CachedCapsule) || !IsValid(CachedMovement))
	{
		UE_LOG(LogTraversal, Warning, TEXT("[NavLinkTraversal] %s - missing owner/capsule/movement"), *OwnerTag(OwningCharacter));
		return false;
	}

	if (IsInTraversal())
	{
		UE_LOG(LogTraversal, Log, TEXT("[NavLinkTraversal] %s - already in traversal, skip"), *OwnerTag(OwningCharacter));
		return false;
	}

	// Verify the owning character has a montage for this traversal type — otherwise
	// StartTraversal would put the character in MOVE_Flying / no-collision with no
	// montage end delegate to fire EndTraversal. Caller relies on this returning false
	// to fall back to teleport. DropDown intentionally has no montage today.
	bool bHasMontage = false;
	if (USkeletalMeshComponent* MeshComp = OwningCharacter->GetMesh())
	{
		if (UAnimInstance* AnimInst = MeshComp->GetAnimInstance())
		{
			if (const UCompanionAnimInstance* CompanionAnim = Cast<UCompanionAnimInstance>(AnimInst))
				bHasMontage = CompanionAnim->HasMontageForType(Type);
			else if (const UExtractionAnimInstance* ExtractionAnim = Cast<UExtractionAnimInstance>(AnimInst))
				bHasMontage = ExtractionAnim->HasMontageForType(Type);
		}
	}
	if (!bHasMontage)
	{
		UE_LOG(LogTraversal, Warning, TEXT("[NavLinkTraversal] %s - no montage configured for Type=%d, refusing — caller should teleport"),
			*OwnerTag(OwningCharacter), (int32)Type);
		return false;
	}

	// Build pawn-ignore list once for the clearance test below.
	FCollisionQueryParams ClearParams;
	BuildPawnIgnoreParams(ClearParams);

	// Short clearance check at End so we don't drop into a wall.
	const float CapsuleRadius = CachedCapsule->GetScaledCapsuleRadius();
	const float CapsuleHalfHeight = CachedCapsule->GetScaledCapsuleHalfHeight();
	const FVector ClearTest(End.X, End.Y, End.Z + CapsuleHalfHeight + TraversalConstants::SurfaceSkinOffset);
	const FCollisionShape ClearShape = FCollisionShape::MakeCapsule(CapsuleRadius, CapsuleHalfHeight);

	const bool bBlocked = GetWorld()->OverlapAnyTestByChannel(
		ClearTest, FQuat::Identity, ECC_WorldStatic, ClearShape, ClearParams);
	if (bBlocked)
	{
		UE_LOG(LogTraversal, Warning, TEXT("[NavLinkTraversal] %s - End clearance blocked at %s"),
			*OwnerTag(OwningCharacter), *ClearTest.ToCompactString());
		return false;
	}

	// Populate runtime state that StartTraversal / UpdateTraversal expect.
	const FVector PathDir = (End - Start).GetSafeNormal();
	VaultWallImpactPoint = Start;
	VaultWallNormal = PathDir.IsNearlyZero() ? -OwningCharacter->GetActorForwardVector() : -PathDir;
	VaultSurfaceLocation = Start;
	VaultTargetLocation = End;
	VaultSurfaceHeight = FMath::Abs(End.Z - Start.Z);

	bWasSprintingAtTraversalEntry = false;

	// StartTraversal sets ActiveTraversalType, switches to MOVE_Flying, disables capsule
	// collision, and primes the snap interp — same setup the trace path uses.
	StartTraversal(Type);

	UE_LOG(LogTraversal, Log, TEXT("[NavLinkTraversal] %s - started Type=%d Start=%s End=%s PlayRate=%.2f"),
		*OwnerTag(OwningCharacter), (int32)Type,
		*Start.ToCompactString(), *End.ToCompactString(), PlayRate);

	// Broadcast — owner's AnimInstance plays the montage and binds the end-delegate that
	// eventually calls EndTraversal(); CompanionAIController writes BB keys.
	OnTraversalStarted.Broadcast(Type, PlayRate, VaultSurfaceLocation, VaultTargetLocation);
	return true;
}

void UTraversalComponent::ExecuteByType(ETraversalType Type, bool bWasSprinting)
{
	if (Type == ETraversalType::None) return;

	bWasSprintingAtTraversalEntry = bWasSprinting;

	StartTraversal(Type);

	float PlayRate = 1.0f;
	switch (Type)
	{
	case ETraversalType::Vault:
		PlayRate = bWasSprinting ? VaultSprintPlayRate : VaultWalkPlayRate;
		break;
	case ETraversalType::Climb:
		PlayRate = bWasSprinting ? ClimbSprintPlayRate : ClimbWalkPlayRate;
		break;
	case ETraversalType::Mantle:
		PlayRate = bWasSprinting ? MantleSprintPlayRate : MantleWalkPlayRate;
		break;
	case ETraversalType::DropDown:
	case ETraversalType::Jump:
	case ETraversalType::SprintJump:
		// Trace-detection never selects these — nav-link path uses TryStartTraversalFromNavLink.
		break;
	default:
		break;
	}

	OnTraversalStarted.Broadcast(Type, PlayRate, VaultSurfaceLocation, VaultTargetLocation);
}

// ---- Detection ----

ETraversalType UTraversalComponent::PerformTraversalDetection()
{
	UE_LOG(LogTraversal, Verbose, TEXT("[Detection] %s - entering"), *OwnerTag(OwningCharacter));

	if (!IsValid(OwningCharacter) || !IsValid(CachedCapsule))
		return ETraversalType::None;

	// Build the pawn-ignore list ONCE — the forward sweep, down trace, and clearance
	// tests all need the same exclusions. Previously each call iterated TActorIterator<APawn>.
	FCollisionQueryParams IgnoreParams;
	BuildPawnIgnoreParams(IgnoreParams);

	FHitResult WallHit;
	const bool bHitWall = TraceForwardForWall(WallHit, IgnoreParams);
	UE_LOG(LogTraversal, Verbose, TEXT("[Detection] %s - forward wall trace: hit=%d"),
		*OwnerTag(OwningCharacter), bHitWall ? 1 : 0);

	if (!bHitWall) return ETraversalType::None;

	if (IsValid(WallHit.GetActor()) && WallHit.GetActor()->IsA(APawn::StaticClass()))
		return ETraversalType::None;

	const FVector Forward = OwningCharacter->GetActorForwardVector();
	const float FacingDot = FVector::DotProduct(WallHit.ImpactNormal, -Forward);
	UE_LOG(LogTraversal, Verbose, TEXT("[Detection] %s - facing dot=%.2f (threshold=%.2f)"),
		*OwnerTag(OwningCharacter), FacingDot, TraversalConstants::VaultWallFacingDotThreshold);

	if (FacingDot < TraversalConstants::VaultWallFacingDotThreshold)
		return ETraversalType::None;

	FHitResult SurfaceHit;
	const bool bHitSurface = TraceDownForSurface(WallHit, SurfaceHit, IgnoreParams);
	UE_LOG(LogTraversal, Verbose, TEXT("[Detection] %s - down surface trace: hit=%d"),
		*OwnerTag(OwningCharacter), bHitSurface ? 1 : 0);

	if (!bHitSurface) return ETraversalType::None;

	const float CapsuleRadius = CachedCapsule->GetScaledCapsuleRadius();
	const float CapsuleHalfHeight = CachedCapsule->GetScaledCapsuleHalfHeight();
	const float FeetZ = OwningCharacter->GetActorLocation().Z - CapsuleHalfHeight;

	VaultWallNormal = WallHit.ImpactNormal;
	VaultWallImpactPoint = WallHit.ImpactPoint;
	VaultSurfaceLocation = SurfaceHit.ImpactPoint;
	VaultSurfaceHeight = SurfaceHit.ImpactPoint.Z - FeetZ;

	UE_LOG(LogTraversal, Verbose, TEXT("[Detection] %s - height calc: feet=%.1f, surfaceTop=%.1f, height=%.1f"),
		*OwnerTag(OwningCharacter), FeetZ, SurfaceHit.ImpactPoint.Z, VaultSurfaceHeight);

	const float SurfOnTopOffset = CapsuleRadius + TraversalConstants::ClearanceBufferOffset;

	auto SetTargetLocation = [&](float Offset)
	{
		VaultTargetLocation = SurfaceHit.ImpactPoint + Forward * Offset;
		VaultTargetLocation.Z = SurfaceHit.ImpactPoint.Z + CapsuleHalfHeight;
	};

	// Vault range
	if (VaultSurfaceHeight >= VaultMinHeight && VaultSurfaceHeight <= VaultMaxHeight)
	{
		const bool bVaultClear = CheckClearance(SurfaceHit.ImpactPoint, VaultLandingForwardOffset, IgnoreParams);
		UE_LOG(LogTraversal, Verbose, TEXT("[Detection] %s - vault clearance ok=%d"),
			*OwnerTag(OwningCharacter), bVaultClear ? 1 : 0);
		if (bVaultClear)
		{
			bool bPreferClimb = false;
			if (VaultSurfaceHeight >= ClimbMinHeight)
			{
				const FVector DropStart(
					SurfaceHit.ImpactPoint.X + Forward.X * VaultLandingForwardOffset,
					SurfaceHit.ImpactPoint.Y + Forward.Y * VaultLandingForwardOffset,
					SurfaceHit.ImpactPoint.Z + 10.f);
				const FVector DropEnd(DropStart.X, DropStart.Y, FeetZ - 10.f);

				FHitResult DropHit;
				const bool bHitGround = GetWorld()->LineTraceSingleByChannel(
					DropHit, DropStart, DropEnd, ECC_Visibility, IgnoreParams);

				bPreferClimb = bHitGround &&
					(DropHit.ImpactPoint.Z > SurfaceHit.ImpactPoint.Z - TraversalConstants::DropThreshold);
			}

			if (!bPreferClimb)
			{
				UE_LOG(LogTraversal, Log, TEXT("[Detection] %s - selected type=Vault"), *OwnerTag(OwningCharacter));
				SetTargetLocation(VaultLandingForwardOffset);
				return ETraversalType::Vault;
			}
		}

		if (VaultSurfaceHeight >= ClimbMinHeight)
		{
			const bool bClimbClear = CheckClearance(SurfaceHit.ImpactPoint, SurfOnTopOffset, IgnoreParams);
			if (bClimbClear)
			{
				UE_LOG(LogTraversal, Log, TEXT("[Detection] %s - selected type=Climb (vault-fallback)"), *OwnerTag(OwningCharacter));
				SetTargetLocation(SurfOnTopOffset);
				return ETraversalType::Climb;
			}
		}
	}

	// Climb-only range
	if (VaultSurfaceHeight > VaultMaxHeight && VaultSurfaceHeight <= ClimbMaxHeight)
	{
		const bool bClimbClear = CheckClearance(SurfaceHit.ImpactPoint, SurfOnTopOffset, IgnoreParams);
		if (bClimbClear)
		{
			UE_LOG(LogTraversal, Log, TEXT("[Detection] %s - selected type=Climb"), *OwnerTag(OwningCharacter));
			SetTargetLocation(SurfOnTopOffset);
			return ETraversalType::Climb;
		}
	}

	// Mantle range
	if (VaultSurfaceHeight > ClimbMaxHeight && VaultSurfaceHeight <= MantleMaxHeight)
	{
		const bool bMantleClear = CheckClearance(SurfaceHit.ImpactPoint, SurfOnTopOffset, IgnoreParams);
		if (bMantleClear)
		{
			UE_LOG(LogTraversal, Log, TEXT("[Detection] %s - selected type=Mantle"), *OwnerTag(OwningCharacter));
			SetTargetLocation(SurfOnTopOffset);
			return ETraversalType::Mantle;
		}
	}

	UE_LOG(LogTraversal, Log, TEXT("[Detection] %s - no band matched (height=%.1f)"),
		*OwnerTag(OwningCharacter), VaultSurfaceHeight);
	return ETraversalType::None;
}

void UTraversalComponent::BuildPawnIgnoreParams(FCollisionQueryParams& OutParams) const
{
	if (IsValid(OwningCharacter))
		OutParams.AddIgnoredActor(OwningCharacter);

	// Ignore all other pawns so player-on-obstacle / other AI bodies don't poison
	// trace results (notably the down-trace finding a pawn capsule's top instead
	// of the wall surface).
	if (UWorld* TraceWorld = GetWorld())
	{
		for (TActorIterator<APawn> It(TraceWorld); It; ++It)
		{
			if (*It && *It != OwningCharacter)
				OutParams.AddIgnoredActor(*It);
		}
	}
}

bool UTraversalComponent::TraceForwardForWall(FHitResult& OutHit, const FCollisionQueryParams& IgnoreParams) const
{
	if (!IsValid(CachedCapsule)) return false;

	const float CapsuleRadius = CachedCapsule->GetScaledCapsuleRadius();
	const float CapsuleHalfHeight = CachedCapsule->GetScaledCapsuleHalfHeight();
	const FVector Forward = OwningCharacter->GetActorForwardVector();

	const float FeetZ = OwningCharacter->GetActorLocation().Z - CapsuleHalfHeight;
	const float TraceHeight = FeetZ + VaultForwardTraceHeight;

	FVector Start = OwningCharacter->GetActorLocation() + Forward * CapsuleRadius;
	Start.Z = TraceHeight;
	const FVector End = Start + Forward * VaultForwardTraceDistance;

	const FCollisionShape SweepShape = FCollisionShape::MakeSphere(VaultForwardTraceRadius);

	const bool bHit = GetWorld()->SweepSingleByChannel(
		OutHit, Start, End, FQuat::Identity,
		ECC_Visibility, SweepShape, IgnoreParams);

	UE_LOG(LogTraversal, Verbose, TEXT("[ForwardTrace] %s - Start=%s, End=%s, Radius=%.1f, ResultHit=%d"),
		*OwnerTag(OwningCharacter), *Start.ToCompactString(), *End.ToCompactString(), VaultForwardTraceRadius, bHit ? 1 : 0);

#if ENABLE_DRAW_DEBUG
	if (bDrawDebugTraces)
	{
		const FColor LineColor = bHit ? FColor::Green : FColor::Red;
		DrawDebugLine(GetWorld(), Start, End, LineColor, false, DebugTraceDuration, 0, 1.0f);
		DrawDebugSphere(GetWorld(), Start, VaultForwardTraceRadius, 12, FColor::Yellow, false, DebugTraceDuration, 0, 1.0f);
		DrawDebugSphere(GetWorld(), End, VaultForwardTraceRadius, 12, LineColor, false, DebugTraceDuration, 0, 1.0f);
		if (bHit)
			DrawDebugPoint(GetWorld(), OutHit.ImpactPoint, 12.0f, FColor::Cyan, false, DebugTraceDuration, 0);
	}
#endif

	return bHit;
}

bool UTraversalComponent::TraceDownForSurface(const FHitResult& WallHit, FHitResult& OutSurfaceHit, const FCollisionQueryParams& IgnoreParams) const
{
	if (!IsValid(CachedCapsule)) return false;

	const float CapsuleHalfHeight = CachedCapsule->GetScaledCapsuleHalfHeight();
	const FVector Forward = OwningCharacter->GetActorForwardVector();
	const float FeetZ = OwningCharacter->GetActorLocation().Z - CapsuleHalfHeight;

	const FVector LedgeOrigin = WallHit.ImpactPoint
		+ Forward * TraversalConstants::VaultLedgeTraceForwardOffset;

	const float MaxTraversalHeight = FMath::Max3(VaultMaxHeight, ClimbMaxHeight, MantleMaxHeight);
	const FVector TraceStart(LedgeOrigin.X, LedgeOrigin.Y,
		FeetZ + MaxTraversalHeight + TraversalConstants::TraversalHeightTraceBuffer);
	const FVector TraceEnd(LedgeOrigin.X, LedgeOrigin.Y, FeetZ);

	const bool bHit = GetWorld()->LineTraceSingleByChannel(
		OutSurfaceHit, TraceStart, TraceEnd, ECC_Visibility, IgnoreParams);

	UE_LOG(LogTraversal, Verbose, TEXT("[DownTrace] %s - TraceStart=%s, TraceEnd=%s, ResultHit=%d, ImpactZ=%.1f"),
		*OwnerTag(OwningCharacter), *TraceStart.ToCompactString(), *TraceEnd.ToCompactString(),
		bHit ? 1 : 0, bHit ? OutSurfaceHit.ImpactPoint.Z : 0.f);

#if ENABLE_DRAW_DEBUG
	if (bDrawDebugTraces)
	{
		const FColor LineColor = bHit ? FColor::Green : FColor::Red;
		DrawDebugLine(GetWorld(), TraceStart, TraceEnd, LineColor, false, DebugTraceDuration, 0, 1.0f);
		if (bHit)
		{
			const float SurfaceH = OutSurfaceHit.ImpactPoint.Z - FeetZ;
			const bool bInRange = SurfaceH >= VaultMinHeight && SurfaceH <= MaxTraversalHeight;
			DrawDebugPoint(GetWorld(), OutSurfaceHit.ImpactPoint, 14.0f,
				bInRange ? FColor::Green : FColor::Orange, false, DebugTraceDuration, 0);
			DrawDebugString(GetWorld(), OutSurfaceHit.ImpactPoint + FVector(0, 0, 20),
				FString::Printf(TEXT("H=%.0fcm"), SurfaceH), nullptr,
				bInRange ? FColor::Green : FColor::Orange, DebugTraceDuration, false);
		}
	}
#endif

	if (!bHit) return false;

	const float SurfaceHeight = OutSurfaceHit.ImpactPoint.Z - FeetZ;
	return SurfaceHeight >= VaultMinHeight && SurfaceHeight <= MaxTraversalHeight;
}

bool UTraversalComponent::CheckClearance(const FVector& SurfaceLocation, float ForwardOffset, const FCollisionQueryParams& IgnoreParams) const
{
	if (!IsValid(CachedCapsule)) return false;

	const float CapsuleRadius = CachedCapsule->GetScaledCapsuleRadius();
	const float CapsuleHalfHeight = CachedCapsule->GetScaledCapsuleHalfHeight();
	const FVector Forward = OwningCharacter->GetActorForwardVector();

	const FVector TestLocation(
		SurfaceLocation.X + Forward.X * ForwardOffset,
		SurfaceLocation.Y + Forward.Y * ForwardOffset,
		SurfaceLocation.Z + CapsuleHalfHeight + TraversalConstants::SurfaceSkinOffset);

	const FCollisionShape TestShape = FCollisionShape::MakeCapsule(
		CapsuleRadius, CapsuleHalfHeight);

	const bool bBlocked = GetWorld()->OverlapAnyTestByChannel(
		TestLocation, FQuat::Identity,
		ECC_WorldStatic, TestShape, IgnoreParams);

	UE_LOG(LogTraversal, Verbose, TEXT("[Clearance] %s - SurfaceLoc=%s, ForwardOffset=%.1f, TestLoc=%s, Blocked=%d"),
		*OwnerTag(OwningCharacter), *SurfaceLocation.ToCompactString(), ForwardOffset,
		*TestLocation.ToCompactString(), bBlocked ? 1 : 0);

	return !bBlocked;
}

// ---- Traversal Execution ----

void UTraversalComponent::StartTraversal(ETraversalType Type)
{
	VaultLockedRotation = OwningCharacter->GetActorRotation();
	bSavedUseControllerRotationYaw = OwningCharacter->bUseControllerRotationYaw;
	OwningCharacter->bUseControllerRotationYaw = false;

	ActiveTraversalType = Type;
	SetComponentTickEnabled(true);

	if (IsValid(CachedMovement))
	{
		CachedMovement->SetMovementMode(MOVE_Flying);
		CachedMovement->Velocity = FVector::ZeroVector;
	}

	if (IsValid(CachedCapsule))
		CachedCapsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	VaultSnapTarget = VaultWallImpactPoint + VaultWallNormal * VaultSnapDistance;
	VaultSnapTarget.Z = OwningCharacter->GetActorLocation().Z;

	if (Type == ETraversalType::Climb || Type == ETraversalType::Mantle)
	{
		const float RefHeight = (Type == ETraversalType::Climb)
			? ClimbAnimReferenceHeight
			: MantleAnimReferenceHeight;
		const float HeightBoost = FMath::Max(0.f, VaultSurfaceHeight - RefHeight);
		VaultSnapTarget.Z += HeightBoost;
	}

	bIsSnappingToVault = true;
	VaultSnapTimeRemaining = TraversalConstants::VaultSnapDuration;

	// Worst-case escape timer — if the montage end delegate never fires (asset broken,
	// montage interrupted by another animation, etc.), force-end the traversal so the
	// character can't be stranded in MOVE_Flying + no-collision forever. Cleared in
	// EndTraversal if the montage finishes normally first.
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(WorstCaseTraversalEndHandle);
		World->GetTimerManager().SetTimer(
			WorstCaseTraversalEndHandle, this, &UTraversalComponent::EndTraversal,
			WorstCaseTraversalDuration, false);
	}
}

void UTraversalComponent::UpdateTraversal(float DeltaTime)
{
	OwningCharacter->SetActorRotation(VaultLockedRotation);

	if (bIsSnappingToVault)
	{
		VaultSnapTimeRemaining -= DeltaTime;
		if (VaultSnapTimeRemaining <= 0.f)
		{
			bIsSnappingToVault = false;
		}
		else
		{
			const FVector Current = OwningCharacter->GetActorLocation();
			const FVector NewLoc = FMath::VInterpTo(Current, VaultSnapTarget, DeltaTime, VaultSnapInterpSpeed);
			OwningCharacter->SetActorLocation(NewLoc);

			if (FVector::DistSquared(NewLoc, VaultSnapTarget) < 1.f)
				bIsSnappingToVault = false;
		}
	}

	// Owner is responsible for calling EndTraversal() when the traversal montage finishes.
	// See AExtractionCharacter::OnTraversalMontageEnded and ACompanionCharacter::OnTraversalMontageEnded
	// — both bind a per-montage Montage_SetEndDelegate after playing the traversal montage.
}

void UTraversalComponent::EndTraversal()
{
	if (ActiveTraversalType == ETraversalType::None) return;

	if (UWorld* World = GetWorld())
		World->GetTimerManager().ClearTimer(WorstCaseTraversalEndHandle);

	ActiveTraversalType = ETraversalType::None;
	bIsSnappingToVault = false;
	SetComponentTickEnabled(false);

	OwningCharacter->bUseControllerRotationYaw = bSavedUseControllerRotationYaw;

	OwningCharacter->SetActorLocation(OwningCharacter->GetActorLocation(), true);

	if (IsValid(CachedCapsule))
		CachedCapsule->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);

	if (IsValid(CachedMovement))
		CachedMovement->SetMovementMode(MOVE_Walking);

	OnTraversalEnded.Broadcast();
}

void UTraversalComponent::OnRep_TraversalType()
{
	if (ActiveTraversalType == ETraversalType::None) return;

	float PlayRate = 1.0f;
	switch (ActiveTraversalType)
	{
	case ETraversalType::Vault:
		PlayRate = bWasSprintingAtTraversalEntry ? VaultSprintPlayRate : VaultWalkPlayRate;
		break;
	case ETraversalType::Climb:
		PlayRate = bWasSprintingAtTraversalEntry ? ClimbSprintPlayRate : ClimbWalkPlayRate;
		break;
	case ETraversalType::Mantle:
		PlayRate = bWasSprintingAtTraversalEntry ? MantleSprintPlayRate : MantleWalkPlayRate;
		break;
	default:
		break;
	}

	// NOTE: VaultSurfaceLocation/VaultTargetLocation are not replicated; on remote clients these
	// will be zero. Mirror feature binds to the locally-controlled player only (see plan §1).
	OnTraversalStarted.Broadcast(ActiveTraversalType, PlayRate, VaultSurfaceLocation, VaultTargetLocation);
}
