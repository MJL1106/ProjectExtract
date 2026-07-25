// ALootContainer — an invisible designer volume draped over an existing drawer / table / shelf.
// One container = one Loot() call: contents are granted to the PLAYER through
// UMissionInventorySubsystem regardless of who looted (companion loot still feeds the player).
// The volume carries no mesh of its own — the world geometry it sits on IS the visual. Any
// reaction (drawer slide, SFX) is BP-driven via OnOpened; C++ stays asset-agnostic.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "World/Lootable.h"
#include "World/LootTypes.h"
#include "World/WorldInteractable.h"
#include "LootContainer.generated.h"

class UBillboardComponent;
class UBoxComponent;
class ALootContainer;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnLootCompleted, ALootContainer*, Container, AActor*, Looter);

UCLASS(Blueprintable, HideCategories = (Replication, Input, LOD, Cooking))
class EXTRACTION_API ALootContainer : public AActor, public ILootable, public IWorldInteractable
{
	GENERATED_BODY()

public:
	ALootContainer();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(BlueprintAssignable, Category = "Loot|Events")
	FOnLootCompleted OnLootCompleted;

	UFUNCTION(BlueprintPure, Category = "Loot")
	bool IsLooted() const { return bLooted; }
#if WITH_DEV_AUTOMATION_TESTS
	int32 TestGetCompletionBroadcastCount() const { return CompletionBroadcastCount; }
#endif

	// --- ILootable ---
	// Kept alongside IWorldInteractable: the companion ping/BT loot path resolves through this.
	virtual void Loot_Implementation(AActor* Looter) override;
	virtual bool CanLoot_Implementation() const override;

	// --- IWorldInteractable ---
	// AExtractionPlayer::TryWorldInteract checks this interface BEFORE the legacy ILootable
	// branch, so these are now the player's route in — they must land on the same loot path.
	virtual bool CanWorldInteract_Implementation(AActor* Interactor) const override;
	virtual void WorldInteract_Implementation(AActor* Interactor) override;
	virtual FText GetWorldInteractionPrompt_Implementation(AActor* Interactor) const override;
	virtual float GetWorldInteractHoldSeconds_Implementation(AActor* Interactor) const override;

protected:
	/** The searchable space. Scale it over the drawer/table/shelf the loot is meant to live in. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Loot")
	TObjectPtr<UBoxComponent> InteractVolume;

#if WITH_EDITORONLY_DATA
	/** Viewport handle for an otherwise invisible actor. Sprite assigned in the BP subclass. */
	UPROPERTY(VisibleAnywhere, Category = "Loot")
	TObjectPtr<UBillboardComponent> Billboard;
#endif

	/** What this container holds. Authored per placed instance. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot")
	TArray<FLootGrant> Contents;

	/** HUD prompt shown while the player looks into this volume. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot")
	FText InteractPrompt = NSLOCTEXT("Loot", "ContainerPromptDefault", "Search");

	/** Set true on a placed instance to start pre-looted (set dressing). Flipped on loot.
	 *  Replicated: CanWorldInteract runs on the CLIENT during the prompt scan, so a client that
	 *  never learned about the flip would keep the "Search" prompt up on an emptied container and
	 *  round-trip every press to a server that silently refuses it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category = "Loot")
	bool bLooted = false;

	/** BP hook: play the drawer/lid reaction (and any SFX). Fired once, on loot. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Loot")
	void OnOpened(AActor* Looter);

private:
#if WITH_DEV_AUTOMATION_TESTS
	int32 CompletionBroadcastCount = 0;
#endif
	bool CanLootRespectingScriptOverride() const;
	void GrantAllContents();
};
