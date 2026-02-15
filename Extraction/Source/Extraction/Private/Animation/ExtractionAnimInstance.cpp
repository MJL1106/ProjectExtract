// Copyright Epic Games, Inc. All Rights Reserved.

#include "ExtractionAnimInstance.h"
#include "ExtractionCharacter.h"
#include "ExtractionAnimDataAsset.h"
#include "Extraction.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/KismetMathLibrary.h"

DEFINE_LOG_CATEGORY(LogExtractionAnim);

namespace ExtractionAnimConstants
{
	/** Minimum ground speed (cm/s) to consider the character as having velocity */
	static constexpr float MinVelocityThreshold = 3.0f;
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
	if (!IsValid(PawnOwner))
	{
		return;
	}

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

	if (!IsValid(OwningCharacter) || !IsValid(MovementComponent))
	{
		return;
	}

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
	if (bHasVelocity)
	{
		const FRotator ActorRotation = OwningCharacter->GetActorRotation();
		Direction = UKismetMathLibrary::NormalizedDeltaRotator(
			GroundVelocity.Rotation(), ActorRotation).Yaw;
	}
	else
	{
		Direction = 0.f;
	}

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

	// Sprint, slide, and prone read from character's replicated state
	bIsSprinting = OwningCharacter->GetIsSprinting();
	bIsSliding = OwningCharacter->GetIsSliding();

	const bool bWasProne = bIsProne;
	bIsProne = OwningCharacter->GetIsProne();
	bIsTransitioningToProne = OwningCharacter->GetIsTransitioningToProne();
	bIsTransitioningFromProne = OwningCharacter->GetIsTransitioningFromProne();

	// Log prone state changes for debugging
	if (bIsProne != bWasProne)
	{
		UE_LOG(LogExtractionAnim, Log,
			TEXT("AnimInstance: bIsProne changed %d -> %d | bIsCrouching=%d | bIsTransToProne=%d | bIsTransFromProne=%d | Speed=%.1f"),
			bWasProne, bIsProne, bIsCrouching, bIsTransitioningToProne, bIsTransitioningFromProne, Speed);
	}

	// bIsADS, bIsAlive are set externally via setters or gameplay systems
}

// ---- Convenience Getters ----

UBlendSpace* UExtractionAnimInstance::GetActiveLocomotionBlendSpace() const
{
	const UExtractionAnimDataAsset* Data = GetActiveAnimData();
	if (!IsValid(Data))
	{
		return nullptr;
	}
	return Data->LocomotionBlendSpace;
}

UExtractionAnimDataAsset* UExtractionAnimInstance::GetActiveAnimData() const
{
	const TObjectPtr<UExtractionAnimDataAsset>* Found = WeaponAnimSets.Find(CurrentWeaponType);
	if (!Found || !IsValid(*Found))
	{
		return nullptr;
	}
	return *Found;
}

UBlendSpace* UExtractionAnimInstance::GetActiveProneLocomotionBlendSpace() const
{
	const UExtractionAnimDataAsset* Data = GetActiveAnimData();
	if (!IsValid(Data))
	{
		return nullptr;
	}
	return Data->ProneLocomotionBlendSpace;
}

// ---- Montage Playback ----

float UExtractionAnimInstance::PlayFireMontage(float PlayRate)
{
	const UExtractionAnimDataAsset* Data = GetActiveAnimData();
	if (!IsValid(Data))
	{
		return 0.f;
	}
	return PlayMontageInternal(Data->FireMontage, PlayRate);
}

float UExtractionAnimInstance::PlayReloadMontage(float PlayRate)
{
	const UExtractionAnimDataAsset* Data = GetActiveAnimData();
	if (!IsValid(Data))
	{
		return 0.f;
	}
	return PlayMontageInternal(Data->ReloadMontage, PlayRate);
}

float UExtractionAnimInstance::PlayEquipMontage(float PlayRate)
{
	const UExtractionAnimDataAsset* Data = GetActiveAnimData();
	if (!IsValid(Data))
	{
		return 0.f;
	}
	return PlayMontageInternal(Data->EquipMontage, PlayRate);
}

float UExtractionAnimInstance::PlayHitReactMontage(float PlayRate)
{
	const UExtractionAnimDataAsset* Data = GetActiveAnimData();
	if (!IsValid(Data))
	{
		return 0.f;
	}
	return PlayRandomMontage(Data->HitReactMontages, PlayRate);
}

float UExtractionAnimInstance::PlayDeathMontage(float PlayRate)
{
	bIsAlive = false;

	const UExtractionAnimDataAsset* Data = GetActiveAnimData();
	if (!IsValid(Data))
	{
		return 0.f;
	}
	return PlayRandomMontage(Data->DeathMontages, PlayRate);
}

float UExtractionAnimInstance::PlayProneTransitionMontage(EProneTransitionType TransitionType, float PlayRate)
{
	const UExtractionAnimDataAsset* Data = GetActiveAnimData();
	if (!IsValid(Data))
	{
		return 0.f;
	}

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
		Montage = Data->CrouchToProneTransition;
		break;
	default:
		return 0.f;
	}

	return PlayMontageInternal(Montage, PlayRate);
}

float UExtractionAnimInstance::PlayProneExitMontage(float PlayRate)
{
	const UExtractionAnimDataAsset* Data = GetActiveAnimData();
	if (!IsValid(Data))
	{
		return 0.f;
	}
	return PlayMontageInternal(Data->ProneToStandTransition, PlayRate);
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
	if (!IsValid(Montage))
	{
		return 0.f;
	}

	const float SafePlayRate = FMath::Max(PlayRate, UE_KINDA_SMALL_NUMBER);
	Montage_Play(Montage, SafePlayRate);
	return Montage->GetPlayLength() / SafePlayRate;
}

float UExtractionAnimInstance::PlayRandomMontage(
	const TArray<TObjectPtr<UAnimMontage>>& Montages, float PlayRate)
{
	if (Montages.Num() == 0)
	{
		return 0.f;
	}

	const int32 Index = FMath::RandRange(0, Montages.Num() - 1);
	UAnimMontage* Chosen = Montages[Index];

	return PlayMontageInternal(Chosen, PlayRate);
}
