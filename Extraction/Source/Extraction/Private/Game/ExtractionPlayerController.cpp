// Copyright Epic Games, Inc. All Rights Reserved.


#include "ExtractionPlayerController.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "InputMappingContext.h"
#include "ExtractionCameraManager.h"
#include "Blueprint/UserWidget.h"
#include "PlayerHealthWidget.h"
#include "AmmoWidget.h"
#include "BarkFeedWidget.h"
#include "CompanionModeWidget.h"
#include "ObjectiveMarkerLayer.h"
#include "LootNotificationWidget.h"
#include "LevelCompleteWidget.h"
#include "LevelFailedWidget.h"
#include "ExtractionGameMode.h"
#include "Extraction.h"
#include "Widgets/Input/SVirtualJoystick.h"

namespace
{
	// Above every HUD layer so the completion/failure screen popup sits on top.
	constexpr int32 LevelEndPopupZOrder = 50;
}

AExtractionPlayerController::AExtractionPlayerController()
{
	// set the player camera manager class
	PlayerCameraManagerClass = AExtractionCameraManager::StaticClass();

	// Default HUD widget class
	static ConstructorHelpers::FClassFinder<UPlayerHealthWidget> HUDWidgetBP(
		TEXT("/Game/Core/UI/WBP_PlayerHealth"));
	if (HUDWidgetBP.Succeeded())
		HUDWidgetClass = HUDWidgetBP.Class;

	// Default ammo widget class
	static ConstructorHelpers::FClassFinder<UAmmoWidget> AmmoBP(
		TEXT("/Game/Core/UI/WBP_Ammo"));
	if (AmmoBP.Succeeded())
		AmmoWidgetClass = AmmoBP.Class;

	// Default bark subtitle feed widget class
	static ConstructorHelpers::FClassFinder<UBarkFeedWidget> BarkFeedBP(
		TEXT("/Game/Core/UI/WBP_BarkFeed"));
	if (BarkFeedBP.Succeeded())
		BarkFeedWidgetClass = BarkFeedBP.Class;
}

void AExtractionPlayerController::BeginPlay()
{
	Super::BeginPlay();

	
	// only spawn touch controls on local player controllers
	if (ShouldUseTouchControls() && IsLocalPlayerController())
	{
		// spawn the mobile controls widget
		MobileControlsWidget = CreateWidget<UUserWidget>(this, MobileControlsWidgetClass);

		if (MobileControlsWidget)
		{
			// add the controls to the player screen
			MobileControlsWidget->AddToPlayerScreen(0);

		} else {

			UE_LOG(LogExtraction, Error, TEXT("Could not spawn mobile controls widget."));

		}

	}

	// Spawn health/shield HUD for local player
	if (IsLocalPlayerController() && HUDWidgetClass)
	{
		HUDWidget = CreateWidget<UPlayerHealthWidget>(this, HUDWidgetClass);
		if (IsValid(HUDWidget))
			HUDWidget->AddToPlayerScreen();
	}

	// Spawn ammo display for local player
	if (IsLocalPlayerController() && AmmoWidgetClass)
	{
		AmmoWidget = CreateWidget<UAmmoWidget>(this, AmmoWidgetClass);
		if (IsValid(AmmoWidget))
			AmmoWidget->AddToPlayerScreen();
	}

	// Spawn enemy bark subtitle feed for local player
	if (IsLocalPlayerController() && BarkFeedWidgetClass)
	{
		BarkFeedWidget = CreateWidget<UBarkFeedWidget>(this, BarkFeedWidgetClass);
		if (IsValid(BarkFeedWidget))
			BarkFeedWidget->AddToPlayerScreen();
	}

	// Spawn companion mode chip for local player
	if (IsLocalPlayerController() && CompanionModeWidgetClass)
	{
		CompanionModeWidget = CreateWidget<UCompanionModeWidget>(this, CompanionModeWidgetClass);
		if (IsValid(CompanionModeWidget))
			CompanionModeWidget->AddToPlayerScreen();
	}

	// Spawn objective waypoint layer for local player
	if (IsLocalPlayerController() && ObjectiveLayerWidgetClass)
	{
		ObjectiveLayerWidget = CreateWidget<UObjectiveMarkerLayer>(this, ObjectiveLayerWidgetClass);
		if (IsValid(ObjectiveLayerWidget))
			ObjectiveLayerWidget->AddToPlayerScreen();
	}

	// Spawn loot acquisition toast for local player
	if (IsLocalPlayerController() && LootToastWidgetClass)
	{
		LootToastWidget = CreateWidget<ULootNotificationWidget>(this, LootToastWidgetClass);
		if (IsValid(LootToastWidget))
			LootToastWidget->AddToPlayerScreen();
	}
}

void AExtractionPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	// only add IMCs for local player controllers
	if (IsLocalPlayerController())
	{
		// Add Input Mapping Context
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
		{
			for (UInputMappingContext* CurrentContext : DefaultMappingContexts)
			{
				Subsystem->AddMappingContext(CurrentContext, 0);
			}

			// only add these IMCs if we're not using mobile touch input
			if (!ShouldUseTouchControls())
			{
				for (UInputMappingContext* CurrentContext : MobileExcludedMappingContexts)
				{
					Subsystem->AddMappingContext(CurrentContext, 0);
				}
			}
		}
	}
	
}

void AExtractionPlayerController::ClientShowLevelComplete_Implementation()
{
	if (!IsLocalPlayerController()) return;
	// A null class here means the game is paused with no restart path — a soft-lock, not a cosmetic miss.
	if (!ensureMsgf(LevelCompleteWidgetClass, TEXT("LevelCompleteWidgetClass not assigned on %s — game is paused with no completion screen."), *GetName()))
		return;

	if (!IsValid(LevelCompleteWidget))
		LevelCompleteWidget = CreateWidget<ULevelCompleteWidget>(this, LevelCompleteWidgetClass);
	if (!IsValid(LevelCompleteWidget)) return;

	if (!LevelCompleteWidget->IsInViewport())
		LevelCompleteWidget->AddToPlayerScreen(LevelEndPopupZOrder);

	FInputModeUIOnly InputMode;
	InputMode.SetWidgetToFocus(LevelCompleteWidget->TakeWidget());
	SetInputMode(InputMode);
	SetShowMouseCursor(true);
}

void AExtractionPlayerController::ClientShowLevelFailed_Implementation(const FText& Reason)
{
	if (!IsLocalPlayerController()) return;
	if (!ensureMsgf(LevelFailedWidgetClass, TEXT("LevelFailedWidgetClass not assigned on %s — game is paused with no failure screen."), *GetName()))
		return;

	if (!IsValid(LevelFailedWidget))
		LevelFailedWidget = CreateWidget<ULevelFailedWidget>(this, LevelFailedWidgetClass);
	if (!IsValid(LevelFailedWidget)) return;

	LevelFailedWidget->SetFailReason(Reason);

	if (!LevelFailedWidget->IsInViewport())
		LevelFailedWidget->AddToPlayerScreen(LevelEndPopupZOrder);

	FInputModeUIOnly InputMode;
	InputMode.SetWidgetToFocus(LevelFailedWidget->TakeWidget());
	SetInputMode(InputMode);
	SetShowMouseCursor(true);
}

void AExtractionPlayerController::RequestRestartLevel()
{
	// FInputModeUIOnly state lives on the GameViewportClient, which survives OpenLevel —
	// restore game input here or the reloaded level starts with input ignored.
	SetInputMode(FInputModeGameOnly());
	SetShowMouseCursor(false);

	ServerRequestRestartLevel();
}

void AExtractionPlayerController::ServerRequestRestartLevel_Implementation()
{
	if (AExtractionGameMode* GameMode = GetWorld()->GetAuthGameMode<AExtractionGameMode>())
		GameMode->RestartCurrentLevel();
}

bool AExtractionPlayerController::ShouldUseTouchControls() const
{
	// are we on a mobile platform? Should we force touch?
	return SVirtualJoystick::ShouldDisplayTouchInterface() || bForceTouchControls;
}
