// AExtracteeCompanion -- the mission's armed extraction VIP: a second companion the player
// cannot command. Placed captive (no AI controller, weapon hidden, kneeling pose via the ABP's
// IsCaptive branch); the player's rescue interact hands over a pistol -- the primary companion
// speaks the handoff line, the VIP replies, the AI controller spawns and the shared companion
// brain takes it from there, pinned to Normal mode. DBNO/revive behave exactly like the primary
// companion. Excluded from every player-command path via bIsPrimaryCompanion = false.

#pragma once

#include "CoreMinimal.h"
#include "Companion/CompanionCharacter.h"
#include "World/WorldInteractable.h"
#include "ExtracteeCompanion.generated.h"

class USoundBase;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnExtracteeCompanionRescued);

UCLASS(Blueprintable)
class EXTRACTION_API AExtracteeCompanion : public ACompanionCharacter, public IWorldInteractable
{
	GENERATED_BODY()

public:
	AExtracteeCompanion();

	virtual float TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent,
		AController* EventInstigator, AActor* DamageCauser) override;

	// Captive reads as neutral to enemy perception; team 0 (player side) once rescued.
	virtual FGenericTeamId GetGenericTeamId() const override
	{
		return bCaptive ? FGenericTeamId::NoTeam : FGenericTeamId(0);
	}

	// Player-commanded mode switches never reach this companion (command component filters on
	// primary), but pin Normal against any direct BP/debug call as well.
	virtual void SetMode(ECompanionMode NewMode) override;

	// --- IWorldInteractable (rescue) ---
	virtual bool CanWorldInteract_Implementation(AActor* Interactor) const override;
	virtual void WorldInteract_Implementation(AActor* Interactor) override;
	virtual FText GetWorldInteractionPrompt_Implementation(AActor* Interactor) const override;

	/** True until rescued. The ABP's captive branch keys off this. */
	UFUNCTION(BlueprintPure, Category = "Extractee")
	bool IsCaptive() const { return bCaptive; }

	/** Objective-flow gate: while false the rescue prompt/interact are refused. */
	void SetRescueEnabled(bool bEnabled) { bRescueEnabled = bEnabled; }

	/** Checkpoint fast-forward: rescue with no ceremony (no VO, no delegate side effects beyond
	 *  the broadcast the flow relies on being already-consumed -- so no broadcast at all). */
	void ForceRescue();

	/** Fired once on a real (interact) rescue. The objective flow starts the extraction wave here. */
	UPROPERTY(BlueprintAssignable, Category = "Extractee")
	FOnExtracteeCompanionRescued OnRescued;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	virtual FText GetBleedoutFailReason() const override;

	/** HUD prompt while looking at the captive. */
	UPROPERTY(EditDefaultsOnly, Category = "Extractee|Rescue")
	FText RescuePrompt = NSLOCTEXT("Extractee", "RescuePrompt", "Rescue");

	/** Spoken by the PRIMARY companion at rescue ("Here -- take my pistol."). Optional. */
	UPROPERTY(EditDefaultsOnly, Category = "Extractee|Rescue")
	TObjectPtr<USoundBase> CompanionHandoffLine;

	/** The VIP's reply, played ReplyLineDelay seconds after the handoff line. Optional. */
	UPROPERTY(EditDefaultsOnly, Category = "Extractee|Rescue")
	TObjectPtr<USoundBase> RescueReplyLine;

	UPROPERTY(EditDefaultsOnly, Category = "Extractee|Rescue", meta = (ClampMin = "0.0"))
	float ReplyLineDelay = 2.8f;

private:
	bool bCaptive = true;

	/** Objective-flow gate (SetRescueEnabled). Defaults on so a hand-placed test VIP works
	 *  without a flow; DemoMap's flow disables it at activation and re-enables at step entry. */
	bool bRescueEnabled = true;

	FTimerHandle ReplyLineTimerHandle;

	/** Shared rescue body: AI on, weapon shown, widgets on. bCeremony adds the VO exchange
	 *  and the OnRescued broadcast (checkpoint fast-forward skips both). */
	void CompleteRescue(bool bCeremony);
};
