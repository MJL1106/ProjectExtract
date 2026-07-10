// Copyright Epic Games, Inc. All Rights Reserved.

#include "ExtractionGameMode.h"
#include "ExtractionCharacter.h"
#include "ExtractionPlayerController.h"
#include "Kismet/GameplayStatics.h"

AExtractionGameMode::AExtractionGameMode()
{
	DefaultPawnClass = AExtractionCharacter::StaticClass();
	PlayerControllerClass = AExtractionPlayerController::StaticClass();
}

void AExtractionGameMode::CompleteLevel()
{
	if (bLevelCompleted) return;
	bLevelCompleted = true;

	UGameplayStatics::SetGamePaused(this, true);

	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (AExtractionPlayerController* PC = Cast<AExtractionPlayerController>(It->Get()))
			PC->ClientShowLevelComplete();
	}
}

void AExtractionGameMode::RestartCurrentLevel()
{
	bLevelCompleted = false;
	UGameplayStatics::SetGamePaused(this, false);
	UGameplayStatics::OpenLevel(this, FName(*UGameplayStatics::GetCurrentLevelName(this, true)));
}
