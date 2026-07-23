// UExtractionAudioSettings — Project Settings entry pointing at the game's USurfaceAudioBank.
// Keeps C++ asset-path-free: the designer sets the bank once under Project Settings → Game →
// Extraction Audio, and UGameAudioSubsystem loads it at world init.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "ExtractionAudioSettings.generated.h"

class USurfaceAudioBank;

UCLASS(config = Game, defaultconfig, meta = (DisplayName = "Extraction Audio"))
class EXTRACTION_API UExtractionAudioSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UPROPERTY(config, EditAnywhere, Category = "Audio")
	TSoftObjectPtr<USurfaceAudioBank> AudioBank;
};
