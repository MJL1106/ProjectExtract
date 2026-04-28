// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "ExtractionTypes.h"
#include "ExtractionAnimInstance.generated.h"

class AExtractionCharacter;
class UExtractionAnimDataAsset;
class UTraversalComponent;
class UCharacterMovementComponent;
class UBlendSpace;
class UAnimMontage;
class UAnimSequence;

DECLARE_LOG_CATEGORY_EXTERN(LogExtractionAnim, Log, All);

/**
 * C++ AnimInstance that provides all data the ABP reads.
 * Create your ABP with this as the parent class.
 */
UCLASS()
class EXTRACTION_API UExtractionAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:

	UExtractionAnimInstance();

	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

	// ---- Data Asset Configuration ----

	/**
	 * Map of weapon type -> animation data asset.
	 * Populate in the ABP class defaults — one entry per weapon type.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation|Config")
	TMap<EWeaponType, TObjectPtr<UExtractionAnimDataAsset>> WeaponAnimSets;

	// ---- Locomotion Properties (read by ABP) ----

	/** Ground speed in cm/s */
	UPROPERTY(BlueprintReadOnly, Category = "Animation|Locomotion")
	float Speed;

	/** Movement direction relative to actor facing [-180, 180] degrees */
	UPROPERTY(BlueprintReadOnly, Category = "Animation|Locomotion")
	float Direction;

	/** Speed normalized to [0..1] where 1 = MaxWalkSpeed */
	UPROPERTY(BlueprintReadOnly, Category = "Animation|Locomotion")
	float NormalizedSpeed;

	// ---- State Flags (read by ABP) ----

	UPROPERTY(BlueprintReadOnly, Category = "Animation|State")
	bool bIsInAir;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|State")
	bool bIsFalling;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|State")
	bool bIsCrouching;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|State")
	bool bIsSprinting;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|State")
	bool bIsSliding;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|State")
	bool bIsProne;

	/** True while a transition-to-prone montage is playing */
	UPROPERTY(BlueprintReadOnly, Category = "Animation|State")
	bool bIsTransitioningToProne;

	/** True while the prone-to-stand montage is playing */
	UPROPERTY(BlueprintReadOnly, Category = "Animation|State")
	bool bIsTransitioningFromProne;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|State")
	bool bIsVaulting;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|State")
	bool bIsClimbing;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|State")
	bool bIsMantling;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|State")
	bool bIsADS;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|State")
	bool bHasVelocity;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|State")
	bool bIsAccelerating;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|State")
	bool bIsAlive;

	// ---- Aim Offset ----

	/** Pitch for aim offset, clamped [-90, 90] */
	UPROPERTY(BlueprintReadOnly, Category = "Animation|AimOffset")
	float AimPitch;

	/** Yaw for aim offset, clamped [-180, 180] */
	UPROPERTY(BlueprintReadOnly, Category = "Animation|AimOffset")
	float AimYaw;

	// ---- Active Weapon State ----

	UPROPERTY(BlueprintReadOnly, Category = "Animation|Weapon")
	EWeaponType CurrentWeaponType;

	// ---- Convenience Getters (for ABP) ----

	/** Returns the active weapon's locomotion blendspace, or nullptr */
	UFUNCTION(BlueprintPure, Category = "Animation|Data", meta = (BlueprintThreadSafe))
	UBlendSpace* GetActiveLocomotionBlendSpace() const;

	/** Returns the active weapon's DataAsset, or nullptr */
	UFUNCTION(BlueprintPure, Category = "Animation|Data", meta = (BlueprintThreadSafe))
	UExtractionAnimDataAsset* GetActiveAnimData() const;

	/** Returns the active weapon's prone locomotion blendspace, or nullptr */
	UFUNCTION(BlueprintPure, Category = "Animation|Data", meta = (BlueprintThreadSafe))
	UBlendSpace* GetActiveProneLocomotionBlendSpace() const;

	/** Returns the active weapon's jump start animation, or nullptr */
	UFUNCTION(BlueprintPure, Category = "Animation|Data", meta = (BlueprintThreadSafe))
	UAnimSequence* GetActiveJumpStartAnim() const;

	/** Returns the active weapon's fall loop animation, or nullptr */
	UFUNCTION(BlueprintPure, Category = "Animation|Data", meta = (BlueprintThreadSafe))
	UAnimSequence* GetActiveFallLoopAnim() const;

	/** Returns the active weapon's land animation, or nullptr */
	UFUNCTION(BlueprintPure, Category = "Animation|Data", meta = (BlueprintThreadSafe))
	UAnimSequence* GetActiveLandAnim() const;

	// ---- Montage Playback API ----

	/** Play the fire montage for the current weapon. Returns duration. */
	UFUNCTION(BlueprintCallable, Category = "Animation|Actions")
	float PlayFireMontage(float PlayRate = 1.0f);

	/** Play the reload montage for the current weapon. Returns duration. */
	UFUNCTION(BlueprintCallable, Category = "Animation|Actions")
	float PlayReloadMontage(float PlayRate = 1.0f);

	/** Play the equip montage for the current weapon. Returns duration. */
	UFUNCTION(BlueprintCallable, Category = "Animation|Actions")
	float PlayEquipMontage(float PlayRate = 1.0f);

	/** Play a random hit reaction montage. Returns duration. */
	UFUNCTION(BlueprintCallable, Category = "Animation|Actions")
	float PlayHitReactMontage(float PlayRate = 1.0f);

	/** Play a random death montage (full body). Returns duration. */
	UFUNCTION(BlueprintCallable, Category = "Animation|Actions")
	float PlayDeathMontage(float PlayRate = 1.0f);

	/** Play the transition-to-prone montage matching the given type. Returns duration. */
	UFUNCTION(BlueprintCallable, Category = "Animation|Actions")
	float PlayProneTransitionMontage(EProneTransitionType TransitionType, float PlayRate = 1.0f);

	/** Play the prone-to-stand transition montage. Returns duration. */
	UFUNCTION(BlueprintCallable, Category = "Animation|Actions")
	float PlayProneExitMontage(float PlayRate = 1.0f);

	/** Play the vault montage. Returns duration. */
	UFUNCTION(BlueprintCallable, Category = "Animation|Actions")
	float PlayVaultMontage(float PlayRate = 1.0f);

	/** Returns true if the vault montage is currently playing (real-time, no frame delay). */
	UFUNCTION(BlueprintPure, Category = "Animation|State")
	bool IsPlayingVaultMontage() const;

	/** Play the climb-up montage. Returns duration. */
	UFUNCTION(BlueprintCallable, Category = "Animation|Actions")
	float PlayClimbMontage(float PlayRate = 1.0f);

	/** Returns true if the climb montage is currently playing. */
	UFUNCTION(BlueprintPure, Category = "Animation|State")
	bool IsPlayingClimbMontage() const;

	/** Play the mantle/pull-up montage. Returns duration. */
	UFUNCTION(BlueprintCallable, Category = "Animation|Actions")
	float PlayMantleMontage(float PlayRate = 1.0f);

	/** Returns true if the mantle montage is currently playing. */
	UFUNCTION(BlueprintPure, Category = "Animation|State")
	bool IsPlayingMantleMontage() const;

	/** Returns true if any traversal montage (vault, climb, or mantle) is currently playing. */
	UFUNCTION(BlueprintPure, Category = "Animation|State")
	bool IsPlayingAnyTraversalMontage() const;

	/** Play the sprint jump montage (full-body root motion). Returns duration. */
	UFUNCTION(BlueprintCallable, Category = "Animation|Actions")
	float PlaySprintJumpMontage(float PlayRate = 1.0f);

	/** Returns true if the sprint jump montage is currently playing. */
	UFUNCTION(BlueprintPure, Category = "Animation|State")
	bool IsPlayingSprintJumpMontage() const;

	/** Returns true if any prone-entry transition montage is currently active (real-time, no frame delay). */
	UFUNCTION(BlueprintPure, Category = "Animation|State")
	bool IsPlayingProneEntryMontage() const;

	/** Set the current weapon type. Validates that a DataAsset exists for it. */
	UFUNCTION(BlueprintCallable, Category = "Animation|Weapon")
	void SetWeaponType(EWeaponType NewWeaponType);

private:

	/** Cached owning character */
	UPROPERTY()
	TObjectPtr<AExtractionCharacter> OwningCharacter;

	/** Cached movement component */
	UPROPERTY()
	TObjectPtr<UCharacterMovementComponent> MovementComponent;

	/** Play a single montage. Returns duration / PlayRate, or 0 on failure. */
	float PlayMontageInternal(UAnimMontage* Montage, float PlayRate);

	/** Pick a random montage from an array and play it. */
	float PlayRandomMontage(const TArray<TObjectPtr<UAnimMontage>>& Montages, float PlayRate);
};
