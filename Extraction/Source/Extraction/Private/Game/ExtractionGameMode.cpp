// Copyright Epic Games, Inc. All Rights Reserved.

#include "ExtractionGameMode.h"
#include "ExtractionCharacter.h"
#include "ExtractionPlayerController.h"

AExtractionGameMode::AExtractionGameMode()
{
	DefaultPawnClass = AExtractionCharacter::StaticClass();
	PlayerControllerClass = AExtractionPlayerController::StaticClass();
}
