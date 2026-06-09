// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "GenericTeamAgentInterface.h"
#include "ExtractionPlayerController.generated.h"

class UInputMappingContext;
class UUserWidget;
class UPlayerHealthWidget;
class UCrosshairWidget;
class UAmmoWidget;
class UBarkFeedWidget;

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

protected:

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

	/** Crosshair widget class */
	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSubclassOf<UCrosshairWidget> CrosshairWidgetClass;

	/** Active crosshair widget instance */
	UPROPERTY()
	TObjectPtr<UCrosshairWidget> CrosshairWidget;

	/** Ammo display widget class */
	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSubclassOf<UAmmoWidget> AmmoWidgetClass;

	/** Active ammo display widget instance */
	UPROPERTY()
	TObjectPtr<UAmmoWidget> AmmoWidget;

	/** Enemy bark subtitle feed widget class */
	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSubclassOf<UBarkFeedWidget> BarkFeedWidgetClass;

	/** Active bark feed widget instance */
	UPROPERTY()
	TObjectPtr<UBarkFeedWidget> BarkFeedWidget;

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
