// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "ExtractionGameInstance.generated.h"

/**
 * Persistent game instance for Extraction.
 * Survives level transitions — use for player loadouts,
 * progression data, and session management.
 */
UCLASS()
class EXTRACTION_API UExtractionGameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	UExtractionGameInstance();
};
