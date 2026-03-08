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
	TObjectPtr<UAnimSequence> FallLoopAnim;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation|Jump")
	TObjectPtr<UAnimSequence> LandAnim;

	/** Full-body root motion montage for sprint jumps. Controls entire arc. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation|Jump")
	TObjectPtr<UAnimMontage> SprintJumpMontage;

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

	// ---- Prone ----

	/** 2D Blendspace for 8-directional prone crawling. Axes: Speed (0-80), Direction (-180, 180). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation|Prone")
	TObjectPtr<UBlendSpace> ProneLocomotionBlendSpace;

	/** Prone idle pose (center of blendspace / no velocity) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation|Prone")
	TObjectPtr<UAnimSequence> ProneIdleAnim;

	// ---- Prone Transitions ----

	/** Transition montage: standing idle -> prone. Root motion. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation|Prone|Transitions")
	TObjectPtr<UAnimMontage> IdleToProneTransition;

	/** Transition montage: walking -> prone. Root motion. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation|Prone|Transitions")
	TObjectPtr<UAnimMontage> WalkToProneTransition;

	/** Transition montage: sprinting -> prone (knee slide). Root motion with forward momentum. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation|Prone|Transitions")
	TObjectPtr<UAnimMontage> SprintToProneTransition;

	/** Transition montage: crouching -> prone. Currently unused — crouch-to-prone uses inertialization only. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation|Prone|Transitions")
	TObjectPtr<UAnimMontage> CrouchToProneTransition;

	/**
	 * Transition montage: prone -> standing. Root motion.
	 * Also reused for prone -> crouch (plays only the first section).
	 * Add a second section (e.g. "StandUp") at the crouch-height keyframe
	 * so prone-to-crouch stops there while prone-to-stand plays the full montage.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation|Prone|Transitions")
	TObjectPtr<UAnimMontage> ProneToStandTransition;

	// ---- Traversal (Vault / Climb / Mantle) ----

	/** Full-body vault montage. Must have root motion enabled. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation|Traversal")
	TObjectPtr<UAnimMontage> VaultMontage;

	/** Climb-up montage for waist-to-chest height obstacles. Root motion required. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation|Traversal")
	TObjectPtr<UAnimMontage> ClimbMontage;

	/** Mantle/pull-up montage for above-head height ledges. Root motion required. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation|Traversal")
	TObjectPtr<UAnimMontage> MantleMontage;

	// ---- Hit Reactions & Death ----

	/** Multiple hit reacts for random selection */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation|Damage")
	TArray<TObjectPtr<UAnimMontage>> HitReactMontages;

	/** Full-body death montages for random selection */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation|Damage")
	TArray<TObjectPtr<UAnimMontage>> DeathMontages;
};
