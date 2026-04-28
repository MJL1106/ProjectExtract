// Copyright Epic Games, Inc. All Rights Reserved.

#include "TraversalComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "DrawDebugHelpers.h"
#include "Net/UnrealNetwork.h"

DEFINE_LOG_CATEGORY(LogTraversal);

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
	if (IsInTraversal()) return false;

	const ETraversalType DetectedType = PerformTraversalDetection();
	if (DetectedType == ETraversalType::None) return false;

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
	default:
		break;
	}

	OnTraversalStarted.Broadcast(Type, PlayRate);
}

// ---- Detection ----

ETraversalType UTraversalComponent::PerformTraversalDetection()
{
	if (!IsValid(OwningCharacter) || !IsValid(CachedCapsule)) return ETraversalType::None;

	FHitResult WallHit;
	if (!TraceForwardForWall(WallHit))
	{
		UE_LOG(LogTraversal, Verbose, TEXT("Traversal: No wall hit"));
		return ETraversalType::None;
	}

	if (IsValid(WallHit.GetActor()) && WallHit.GetActor()->IsA(APawn::StaticClass()))
		return ETraversalType::None;

	const FVector Forward = OwningCharacter->GetActorForwardVector();
	const float FacingDot = FVector::DotProduct(WallHit.ImpactNormal, -Forward);
	if (FacingDot < TraversalConstants::VaultWallFacingDotThreshold)
	{
		UE_LOG(LogTraversal, Verbose, TEXT("Traversal: Wall not facing us (dot=%.2f)"), FacingDot);
		return ETraversalType::None;
	}

	FHitResult SurfaceHit;
	if (!TraceDownForSurface(WallHit, SurfaceHit))
	{
		UE_LOG(LogTraversal, Verbose, TEXT("Traversal: No surface found above wall"));
		return ETraversalType::None;
	}

	const float CapsuleRadius = CachedCapsule->GetScaledCapsuleRadius();
	const float CapsuleHalfHeight = CachedCapsule->GetScaledCapsuleHalfHeight();
	const float FeetZ = OwningCharacter->GetActorLocation().Z - CapsuleHalfHeight;

	VaultWallNormal = WallHit.ImpactNormal;
	VaultWallImpactPoint = WallHit.ImpactPoint;
	VaultSurfaceLocation = SurfaceHit.ImpactPoint;
	VaultSurfaceHeight = SurfaceHit.ImpactPoint.Z - FeetZ;

	const float SurfOnTopOffset = CapsuleRadius + TraversalConstants::ClearanceBufferOffset;

	auto SetTargetLocation = [&](float Offset)
	{
		VaultTargetLocation = SurfaceHit.ImpactPoint + Forward * Offset;
		VaultTargetLocation.Z = SurfaceHit.ImpactPoint.Z + CapsuleHalfHeight;
	};

	// Vault range
	if (VaultSurfaceHeight >= VaultMinHeight && VaultSurfaceHeight <= VaultMaxHeight)
	{
		const bool bVaultClear = CheckClearance(SurfaceHit.ImpactPoint, VaultLandingForwardOffset);
		if (bVaultClear)
		{
			bool bPreferClimb = false;
			if (VaultSurfaceHeight >= ClimbMinHeight)
			{
				FCollisionQueryParams DropParams;
				DropParams.AddIgnoredActor(OwningCharacter);

				const FVector DropStart(
					SurfaceHit.ImpactPoint.X + Forward.X * VaultLandingForwardOffset,
					SurfaceHit.ImpactPoint.Y + Forward.Y * VaultLandingForwardOffset,
					SurfaceHit.ImpactPoint.Z + 10.f);
				const FVector DropEnd(DropStart.X, DropStart.Y, FeetZ - 10.f);

				FHitResult DropHit;
				const bool bHitGround = GetWorld()->LineTraceSingleByChannel(
					DropHit, DropStart, DropEnd, ECC_Visibility, DropParams);

				bPreferClimb = bHitGround &&
					(DropHit.ImpactPoint.Z > SurfaceHit.ImpactPoint.Z - TraversalConstants::DropThreshold);
			}

			if (!bPreferClimb)
			{
				SetTargetLocation(VaultLandingForwardOffset);
				return ETraversalType::Vault;
			}
		}

		if (VaultSurfaceHeight >= ClimbMinHeight)
		{
			const bool bClimbClear = CheckClearance(SurfaceHit.ImpactPoint, SurfOnTopOffset);
			if (bClimbClear)
			{
				SetTargetLocation(SurfOnTopOffset);
				return ETraversalType::Climb;
			}
		}
	}

	// Climb-only range
	if (VaultSurfaceHeight > VaultMaxHeight && VaultSurfaceHeight <= ClimbMaxHeight)
	{
		const bool bClimbClear = CheckClearance(SurfaceHit.ImpactPoint, SurfOnTopOffset);
		if (bClimbClear)
		{
			SetTargetLocation(SurfOnTopOffset);
			return ETraversalType::Climb;
		}
	}

	// Mantle range
	if (VaultSurfaceHeight > ClimbMaxHeight && VaultSurfaceHeight <= MantleMaxHeight)
	{
		const bool bMantleClear = CheckClearance(SurfaceHit.ImpactPoint, SurfOnTopOffset);
		if (bMantleClear)
		{
			SetTargetLocation(SurfOnTopOffset);
			return ETraversalType::Mantle;
		}
	}

	return ETraversalType::None;
}

bool UTraversalComponent::TraceForwardForWall(FHitResult& OutHit) const
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

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(OwningCharacter);

	const FCollisionShape SweepShape = FCollisionShape::MakeSphere(VaultForwardTraceRadius);

	const bool bHit = GetWorld()->SweepSingleByChannel(
		OutHit, Start, End, FQuat::Identity,
		ECC_Visibility, SweepShape, QueryParams);

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

bool UTraversalComponent::TraceDownForSurface(const FHitResult& WallHit, FHitResult& OutSurfaceHit) const
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

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(OwningCharacter);

	const bool bHit = GetWorld()->LineTraceSingleByChannel(
		OutSurfaceHit, TraceStart, TraceEnd, ECC_Visibility, QueryParams);

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

bool UTraversalComponent::CheckClearance(const FVector& SurfaceLocation, float ForwardOffset) const
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

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(OwningCharacter);

	const bool bBlocked = GetWorld()->OverlapAnyTestByChannel(
		TestLocation, FQuat::Identity,
		ECC_WorldStatic, TestShape, QueryParams);

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

	OnTraversalStarted.Broadcast(ActiveTraversalType, PlayRate);
}
