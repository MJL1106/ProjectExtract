// UBarkSubsystem — routes enemy barks to the subtitle feed with per-speaker and global dedup cooldowns.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "UObject/ObjectKey.h"
#include "EnemyTypes.h"
#include "BarkSubsystem.generated.h"

class UBarkSetData;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnBarkRequested, FText, SpeakerName, FText, Line);

UCLASS()
class EXTRACTION_API UBarkSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Picks a line for the bark type and broadcasts it to the subtitle feed, honouring cooldowns. */
	void RequestBark(const AActor* Speaker, const UBarkSetData* BarkSet, EBarkType Type, const FText& SpeakerName);

	UPROPERTY(BlueprintAssignable, Category = "Enemy|Barks")
	FOnBarkRequested OnBarkRequested;

private:
	/** Same bark type from any speaker within this window is dropped (no five-voice "Contact!" stacks). */
	static constexpr float GlobalTypeDedupWindow = 1.5f;

	TMap<TPair<FObjectKey, uint8>, float> LastBarkTimePerSpeaker;
	TMap<EBarkType, float> LastBarkTimePerType;
};
