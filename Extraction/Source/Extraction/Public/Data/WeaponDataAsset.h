// Data-driven weapon configuration. One asset per weapon archetype.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "ExtractionTypes.h"
#include "EnemyTypes.h"
#include "Animation/AnimMontage.h"
#include "WeaponDataAsset.generated.h"

class UExtractionDamageType;

/**
 * Per-weapon animation slot set — every montage the enemy anim instance can play for this weapon.
 * All fields are EditDefaultsOnly; designers assign montages in the weapon's DataAsset.
 * Null entries fall through gracefully (the play function early-returns when the montage is null).
 */
USTRUCT(BlueprintType)
struct EXTRACTION_API FEnemyWeaponAnimSet
{
	GENERATED_BODY()

	/** Full-auto fire loop — plays while bIsFiring. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Animation")
	TObjectPtr<UAnimMontage> FireLoop;

	/** Single-shot fire kick — plays via OnWeaponFired delegate. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Animation")
	TObjectPtr<UAnimMontage> FireSingle;

	/** Tactical reload. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Animation")
	TObjectPtr<UAnimMontage> Reload;

	/** Weapon-mesh reload montage played on the gun visual's WeaponMesh (KINEMATION e.g. AM_W_AK105_Reload_Tac).
	 *  Animates the gun's Magazine_Parent bone so the attached magazine drops out and reseats. Optional. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Animation")
	TObjectPtr<UAnimMontage> WeaponReload = nullptr;

	/** Weapon-mesh fire montage played on the gun visual's WeaponMesh (KINEMATION e.g. AM_W_AK105_Fire).
	 *  Cycles the bolt/charging handle once per shot — re-triggered every round. Optional. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Animation")
	TObjectPtr<UAnimMontage> WeaponFire = nullptr;

	/** Melee strike. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Animation")
	TObjectPtr<UAnimMontage> Melee;

	/** Grenade throw (upper-body slot). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Animation")
	TObjectPtr<UAnimMontage> Grenade;

	/** Death — plays before ragdoll. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Animation")
	TObjectPtr<UAnimMontage> Death;

	/**
	 * Additive hit flinch — plays on an additive upper-body slot regardless of combat state.
	 * Separate from the full-body HitReact which remains gated on !bInCombat.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Animation")
	TObjectPtr<UAnimMontage> HitReactFlinch;
};

/**
 * Holds all tuning values for a single weapon type.
 * Create one DataAsset instance per weapon archetype (AR, SMG, etc.)
 * and reference it from the weapon actor's DefaultWeaponData property.
 */
UCLASS(BlueprintType)
class EXTRACTION_API UWeaponDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:

	// ---- Fire ----

	/** Rounds per second (e.g. 10.0 = 600 RPM) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Fire", meta = (ClampMin = "0.1"))
	float FireRate = 10.0f;

	/** Whether holding fire continuously shoots */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Fire")
	bool bIsAutomatic = true;

	/** Base damage per hit before hitbox multipliers */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Fire", meta = (ClampMin = "0.0"))
	float BaseDamage = 25.0f;

	/** Maximum hitscan trace range in cm */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Fire", meta = (ClampMin = "100.0"))
	float MaxRange = 10000.0f;

	/** Damage type class applied on hit */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Fire")
	TSubclassOf<UExtractionDamageType> DamageTypeClass;

	// ---- Ammo ----

	/** Shots per magazine */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Ammo", meta = (ClampMin = "1"))
	int32 MagazineSize = 30;

	/** Minimum ammo needed for a useful peek/burst. Companion reloads instead of peeking when current mag < this value. */
	UPROPERTY(EditDefaultsOnly, Category = "Combat", meta = (ClampMin = 1, UIMin = 1))
	int32 BurstCount = 3;

	/** Starting reserve ammo (total extra rounds carried) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Ammo", meta = (ClampMin = "0"))
	int32 DefaultReserveAmmo = 120;

	/** Time in seconds for a full reload */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Ammo", meta = (ClampMin = "0.1"))
	float ReloadTime = 2.2f;

	/**
	 * When true, the enemy reload plays the body + gun montages' Loop section once per shell,
	 * ticking CurrentAmmo +1 per shell as each seats, and finishes montage-driven.
	 * Default false keeps the existing single-play full-refill behaviour for every other weapon.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Ammo")
	bool bShellByShellReload = false;

	/** Section name on the body/gun reload montages that represents one shell insert.
	 *  Must match the authored section name exactly. Ignored when bShellByShellReload is false. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Ammo")
	FName ShellReloadLoopSection = TEXT("Loop");

	/** Section name on the body/gun reload montages that plays after the last shell seats.
	 *  Must match the authored section name exactly. Ignored when bShellByShellReload is false. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Ammo")
	FName ShellReloadEndSection = TEXT("End");

	// ---- ADS ----

	/** Target field of view while aiming down sights */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|ADS", meta = (ClampMin = "20.0", ClampMax = "120.0"))
	float ADSFOV = 65.0f;

	/** Time in seconds for the FOV transition */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|ADS", meta = (ClampMin = "0.01"))
	float ADSTransitionTime = 0.15f;

	/** Max walk speed while ADS in cm/s */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|ADS", meta = (ClampMin = "0.0"))
	float ADSMovementSpeed = 400.0f;

	// ---- Recoil ----

	/** Recoil pattern data for this weapon */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Recoil")
	FRecoilPattern RecoilPattern;

	// ---- AI Noise ----

	/** Loudness of each shot for AI hearing (1 = unsuppressed rifle reference). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Noise", meta = (ClampMin = "0.0"))
	float NoiseLoudness = 1.0f;

	/** Max range (cm) at which AI hearing can register a shot. Suppressed weapon assets set this low. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Noise", meta = (ClampMin = "0.0"))
	float NoiseRange = 3000.f;

	/** Loudness of a reload for AI hearing. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Noise", meta = (ClampMin = "0.0"))
	float ReloadNoiseLoudness = 0.3f;

	/** Max range (cm) at which AI hearing can register a reload. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Noise", meta = (ClampMin = "0.0"))
	float ReloadNoiseRange = 600.f;

	/** Marks this weapon as suppressed (stealth-kill semantics; pair with low NoiseLoudness/NoiseRange). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Noise")
	bool bSuppressed = false;

	// ---- Kit Weapon Bridge ----

	/** Kit-side procedural animation values asset (BP-derived DataAsset, e.g. DT_ProceduralAnimValues / DA_Anim_Rifle). Read at runtime via IKitWeaponInterface::GetKitProceduralValues. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Kit Weapon Bridge")
	TObjectPtr<UDataAsset> KitWeaponPoseAsset;

	/** The kit BP_Item_Base weapon spawned for the first-person visual + arm posing (the kit's procedural animation drives the arms for this weapon). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Kit Weapon Bridge")
	TSubclassOf<AActor> KitVisualWeaponClass;

	// ---- Enemy Animation ----

	/**
	 * Weapon animation family. Controls the ABP grip pose, locomotion blend, and aim-offset
	 * variant selected by the enemy's AnimGraph. Default = Rifle so existing enemies are unchanged
	 * until the DA is explicitly configured.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Animation")
	EEnemyWeaponAnimType EnemyWeaponAnimType = EEnemyWeaponAnimType::Rifle;

	/**
	 * Full animation set for this weapon when carried by an enemy.
	 * All montage fields are null by default — the anim instance falls back to its own legacy
	 * single-field UPROPERTYs when a set slot is null (backward-compatible).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Animation")
	FEnemyWeaponAnimSet EnemyAnimSet;

	/**
	 * Socket name on the VISIBLE weapon mesh (ThirdPersonVisualActor's skeletal mesh) used as
	 * the left-hand IK target. NAME_None (default) disables IK — correct for one-handed weapons
	 * (Pistol/Revolver/Shield) and any weapon without an authored socket yet. Set per-weapon
	 * in-engine once the socket exists on the visual actor's skeleton.
	 * UEnemyAnimInstance caches validity on weapon equip, not per-frame.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Animation")
	FName LeftHandGripSocket = NAME_None;

	/**
	 * Inline visual-only recoil profile for this weapon's enemy body kick.
	 * Drives the UEnemyAnimInstance C++ spring solver (spine rotation + weapon kickback).
	 * Replaces the old KINEMATION DA_RecoilData reference — no external asset required.
	 * Tune PitchKick/RecoverySpeed/Sharpness first; all values in degrees or cm.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Animation")
	FEnemyRecoilProfile EnemyRecoilProfile;

	/**
	 * Inline visual-only ADS arm-raise profile for this weapon's enemy clavicle lift while firing.
	 * Drives UEnemyAnimInstance — arms ease up to a braced pose on fire start and ease back on stop.
	 * Tune RaisePitch (degrees, positive = up) and BlendSpeed first; HoldTime smooths semi-auto.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Animation")
	FEnemyAdsProfile EnemyAdsProfile;

};
