// AInteractionVolume — a box the player can interact with and nothing else. No mesh, no logic of
// its own: it raises UInteractionEventSubsystem::NotifyWorldInteract on a successful use and lets an
// AObjectiveStep's Interacted condition decide what that means. That is the whole point — a designer
// drapes one over a radio, a console or a lift button that already has art, wires the beat, and never
// touches C++ or a Blueprint graph.
//
// Most of these start switched OFF and are turned on by an objective beat's ActivateActor effect, so
// the player cannot call for extraction before the mission asks for it.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "World/WorldInteractable.h"
#include "InteractionVolume.generated.h"

class UBoxComponent;

DECLARE_LOG_CATEGORY_EXTERN(LogInteractionVolume, Log, All);

UCLASS(Blueprintable, HideCategories = (Replication, Input, LOD, Cooking))
class EXTRACTION_API AInteractionVolume : public AActor, public IWorldInteractable
{
	GENERATED_BODY()

public:
	AInteractionVolume();

	// --- IWorldInteractable ---

	virtual bool CanWorldInteract_Implementation(AActor* Interactor) const override;
	virtual void WorldInteract_Implementation(AActor* Interactor) override;
	virtual FText GetWorldInteractionPrompt_Implementation(AActor* Interactor) const override;
	virtual float GetWorldInteractHoldSeconds_Implementation(AActor* Interactor) const override;

	/** Switch the volume on or off. Idempotent, and the one way in — an objective beat's
	 *  ActivateActor effect calls this, as can a Blueprint or level script. A single-use volume that
	 *  has already been used stays refused whatever this is set to. */
	UFUNCTION(BlueprintCallable, Category = "Interaction Volume")
	void SetInteractionEnabled(bool bNewEnabled);

	UFUNCTION(BlueprintPure, Category = "Interaction Volume")
	bool IsInteractionEnabled() const { return bInteractionEnabled; }

	UFUNCTION(BlueprintPure, Category = "Interaction Volume")
	bool HasBeenUsed() const { return bUsed; }

protected:
	virtual void PostInitializeComponents() override;
	virtual void BeginPlay() override;

	/** The interact surface. Sized in the viewport; there is deliberately no mesh, so this box IS
	 *  the actor as far as the player is concerned. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Interaction Volume")
	TObjectPtr<UBoxComponent> InteractionBox;

	/** HUD prompt shown while the player is looking at an enabled volume. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction Volume")
	FText InteractionPrompt;

	/** Seconds the player must hold Interact. Zero makes it an instant press — fine for a door, wrong
	 *  for anything the fiction says takes a moment (calling for extraction, arming a charge). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction Volume", meta = (ClampMin = "0.0"))
	float HoldSeconds = 2.f;

	/** Off by default: an interaction that matters to the mission is switched on by the beat that
	 *  asks for it, not standing live from level load. Turn it on for a volume nothing gates. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction Volume")
	bool bStartsEnabled = false;

	/** One use and it is spent for the level. Off makes it a repeatable switch. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction Volume")
	bool bSingleUse = true;

private:
	bool bInteractionEnabled = false;
	bool bUsed = false;

	/** Enabled AND not spent. The prompt scan, the interact commit and the every-frame re-check the
	 *  player runs under a hold all resolve through this one answer. */
	bool CanBeUsed() const { return bInteractionEnabled && !(bSingleUse && bUsed); }

	/** Drops the box out of the interact trace entirely while the volume is refused, rather than
	 *  leaving it blocking the ray with a CanWorldInteract that says no — a dead box in front of a
	 *  live interactable would otherwise eat the player's crosshair and the prompt behind it. */
	void ApplyCollisionState();

#if WITH_EDITOR
	void ValidateConfig() const;
#endif
};
