// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "ExtractionTypes.h"
#include "ExtractionAnimDataAsset.generated.h"

class UBlendSpace;
class UAnimMontage;
class UAnimSequence;

/**
 * Holds all animation references for a single weapon state.
 * Create one DataAsset instance per weapon type (Unarmed, Pistol, Rifle, etc.)
 * and assign them to the AnimInstance's WeaponAnimSets map.
 */
UCLASS(BlueprintType)
class EXTRACTION_API UExtractionAnimDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:

	// ---- Identification ----

	/** The weapon type this animation set corresponds to */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation|Identity")
	EWeaponType WeaponType = EWeaponType::Unarmed;

	// ---- Locomotion ----

	/** 2D Blendspace: axes are Speed and Direction. Drives 8-dir locomotion. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation|Locomotion")
	TObjectPtr<UBlendSpace> LocomotionBlendSpace;

	/** Idle pose (fallback or blendspace center) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation|Locomotion")
	TObjectPtr<UAnimSequence> IdleAnim;

	// ---- Jump / In-Air ----

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation|Jump")
	TObjectPtr<UAnimSequence> JumpStartAnim;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation|Jump")
	TObjectPtr<UAnimSequence> JumpApexAnim;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation|Jump")
	TObjectPtr<UAnimSequence> FallLoopAnim;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation|Jump")
	TObjectPtr<UAnimSequence> LandAnim;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation|Jump")
	TObjectPtr<UAnimSequence> JumpRecoveryAdditiveAnim;

	// ---- Weapon Actions (Upper Body) ----

	/** ADS idle pose — blended onto upper body layer */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation|Weapon")
	TObjectPtr<UAnimSequence> ADSIdlePose;

	/** Aim offset blendspace for look direction */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation|Weapon")
	TObjectPtr<UBlendSpace> AimOffset;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation|Weapon")
	TObjectPtr<UAnimMontage> FireMontage;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation|Weapon")
	TObjectPtr<UAnimMontage> DryFireMontage;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation|Weapon")
	TObjectPtr<UAnimMontage> ReloadMontage;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation|Weapon")
	TObjectPtr<UAnimMontage> EquipMontage;

	// ---- Hit Reactions & Death ----

	/** Multiple hit reacts for random selection */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation|Damage")
	TArray<TObjectPtr<UAnimMontage>> HitReactMontages;

	/** Full-body death montages for random selection */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation|Damage")
	TArray<TObjectPtr<UAnimMontage>> DeathMontages;
};
