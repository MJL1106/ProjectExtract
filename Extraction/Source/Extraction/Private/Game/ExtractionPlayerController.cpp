// Copyright Epic Games, Inc. All Rights Reserved.


#include "ExtractionPlayerController.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "InputMappingContext.h"
#include "ExtractionCameraManager.h"
#include "Blueprint/UserWidget.h"
#include "ObjectiveMarkerLayer.h"
#include "UI/AIOverlayLayer.h"
#include "Game/ObjectiveSubsystem.h"
#include "World/ObjectiveMarkerDisplay.h"
#include "LevelCompleteWidget.h"
#include "LevelFailedWidget.h"
#include "RevivePromptWidget.h"
#include "HitmarkerWidget.h"
#include "DamageNumberWidget.h"
#include "AttachmentStatPreviewWidget.h"
#include "UI/PickupToastStackWidget.h"
#include "ConsumableWidget.h"
#include "TutorialBriefingWidget.h"
#include "UI/ExtractionHudBridgeComponent.h"
#include "UI/LowHealthVignetteWidget.h"
#include "GameFramework/HUD.h"
#include "ExtractionGameMode.h"
#include "ExtractionGameInstance.h"
#include "Extraction.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Kismet/GameplayStatics.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Input/SVirtualJoystick.h"

// TEMP (demo recording): catches F10 ahead of Slate focus, so the restart hotkey still works while
// the death / level-complete screen owns input in UI-only mode. Remove with the rest of the hotkey.
namespace
{
	class FDemoRestartInputProcessor : public IInputProcessor
	{
	public:
		explicit FDemoRestartInputProcessor(AExtractionPlayerController* InOwner) : Owner(InOwner) {}

		virtual void Tick(const float, FSlateApplication&, TSharedRef<ICursor>) override {}

		virtual bool HandleKeyDownEvent(FSlateApplication&, const FKeyEvent& InKeyEvent) override
		{
			if (InKeyEvent.GetKey() != EKeys::F10 || InKeyEvent.IsRepeat()) return false;
			if (AExtractionPlayerController* PC = Owner.Get())
			{
				PC->HandleDemoRestartKeyPressed();
				return true;
			}
			return false;
		}

	private:
		TWeakObjectPtr<AExtractionPlayerController> Owner;
	};
}
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

	// Code-built widget with no required BP subclass — defaulting the class here means the vignette
	// works with zero designer wiring, while a BP subclass assigned in defaults still overrides it.
	LowHealthVignetteWidgetClass = ULowHealthVignetteWidget::StaticClass();
}

void AExtractionPlayerController::BeginPlay()
{
	Super::BeginPlay();

	RestoreHUD();

	if (!IsLocalPlayerController()) return;

	ArmTutorialBriefing();

	// TEMP (demo recording): remove with the rest of the F10 hotkey.
	if (FSlateApplication::IsInitialized())
	{
		DemoRestartInputProcessor = MakeShared<FDemoRestartInputProcessor>(this);
		FSlateApplication::Get().RegisterInputPreProcessor(DemoRestartInputProcessor);
	}

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

	// TEMP (demo recording): remove with the rest of the F10 hotkey.
	if (DemoRestartInputProcessor.IsValid() && FSlateApplication::IsInitialized())
		FSlateApplication::Get().UnregisterInputPreProcessor(DemoRestartInputProcessor);
	DemoRestartInputProcessor.Reset();

	Super::EndPlay(EndPlayReason);
}

void AExtractionPlayerController::ArmTutorialBriefing()
{
	if (!IsLocalPlayerController()) return;
	if (!IsCurrentMapATutorialMap()) return;

	// No already-seen gate: the briefing is the level's controls reminder, not a one-time tutorial, so
	// it opens on every level start. The seen flag is still written on dismiss for anything else reading it.

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
	// Empty list = every map qualifies. The briefing is the default level-start screen; TutorialMaps is
	// the opt-out, an explicit allow-list a designer populates only to restrict it to named levels.
	if (TutorialMaps.IsEmpty()) return true;

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
	// Deliberately before the hide below: a failed create here would otherwise leave the HUD hidden
	// with no briefing on screen to dismiss it, and the game unpaused underneath.
	if (!IsValid(TutorialBriefingWidget)) return;

	// Must precede AddToPlayerScreen — that is what runs the briefing's construct, and its sweep
	// records the prior visibility of everything still showing. With the overlay already collapsed
	// the sweep is left only the kit's own widgets (crosshair, companion HUD) to catch, so the two
	// restores never contend for the same widget.
	SetPauseHudHidden(true);

	// IsInViewport() alone would be trusted here and it lies after a Blueprint "Remove All Widgets"
	// (see IsWidgetLiveOnPlayerScreen). RemoveFromParent first releases the stale registration, or the
	// re-add is refused as a duplicate and the game pauses behind a briefing nobody can see.
	if (!IsWidgetLiveOnPlayerScreen(TutorialBriefingWidget))
	{
		TutorialBriefingWidget->RemoveFromParent();
		TutorialBriefingWidget->AddToPlayerScreen(TutorialBriefingZOrder);
	}

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

	// RemoveFromParent runs the widget's destruct, which restores everything its sweep hid.
	if (IsValid(TutorialBriefingWidget))
		TutorialBriefingWidget->RemoveFromParent();
	TutorialBriefingWidget = nullptr;

	// After the sweep's restore, so the two never write the same widget in the same frame.
	SetPauseHudHidden(false);

	// Idempotent: leaves a HUD that survived the briefing untouched, re-adds anything that was torn
	// off underneath it. Creates and re-adds only — it never writes visibility, so it cannot undo
	// the line above or reveal anything the HUD modules own.
	RestoreHUD();

	// The briefing forces an immediate mapping rebuild in its own construct, which now happens at
	// level start — early enough that the pawn's contexts may not be applied yet. Any HUD key hint
	// that resolved in that window latched the partial table (a gamepad key, or "[unbound]"), and
	// the retry timers that would have corrected it do not tick while the briefing holds the pause.
	// One forced rebuild here, after the unpause, republishes the complete table and re-broadcasts
	// ControlMappingsRebuiltDelegate so every hint re-resolves against it.
	if (const ULocalPlayer* LP = GetLocalPlayer())
	{
		if (UEnhancedInputLocalPlayerSubsystem* Input = LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
		{
			FModifyContextOptions RebuildOptions;
			RebuildOptions.bForceImmediately = true;
			Input->RequestRebuildControlMappings(RebuildOptions);
		}
	}

	if (UExtractionGameInstance* GI = GetGameInstance<UExtractionGameInstance>())
		GI->SetTutorialBriefingSeen(GetCurrentLevelName());
}

void AExtractionPlayerController::RestoreHUD()
{
	if (!IsLocalPlayerController()) return;

	// Add order is layer order — every widget here shares HUDLayerZOrder, so the viewport stacks
	// them in the order they are added. Keep this sequence as-is; it is BeginPlay's original order.
	// The vignette goes first on purpose: a screen-wide tint belongs under every HUD element.
	EnsureOnPlayerScreen(this, LowHealthVignetteWidget, LowHealthVignetteWidgetClass, HUDLayerZOrder);

	if (ShouldUseTouchControls())
	{
		EnsureOnPlayerScreen(this, MobileControlsWidget, MobileControlsWidgetClass, HUDLayerZOrder);
		if (MobileControlsWidgetClass && !IsValid(MobileControlsWidget))
			UE_LOG(LogExtraction, Error, TEXT("Could not spawn mobile controls widget."));
	}

	EnsureOnPlayerScreen(this, ObjectiveLayerWidget, ObjectiveLayerWidgetClass, HUDLayerZOrder);
	// Rebuilt by the sweep rather than exempted from it: the layer holds no state worth preserving —
	// it reconciles its cards from the subsystem snapshot every RefreshInterval — and EnsureOnPlayerScreen
	// re-adds the surviving instance anyway, so a briefing/pause round-trip costs at most one refresh.
	EnsureOnPlayerScreen(this, AIOverlayWidget, AIOverlayWidgetClass, HUDLayerZOrder);
	EnsureOnPlayerScreen(this, RevivePromptWidget, RevivePromptWidgetClass, HUDLayerZOrder);
	EnsureOnPlayerScreen(this, HitmarkerWidget, HitmarkerWidgetClass, HUDLayerZOrder);
	EnsureOnPlayerScreen(this, DamageNumberWidget, DamageNumberWidgetClass, HUDLayerZOrder);
	EnsureOnPlayerScreen(this, ConsumableWidget, ConsumableWidgetClass, HUDLayerZOrder);
}

void AExtractionPlayerController::CollectScreenOverlayWidgets(TArray<UUserWidget*>& OutWidgets) const
{
	// Deliberately NOT the mobile controls: those are input affordances, and the pause screen takes
	// UI-only input anyway. Nor anything EHB owns — that tree answers to its own manager. Nor the
	// AI overlay: it exists only while the user holds ai.Overlay up, and the demo-cam HUD hide
	// rides this same group — a cinematic eject with the debug cards up is exactly the shot the
	// overlay was asked for, so the console var is its ONLY off switch.
	UUserWidget* const Group[] =
	{
		ObjectiveLayerWidget.Get(), RevivePromptWidget.Get(),
		HitmarkerWidget.Get(), DamageNumberWidget.Get(), ConsumableWidget.Get(),
		LowHealthVignetteWidget.Get()
	};

	OutWidgets.Reset();
	OutWidgets.Reserve(UE_ARRAY_COUNT(Group));
	for (UUserWidget* Widget : Group)
		if (IsValid(Widget)) OutWidgets.Add(Widget);
}

// Parameter is bInHidden, not bHidden: AActor already declares a bHidden UPROPERTY and UHT
// rejects a reflected function parameter that shadows one.
void AExtractionPlayerController::SetScreenOverlayHidden(bool bInHidden)
{
	if (bInHidden == bScreenOverlayHidden) return;
	bScreenOverlayHidden = bInHidden;

	if (!bInHidden)
	{
		for (const FOverlayVisibilityRecord& Record : HiddenOverlayWidgets)
		{
			UUserWidget* Widget = Record.Widget.Get();
			if (IsValid(Widget)) Widget->SetVisibility(Record.PriorVisibility);
		}
		HiddenOverlayWidgets.Reset();
		return;
	}

	TArray<UUserWidget*> Overlay;
	CollectScreenOverlayWidgets(Overlay);

	HiddenOverlayWidgets.Reset();
	HiddenOverlayWidgets.Reserve(Overlay.Num());
	for (UUserWidget* Widget : Overlay)
	{
		const ESlateVisibility Current = Widget->GetVisibility();
		// Already hidden for its own reasons (the hitmarker between hits) — leave it, and do not
		// record it, so the restore cannot wrongly make it visible.
		if (Current == ESlateVisibility::Collapsed || Current == ESlateVisibility::Hidden) continue;

		HiddenOverlayWidgets.Add({ Widget, Current });
		Widget->SetVisibility(ESlateVisibility::Collapsed);
	}
}

UExtractionHudBridgeComponent* AExtractionPlayerController::ResolveHudBridge() const
{
	if (UExtractionHudBridgeComponent* Cached = CachedHudBridge.Get()) return Cached;

	AHUD* Hud = GetHUD();
	if (!IsValid(Hud)) return nullptr;

	UExtractionHudBridgeComponent* Bridge = Hud->FindComponentByClass<UExtractionHudBridgeComponent>();
	CachedHudBridge = Bridge;
	return Bridge;
}

void AExtractionPlayerController::SetPauseHudHidden(bool bInHidden)
{
	SetScreenOverlayHidden(bInHidden);

	if (UExtractionHudBridgeComponent* Bridge = ResolveHudBridge())
		Bridge->SetHudHidden(bInHidden, PauseHudFadeSeconds);
}

void AExtractionPlayerController::RegisterAttachmentStatPreview(UAttachmentStatPreviewWidget* Widget)
{
	if (!IsValid(Widget)) return;
	AttachmentStatPreview = Widget;
}

void AExtractionPlayerController::UnregisterAttachmentStatPreview(UAttachmentStatPreviewWidget* Widget)
{
	// Identity check, not a blind clear: a module rebuilt on a context switch constructs the new
	// panel before destructing the old one, so the old one's teardown arrives second and would
	// otherwise unregister its own replacement.
	if (AttachmentStatPreview.Get() != Widget) return;
	AttachmentStatPreview.Reset();
}

void AExtractionPlayerController::RegisterPickupToastStack(UPickupToastStackWidget* Widget)
{
	if (!IsValid(Widget)) return;
	PickupToastStack = Widget;
}

void AExtractionPlayerController::ClearPickupToastStack(UPickupToastStackWidget* Widget)
{
	// Identity check, not a blind clear: a module rebuilt on a context switch constructs the new
	// stack before destructing the old one, so the old one's teardown arrives second and would
	// otherwise clear its own replacement.
	if (PickupToastStack.Get() != Widget) return;
	PickupToastStack.Reset();
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

		// Bound as a raw key rather than an Enhanced Input action so the pause screen needs no IA or
		// IMC asset. This is the OPEN half of the toggle only: the briefing takes UI-only input while it
		// is up, so this binding cannot fire to close it — UTutorialBriefingWidget::NativeOnKeyDown does.
		if (IsValid(InputComponent))
		{
			InputComponent->BindKey(EKeys::Escape, IE_Pressed, this, &AExtractionPlayerController::HandlePauseKeyPressed);
			// Pad parity with the widget's close key. Without it a pad player can dismiss the briefing
			// once and has no way back to it.
			InputComponent->BindKey(EKeys::Gamepad_Special_Right, IE_Pressed, this, &AExtractionPlayerController::HandlePauseKeyPressed);
		}
	}

}

void AExtractionPlayerController::HandleDemoRestartKeyPressed()
{
	// TEMP (demo recording). Routes through the failure screen's own restart path, so it clears the
	// end-screen flags, unpauses, restores game input and travels by full package path.
	if (!IsLocalPlayerController()) return;
	RequestRestartLevel();
}

void AExtractionPlayerController::HandlePauseKeyPressed()
{
	// Open-only: the briefing swallows Escape once it is up and closes itself through its own key
	// handler, so a live briefing here means the key leaked and the correct answer is to do nothing.
	// Deliberately the liveness check and not IsValid — an instance torn off screen by a Blueprint
	// "Remove All Widgets" is still a valid object, and refusing to re-show it there would leave the
	// game paused with UI-only input and Escape permanently dead. The second guard keeps Escape from
	// stacking a briefing on top of the level-complete or level-failed screens — both of those pause
	// the game themselves and own the restart path.
	if (IsWidgetLiveOnPlayerScreen(TutorialBriefingWidget)) return;
	if (IsValid(LevelCompleteWidget) || IsValid(LevelFailedWidget)) return;

	ShowTutorialBriefing();
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

	// The overlays (low-health vignette included) must not keep painting under the end screen.
	// No restore path needed: leaving this popup reloads the level either way.
	SetScreenOverlayHidden(true);

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

	// Same rule as the completion screen: no overlay paints under the end screen.
	SetScreenOverlayHidden(true);

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
