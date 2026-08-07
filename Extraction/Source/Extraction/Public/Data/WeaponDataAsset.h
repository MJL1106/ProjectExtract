// Data-driven weapon configuration. One asset per weapon archetype.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "ExtractionTypes.h"
#include "EnemyTypes.h"
#include "Animation/AnimMontage.h"
#include "WeaponDataAsset.generated.h"

class UExtractionDamageType;
class UMaterialInterface;
class UNiagaraSystem;
class UDamageMitigationSettings;
class UWeaponAttachmentDataAsset;
class USoundBase;
class UTexture2D;

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

	virtual void PostLoad() override;

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

	/** Hitscan pellets per shot. 1 = single trace (default — every non-shotgun weapon is unchanged). >1 = shotgun spread. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Fire", meta = (ClampMin = "1"))
	int32 PelletCount = 1;

	/** Cone half-angle (deg) applied to pellets beyond the center one. 0 = no spread. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Fire", meta = (ClampMin = "0.0"))
	float PelletSpreadDeg = 0.f;

	/** Enable range-based per-pellet damage falloff. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Fire")
	bool bUseDamageFalloff = false;

	/** Full damage up to this distance (cm). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Fire", meta = (ClampMin = "0.0"))
	float DamageFalloffStartRange = 0.f;

	/** Damage reaches MinDamageFraction at/after this distance (cm). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Fire", meta = (ClampMin = "0.0"))
	float DamageFalloffEndRange = 0.f;

	/** Per-pellet damage fraction at/after DamageFalloffEndRange. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Fire", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MinDamageFraction = 1.f;

	/** Damage type class applied on hit */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Fire")
	TSubclassOf<UExtractionDamageType> DamageTypeClass;

	/** Hip-fire cone half-angle (deg) applied to PLAYER shots when not ADS. 0 = laser-accurate
	 *  hip fire (current behaviour). AI fire is unaffected — enemies/companion keep their own
	 *  inaccuracy model via IAIShooterInterface. Attachments scale this via HipSpreadMult. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Fire", meta = (ClampMin = "0.0"))
	float HipFireSpreadDeg = 0.f;

	/** Sphere-sweep radius (cm) for PLAYER hitscan traces — slight hit forgiveness so near-miss
	 *  shots (especially headshots) connect. 0 = exact line trace. AI fire always line-traces so
	 *  enemies don't inherit the forgiveness. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Fire", meta = (ClampMin = "0.0", ClampMax = "10.0"))
	float BulletSweepRadius = 2.f;

	// ---- HUD ----

	/** Weapon name shown on the HUD's active-weapon readout. Empty = the HUD falls back to
	 *  whatever it displays for an unnamed weapon (the C++ side never substitutes an asset name). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|HUD")
	FText DisplayName;

	/** Weapon silhouette shown next to the name on the HUD. Null = icon slot stays empty.
	 *  Hard reference, same as every other asset field on this DA — the whole asset is already
	 *  resident whenever a weapon of this archetype exists. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|HUD")
	TObjectPtr<UTexture2D> HudIcon;

	// ---- Attachments ----

	/** Per-slot attachment gameplay options, indexed by the kit ST_Attachments enum byte for that
	 *  slot — index N holds the modifier asset for kit enum value N. Null / missing index = the
	 *  selection has no gameplay effect (cosmetic only). Authored per weapon; a weapon without a
	 *  slot (e.g. pistol handguard) leaves the array empty. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Attachments")
	TArray<TObjectPtr<UWeaponAttachmentDataAsset>> SightAttachments;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Attachments")
	TArray<TObjectPtr<UWeaponAttachmentDataAsset>> MuzzleAttachments;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Attachments")
	TArray<TObjectPtr<UWeaponAttachmentDataAsset>> LaserAttachments;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Attachments")
	TArray<TObjectPtr<UWeaponAttachmentDataAsset>> GripAttachments;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Attachments")
	TArray<TObjectPtr<UWeaponAttachmentDataAsset>> HandguardAttachments;

	/** Per-slot attachment COMPATIBILITY: the kit enum bytes this weapon accepts in each slot.
	 *  Curated per gun — world attachment pickups refuse any option not listed for the held
	 *  weapon. Empty list = the weapon lacks that slot entirely. Separate from the *Attachments
	 *  stat arrays above (those map byte -> gameplay modifiers; null there means cosmetic-only,
	 *  not unsupported). Byte 0 (empty/ironsight) never needs listing — pickups never carry it. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Attachments|Compatibility")
	TArray<uint8> AcceptedSightOptions;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Attachments|Compatibility")
	TArray<uint8> AcceptedMuzzleOptions;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Attachments|Compatibility")
	TArray<uint8> AcceptedLaserOptions;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Attachments|Compatibility")
	TArray<uint8> AcceptedGripOptions;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Attachments|Compatibility")
	TArray<uint8> AcceptedHandguardOptions;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Attachments|Compatibility")
	TArray<uint8> AcceptedBarrelOptions;

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

	/** When true, reloads never consume reserve and CanReload ignores the reserve>0 requirement —
	 *  the magazine and reload cadence stay finite, only the pool is bottomless. Set on the
	 *  companion's weapon (design: companion never permanently runs dry). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Ammo")
	bool bInfiniteReserve = false;

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

	/** Settle time (seconds) after a reload completes before the weapon may fire again. Lets the
	 *  reload animation's end section play out so the gun is back in the hand before the next shot.
	 *  CanFire() blocks until this elapses. 0 = fire immediately (player weapons leave it at 0). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Ammo", meta = (ClampMin = "0.0"))
	float PostReloadFireDelay = 0.f;

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

	/** Extra look-sensitivity multiplier while ADS, on top of the automatic ADSFOV/90 scaling.
	 *  1 = FOV-relative only (CoD-style). Set below 1 for optics whose magnification lives in a
	 *  render-target lens rather than the camera FOV (sniper scope). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|ADS", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float ADSLookSensitivityMult = 1.0f;

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

	// ---- AI Damage Mitigation ----

	/** Per-weapon AI damage mitigation settings. Null = gate disabled (fail-open).
	 *  Player weapon DAs leave this null so player bullets always damage. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|AI")
	TObjectPtr<UDamageMitigationSettings> DamageMitigation;

	// ---- FX ----

	/** Niagara particle system spawned at the muzzle on each shot. Null = no muzzle flash.
	 *  The system is attached once and re-activated per shot (pooled, never spawned per shot). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|FX")
	TObjectPtr<UNiagaraSystem> MuzzleFlashFX;

	/** Relative rotation applied to the first-person muzzle flash where the kit item's Muzzle
	 *  component axes don't match the FX forward axis (e.g. Infima frames are Y-forward while
	 *  the flash emits along X — set Yaw=90 there). Zero = aligned with the Muzzle component. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|FX")
	FRotator FirstPersonMuzzleFlashRotation = FRotator::ZeroRotator;

	/** Niagara system spawned per shot as a bullet tracer streak along the fire line.
	 *  Null = no tracer. Spawned at the muzzle, oriented toward the end point, engine-pooled
	 *  via ENCPoolMethod::AutoRelease (satisfies >1/sec pooling rule without a hand-rolled pool). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|FX")
	TObjectPtr<UNiagaraSystem> TracerFX;

	/** Name of the Niagara Vector user parameter that receives the shot end position.
	 *  Keeps C++ agnostic to whichever tracer system pack is assigned. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|FX")
	FName TracerEndParamName = TEXT("TracerEnd");

	/** Decal material stamped on world geometry per pellet hit (bullet hole). Null = no decal.
	 *  Rendered through the pooled UImpactDecalSubsystem ring buffer. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|FX")
	TObjectPtr<UMaterialInterface> ImpactDecalMaterial;

	/** Bullet-hole decal edge size (cm). Slight per-hit jitter is applied on top. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|FX", meta = (ClampMin = "1.0"))
	float ImpactDecalSize = 8.f;

	/** Seconds before a bullet-hole decal starts fading out. The pool cap recycles the oldest
	 *  holes regardless, so this is a soft lifetime. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|FX", meta = (ClampMin = "1.0"))
	float ImpactDecalLifetime = 60.f;

	/** Niagara puff/sparks spawned at each world impact point, oriented to the surface normal.
	 *  Null = no impact FX. Engine-pooled (AutoRelease), same as TracerFX. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|FX")
	TObjectPtr<UNiagaraSystem> ImpactFX;

	/** Fire report played at the muzzle per shot. Null = silent (enemy DAs stay null until assigned).
	 *  Played from C++ (Multicast_PlayFireFX) — the kit BP fire-audio chain is dead code for the
	 *  C++-driven fire path, so this field is the single source of the shot sound. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|FX")
	TObjectPtr<USoundBase> FireSound;

	/** Fire report used instead of FireSound while a suppressor is equipped (muzzle-slot attachment)
	 *  or the weapon is effectively suppressed. Null = FireSound plays regardless of suppressor. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|FX")
	TObjectPtr<USoundBase> SuppressedFireSound;

	/** Click played once per trigger press when firing dry (empty mag, not reloading). Null = silent. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|FX")
	TObjectPtr<USoundBase> DryFireSound;

	/** Reload foley played attached to the weapon for the duration of a reload (authored to match
	 *  the reload animation timings). Null = silent reload (current behaviour — enemy DAs stay null). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|FX")
	TObjectPtr<USoundBase> ReloadSound;

	/** Variant played when reloading from an empty magazine (adds the bolt/slide-release layer).
	 *  Null = fall back to ReloadSound for both reload types. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|FX")
	TObjectPtr<USoundBase> ReloadEmptySound;

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

	// ---- Enemy Patrol Weapon Alignment ----

	/** Location offset (cm) applied to the weapon's rest relative transform while patrolling.
	 *  Defines the relaxed-carry position RELATIVE to the ADS/rest attach. Zero = weapon stays at ADS.
	 *  Designers tune per weapon; no behavior change until non-zero values are entered. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Animation",
		meta = (DisplayName = "Patrol Align Location Offset"))
	FVector PatrolAlignLocationOffset = FVector::ZeroVector;

	/** Rotation offset (degrees) applied to the weapon's rest relative transform while patrolling.
	 *  Defines the relaxed-carry rotation RELATIVE to the ADS/rest attach. Zero = weapon stays at ADS.
	 *  Designers tune per weapon; no behavior change until non-zero values are entered. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Animation",
		meta = (DisplayName = "Patrol Align Rotation Offset"))
	FRotator PatrolAlignRotationOffset = FRotator::ZeroRotator;

	// ---- Enemy Hand-Swap (two-socket weapon carry) ----

	/** Skeleton socket used when the weapon is in the COMBAT hand (aim/fire/melee).
	 *  NAME_None (default) falls back to the character's WeaponSocket member, so existing
	 *  archetypes are completely unaffected. Set this to the same socket name as WeaponSocket
	 *  on the character when using hand-swap so the DA is the single source of truth. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Animation",
		meta = (DisplayName = "Enemy Combat Hand Socket"))
	FName EnemyCombatHandSocket = NAME_None;

	/** Skeleton socket used when the weapon is in the PATROL/IDLE hand.
	 *  NAME_None (default) disables the hand-swap feature entirely — no re-attach ever happens,
	 *  making every archetype without this field set completely unaffected.
	 *  When set, the weapon eases between this socket and EnemyCombatHandSocket as the enemy
	 *  transitions between patrol and combat awareness states. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Animation",
		meta = (DisplayName = "Enemy Patrol Hand Socket"))
	FName EnemyPatrolHandSocket = NAME_None;

};
