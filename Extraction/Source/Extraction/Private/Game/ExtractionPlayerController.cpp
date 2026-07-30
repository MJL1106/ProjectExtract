// Copyright Epic Games, Inc. All Rights Reserved.


#include "ExtractionPlayerController.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "InputMappingContext.h"
#include "ExtractionCameraManager.h"
#include "Blueprint/UserWidget.h"
#include "PlayerHealthWidget.h"
#include "AmmoWidget.h"
#include "CompanionModeWidget.h"
#include "ObjectiveMarkerLayer.h"
#include "ObjectiveTextPanelWidget.h"
#include "Game/ObjectiveSubsystem.h"
#include "World/ObjectiveMarkerDisplay.h"
#include "LootNotificationWidget.h"
#include "LevelCompleteWidget.h"
#include "LevelFailedWidget.h"
#include "RevivePromptWidget.h"
#include "HitmarkerWidget.h"
#include "DamageNumberWidget.h"
#include "ConsumableWidget.h"
#include "ExtractionGameMode.h"
#include "Extraction.h"
#include "Widgets/Input/SVirtualJoystick.h"
#include "Widgets/SWidget.h"

namespace
{
	// Above every HUD layer so the completion/failure screen popup sits on top.
	constexpr int32 LevelEndPopupZOrder = 50;

	// Every widget the controller owns shares the base layer; add order decides the stack.
	constexpr int32 HUDLayerZOrder = 0;

	/** True only when the widget is both registered with the viewport AND its Slate widget still has
	 *  a live parent. IsInViewport() alone is not enough: it reads a cached flag that only
	 *  UGameViewportSubsystem maintains, while UWidgetLayoutLibrary::RemoveAllWidgets empties the
	 *  viewport overlay through UGameViewportClient without telling that subsystem — so an orphaned
	 *  widget keeps reporting itself as added while being nowhere on screen. */
	bool IsWidgetLiveOnPlayerScreen(const UUserWidget* Widget)
	{
		if (!IsValid(Widget)) return false;
		if (!Widget->IsInViewport()) return false;

		const TSharedPtr<SWidget> CachedWidget = Widget->GetCachedWidget();
		return CachedWidget.IsValid() && CachedWidget->IsParentValid();
	}

	/** Creates the widget if it does not exist, then puts it on the player screen if it is not
	 *  already there. Existing widgets are re-added rather than rebuilt, so pooled children and
	 *  accumulated state survive; an unassigned class is skipped silently. */
	template <typename TWidget>
	void EnsureOnPlayerScreen(APlayerController* Owner, TObjectPtr<TWidget>& Instance, const TSubclassOf<TWidget>& WidgetClass, int32 ZOrder)
	{
		if (!WidgetClass) return;

		const bool bFreshInstance = !IsValid(Instance);
		if (bFreshInstance) Instance = CreateWidget<TWidget>(Owner, WidgetClass);
		if (!IsValid(Instance)) return;

		if (!bFreshInstance)
		{
			if (IsWidgetLiveOnPlayerScreen(Instance)) return;
			// Releases a stale viewport registration so the re-add is not refused as a duplicate.
			Instance->RemoveFromParent();
		}

		if (Instance->AddToPlayerScreen(ZOrder)) return;
		if (bFreshInstance) return;

		// The viewport refused the re-add. Losing this widget's state beats losing the widget.
		Instance = CreateWidget<TWidget>(Owner, WidgetClass);
		if (IsValid(Instance)) Instance->AddToPlayerScreen(ZOrder);
	}
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

}

void AExtractionPlayerController::BeginPlay()
{
	Super::BeginPlay();

	RestoreHUD();

	if (!IsLocalPlayerController()) return;

	// Supply world-space marker display class to the objective subsystem. Deliberately NOT part of
	// RestoreHUD: the subsystem keeps the class for the level's lifetime, and re-supplying it on
	// every rebuild would be redundant work on a path that must stay side-effect free.
	if (!MarkerDisplayClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("AExtractionPlayerController: MarkerDisplayClass is null -- "
			"world-space objective markers will not spawn. Assign it in the BP subclass defaults."));
		return;
	}

	if (UObjectiveSubsystem* Objectives = GetWorld()->GetSubsystem<UObjectiveSubsystem>())
		Objectives->SetMarkerDisplayClass(MarkerDisplayClass);
}

void AExtractionPlayerController::RestoreHUD()
{
	if (!IsLocalPlayerController()) return;

	// Add order is layer order — every widget here shares HUDLayerZOrder, so the viewport stacks
	// them in the order they are added. Keep this sequence as-is; it is BeginPlay's original order.
	if (ShouldUseTouchControls())
	{
		EnsureOnPlayerScreen(this, MobileControlsWidget, MobileControlsWidgetClass, HUDLayerZOrder);
		if (MobileControlsWidgetClass && !IsValid(MobileControlsWidget))
			UE_LOG(LogExtraction, Error, TEXT("Could not spawn mobile controls widget."));
	}

	EnsureOnPlayerScreen(this, HUDWidget, HUDWidgetClass, HUDLayerZOrder);
	EnsureOnPlayerScreen(this, AmmoWidget, AmmoWidgetClass, HUDLayerZOrder);
	EnsureOnPlayerScreen(this, CompanionModeWidget, CompanionModeWidgetClass, HUDLayerZOrder);
	EnsureOnPlayerScreen(this, ObjectiveLayerWidget, ObjectiveLayerWidgetClass, HUDLayerZOrder);
	EnsureOnPlayerScreen(this, ObjectiveTextPanelWidget, ObjectiveTextPanelWidgetClass, HUDLayerZOrder);
	EnsureOnPlayerScreen(this, LootToastWidget, LootToastWidgetClass, HUDLayerZOrder);
	EnsureOnPlayerScreen(this, RevivePromptWidget, RevivePromptWidgetClass, HUDLayerZOrder);
	EnsureOnPlayerScreen(this, HitmarkerWidget, HitmarkerWidgetClass, HUDLayerZOrder);
	EnsureOnPlayerScreen(this, DamageNumberWidget, DamageNumberWidgetClass, HUDLayerZOrder);
	EnsureOnPlayerScreen(this, ConsumableWidget, ConsumableWidgetClass, HUDLayerZOrder);
}

void AExtractionPlayerController::NotifyDamageDealt(AActor* Victim, float Damage, float HeadshotDamage, bool bKilled, const FVector& WorldLocation)
{
	if (!IsLocalPlayerController()) return;
	OnDamageDealt.Broadcast(Victim, Damage, HeadshotDamage, bKilled, WorldLocation);
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
