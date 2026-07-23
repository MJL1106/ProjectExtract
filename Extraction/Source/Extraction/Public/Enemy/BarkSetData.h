// UBarkSetData — per-archetype bark lines and VO, keyed by bark type.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "EnemyTypes.h"
#include "BarkSetData.generated.h"

class USoundBase;
class USoundAttenuation;

/** One spoken variant of a bark — subtitle text plus its own VO take. */
USTRUCT(BlueprintType)
struct FBarkVariant
{
	GENERATED_BODY()

	/** Subtitle/debug text for this take. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bark")
	FText Line;

	/** VO for this take, played 3D at the speaker. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bark")
	TObjectPtr<USoundBase> Sound;

	/** Optional situational tag (e.g. direction or archetype the line names). NAME_None = generic —
	 *  always eligible. A tagged variant is only picked when the trigger requests the same tag, so a
	 *  "Sniper — get down!" line can never fire about a Heavy. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bark")
	FName Context;
};

USTRUCT(BlueprintType)
struct FBarkDefinition
{
	GENERATED_BODY()

	/** Spoken variants — one is picked at random per bark (filtered by Context when requested). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bark")
	TArray<FBarkVariant> Variants;

	/** Per-speaker cooldown for this bark type. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bark", meta = (ClampMin = "0.0"))
	float CooldownSeconds = 6.f;

	/** Arbitration priority: 0 = ambient/banter (only surfaces in lulls), 1 = normal combat call,
	 *  2+ = critical telegraph (grenade out, man down) — interrupts the active voice line. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bark", meta = (ClampMin = "0"))
	int32 Priority = 1;
};

UCLASS(BlueprintType)
class EXTRACTION_API UBarkSetData : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Barks")
	TMap<EBarkType, FBarkDefinition> Barks;

	/** Distance attenuation applied to every VO line from this set — designer-authored falloff
	 *  (enemy voices fade to silent around the subsystem's earshot range). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Barks")
	TObjectPtr<USoundAttenuation> Attenuation;

	/** Linear volume multiplier applied to every VO line from this set at playback. >1 makes this
	 *  faction's shouts louder; tune per-set in the editor without a rebuild. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Barks", meta = (ClampMin = "0.0"))
	float VolumeMultiplier = 1.5f;
};
