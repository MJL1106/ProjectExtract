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
class UPlayerHealthWidget;
class UAmmoWidget;
class UCompanionModeWidget;
class UObjectiveMarkerLayer;
class UObjectiveTextPanelWidget;
class AObjectiveMarkerDisplay;
class ULootNotificationWidget;
class ULevelCompleteWidget;
class ULevelFailedWidget;
class URevivePromptWidget;

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

	/** Health/Shield HUD widget class (set to WBP_PlayerHealth in BP defaults) */
	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSubclassOf<UPlayerHealthWidget> HUDWidgetClass;

	/** Active HUD widget instance */
	UPROPERTY()
	TObjectPtr<UPlayerHealthWidget> HUDWidget;

	/** Ammo display widget class */
	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSubclassOf<UAmmoWidget> AmmoWidgetClass;

	/** Active ammo display widget instance */
	UPROPERTY()
	TObjectPtr<UAmmoWidget> AmmoWidget;

	/** Companion mode HUD chip class (assigned in BP defaults — no C++ asset path). */
	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSubclassOf<UCompanionModeWidget> CompanionModeWidgetClass;

	/** Active companion mode chip instance */
	UPROPERTY()
	TObjectPtr<UCompanionModeWidget> CompanionModeWidget;

	/** Objective waypoint layer class (assigned in BP defaults — no C++ asset path). */
	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSubclassOf<UObjectiveMarkerLayer> ObjectiveLayerWidgetClass;

	/** Active objective waypoint layer instance */
	UPROPERTY()
	TObjectPtr<UObjectiveMarkerLayer> ObjectiveLayerWidget;

	/** Screen-space objective text panel class (assigned in BP defaults — no C++ asset path). */
	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSubclassOf<UObjectiveTextPanelWidget> ObjectiveTextPanelWidgetClass;

	/** Active objective text panel instance */
	UPROPERTY()
	TObjectPtr<UObjectiveTextPanelWidget> ObjectiveTextPanelWidget;

	/** World-space marker display actor class (assigned in BP defaults — no C++ asset path).
	 *  Supplied to UObjectiveSubsystem::SetMarkerDisplayClass during local player setup. */
	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSubclassOf<AObjectiveMarkerDisplay> MarkerDisplayClass;

	/** Loot acquisition toast class (assigned in BP defaults — no C++ asset path). */
	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSubclassOf<ULootNotificationWidget> LootToastWidgetClass;

	/** Active loot toast instance */
	UPROPERTY()
	TObjectPtr<ULootNotificationWidget> LootToastWidget;

	/** "[E] Revive" prompt class (assigned in BP defaults — no C++ asset path). */
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

	/** If true, the player will use UMG touch controls even if not playing on mobile platforms */
	UPROPERTY(EditAnywhere, Config, Category = "Input|Touch Controls")
	bool bForceTouchControls = false;

	/** Gameplay initialization */
	virtual void BeginPlay() override;

	/** Input mapping context setup */
	virtual void SetupInputComponent() override;

	/** Returns true if the player should use UMG touch controls */
	bool ShouldUseTouchControls() const;
};
