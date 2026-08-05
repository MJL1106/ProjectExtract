// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "GenericTeamAgentInterface.h"
#include "ExtractionPlayerController.generated.h"

class UInputMappingContext;
class UUserWidget;
class UHitmarkerWidget;
class UDamageNumberWidget;
class UObjectiveMarkerLayer;
class AObjectiveMarkerDisplay;
class ULevelCompleteWidget;
class ULevelFailedWidget;
class URevivePromptWidget;
class UConsumableWidget;
class UAttachmentStatPreviewWidget;
class UTutorialBriefingWidget;
class UExtractionHudBridgeComponent;
class UPickupToastStackWidget;

/** Fired on the local player controller when the player's weapon deals damage. One event per
 *  trigger pull per victim (shotgun pellets aggregated). HeadshotDamage > 0 marks a headshot;
 *  bKilled marks the hit that dropped the victim. Hitmarker + damage-number widgets bind here. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FiveParams(FOnPlayerDamageDealt, AActor*, Victim, float, Damage, float, HeadshotDamage, bool, bKilled, FVector, WorldLocation);

/**
 *  Simple first person Player Controller
 *  Manages the input mapping context.
 *  Overrides the Player Camera Manager class.
 */
UCLASS(config="Game")
class EXTRACTION_API AExtractionPlayerController : public APlayerController, public IGenericTeamAgentInterface
{
	GENERATED_BODY()
	
public:

	/** Constructor */
	AExtractionPlayerController();

	// --- IGenericTeamAgentInterface ---
	virtual FGenericTeamId GetGenericTeamId() const override { return FGenericTeamId(0); }

	/** Shows the Level Complete screen on the owning client and switches to UI-only input. */
	UFUNCTION(Client, Reliable)
	void ClientShowLevelComplete();

	/** Shows the Level Failed screen on the owning client and switches to UI-only input. */
	UFUNCTION(Client, Reliable)
	void ClientShowLevelFailed(const FText& Reason);

	/** Called by the completion/failure widget: routes the restart to the server's GameMode. */
	void RequestRestartLevel();

	/** Called by the weapon damage pass when this controller's pawn dealt damage. */
	void NotifyDamageDealt(AActor* Victim, float Damage, float HeadshotDamage, bool bKilled, const FVector& WorldLocation);

	/** Puts every HUD widget this controller owns back on the player screen: creates the ones that no
	 *  longer exist, re-adds the ones that were torn off, and leaves the ones already up completely
	 *  alone so their state, pooled children and delegate bindings survive. Idempotent — safe to call
	 *  at any time, including while the game is paused. BeginPlay builds the HUD through this same
	 *  call, so the two paths can never drift.
	 *
	 *  WHY THIS EXISTS — DO NOT DELETE AS UNUSED. Its only C++ caller is BeginPlay; the real caller is
	 *  the pause/resume Blueprint flow. Any Blueprint "Remove All Widgets" node
	 *  (UWidgetLayoutLibrary::RemoveAllWidgets) tears EVERY widget off the player screen, including
	 *  the top-level widgets created here that no Blueprint knows how to rebuild — the pawn's
	 *  CreateHUD only restores the three legacy kit widgets. Without this call on the resume path, one
	 *  pause cycle permanently kills hitmarkers, damage numbers, the objective marker layer and the
	 *  revive prompt for the rest of the session.
	 *
	 *  Health, ammo, the objective text panel, the loot toast, the companion mode chip and the
	 *  attachment stat preview are deliberately absent: they are HUD-actor modules now, and this
	 *  controller must not re-create them. The first four are fed by UExtractionHudBridgeComponent;
	 *  the companion mode chip binds itself to the pawn's command component and needs nothing from
	 *  here; the attachment stat preview also drives itself but registers back so BP pickups can
	 *  still reach it (see RegisterAttachmentStatPreview). */
	UFUNCTION(BlueprintCallable, Category = "UI")
	void RestoreHUD();

	/** Called by the briefing screen's confirm button — the only way out of the gate. Unpauses,
	 *  hands input back to the game, tears the screen down and records that it has been seen. */
	void DismissTutorialBriefing();

	/** Hides or shows the screen-space overlay THIS controller owns, as one group: the objective
	 *  marker layer, revive prompt, hitmarker, damage numbers and consumable panel.
	 *
	 *  One of three systems the pause screen has to quiet, and the only one C++ drives directly —
	 *  the HUD modules answer to their own manager (UExtractionHudBridgeComponent::SetHudHidden)
	 *  and the kit's own widgets are caught by the briefing's top-level sweep.
	 *
	 *  Idempotent, and null-safe against any member being absent. Prior visibility is captured on
	 *  hide and put back on show, so a widget that manages its own visibility (the hitmarker is
	 *  collapsed until a hit lands) is not forced visible by a restore. A widget already hidden for
	 *  its own reasons is left alone and NOT recorded, so the restore cannot reveal it. */
	UFUNCTION(BlueprintCallable, Category = "UI")
	void SetScreenOverlayHidden(bool bInHidden);

	/** True while SetScreenOverlayHidden(true) is in effect. */
	UFUNCTION(BlueprintPure, Category = "UI")
	bool IsScreenOverlayHidden() const { return bScreenOverlayHidden; }

	/** Active attachment stat preview panel, or null when none is live. The pickup BP calls
	 *  ShowForAttachment / ShowForWeapon / HidePreview on this widget to drive the HUD panel.
	 *
	 *  The controller no longer creates this panel — it lives inside a HUD module, which C++ cannot
	 *  name without an asset path. UAttachmentStatPreviewWidget registers itself here on construct
	 *  instead (see RegisterAttachmentStatPreview), so the getter's contract is unchanged: a live
	 *  panel or null, never a stale one. */
	UFUNCTION(BlueprintPure, Category = "UI|Attachments")
	UAttachmentStatPreviewWidget* GetAttachmentStatPreview() const { return AttachmentStatPreview.Get(); }

	/** Called by UAttachmentStatPreviewWidget::NativeConstruct. Last construct wins — during a HUD
	 *  context switch the incoming panel is built before the outgoing one is torn down, and the
	 *  incoming one is the one the pickups should be driving. */
	UFUNCTION(BlueprintCallable, Category = "UI|Attachments")
	void RegisterAttachmentStatPreview(UAttachmentStatPreviewWidget* Widget);

	/** Called by UAttachmentStatPreviewWidget::NativeDestruct. Ignores anything that is not the
	 *  currently registered panel, so a late teardown cannot unregister its own replacement. */
	UFUNCTION(BlueprintCallable, Category = "UI|Attachments")
	void UnregisterAttachmentStatPreview(UAttachmentStatPreviewWidget* Widget);

	/** The objective waypoint layer instance, or null before BeginPlay has created it. The ping marker
	 *  needs this because it lives in the HUD widget tree while the objective layer is a separate
	 *  PC-owned top-level widget — there is no parent/child path between the two, so this accessor is
	 *  the only way across. */
	UFUNCTION(BlueprintPure, Category = "UI")
	UObjectiveMarkerLayer* GetObjectiveMarkerLayer() const { return ObjectiveLayerWidget.Get(); }

	/** Active pickup toast stack, or null when none is live. It lives inside a HUD module, which C++
	 *  cannot name without an asset path, and — unlike the objective layer — this controller has no
	 *  BlueprintCallable path to the HUD bridge that owns that module (ResolveHudBridge is private and
	 *  not a UFUNCTION). UPickupToastStackWidget registers itself here on construct instead (see
	 *  RegisterPickupToastStack), which is what lets the kit's attachment pickup Blueprint reach it at
	 *  all: this getter is the only route from BP_Attachment_Pickup to the toast widget. */
	UFUNCTION(BlueprintPure, Category = "UI|Pickups")
	UPickupToastStackWidget* GetPickupToastStack() const { return PickupToastStack.Get(); }

	/** Called by UPickupToastStackWidget::NativeConstruct. Last construct wins — during a HUD context
	 *  switch the incoming stack is built before the outgoing one is torn down, and the incoming one
	 *  is the one pickups should be driving. */
	void RegisterPickupToastStack(UPickupToastStackWidget* Widget);

	/** Called by UPickupToastStackWidget::NativeDestruct. Ignores anything that is not the currently
	 *  registered stack, so a late teardown cannot clear its own replacement. */
	void ClearPickupToastStack(UPickupToastStackWidget* Widget);

	/** Broadcast on the local controller for HUD hit feedback (hitmarker, damage numbers). */
	UPROPERTY(BlueprintAssignable, Category = "UI|Events")
	FOnPlayerDamageDealt OnDamageDealt;

protected:

	/** Server side of the restart request. */
	UFUNCTION(Server, Reliable)
	void ServerRequestRestartLevel();

	/** Level Complete screen class (assigned in BP defaults — no C++ asset path). */
	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSubclassOf<ULevelCompleteWidget> LevelCompleteWidgetClass;

	/** Active Level Complete screen instance */
	UPROPERTY()
	TObjectPtr<ULevelCompleteWidget> LevelCompleteWidget;

	/** Level Failed screen class (assigned in BP defaults — no C++ asset path). */
	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSubclassOf<ULevelFailedWidget> LevelFailedWidgetClass;

	/** Active Level Failed screen instance */
	UPROPERTY()
	TObjectPtr<ULevelFailedWidget> LevelFailedWidget;

	/** Input Mapping Contexts */
	UPROPERTY(EditAnywhere, Category="Input|Input Mappings")
	TArray<UInputMappingContext*> DefaultMappingContexts;

	/** Input Mapping Contexts */
	UPROPERTY(EditAnywhere, Category="Input|Input Mappings")
	TArray<UInputMappingContext*> MobileExcludedMappingContexts;

	/** Mobile controls widget to spawn */
	UPROPERTY(EditAnywhere, Category="Input|Touch Controls")
	TSubclassOf<UUserWidget> MobileControlsWidgetClass;

	/** Pointer to the mobile controls widget */
	UPROPERTY()
	TObjectPtr<UUserWidget> MobileControlsWidget;

	/** Objective waypoint layer class (assigned in BP defaults — no C++ asset path). */
	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSubclassOf<UObjectiveMarkerLayer> ObjectiveLayerWidgetClass;

	/** Active objective waypoint layer instance */
	UPROPERTY()
	TObjectPtr<UObjectiveMarkerLayer> ObjectiveLayerWidget;

	/** World-space marker display actor class (assigned in BP defaults — no C++ asset path).
	 *  Supplied to UObjectiveSubsystem::SetMarkerDisplayClass during local player setup. */
	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSubclassOf<AObjectiveMarkerDisplay> MarkerDisplayClass;

	/** "[F] Revive" prompt class (assigned in BP defaults — no C++ asset path). Deliberately still
	 *  here while the HUD module's revive label and interact-candidate switching are unproven —
	 *  this is the fallback until they are. */
	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSubclassOf<URevivePromptWidget> RevivePromptWidgetClass;

	/** Active revive prompt instance */
	UPROPERTY()
	TObjectPtr<URevivePromptWidget> RevivePromptWidget;

	/** Hitmarker widget class (assigned in BP defaults — no C++ asset path). */
	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSubclassOf<UHitmarkerWidget> HitmarkerWidgetClass;

	/** Active hitmarker instance */
	UPROPERTY()
	TObjectPtr<UHitmarkerWidget> HitmarkerWidget;

	/** Damage-number layer class (assigned in BP defaults — no C++ asset path). */
	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSubclassOf<UDamageNumberWidget> DamageNumberWidgetClass;

	/** Active damage-number layer instance */
	UPROPERTY()
	TObjectPtr<UDamageNumberWidget> DamageNumberWidget;

	/** Consumable HUD (stim + grenade slots) class (assigned in BP defaults — no C++ asset path). */
	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSubclassOf<UConsumableWidget> ConsumableWidgetClass;

	/** Active consumable HUD instance */
	UPROPERTY()
	TObjectPtr<UConsumableWidget> ConsumableWidget;

	/** The HUD module's stat preview panel, registered by the widget itself. Weak: the module owns
	 *  its lifetime and can tear it down on a context switch without telling this controller. */
	TWeakObjectPtr<UAttachmentStatPreviewWidget> AttachmentStatPreview;

	/** The HUD module's pickup toast stack, registered by the widget itself. Weak, same reasoning as
	 *  AttachmentStatPreview above — deliberately forward-declared only (see the class list at the top
	 *  of this header): TWeakObjectPtr never needs a complete type to store, assign or Get() one. */
	TWeakObjectPtr<UPickupToastStackWidget> PickupToastStack;

	/** Controls briefing screen class (assigned in BP defaults — no C++ asset path). */
	UPROPERTY(EditDefaultsOnly, Category = "UI|Tutorial")
	TSubclassOf<UTutorialBriefingWidget> TutorialBriefingWidgetClass;

	/** Active controls briefing instance */
	UPROPERTY()
	TObjectPtr<UTutorialBriefingWidget> TutorialBriefingWidget;

	/** Maps that open with the controls briefing. Designer assigns them — C++ names no level, so
	 *  adding a second tutorial map is a defaults change, not a code change. */
	UPROPERTY(EditDefaultsOnly, Category = "UI|Tutorial")
	TArray<TSoftObjectPtr<UWorld>> TutorialMaps;

	/** Fade passed to the HUD module manager when the pause screen hides it. 0 = instant. Kept
	 *  short deliberately: the pause screen appears in the same frame and a long fade shows the
	 *  HUD sliding out from underneath it.
	 *
	 *  ⚠ Raising this above 0 REQUIRES ExcludedRootClass to be assigned on the briefing widget.
	 *  At 0 the module root collapses synchronously before the briefing's top-level sweep runs, so
	 *  the sweep skips it. Mid-fade the root is still Visible, so the sweep captures and collapses
	 *  it, and its restore on dismiss writes back a visibility the fade has already moved past --
	 *  a flash, or a HUD stuck invisible if the fade had reached zero alpha. */
	UPROPERTY(EditDefaultsOnly, Category = "UI", meta = (ClampMin = "0.0"))
	float PauseHudFadeSeconds = 0.f;

	/** If true, the player will use UMG touch controls even if not playing on mobile platforms */
	UPROPERTY(EditAnywhere, Config, Category = "Input|Touch Controls")
	bool bForceTouchControls = false;

	/** Gameplay initialization */
	virtual void BeginPlay() override;

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Input mapping context setup */
	virtual void SetupInputComponent() override;

	/** Returns true if the player should use UMG touch controls */
	bool ShouldUseTouchControls() const;

private:
	/** Queues the briefing for next tick when this map is a tutorial map and it has not been seen. */
	void ArmTutorialBriefing();

	/** True when the current world's package matches one of TutorialMaps. */
	bool IsCurrentMapATutorialMap() const;

	/** The current world's package name with any PIE prefix stripped. Used to level-key the seen
	 *  flag and to match against TutorialMaps. */
	FName GetCurrentLevelName() const;

	/** Puts the briefing on screen, takes input UI-only and pauses the game. */
	void ShowTutorialBriefing();

	/** Escape handler. Re-opens the briefing as a pause screen, bypassing both the tutorial-map and
	 *  already-seen gates that only apply to the automatic level-start show. */
	void HandlePauseKeyPressed();

	/** Quiets (or restores) the two HUD systems C++ can reach: this controller's overlay group and
	 *  the HUD actor's module tree. The third — the kit's own top-level widgets — is the briefing
	 *  widget's sweep, which runs from its own construct/destruct.
	 *
	 *  Called BEFORE the briefing is built on the way in, so the sweep finds the overlay already
	 *  collapsed and does not record it a second time; and AFTER the briefing is torn down on the
	 *  way out, so the sweep's restore and this one cannot fight over the same widgets. */
	void SetPauseHudHidden(bool bInHidden);

	/** The bridge component on the HUD actor, or null when there is no HUD actor (the HUD class is
	 *  a GameMode setting and a level may legitimately run without one). Cached weakly — the HUD
	 *  actor outlives the controller in practice but is not owned by it. */
	UExtractionHudBridgeComponent* ResolveHudBridge() const;

	mutable TWeakObjectPtr<UExtractionHudBridgeComponent> CachedHudBridge;

	/** Fills OutWidgets with the overlay group, skipping any member that does not exist. */
	void CollectScreenOverlayWidgets(TArray<UUserWidget*>& OutWidgets) const;

	/** One entry per overlay widget SetScreenOverlayHidden collapsed, with the visibility to put
	 *  back. Mirrors the briefing sweep's record so the two behave identically. */
	struct FOverlayVisibilityRecord
	{
		TWeakObjectPtr<UUserWidget> Widget;
		ESlateVisibility PriorVisibility;
	};

	TArray<FOverlayVisibilityRecord> HiddenOverlayWidgets;

	/** Latch behind SetScreenOverlayHidden's idempotency. Without it a second hide would capture
	 *  the already-collapsed state as the value to restore, and the overlay would never come back. */
	bool bScreenOverlayHidden = false;
};
