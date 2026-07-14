// Copyright Epic Games, Inc. All Rights Reserved.

#include "ExtractionGameMode.h"
#include "ExtractionCharacter.h"
#include "ExtractionGameInstance.h"
#include "ExtractionPlayerController.h"
#include "Kismet/GameplayStatics.h"

AExtractionGameMode::AExtractionGameMode()
{
	DefaultPawnClass = AExtractionCharacter::StaticClass();
	PlayerControllerClass = AExtractionPlayerController::StaticClass();
}

void AExtractionGameMode::CompleteLevel()
{
	if (bLevelCompleted || bLevelFailed) return;
	bLevelCompleted = true;

	// A finished level leaves no resume point behind — the next run starts clean.
	if (UExtractionGameInstance* GameInstance = GetGameInstance<UExtractionGameInstance>())
		GameInstance->ClearCheckpoint();

	UGameplayStatics::SetGamePaused(this, true);

	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (AExtractionPlayerController* PC = Cast<AExtractionPlayerController>(It->Get()))
			PC->ClientShowLevelComplete();
	}
}

void AExtractionGameMode::FailLevel(const FText& Reason)
{
	if (bLevelCompleted || bLevelFailed) return;
	bLevelFailed = true;

	UGameplayStatics::SetGamePaused(this, true);

	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (AExtractionPlayerController* PC = Cast<AExtractionPlayerController>(It->Get()))
			PC->ClientShowLevelFailed(Reason);
	}
}

void AExtractionGameMode::RestartCurrentLevel()
{
	bLevelCompleted = false;
	bLevelFailed = false;
	UGameplayStatics::SetGamePaused(this, false);
	UGameplayStatics::OpenLevel(this, FName(*UGameplayStatics::GetCurrentLevelName(this, true)));
}
