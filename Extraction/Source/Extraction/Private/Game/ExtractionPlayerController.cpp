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
#include "TutorialBriefingWidget.h"
#include "ExtractionGameMode.h"
#include "ExtractionGameInstance.h"
#include "Extraction.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Kismet/GameplayStatics.h"
#include "Widgets/Input/SVirtualJoystick.h"
#include "Widgets/SWidget.h"

namespace
{
	// Above every HUD layer so the completion/failure screen popup sits on top.
	constexpr int32 LevelEndPopupZOrder = 50;

	// Above the completion/failure popup: the briefing gates the start of play, nothing outranks it.
	constexpr int32 TutorialBriefingZOrder = 100;

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

	ArmTutorialBriefing();

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

void AExtractionPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// The briefing is armed with SetTimerForNextTick, which hands back no FTimerHandle to clear — so
	// the object-scoped clear is the one that covers it. Nothing else on this controller uses timers.
	if (const UWorld* World = GetWorld())
		World->GetTimerManager().ClearAllTimersForObject(this);

	Super::EndPlay(EndPlayReason);
}

void AExtractionPlayerController::ArmTutorialBriefing()
{
	if (!IsLocalPlayerController()) return;
	if (!IsCurrentMapATutorialMap()) return;

	const FName LevelName = GetCurrentLevelName();
	const UExtractionGameInstance* GI = GetGameInstance<UExtractionGameInstance>();
	if (IsValid(GI) && GI->HasSeenTutorialBriefing(LevelName)) return;

	// Deliberately not shown inline: BP_ExtractionCharacter's BeginPlay graph ends with an
	// unconditional Set Input Mode Game Only, and pawn-vs-controller BeginPlay order is not
	// guaranteed, so an inline show gets stomped roughly half the time. Next tick is after both.
	GetWorldTimerManager().SetTimerForNextTick(this, &AExtractionPlayerController::ShowTutorialBriefing);
}

FName AExtractionPlayerController::GetCurrentLevelName() const
{
	const UWorld* World = GetWorld();
	if (!IsValid(World)) return NAME_None;

	// PIE renames the world package (UEDPIE_0_<Map>), so the raw name never matches designer-
	// assigned soft refs — strip the prefix so both sides compare the same string.
	return FName(*UWorld::RemovePIEPrefix(World->GetOutermost()->GetName()));
}

bool AExtractionPlayerController::IsCurrentMapATutorialMap() const
{
	if (TutorialMaps.IsEmpty()) return false;

	const FString CurrentPackage = GetCurrentLevelName().ToString();
	if (CurrentPackage.IsEmpty()) return false;

	for (const TSoftObjectPtr<UWorld>& Map : TutorialMaps)
	{
		if (Map.IsNull()) continue;
		if (Map.GetLongPackageName() == CurrentPackage) return true;
	}

	return false;
}

void AExtractionPlayerController::ShowTutorialBriefing()
{
	// A null class here means a paused game with no way to unpause — a soft-lock, not a cosmetic miss.
	if (!ensureMsgf(TutorialBriefingWidgetClass, TEXT("TutorialBriefingWidgetClass not assigned on %s — briefing skipped."), *GetName()))
		return;

	if (!IsValid(TutorialBriefingWidget))
		TutorialBriefingWidget = CreateWidget<UTutorialBriefingWidget>(this, TutorialBriefingWidgetClass);
	if (!IsValid(TutorialBriefingWidget)) return;

	if (!TutorialBriefingWidget->IsInViewport())
		TutorialBriefingWidget->AddToPlayerScreen(TutorialBriefingZOrder);

	FInputModeUIOnly InputMode;
	InputMode.SetWidgetToFocus(TutorialBriefingWidget->TakeWidget());
	SetInputMode(InputMode);
	SetShowMouseCursor(true);

	UGameplayStatics::SetGamePaused(this, true);
}

void AExtractionPlayerController::DismissTutorialBriefing()
{
	// Idempotency gate. A double-click on confirm fires this twice; without the gate the second pass
	// would force-unpause and force game input on whatever came up in between (a pause menu, the
	// level-complete screen), and RestoreHUD would run for no reason.
	if (!IsValid(TutorialBriefingWidget)) return;

	UGameplayStatics::SetGamePaused(this, false);

	SetInputMode(FInputModeGameOnly());
	SetShowMouseCursor(false);

	if (IsValid(TutorialBriefingWidget))
		TutorialBriefingWidget->RemoveFromParent();
	TutorialBriefingWidget = nullptr;

	// Idempotent: leaves a HUD that survived the briefing untouched, re-adds anything that was torn
	// off underneath it.
	RestoreHUD();

	if (UExtractionGameInstance* GI = GetGameInstance<UExtractionGameInstance>())
		GI->SetTutorialBriefingSeen(GetCurrentLevelName());
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
