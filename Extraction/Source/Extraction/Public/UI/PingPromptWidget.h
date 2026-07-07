// UPingPromptWidget -- contextual key-hint prompt shown when the player pings a target.
// Shows command-specific hints (Breach / Takedown-Knife / Takedown-Shoot / Loot) and hides when cleared.
//
// WBP must contain:
//   UPanelWidget "BreachContainer"      -- visible during Breach pending
//   UTextBlock   "BreachHintText"       -- displays BreachHint text
//   UPanelWidget "TakedownContainer"    -- visible during Takedown pending
//   UTextBlock   "KnifeHintText"        -- displays KnifeHint text
//   UTextBlock   "ShootHintText"        -- displays ShootHint text
//   UPanelWidget "LootContainerPanel"   -- (optional) visible during Loot pending
//   UTextBlock   "LootHintText"         -- (optional) displays LootHint text

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Companion/CompanionCommandTypes.h"
#include "PingPromptWidget.generated.h"

class UCompanionCommandComponent;
class UPanelWidget;
class UTextBlock;

UCLASS(Abstract, Blueprintable)
class EXTRACTION_API UPingPromptWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	// --- Bound widgets (designer wires these in WBP) ---

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UPanelWidget> BreachContainer;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> BreachHintText;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UPanelWidget> TakedownContainer;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> KnifeHintText;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> ShootHintText;

	/** Optional so existing WBPs keep compiling until the loot panel is added in-editor. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UPanelWidget> LootContainerPanel;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> LootHintText;

	// --- Designer-editable hint labels ---

	UPROPERTY(EditAnywhere, Category = "Ping|Hints")
	FText BreachHint = NSLOCTEXT("PingPrompt", "Breach", "[I] Breach");

	UPROPERTY(EditAnywhere, Category = "Ping|Hints")
	FText KnifeHint = NSLOCTEXT("PingPrompt", "Knife", "[Y] Knife");

	UPROPERTY(EditAnywhere, Category = "Ping|Hints")
	FText ShootHint = NSLOCTEXT("PingPrompt", "Shoot", "[U] Shot");

	UPROPERTY(EditAnywhere, Category = "Ping|Hints")
	FText LootHint = NSLOCTEXT("PingPrompt", "Loot", "[I] Loot");

private:
	UFUNCTION()
	void HandlePingChanged(ECompanionCommand PendingCommand, AActor* PingedTarget);

	TWeakObjectPtr<UCompanionCommandComponent> CachedCommandComp;
};
