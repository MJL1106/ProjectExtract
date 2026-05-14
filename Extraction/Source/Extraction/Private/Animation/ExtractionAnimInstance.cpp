// Copyright Epic Games, Inc. All Rights Reserved.

#include "ExtractionAnimInstance.h"
#include "ExtractionCharacter.h"
#include "TraversalComponent.h"
#include "ExtractionAnimDataAsset.h"
#include "Extraction.h"
#include "Animation/AnimMontage.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/KismetMathLibrary.h"

DEFINE_LOG_CATEGORY(LogExtractionAnim);

namespace ExtractionAnimConstants
{
	/** Minimum ground speed (cm/s) to consider the character as having velocity */
	static constexpr float MinVelocityThreshold = 3.0f;

	/** Interpolation speed for Direction smoothing — prevents snapping when speed crosses the velocity threshold */
	static constexpr float DirectionInterpSpeed = 15.0f;

	/** Blend-in time (seconds) for prone<->crouch transitions using explicit Inertialization */
	static constexpr float ProneTransitionBlendTime = 0.2f;

}

UExtractionAnimInstance::UExtractionAnimInstance()
	: Speed(0.f)
	, Direction(0.f)
	, NormalizedSpeed(0.f)
	, bIsInAir(false)
	, bIsFalling(false)
	, bIsCrouching(false)
	, bIsSprinting(false)
	, bIsSliding(false)
	, bIsProne(false)
	, bIsTransitioningToProne(false)
	, bIsTransitioningFromProne(false)
	, bIsVaulting(false)
	, bIsClimbing(false)
	, bIsMantling(false)
	, bIsADS(false)
	, bHasVelocity(false)
	, bIsAccelerating(false)
	, bIsAlive(true)
	, AimPitch(0.f)
	, AimYaw(0.f)
	, CurrentWeaponType(EWeaponType::Rifle)
{
}

void UExtractionAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();

	APawn* PawnOwner = TryGetPawnOwner();
	if (!IsValid(PawnOwner)) return;

	OwningCharacter = Cast<AExtractionCharacter>(PawnOwner);
	if (!IsValid(OwningCharacter))
	{
		UE_LOG(LogExtractionAnim, Warning,
			TEXT("AnimInstance owner '%s' is not an AExtractionCharacter."),
			*GetNameSafe(PawnOwner));
		return;
	}

	MovementComponent = OwningCharacter->GetCharacterMovement();
	if (!IsValid(MovementComponent))
	{
		UE_LOG(LogExtractionAnim, Warning,
			TEXT("No CharacterMovementComponent found on '%s'."),
			*GetNameSafe(OwningCharacter));
	}
}

void UExtractionAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	if (!IsValid(OwningCharacter) || !IsValid(MovementComponent)) return;

	// --- Velocity / Speed (no allocations) ---
	const FVector Velocity = MovementComponent->Velocity;
	const FVector GroundVelocity(Velocity.X, Velocity.Y, 0.f);
	Speed = GroundVelocity.Size();
	bHasVelocity = Speed > ExtractionAnimConstants::MinVelocityThreshold;

	const float MaxSpeed = MovementComponent->MaxWalkSpeed;
	NormalizedSpeed = (MaxSpeed > UE_KINDA_SMALL_NUMBER)
		? FMath::Clamp(Speed / MaxSpeed, 0.f, 1.f)
		: 0.f;

	// --- Direction (relative to actor facing) ---
	// Interpolate toward target to prevent snapping when speed crosses the velocity threshold.
	float TargetDirection = 0.f;
	if (bHasVelocity)
	{
		const FRotator ActorRotation = OwningCharacter->GetActorRotation();
		TargetDirection = UKismetMathLibrary::NormalizedDeltaRotator(
			GroundVelocity.Rotation(), ActorRotation).Yaw;
	}
	Direction = FMath::FInterpTo(Direction, TargetDirection, DeltaSeconds, ExtractionAnimConstants::DirectionInterpSpeed);

	// --- Movement state flags ---
	bIsInAir = MovementComponent->IsFalling();
	bIsFalling = bIsInAir && (Velocity.Z < 0.f);
	bIsCrouching = MovementComponent->IsCrouching();
	bIsAccelerating = MovementComponent->GetCurrentAcceleration().SizeSquared() > UE_KINDA_SMALL_NUMBER;

	// --- Aim offset ---
	const FRotator AimRotation = OwningCharacter->GetBaseAimRotation();
	const FRotator ActorRotation = OwningCharacter->GetActorRotation();
	const FRotator AimDelta = UKismetMathLibrary::NormalizedDeltaRotator(AimRotation, ActorRotation);
	AimPitch = FMath::ClampAngle(AimDelta.Pitch, -90.f, 90.f);
	AimYaw = FMath::ClampAngle(AimDelta.Yaw, -180.f, 180.f);

	// Sprint, slide, prone, and vault read from character's replicated state
	bIsSprinting = OwningCharacter->GetIsSprinting();
	bIsSliding = OwningCharacter->GetIsSliding();
	// Capture previous prone state before updating — used to catch the exit frame
	const bool bWasProne = bIsProne;
	bIsProne = OwningCharacter->GetIsProne();
	const UTraversalComponent* TraversalComp = OwningCharacter->GetTraversalComponent();
	if (IsValid(TraversalComp))
	{
		const ETraversalType Traversal = TraversalComp->GetActiveType();
		bIsVaulting  = (Traversal == ETraversalType::Vault);
		bIsClimbing  = (Traversal == ETraversalType::Climb);
		bIsMantling  = (Traversal == ETraversalType::Mantle);
	}
	else
	{
		bIsVaulting  = false;
		bIsClimbing  = false;
		bIsMantling  = false;
	}

	// --- Prone transition flags (only check montages when prone-related) ---
	if (bIsProne || bWasProne || bIsTransitioningToProne || bIsTransitioningFromProne)
	{
		const UExtractionAnimDataAsset* AnimData = GetActiveAnimData();
		if (IsValid(AnimData))
		{
			bIsTransitioningToProne =
				Montage_IsPlaying(AnimData->IdleToProneTransition)   ||
				Montage_IsPlaying(AnimData->WalkToProneTransition)   ||
				Montage_IsPlaying(AnimData->SprintToProneTransition);

			bIsTransitioningFromProne = Montage_IsPlaying(AnimData->ProneToStandTransition);
		}
		else
		{
			bIsTransitioningToProne   = false;
			bIsTransitioningFromProne = false;
		}
	}

	// bIsADS, bIsAlive are set externally via setters or gameplay systems
}

// ---- Convenience Getters ----

UBlendSpace* UExtractionAnimInstance::GetActiveLocomotionBlendSpace() const
{
	const UExtractionAnimDataAsset* Data = GetActiveAnimData();
	if (!IsValid(Data)) return nullptr;
	return Data->LocomotionBlendSpace;
}

UExtractionAnimDataAsset* UExtractionAnimInstance::GetActiveAnimData() const
{
	const TObjectPtr<UExtractionAnimDataAsset>* Found = WeaponAnimSets.Find(CurrentWeaponType);
	if (!Found || !IsValid(*Found)) return nullptr;
	return *Found;
}

UBlendSpace* UExtractionAnimInstance::GetActiveProneLocomotionBlendSpace() const
{
	const UExtractionAnimDataAsset* Data = GetActiveAnimData();
	if (!IsValid(Data)) return nullptr;
	return Data->ProneLocomotionBlendSpace;
}

UAnimSequence* UExtractionAnimInstance::GetActiveJumpStartAnim() const
{
	const UExtractionAnimDataAsset* Data = GetActiveAnimData();
	if (!IsValid(Data)) return nullptr;
	return Data->JumpStartAnim;
}

UAnimSequence* UExtractionAnimInstance::GetActiveFallLoopAnim() const
{
	const UExtractionAnimDataAsset* Data = GetActiveAnimData();
	if (!IsValid(Data)) return nullptr;
	return Data->FallLoopAnim;
}

UAnimSequence* UExtractionAnimInstance::GetActiveLandAnim() const
{
	const UExtractionAnimDataAsset* Data = GetActiveAnimData();
	if (!IsValid(Data)) return nullptr;
	return Data->LandAnim;
}

// ---- Montage Playback ----

float UExtractionAnimInstance::PlayFireMontage(float PlayRate)
{
	const UExtractionAnimDataAsset* Data = GetActiveAnimData();
	if (!IsValid(Data)) return 0.f;
	return PlayMontageInternal(Data->FireMontage, PlayRate);
}

float UExtractionAnimInstance::PlayReloadMontage(float PlayRate)
{
	const UExtractionAnimDataAsset* Data = GetActiveAnimData();
	if (!IsValid(Data)) return 0.f;
	return PlayMontageInternal(Data->ReloadMontage, PlayRate);
}

float UExtractionAnimInstance::PlayEquipMontage(float PlayRate)
{
	const UExtractionAnimDataAsset* Data = GetActiveAnimData();
	if (!IsValid(Data)) return 0.f;
	return PlayMontageInternal(Data->EquipMontage, PlayRate);
}

float UExtractionAnimInstance::PlayHitReactMontage(float PlayRate)
{
	const UExtractionAnimDataAsset* Data = GetActiveAnimData();
	if (!IsValid(Data)) return 0.f;
	return PlayRandomMontage(Data->HitReactMontages, PlayRate);
}

float UExtractionAnimInstance::PlayDeathMontage(float PlayRate)
{
	bIsAlive = false;

	const UExtractionAnimDataAsset* Data = GetActiveAnimData();
	if (!IsValid(Data)) return 0.f;
	return PlayRandomMontage(Data->DeathMontages, PlayRate);
}

float UExtractionAnimInstance::PlayProneTransitionMontage(EProneTransitionType TransitionType, float PlayRate)
{
	const UExtractionAnimDataAsset* Data = GetActiveAnimData();
	if (!IsValid(Data)) return 0.f;

	UAnimMontage* Montage = nullptr;
	switch (TransitionType)
	{
	case EProneTransitionType::FromIdle:
		Montage = Data->IdleToProneTransition;
		break;
	case EProneTransitionType::FromWalk:
		Montage = Data->WalkToProneTransition;
		break;
	case EProneTransitionType::FromSprint:
		Montage = Data->SprintToProneTransition;
		break;
	case EProneTransitionType::FromCrouch:
		// No montage — character UnCrouches immediately and the ABP transitions via inertialization
		return 0.f;
	default:
		return 0.f;
	}

	return PlayMontageInternal(Montage, PlayRate);
}

float UExtractionAnimInstance::PlayProneExitMontage(float PlayRate)
{
	const UExtractionAnimDataAsset* Data = GetActiveAnimData();
	if (!IsValid(Data)) return 0.f;
	return PlayMontageInternal(Data->ProneToStandTransition, PlayRate);
}

bool UExtractionAnimInstance::IsPlayingProneEntryMontage() const
{
	const UExtractionAnimDataAsset* AnimData = GetActiveAnimData();
	if (!IsValid(AnimData)) return false;

	return Montage_IsPlaying(AnimData->IdleToProneTransition)   ||
	       Montage_IsPlaying(AnimData->WalkToProneTransition)   ||
	       Montage_IsPlaying(AnimData->SprintToProneTransition);
}

float UExtractionAnimInstance::PlayVaultMontage(float PlayRate)
{
	const UExtractionAnimDataAsset* Data = GetActiveAnimData();
	if (!IsValid(Data)) return 0.f;
	return PlayMontageInternal(Data->VaultMontage, PlayRate);
}

bool UExtractionAnimInstance::IsPlayingVaultMontage() const
{
	const UExtractionAnimDataAsset* AnimData = GetActiveAnimData();
	if (!IsValid(AnimData)) return false;
	return Montage_IsPlaying(AnimData->VaultMontage);
}

float UExtractionAnimInstance::PlayClimbMontage(float PlayRate)
{
	const UExtractionAnimDataAsset* Data = GetActiveAnimData();
	if (!IsValid(Data)) return 0.f;
	return PlayMontageInternal(Data->ClimbMontage, PlayRate);
}

bool UExtractionAnimInstance::IsPlayingClimbMontage() const
{
	const UExtractionAnimDataAsset* AnimData = GetActiveAnimData();
	if (!IsValid(AnimData)) return false;
	return Montage_IsPlaying(AnimData->ClimbMontage);
}

float UExtractionAnimInstance::PlayMantleMontage(float PlayRate)
{
	const UExtractionAnimDataAsset* Data = GetActiveAnimData();
	if (!IsValid(Data)) return 0.f;
	return PlayMontageInternal(Data->MantleMontage, PlayRate);
}

bool UExtractionAnimInstance::IsPlayingMantleMontage() const
{
	const UExtractionAnimDataAsset* AnimData = GetActiveAnimData();
	if (!IsValid(AnimData)) return false;
	return Montage_IsPlaying(AnimData->MantleMontage);
}

bool UExtractionAnimInstance::IsPlayingAnyTraversalMontage() const
{
	return IsPlayingVaultMontage() || IsPlayingClimbMontage() || IsPlayingMantleMontage();
}

bool UExtractionAnimInstance::HasMontageForType(ETraversalType Type) const
{
	const UExtractionAnimDataAsset* Data = GetActiveAnimData();
	if (!IsValid(Data)) return false;
	switch (Type)
	{
	case ETraversalType::Vault:  return IsValid(Data->VaultMontage);
	case ETraversalType::Climb:  return IsValid(Data->ClimbMontage);
	case ETraversalType::Mantle: return IsValid(Data->MantleMontage);
	default: return false;
	}
}

float UExtractionAnimInstance::PlaySprintJumpMontage(float PlayRate)
{
	const UExtractionAnimDataAsset* Data = GetActiveAnimData();
	if (!IsValid(Data)) return 0.f;
	return PlayMontageInternal(Data->SprintJumpMontage, PlayRate);
}

bool UExtractionAnimInstance::IsPlayingSprintJumpMontage() const
{
	const UExtractionAnimDataAsset* AnimData = GetActiveAnimData();
	if (!IsValid(AnimData)) return false;
	return Montage_IsPlaying(AnimData->SprintJumpMontage);
}

void UExtractionAnimInstance::SetWeaponType(EWeaponType NewWeaponType)
{
	if (!WeaponAnimSets.Contains(NewWeaponType))
	{
		UE_LOG(LogExtractionAnim, Warning,
			TEXT("No AnimDataAsset registered for weapon type %d. Keeping current type %d."),
			static_cast<int32>(NewWeaponType), static_cast<int32>(CurrentWeaponType));
		return;
	}

	CurrentWeaponType = NewWeaponType;
}

// ---- Internal Helpers ----

float UExtractionAnimInstance::PlayMontageInternal(UAnimMontage* Montage, float PlayRate)
{
	if (!IsValid(Montage)) return 0.f;

	const float SafePlayRate = (FMath::Abs(PlayRate) < UE_KINDA_SMALL_NUMBER) ? 1.0f : PlayRate;
	Montage_Play(Montage, SafePlayRate);
	return Montage->GetPlayLength() / FMath::Abs(SafePlayRate);
}

float UExtractionAnimInstance::PlayRandomMontage(
	const TArray<TObjectPtr<UAnimMontage>>& Montages, float PlayRate)
{
	if (Montages.Num() == 0) return 0.f;

	const int32 Index = FMath::RandRange(0, Montages.Num() - 1);
	UAnimMontage* Chosen = Montages[Index];

	return PlayMontageInternal(Chosen, PlayRate);
}
