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

	/** Idempotent level completion: pauses gameplay and shows the completion UI on every player. */
	UFUNCTION(BlueprintCallable, Category = "Game|Completion")
	void CompleteLevel();

	/** Unpauses and reloads the current level, resetting all one-shot state. */
	UFUNCTION(BlueprintCallable, Category = "Game|Completion")
	void RestartCurrentLevel();

private:
	bool bLevelCompleted = false;
};
