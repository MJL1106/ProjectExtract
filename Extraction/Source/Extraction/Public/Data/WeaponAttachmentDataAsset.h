// Per-attachment gameplay modifiers. One asset per attachment option (e.g. DA_Attach_SuppressorLong).

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "ExtractionTypes.h"
#include "WeaponAttachmentDataAsset.generated.h"

class UNiagaraSystem;
class UTexture2D;

/**
 * Stat modifiers an attachment applies to its weapon's base UWeaponDataAsset values.
 * Multiplicative fields default to 1 (neutral); additive fields default to 0.
 * Modifiers from every mounted attachment are combined (mults multiply, deltas add)
 * into the weapon's cached effective stats.
 */
USTRUCT(BlueprintType)
struct EXTRACTION_API FWeaponStatModifiers
{
	GENERATED_BODY()

	/** Multiplies BaseDamage. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Modifiers", meta = (ClampMin = "0.0"))
	float DamageMult = 1.f;

	/** Multiplies the pitch (vertical) component of each recoil pattern point. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Modifiers", meta = (ClampMin = "0.0"))
	float RecoilPitchMult = 1.f;

	/** Multiplies the yaw (horizontal) component of each recoil pattern point. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Modifiers", meta = (ClampMin = "0.0"))
	float RecoilYawMult = 1.f;

	/** Multiplies ADSTransitionTime (>1 = slower to aim). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Modifiers", meta = (ClampMin = "0.0"))
	float ADSTransitionMult = 1.f;

	/** Multiplies ADSMovementSpeed. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Modifiers", meta = (ClampMin = "0.0"))
	float ADSMoveSpeedMult = 1.f;

	/** Multiplies the weapon's HipFireSpreadDeg (laser sets this < 1). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Modifiers", meta = (ClampMin = "0.0"))
	float HipSpreadMult = 1.f;

	/** Multiplies NoiseLoudness for AI hearing (suppressors set this < 1). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Modifiers", meta = (ClampMin = "0.0"))
	float NoiseLoudnessMult = 1.f;

	/** Multiplies NoiseRange for AI hearing (suppressors set this < 1). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Modifiers", meta = (ClampMin = "0.0"))
	float NoiseRangeMult = 1.f;

	/** Multiplies DamageFalloffStartRange (>1 = holds full damage further out). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Modifiers", meta = (ClampMin = "0.0"))
	float FalloffStartMult = 1.f;

	/** Added to the weapon's ADSFOV (negative = more zoom). Ignored when an override is set. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Modifiers")
	float ADSFOVDelta = 0.f;

	/** Replaces the weapon's ADSFOV entirely when > 0 (scopes). 0 = no override. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Modifiers", meta = (ClampMin = "0.0", ClampMax = "120.0"))
	float ADSFOVOverride = 0.f;
};

/**
 * One attachment option's identity + gameplay effects. Referenced from the per-slot
 * option arrays on UWeaponDataAsset (indexed by the kit ST_Attachments enum byte)
 * and by world attachment pickups / the loadout UI.
 */
UCLASS(BlueprintType)
class EXTRACTION_API UWeaponAttachmentDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:

	/** Which slot this attachment occupies. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attachment")
	EAttachmentSlot Slot = EAttachmentSlot::Sight;

	/** UI display name. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attachment")
	FText DisplayName;

	/** UI icon. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attachment")
	TObjectPtr<UTexture2D> Icon;

	/** Stat modifiers applied while this attachment is mounted. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attachment")
	FWeaponStatModifiers Modifiers;

	/** Marks the weapon as suppressed while mounted (stealth-kill semantics).
	 *  Effective suppression = weapon DA bSuppressed OR any mounted attachment with this set. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attachment")
	bool bSetsSuppressed = false;

	/** Replaces the weapon's muzzle flash FX while mounted (suppressors point at the silenced flash).
	 *  Null = keep the weapon DA's MuzzleFlashFX. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attachment")
	TObjectPtr<UNiagaraSystem> MuzzleFlashFXOverride;
};
