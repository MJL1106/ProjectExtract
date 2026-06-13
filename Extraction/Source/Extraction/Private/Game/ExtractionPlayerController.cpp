// Copyright Epic Games, Inc. All Rights Reserved.


#include "ExtractionPlayerController.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "InputMappingContext.h"
#include "ExtractionCameraManager.h"
#include "Blueprint/UserWidget.h"
#include "PlayerHealthWidget.h"
#include "CrosshairWidget.h"
#include "AmmoWidget.h"
#include "BarkFeedWidget.h"
#include "Extraction.h"
#include "Widgets/Input/SVirtualJoystick.h"

AExtractionPlayerController::AExtractionPlayerController()
{
	// set the player camera manager class
	PlayerCameraManagerClass = AExtractionCameraManager::StaticClass();

	// Default HUD widget class
	static ConstructorHelpers::FClassFinder<UPlayerHealthWidget> HUDWidgetBP(
		TEXT("/Game/Core/UI/WBP_PlayerHealth"));
	if (HUDWidgetBP.Succeeded())
		HUDWidgetClass = HUDWidgetBP.Class;

	// Default crosshair widget class
	static ConstructorHelpers::FClassFinder<UCrosshairWidget> CrosshairBP(
		TEXT("/Game/Core/UI/WBP_Crosshair"));
	if (CrosshairBP.Succeeded())
		CrosshairWidgetClass = CrosshairBP.Class;

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

	// Spawn crosshair for local player
	if (IsLocalPlayerController() && CrosshairWidgetClass)
	{
		CrosshairWidget = CreateWidget<UCrosshairWidget>(this, CrosshairWidgetClass);
		if (IsValid(CrosshairWidget))
			CrosshairWidget->AddToPlayerScreen();
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

bool AExtractionPlayerController::ShouldUseTouchControls() const
{
	// are we on a mobile platform? Should we force touch?
	return SVirtualJoystick::ShouldDisplayTouchInterface() || bForceTouchControls;
}
