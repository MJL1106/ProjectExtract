// Copyright Epic Games, Inc. All Rights Reserved.

#include "TraversalComponent.h"
#include "AIController.h"
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
#include "Perception/AISense_Hearing.h"
#include "TimerManager.h"
#include "World/DoorBase.h"

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
	/** Max 2D distance between the forward-trace hit and the nav-link Start point.
	 *  Hits beyond this are from a different obstacle/prop and trigger the endpoint fallback. */
	static constexpr float NavLinkWallMatchTolerance = 120.f;
	/** Seconds between wall-detection polls during the approach phase. */
	static constexpr float NavLinkPollInterval = 0.1f;
	/** Seconds after a teleport-abort before this link can re-enter the approach phase. */
	static constexpr float NavLinkReentryCooldown = 1.0f;
	/** Max downward distance (cm) the floor-snap searches for a surface below the landing position. */
	static constexpr float FloorSnapMaxDrop = 150.f;
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
	bNavLinkApproaching = false;

	if (UWorld* World = GetWorld())
		World->GetTimerManager().ClearTimer(WorstCaseTraversalEndHandle);

	Super::EndPlay(EndPlayReason);
}

void UTraversalComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!IsValid(OwningCharacter)) return;
	if (!OwningCharacter->IsLocallyControlled() && !OwningCharacter->HasAuthority()) return;

	if (bNavLinkApproaching) { UpdateNavLinkApproach(DeltaTime); return; }
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
		UE_LOG(LogTraversal, Verbose, TEXT("[TryStartTraversal] %s - no traversal possible"), *OwnerTag(OwningCharacter));
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
	UE_LOG(LogTraversal, Verbose, TEXT("[VAULT_DEBUG] DetectTraversalAhead start on owner=%s"), *GetNameSafe(GetOwner()));

	OutType = PerformTraversalDetection();
	const bool bResult = (OutType != ETraversalType::None);
	if (bResult) OutSnapTarget = VaultTargetLocation;

	UE_LOG(LogTraversal, Verbose, TEXT("[VAULT_DEBUG] DetectTraversalAhead result=%s detected_type=%d snap=%s"),
		bResult ? TEXT("true") : TEXT("false"), (int32)OutType, *OutSnapTarget.ToString());

	return bResult;
}

void UTraversalComponent::CancelTraversal()
{
	if (bNavLinkApproaching) { EndNavLinkApproach(false); return; }
	if (IsInTraversal())
		EndTraversal();
}

bool UTraversalComponent::OwnerHasMontageForType(ETraversalType Type) const
{
	if (!IsValid(OwningCharacter)) return false;
	USkeletalMeshComponent* MeshComp = OwningCharacter->GetMesh();
	if (!MeshComp) return false;
	UAnimInstance* AnimInst = MeshComp->GetAnimInstance();
	if (!AnimInst) return false;
	if (const UCompanionAnimInstance* CompAnim = Cast<UCompanionAnimInstance>(AnimInst))
		return CompAnim->HasMontageForType(Type);
	if (const UExtractionAnimInstance* ExtrAnim = Cast<UExtractionAnimInstance>(AnimInst))
		return ExtrAnim->HasMontageForType(Type);
	return false;
}

bool UTraversalComponent::TryStartTraversalFromNavLink(ETraversalType Type, const FVector& Start, const FVector& End, float PlayRate, bool bTeleportOnTimeout)
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

	if (IsBusy())
	{
		UE_LOG(LogTraversal, Log, TEXT("[NavLinkTraversal] %s - already busy (approach or traversal), skip"), *OwnerTag(OwningCharacter));
		return false;
	}

	// Re-entry cooldown: if the last approach ended via teleport, refuse re-entry for a short window
	// to prevent a busy-loop on a chronically-failing link.
	if (UWorld* ReentryWorld = GetWorld())
	{
		const float Now = ReentryWorld->GetTimeSeconds();
		if (Now - NavLinkLastAbortTime < TraversalConstants::NavLinkReentryCooldown)
		{
			UE_LOG(LogTraversal, Verbose, TEXT("[NavLinkTraversal] %s - re-entry cooldown (%.2fs remaining), skip"),
				*OwnerTag(OwningCharacter), TraversalConstants::NavLinkReentryCooldown - (Now - NavLinkLastAbortTime));
			return false;
		}
	}

	// Verify the owning character has a montage for this traversal type — otherwise
	// StartTraversal would put the character in MOVE_Flying / no-collision with no
	// montage end delegate to fire EndTraversal. Caller relies on this returning false
	// to fall back to teleport. DropDown intentionally has no montage today.
	if (!OwnerHasMontageForType(Type))
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

	// Pre-flight guards passed — enter the approach phase.
	// The companion walks toward the wall via raw movement input each tick until the wall
	// enters forward-trace range, then we commit the vault (StartTraversal + broadcast).
	// We do NOT vault here — StartTraversal will fire from UpdateNavLinkApproach once the wall is detected.
	NavLinkStart = Start;
	NavLinkEnd = End;
	NavLinkApproachType = Type;
	NavLinkApproachPlayRate = PlayRate;
	bNavLinkTeleportOnTimeout = bTeleportOnTimeout;
	NavLinkApproachElapsed = 0.f;
	NavLinkPollAccumulator = 0.f;

	// Build pawn-ignore params once — reused every poll tick to avoid per-frame TActorIterator.
	NavLinkApproachIgnoreParams = FCollisionQueryParams();
	BuildPawnIgnoreParams(NavLinkApproachIgnoreParams);

	FVector Dir = FVector(End.X - Start.X, End.Y - Start.Y, 0.f).GetSafeNormal();
	if (Dir.IsNearlyZero())
		Dir = OwningCharacter->GetActorForwardVector().GetSafeNormal2D();
	NavLinkApproachDir = Dir;

	if (!Dir.IsNearlyZero())
		OwningCharacter->SetActorRotation(FRotator(0.f, Dir.Rotation().Yaw, 0.f));

	// Suppress the AI controller's desired-rotation so our per-tick SetActorRotation doesn't fight it.
	if (IsValid(CachedMovement))
	{
		bSavedUseControllerDesiredRotation = CachedMovement->bUseControllerDesiredRotation;
		CachedMovement->bUseControllerDesiredRotation = false;
	}
	if (AAIController* AIC = Cast<AAIController>(OwningCharacter->GetController()))
		AIC->ClearFocus(EAIFocusPriority::Gameplay);

	bNavLinkApproaching = true;
	SetComponentTickEnabled(true);

	UE_LOG(LogTraversal, Log, TEXT("[NavLinkTraversal] %s - approach phase started toward wall (Type=%d Start=%s End=%s)"),
		*OwnerTag(OwningCharacter), (int32)Type, *Start.ToCompactString(), *End.ToCompactString());
	return true;
}

void UTraversalComponent::UpdateNavLinkApproach(float DeltaTime)
{
	if (!IsValid(OwningCharacter)) { EndNavLinkApproach(false); return; }

	// Timeout check runs first so a persistently-airborne companion still progresses toward abort.
	NavLinkApproachElapsed += DeltaTime;
	if (NavLinkApproachElapsed > NavLinkApproachTimeout)
	{
		UE_LOG(LogTraversal, Warning, TEXT("[NavLinkTraversal] %s - approach timed out after %.1fs"),
			*OwnerTag(OwningCharacter), NavLinkApproachElapsed);
		EndNavLinkApproach(bNavLinkTeleportOnTimeout);
		return;
	}

	// Movement input runs every frame regardless of poll rate.
	if (!NavLinkApproachDir.IsNearlyZero())
	{
		OwningCharacter->SetActorRotation(FRotator(0.f, NavLinkApproachDir.Rotation().Yaw, 0.f));
		OwningCharacter->AddMovementInput(NavLinkApproachDir, 1.0f);
	}

	// Skip wall detection while airborne — FeetZ math is meaningless mid-fall.
	// A persistently-airborne companion hits the timeout and teleports.
	if (CachedMovement && CachedMovement->IsFalling()) return;

	// Throttle the wall-detection poll to avoid per-frame sphere sweeps.
	NavLinkPollAccumulator += DeltaTime;
	if (NavLinkPollAccumulator < TraversalConstants::NavLinkPollInterval) return;
	NavLinkPollAccumulator = 0.f;

	if (!ResolveNavLinkWallData(NavLinkApproachType, NavLinkStart, NavLinkEnd, NavLinkApproachIgnoreParams))
		return;

	// Re-verify the montage right before committing — it may have been unassigned since
	// the approach started (up to NavLinkApproachTimeout seconds ago).
	if (!OwnerHasMontageForType(NavLinkApproachType))
	{
		UE_LOG(LogTraversal, Warning,
			TEXT("[NavLinkTraversal] %s - montage gone for Type=%d at commit time, aborting to teleport"),
			*OwnerTag(OwningCharacter), (int32)NavLinkApproachType);
		EndNavLinkApproach(bNavLinkTeleportOnTimeout);
		return;
	}

	// Restore the movement rotation flag before StartTraversal takes over its own save/restore.
	if (IsValid(CachedMovement))
		CachedMovement->bUseControllerDesiredRotation = bSavedUseControllerDesiredRotation;

	VaultTargetLocation = NavLinkEnd;
	bWasSprintingAtTraversalEntry = false;
	bNavLinkApproaching = false;
	bCurrentTraversalFromNavLink = true;
	StartTraversal(NavLinkApproachType);
	OnTraversalStarted.Broadcast(NavLinkApproachType, NavLinkApproachPlayRate, VaultSurfaceLocation, VaultTargetLocation);
}

void UTraversalComponent::EndNavLinkApproach(bool bTeleportToEnd)
{
	bNavLinkApproaching = false;

	// Restore the movement rotation flag that was suppressed at approach entry.
	if (IsValid(CachedMovement))
		CachedMovement->bUseControllerDesiredRotation = bSavedUseControllerDesiredRotation;

	if (!IsInTraversal())
		SetComponentTickEnabled(false);

	if (bTeleportToEnd && IsValid(OwningCharacter))
	{
		NavLinkLastAbortTime = GetWorld() ? GetWorld()->GetTimeSeconds() : NavLinkLastAbortTime;
		const bool bTeleportOk = OwningCharacter->TeleportTo(NavLinkEnd, OwningCharacter->GetActorRotation(), false, false);
		if (!bTeleportOk)
			UE_LOG(LogTraversal, Warning, TEXT("[NavLinkTraversal] %s - TeleportTo(%s) failed (destination blocked)"),
				*OwnerTag(OwningCharacter), *NavLinkEnd.ToCompactString());
	}

	// Signal the nav-link callback (bound to OnTraversalEnded) to ResumePathFollowing,
	// even though no montage played in the timeout/abort case.
	OnTraversalEnded.Broadcast();
}

void UTraversalComponent::ExecuteByType(ETraversalType Type, bool bWasSprinting)
{
	UE_LOG(LogTraversal, Verbose, TEXT("[VAULT_DEBUG] ExecuteByType type=%d bIsSprinting=%s on owner=%s"),
		(int32)Type, bWasSprinting ? TEXT("true") : TEXT("false"), *GetNameSafe(GetOwner()));

	if (Type == ETraversalType::None) return;

	bWasSprintingAtTraversalEntry = bWasSprinting;
	bCurrentTraversalFromNavLink = false;

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

	UE_LOG(LogTraversal, Verbose, TEXT("[VAULT_DEBUG] Broadcasting OnTraversalStarted type=%d"), (int32)Type);
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

	bool bHit = GetWorld()->SweepSingleByChannel(
		OutHit, Start, End, FQuat::Identity,
		ECC_Visibility, SweepShape, IgnoreParams);

	// Doors are never traversal targets: a half-open or swinging leaf reads as a thin vaultable
	// wall (pawn pass-through doesn't hide it from this Visibility sweep), and climbing one
	// leaves the character hanging in the doorway when the "obstacle" rotates away.
	if (bHit && OutHit.GetActor() && OutHit.GetActor()->IsA<ADoorBase>())
	{
		UE_LOG(LogTraversal, Verbose, TEXT("[ForwardTrace] %s - hit door %s, rejecting as traversal target"),
			*OwnerTag(OwningCharacter), *GetNameSafe(OutHit.GetActor()));
		bHit = false;
	}

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

	bool bHit = GetWorld()->LineTraceSingleByChannel(
		OutSurfaceHit, TraceStart, TraceEnd, ECC_Visibility, IgnoreParams);

	// Same rule as the forward sweep: a door top is never a landing surface.
	if (bHit && OutSurfaceHit.GetActor() && OutSurfaceHit.GetActor()->IsA<ADoorBase>())
		bHit = false;

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

bool UTraversalComponent::ResolveNavLinkWallData(ETraversalType Type, const FVector& Start, const FVector& End, const FCollisionQueryParams& IgnoreParams)
{
	if (!IsValid(OwningCharacter) || !IsValid(CachedCapsule)) return false;

	// Step 1: Align facing to the link's horizontal travel direction BEFORE tracing so that
	// TraceForwardForWall (which uses GetActorForwardVector() internally) shoots perpendicular
	// to the obstacle.  StartTraversal then snapshots VaultLockedRotation from this orientation,
	// which locks root motion in the correct world direction for the entire traversal.
	const FVector HorizDelta = FVector(End.X - Start.X, End.Y - Start.Y, 0.f);
	if (!HorizDelta.IsNearlyZero())
	{
		const FRotator FaceDir(0.f, HorizDelta.Rotation().Yaw, 0.f);
		OwningCharacter->SetActorRotation(FaceDir);
	}

	// Step 2: Run the standard forward wall trace.
	FHitResult WallHit;
	if (!TraceForwardForWall(WallHit, IgnoreParams))
	{
		UE_LOG(LogTraversal, Verbose, TEXT("[NavLinkTraversal] %s - forward trace missed — using endpoint fallback"), *OwnerTag(OwningCharacter));
		return false;
	}

	// Reject the hit if it belongs to a different obstacle/prop — a mismatched hit would
	// re-face the actor toward the wrong wall and corrupt the snap basis.
	// We validate against the companion's current position (it has walked up to the wall
	// during the approach phase), not the original link Start which may be far back.
	if (FVector::Dist2D(WallHit.ImpactPoint, OwningCharacter->GetActorLocation()) > TraversalConstants::NavLinkWallMatchTolerance)
	{
		UE_LOG(LogTraversal, Verbose,
			TEXT("[NavLinkTraversal] %s - forward hit (%.0f) too far from current position (tol=%.0f) — using endpoint fallback"),
			*OwnerTag(OwningCharacter),
			FVector::Dist2D(WallHit.ImpactPoint, OwningCharacter->GetActorLocation()),
			TraversalConstants::NavLinkWallMatchTolerance);
		return false;
	}

	// Re-face precisely square to the actual wall normal so root motion starts flush.
	// This second rotation overrides the rough travel-dir facing from Step 1.
	const FVector WallFlatNormal = FVector(WallHit.ImpactNormal.X, WallHit.ImpactNormal.Y, 0.f).GetSafeNormal();
	if (!WallFlatNormal.IsNearlyZero())
	{
		const FRotator SquareToWall(0.f, (-WallFlatNormal).Rotation().Yaw, 0.f);
		OwningCharacter->SetActorRotation(SquareToWall);
	}

	FHitResult SurfaceHit;
	if (!TraceDownForSurface(WallHit, SurfaceHit, IgnoreParams))
	{
		UE_LOG(LogTraversal, Verbose, TEXT("[NavLinkTraversal] %s - down trace missed — using endpoint fallback"), *OwnerTag(OwningCharacter));
		return false;
	}

	// Populate from real geometry — identical to PerformTraversalDetection.
	VaultWallNormal      = WallHit.ImpactNormal;
	VaultWallImpactPoint = WallHit.ImpactPoint;
	VaultSurfaceLocation = SurfaceHit.ImpactPoint;
	const float FeetZ    = OwningCharacter->GetActorLocation().Z - CachedCapsule->GetScaledCapsuleHalfHeight();
	VaultSurfaceHeight   = SurfaceHit.ImpactPoint.Z - FeetZ;

	UE_LOG(LogTraversal, Log, TEXT("[NavLinkTraversal] %s - real geometry resolved: wallNorm=%s surfZ=%.1f height=%.1f"),
		*OwnerTag(OwningCharacter), *VaultWallNormal.ToCompactString(), SurfaceHit.ImpactPoint.Z, VaultSurfaceHeight);

	// Warn the designer if the resolved height falls outside the authored type's expected band.
	// This is a data-quality warning only — the authored type remains authoritative.
	bool bHeightInBand = false;
	switch (Type)
	{
	case ETraversalType::Vault:
		bHeightInBand = (VaultSurfaceHeight >= VaultMinHeight && VaultSurfaceHeight <= VaultMaxHeight);
		break;
	case ETraversalType::Climb:
		bHeightInBand = (VaultSurfaceHeight > VaultMaxHeight && VaultSurfaceHeight <= ClimbMaxHeight);
		break;
	case ETraversalType::Mantle:
		bHeightInBand = (VaultSurfaceHeight > ClimbMaxHeight && VaultSurfaceHeight <= MantleMaxHeight);
		break;
	default:
		bHeightInBand = true;
		break;
	}
	if (!bHeightInBand)
	{
		UE_LOG(LogTraversal, Warning,
			TEXT("[NavLinkTraversal] %s - authored Type=%d height band mismatch: resolved height=%.1f — check nav-link placement or band tuning values"),
			*OwnerTag(OwningCharacter), (int32)Type, VaultSurfaceHeight);
	}

	return true;
}

// ---- Traversal Execution ----

void UTraversalComponent::StartTraversal(ETraversalType Type)
{
	if (OwningCharacter->HasAuthority() && TraversalNoiseRange > 0.f)
		UAISense_Hearing::ReportNoiseEvent(GetWorld(), OwningCharacter->GetActorLocation(), TraversalNoiseLoudness, OwningCharacter, TraversalNoiseRange, TEXT("Traversal"));

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

	const ETraversalType EndingType = ActiveTraversalType;
	const bool bWasNavLinkTraversal = bCurrentTraversalFromNavLink;
	bCurrentTraversalFromNavLink = false;

	ActiveTraversalType = ETraversalType::None;
	bIsSnappingToVault = false;
	SetComponentTickEnabled(false);

	// Guard all owner dereferences — the worst-case timer can fire after the companion is destroyed.
	if (!IsValid(OwningCharacter)) return;

	// a) Restore rotation yaw flag.
	OwningCharacter->bUseControllerRotationYaw = bSavedUseControllerRotationYaw;

	// b) Re-enable collision so the depenetration sweep that follows is effective.
	if (IsValid(CachedCapsule))
		CachedCapsule->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);

	// c) Zero residual root-motion velocity before placement.
	if (IsValid(CachedMovement))
		CachedMovement->StopMovementImmediately();

	// d) In-place depenetrate — now that collision is restored, the sweep can push out of geometry.
	//    Covers the non-nav-link (player) path and is a cheap no-op when not embedded.
	OwningCharacter->SetActorLocation(OwningCharacter->GetActorLocation(), true);

	// e) Z-only floor correction for nav-link landings — keeps companion XY where root motion
	//    left it and corrects Z to the actual surface so it doesn't slip off or hover.
	if (IsValid(CachedCapsule) && bWasNavLinkTraversal)
	{
		const bool bSnapType = (EndingType == ETraversalType::Climb
			|| EndingType == ETraversalType::Mantle
			|| EndingType == ETraversalType::Vault);
		if (bSnapType)
			SnapToFloorAfterNavLink();
	}

	// f) Enter walking mode from rest at the correct position.
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

void UTraversalComponent::SnapToFloorAfterNavLink()
{
	if (!IsValid(OwningCharacter)) return;
	if (!IsValid(CachedCapsule)) return;

	UWorld* World = GetWorld();
	if (!World) return;

	const FVector Loc = OwningCharacter->GetActorLocation();
	const float CapsuleHalfHeight = CachedCapsule->GetScaledCapsuleHalfHeight();

	FCollisionQueryParams SnapParams;
	BuildPawnIgnoreParams(SnapParams);

	FVector Start = Loc;
	Start.Z += CapsuleHalfHeight;
	FVector End = Loc;
	End.Z -= (CapsuleHalfHeight + TraversalConstants::FloorSnapMaxDrop);

	FHitResult Hit;
	const bool bHit = World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, SnapParams);

	if (bHit)
	{
		FVector NewLoc = Loc;
		NewLoc.Z = Hit.ImpactPoint.Z + CapsuleHalfHeight + NavLinkLandingZOffset;
		OwningCharacter->SetActorLocation(NewLoc, /*bSweep*/ true);
		UE_LOG(LogTraversal, Verbose, TEXT("[NavLinkSnap] %s - snapped to floor at Z=%.1f (impact=%.1f offset=%.1f)"),
			*OwnerTag(OwningCharacter), NewLoc.Z, Hit.ImpactPoint.Z, NavLinkLandingZOffset);
	}
	else
	{
		UE_LOG(LogTraversal, Verbose, TEXT("[NavLinkSnap] %s - no floor found within %.0fcm, position unchanged"),
			*OwnerTag(OwningCharacter), TraversalConstants::FloorSnapMaxDrop);
	}
}
