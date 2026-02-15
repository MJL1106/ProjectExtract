// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "ExtractionGameMode.generated.h"

/**
 * Base GameMode for Extraction.
 * Sets default pawn, controller, and HUD classes.
 * Subclass or use a Blueprint child for level-specific overrides.
 */
UCLASS()
class EXTRACTION_API AExtractionGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AExtractionGameMode();
};



