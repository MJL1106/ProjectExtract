// UBarkSetData — per-archetype bark lines and sounds, keyed by bark type.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "EnemyTypes.h"
#include "BarkSetData.generated.h"

class USoundBase;

USTRUCT(BlueprintType)
struct FBarkDefinition
{
	GENERATED_BODY()

	/** Subtitle lines — one is picked at random per bark. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bark")
	TArray<FText> Lines;

	/** Per-speaker cooldown for this bark type. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bark", meta = (ClampMin = "0.0"))
	float CooldownSeconds = 6.f;

	/** Optional SFX played at the speaker's location. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bark")
	TObjectPtr<USoundBase> Sound;
};

UCLASS(BlueprintType)
class EXTRACTION_API UBarkSetData : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Barks")
	TMap<EBarkType, FBarkDefinition> Barks;
};
