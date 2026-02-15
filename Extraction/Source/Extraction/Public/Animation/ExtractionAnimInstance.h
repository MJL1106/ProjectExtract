// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "ExtractionTypes.h"
#include "ExtractionAnimInstance.generated.h"

class AExtractionCharacter;
class UExtractionAnimDataAsset;
class UCharacterMovementComponent;
class UBlendSpace;
class UAnimMontage;

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
	UFUNCTION(BlueprintPure, Category = "Animation|Data")
	UBlendSpace* GetActiveLocomotionBlendSpace() const;

	/** Returns the active weapon's DataAsset, or nullptr */
	UFUNCTION(BlueprintPure, Category = "Animation|Data")
	UExtractionAnimDataAsset* GetActiveAnimData() const;

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
