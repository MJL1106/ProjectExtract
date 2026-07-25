// AExtracteeCompanion -- the mission's armed extraction VIP: a second companion the player
// cannot command. Placed captive (no AI controller, weapon hidden, kneeling pose via the ABP's
// IsCaptive branch). DBNO/revive behave exactly like the primary companion. Excluded from every
// player-command path via bIsPrimaryCompanion = false.
//
// The rescue runs as a three-stage beat, not one instant:
//   1. HOLD START  -- the player begins the hold-to-interact. The primary companion speaks the
//                     opening line under it. Nothing else changes, so a cancelled hold is a no-op.
//   2. FREED       -- the hold completes. Captive clears, the AI controller spawns and the VIP
//                     stands and follows, but the pistol stays hidden and the ABP holds an
//                     UNARMED locomotion branch. The primary companion offers the pistol.
//   3. ARMED       -- the handoff line ends. The pistol appears, the ABP swaps to the pistol pose,
//                     the VIP replies, and OnRescued fires -- which is what starts the defence
//                     wave, one designer-set second later.
//
// Damage immunity spans stages 1-2: the team flips to player-side the moment the VIP is freed,
// so without it enemies get a free unarmed target for the length of the handoff line.

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

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

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

	// No shooting an invisible pistol: the shared brain is already running through the unarmed
	// handoff window, so both the fire call and the decorators that gate on it must refuse.
	virtual void StartWeaponFire() override;
	virtual bool CanFire() const override;

	// --- IWorldInteractable (rescue) ---
	virtual bool CanWorldInteract_Implementation(AActor* Interactor) const override;
	virtual void WorldInteract_Implementation(AActor* Interactor) override;
	virtual FText GetWorldInteractionPrompt_Implementation(AActor* Interactor) const override;
	virtual float GetWorldInteractHoldSeconds_Implementation(AActor* Interactor) const override;
	virtual void OnWorldInteractHoldStarted_Implementation(AActor* Interactor) override;

	/** True until rescued. The ABP's captive branch keys off this. */
	UFUNCTION(BlueprintPure, Category = "Extractee")
	bool IsCaptive() const { return bCaptive; }

	/** False between being freed and the pistol handoff landing — the ABP runs its unarmed
	 *  locomotion branch through that window and swaps to the pistol pose when this flips.
	 *  Meaningless while captive (the captive branch outranks it). */
	UFUNCTION(BlueprintPure, Category = "Extractee")
	bool IsArmed() const { return bArmed; }

	/** Objective-flow gate: while false the rescue prompt/interact are refused. */
	void SetRescueEnabled(bool bEnabled) { bRescueEnabled = bEnabled; }

	/** Checkpoint fast-forward: rescue with no ceremony (no VO, no delegate side effects beyond
	 *  the broadcast the flow relies on being already-consumed -- so no broadcast at all). */
	void ForceRescue();

	/** Fired once on a real (interact) rescue, at the moment the VIP is ARMED — not when freed.
	 *  The objective flow starts the extraction wave from here, after WaveDelayAfterArmed. */
	UPROPERTY(BlueprintAssignable, Category = "Extractee")
	FOnExtracteeCompanionRescued OnRescued;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	virtual FText GetBleedoutFailReason() const override;

	/** HUD prompt while looking at the captive. */
	UPROPERTY(EditDefaultsOnly, Category = "Extractee|Rescue")
	FText RescuePrompt = NSLOCTEXT("Extractee", "RescuePrompt", "Rescue");

	/** Seconds the player must hold Interact to free the VIP. The opening line runs under it,
	 *  so keep it at or above that line's length. */
	UPROPERTY(EditDefaultsOnly, Category = "Extractee|Rescue", meta = (ClampMin = "0.0"))
	float RescueHoldSeconds = 5.f;

	/** Spoken by the PRIMARY companion when the hold BEGINS ("Let's get you out of here").
	 *  Optional — unassigned, the hold simply runs silent. */
	UPROPERTY(EditDefaultsOnly, Category = "Extractee|Rescue")
	TObjectPtr<USoundBase> CompanionRescueStartLine;

	/** Spoken by the PRIMARY companion when the VIP is FREED ("Here -- take my pistol."). The
	 *  pistol appears as this line ends. Optional — unassigned, the VIP arms immediately. */
	UPROPERTY(EditDefaultsOnly, Category = "Extractee|Rescue")
	TObjectPtr<USoundBase> CompanionHandoffLine;

	/** The VIP's reply, played once he is armed. Optional. */
	UPROPERTY(EditDefaultsOnly, Category = "Extractee|Rescue")
	TObjectPtr<USoundBase> RescueReplyLine;

	/** Extra beat between arming and the reply, on top of the handoff line ending. */
	UPROPERTY(EditDefaultsOnly, Category = "Extractee|Rescue", meta = (ClampMin = "0.0"))
	float ReplyLineDelay = 0.f;

	/** Delay from the pistol appearing to OnRescued firing — the defence wave's head start. */
	UPROPERTY(EditDefaultsOnly, Category = "Extractee|Rescue", meta = (ClampMin = "0.0"))
	float WaveDelayAfterArmed = 1.f;

private:
	/** Replicated: clients gate the rescue prompt on CanWorldInteract, and the ABP branches on
	 *  IsCaptive — both read wrong on a client if this only ever moves on the server. */
	UPROPERTY(Replicated)
	bool bCaptive = true;

	/** See IsArmed. Set at the end of the handoff beat (or immediately when there is no line).
	 *  Replicated for the ABP's unarmed-vs-pistol branch. */
	UPROPERTY(Replicated)
	bool bArmed = false;

	/** Objective-flow gate (SetRescueEnabled). Defaults on so a hand-placed test VIP works
	 *  without a flow; DemoMap's flow disables it at activation and re-enables at step entry. */
	bool bRescueEnabled = true;

	/** Once-only guard on the opening line: a cancelled hold can't un-say it, so re-pressing
	 *  must not stack another take on top. */
	bool bRescueStartLineSpoken = false;

	FTimerHandle ArmTimerHandle;
	FTimerHandle ReplyLineTimerHandle;
	FTimerHandle WaveTimerHandle;

	/** Stage 2: captive off, AI on, still unarmed. bCeremony runs the handoff beat and the
	 *  delayed OnRescued; the checkpoint fast-forward skips straight to armed. */
	void CompleteRescue(bool bCeremony);

	/** Stage 3: pistol visible, ABP swaps to the armed pose, reply line, then the wave timer. */
	void ArmWithPistol(bool bCeremony);
};
