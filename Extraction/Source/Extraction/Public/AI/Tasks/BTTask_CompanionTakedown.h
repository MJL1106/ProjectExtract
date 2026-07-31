// BT task -- companion coordinated takedown. Approaches victim (knife: crouched sneak to stab
// range; shoot: repositions until it has a line), arms the takedown on CompanionCharacter, then
// waits for the player's commit signal.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "Companion/CompanionCommandTypes.h"
#include "BTTask_CompanionTakedown.generated.h"

class ACompanionCharacter;
class AAIController;

UCLASS()
class EXTRACTION_API UBTTask_CompanionTakedown : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_CompanionTakedown();

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual void OnTaskFinished(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTNodeResult::Type TaskResult) override;
	virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual FString GetStaticDescription() const override;

protected:
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector CommandTargetActorKey;

	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector TakedownMethodKey;

	/** Accept radius for knife approach MoveToLocation. */
	UPROPERTY(EditAnywhere, Category = "Takedown|Knife", meta = (ClampMin = "10.0"))
	float KnifeApproachAcceptRadius = 60.f;

	/** How far behind/beside the victim the companion's stab anchor is placed. */
	UPROPERTY(EditAnywhere, Category = "Takedown|Knife", meta = (ClampMin = "30.0"))
	float KnifeAnchorDistance = 120.f;

	/** Lateral offset angle (degrees) from behind the victim for the stab anchor.
	 *  0 = directly behind; 90 = perpendicular. */
	UPROPERTY(EditAnywhere, Category = "Takedown|Knife", meta = (ClampMin = "0.0", ClampMax = "90.0"))
	float KnifeAnchorAngleDegrees = 30.f;

	/** Max time (seconds) allowed for the knife approach before aborting. */
	UPROPERTY(EditAnywhere, Category = "Takedown|Knife", meta = (ClampMin = "5.0"))
	float KnifeApproachTimeout = 15.f;

	/** Max time (seconds) the companion holds the armed position before aborting
	 *  (prevents starving follow/combat if the player never commits).
	 *  Stands down while the player is still deciding (see IsPlayerStillDeciding) — the takedown is
	 *  synced and unfired, so OverallTakedownTimeout replaces it as the hold's abort condition.
	 *  Keeps its original meaning the instant the player commits (or on an autonomous solo run). */
	UPROPERTY(EditAnywhere, Category = "Takedown", meta = (ClampMin = "1.0"))
	float ArmedHoldTimeout = 10.f;

	/** Absolute ceiling (seconds) on the whole takedown task once the player is still deciding
	 *  (see IsPlayerStillDeciding). ShootApproachTimeout, MaxArmedReanchors and ArmedHoldTimeout all
	 *  stand down while the player hasn't committed yet — this is the one budget that survives that,
	 *  so the branch can never be starved forever if the player instead wanders off mid-setup and
	 *  never comes back. Deliberately generous: the point of this fix is patience, not a disguised
	 *  short timeout with extra steps. */
	UPROPERTY(EditAnywhere, Category = "Takedown", meta = (ClampMin = "5.0"))
	float OverallTakedownTimeout = 45.f;

	/** Min seconds the autonomous shoot waits (after armed) before firing. */
	UPROPERTY(EditAnywhere, Category = "Takedown|Shoot", meta = (ClampMin = "0.5"))
	float AutonomousShootDelayMin = 2.f;

	/** Max seconds the autonomous shoot waits (after armed) before firing. */
	UPROPERTY(EditAnywhere, Category = "Takedown|Shoot", meta = (ClampMin = "0.5"))
	float AutonomousShootDelayMax = 4.f;

	/** Accept radius for the shoot repositioning MoveToLocation. Rarely reached — the approach arms
	 *  the moment the line clears, so this only matters when the anchor IS the firing spot. */
	UPROPERTY(EditAnywhere, Category = "Takedown|Shoot", meta = (ClampMin = "10.0"))
	float ShootApproachAcceptRadius = 60.f;

	/** Step distance (cm) for the shoot repositioning fan taken from the companion's own position
	 *  (right / left / back / back-right / back-left). Tried at this distance, then at double it. */
	UPROPERTY(EditAnywhere, Category = "Takedown|Shoot", meta = (ClampMin = "30.0"))
	float ShootAnchorStepDistance = 200.f;

	/** Distance (cm) from the victim for the ring fallback, used only when no step off the
	 *  companion's own position finds a line. Keep inside effective weapon range. */
	UPROPERTY(EditAnywhere, Category = "Takedown|Shoot", meta = (ClampMin = "100.0"))
	float ShootAnchorRingStandoff = 500.f;

	/** Max time (seconds) across ALL shoot repositioning before aborting to normal gunfire.
	 *  Stands down while the player is still deciding (see IsPlayerStillDeciding) — OverallTakedownTimeout
	 *  replaces it so the approach isn't punished for the player taking their time. Keeps its original
	 *  meaning the instant the player commits (or on an autonomous solo run). */
	UPROPERTY(EditAnywhere, Category = "Takedown|Shoot", meta = (ClampMin = "1.0"))
	float ShootApproachTimeout = 8.f;

	/** Minimum seconds between patient-path re-anchors (see PatientReanchorCooldownElapsed). Unbounded
	 *  patient re-anchoring (Fix 2) still has to be throttled, or a victim flickering across a pillar
	 *  edge could drive a full ComputeShootAnchor scan (worst case 18 nav projections + 18 line
	 *  traces) every recheck tick. Deliberately short — this throttles a flicker, it isn't a budget. */
	UPROPERTY(EditAnywhere, Category = "Takedown|Shoot", meta = (ClampMin = "0.05"))
	float PatientReanchorCooldown = 0.75f;

	/** Seconds between line-of-sight re-tests while armed and waiting to fire — the victim can walk
	 *  behind cover during the settle. Throttled because a full trace every tick is wasted work. */
	UPROPERTY(EditAnywhere, Category = "Takedown|Shoot", meta = (ClampMin = "0.05"))
	float ShootLosRecheckInterval = 0.25f;

	/** Seconds the companion stays locked onto a victim it failed to take down silently, killing it
	 *  with normal gunfire before it may pick any other combat target. Safety valve only — the lock
	 *  releases the moment the victim dies. 0 disables the commitment entirely. */
	UPROPERTY(EditAnywhere, Category = "Takedown", meta = (ClampMin = "0.0"))
	float TakedownCommitHoldSeconds = 8.f;

private:
	enum class EPhase : uint8
	{
		Approaching,    // Knife: moving to stab anchor. Shoot: repositioning for a clear line
		Armed,          // In position, waiting for player commit
		Executing,      // Takedown in progress (montage / shot)
		Done,
	};

	void CleanupTask(ACompanionCharacter* Companion);

	/** Try +angle, -angle, 0 behind the victim; return the first nav-reachable anchor. */
	FVector ComputeKnifeAnchor(const AActor* Victim) const;

	/** Single-candidate helper for ComputeKnifeAnchor. */
	FVector ComputeAnchorCandidate(const FVector& VictimLoc, const FVector& Behind, float AngleDeg) const;

	/** Shoot entry: arms in place when the line is already clear, otherwise starts the reposition.
	 *  False = nowhere on the navmesh can see the victim, so the caller must fail the task. */
	bool BeginShootTakedown(ACompanionCharacter* Companion, AAIController* AIC, AActor* Victim);

	/** Shoot: picks a firing anchor and issues the move. False = no anchor, or nav refused it. */
	bool BeginShootApproach(ACompanionCharacter* Companion, AAIController* AIC, AActor* Victim);

	/** Shoot: arms the takedown exactly where the companion stands (line already verified clear). */
	void ArmShootInPlace(ACompanionCharacter* Companion, AAIController* AIC);

	/** Shoot approach tick. Returns true once it has finished the latent task — the caller must
	 *  return immediately, since CleanupTask has already torn the node state down by then. */
	bool TickShootApproach(UBehaviorTreeComponent& OwnerComp, AAIController* AIC,
		ACompanionCharacter* Companion, AActor* Victim, float DeltaSeconds);

	/** True when the takedown is synced (not autonomous), the player has not yet committed, and
	 *  execution hasn't already started — i.e. the player is still deciding, so the companion still
	 *  owes them a firing position and every short abort budget (ShootApproachTimeout,
	 *  MaxArmedReanchors, ArmedHoldTimeout) stands down in favour of OverallTakedownTimeout. The
	 *  player firing early flips IsCommandedTakedownPlayerCommitted() (or starts Executing) and
	 *  hands control straight back to the original short budgets — the one acceptable way to still
	 *  end up out of position. */
	bool IsPlayerStillDeciding(const ACompanionCharacter* Companion) const;

	/** Patient-path re-anchor cooldown gate: true (and resets the accumulator) once
	 *  PatientReanchorCooldown has elapsed since the last patient re-anchor attempt; false while
	 *  still cooling down. Shared by the approach dead-settle and the armed re-break — both are
	 *  unbounded while patient, so both need the same throttle against a flickering line driving a
	 *  search loop. */
	bool ConsumePatientReanchorCooldown();

	/** Approach dead-settle: the reposition move finished with the line still blocked. Non-patient
	 *  keeps the original single-shot re-anchor (bShootReanchorUsed); patient re-anchors unbounded,
	 *  throttled only by ConsumePatientReanchorCooldown, since the player waiting is exactly when the
	 *  companion must keep trying rather than give up. Either path hard-fails to gunfire the moment
	 *  BeginShootApproach itself returns false — that means ComputeShootAnchor found no reachable
	 *  navmesh point ANYWHERE with a line to the victim, i.e. there is genuinely no firing position
	 *  left to look for, patient or not. Returns true once FinishLatentTask has fired (caller must
	 *  return immediately); false to keep going (re-anchor issued, or cooling down). */
	bool HandleShootApproachDeadSettle(UBehaviorTreeComponent& OwnerComp, AAIController* AIC,
		ACompanionCharacter* Companion, AActor* Victim, bool bPatient);

	/** Armed-phase LoS re-break: the victim stepped behind cover while the companion held its aim.
	 *  Non-patient keeps the original MaxArmedReanchors cap; patient stands it down and instead
	 *  throttles on ConsumePatientReanchorCooldown, same rationale as the approach dead-settle.
	 *  ArmedReanchorCount still increments either way — it keeps driving the log line and still
	 *  gates the non-patient cap. A failed BeginShootApproach is a hard fail regardless of patience:
	 *  no reachable firing spot exists. Returns true once FinishLatentTask has fired (caller must
	 *  return immediately); false to keep going (re-anchor issued, or cooling down). */
	bool HandleArmedShootLosBreak(UBehaviorTreeComponent& OwnerComp, AAIController* AIC,
		ACompanionCharacter* Companion, AActor* Victim, bool bPatient);

	/** Shoot: nav-reachable spot with a clear line to the victim. Fan from the companion's own
	 *  position first (cheapest, smallest displacement), ring around the victim as the fallback. */
	bool ComputeShootAnchor(const ACompanionCharacter* Companion, const AActor* Victim, FVector& OutAnchor) const;

	/** One fan pass at StepDistance: lateral right/left, back, back-right, back-left. */
	bool TryShootFanAnchor(const ACompanionCharacter* Companion, const AActor* Victim, float StepDistance, FVector& OutAnchor) const;

	/** Ring fallback: yaw offsets around the victim at ShootAnchorRingStandoff, nearest wins. */
	bool TryShootRingAnchor(const ACompanionCharacter* Companion, const AActor* Victim, FVector& OutAnchor) const;

	/** Single-candidate helper for the fan/ring passes: nav-project, then line-verify. */
	bool TryShootAnchorCandidate(const ACompanionCharacter* Companion, const AActor* Victim,
		const FVector& RawCandidate, FVector& OutProjected) const;

	/** Returns Succeeded if the victim is dead/destroyed, Failed otherwise. */
	EBTNodeResult::Type CompletionResult() const;

	TWeakObjectPtr<AActor> CachedVictim;
	ETakedownMethod CachedMethod = ETakedownMethod::Knife;
	EPhase Phase = EPhase::Approaching;
	float ApproachElapsed = 0.f;
	float ArmedHoldElapsed = 0.f;
	FVector CachedKnifeAnchor = FVector::ZeroVector;
	bool bMoveRequestSent = false;

	/** Seconds since ExecuteTask started this task run. Ticks every frame regardless of phase —
	 *  the one budget (via OverallTakedownTimeout) that survives the player being patiently waited
	 *  on standing every other abort budget down. Reset alongside the other accumulators in
	 *  ExecuteTask and CleanupTask. */
	float TaskElapsed = 0.f;

	/** IsPlayerStillDeciding() result as of the last TickShootApproach call. Lets TickShootApproach
	 *  detect the patient->non-patient edge (player commits, or execution starts, mid-approach) and
	 *  zero ApproachElapsed on that exact tick — see the reset in TickShootApproach for why: without
	 *  it, ApproachElapsed keeps accumulating through the whole patient wait and can already be well
	 *  past ShootApproachTimeout the instant the player commits, aborting to gunfire immediately
	 *  instead of giving the resumed approach its full grace window. Scoped to TickShootApproach only
	 *  (not a whole-task edge) so a knife takedown's KnifeApproachTimeout — which never consults
	 *  patience — is untouched. Reset alongside the other accumulators in ExecuteTask/CleanupTask. */
	bool bWasApproachPatient = false;

	/** True when this is a solo takedown (lone eligible enemy in the volume). */
	bool bAutonomous = false;
	/** Guards autonomous knife/shoot so CommitTakedownNow fires exactly once. */
	bool bAutonomousCommitSent = false;
	float AutonomousShootElapsed = 0.f;
	float AutonomousShootDelay = 0.f;

	/** Shoot: seconds since the current reposition move was issued. Debounces the settle test —
	 *  GetMoveStatus still reads Idle for a frame or two after MoveToLocation. */
	float ShootMoveIssuedElapsed = 0.f;
	/** Shoot: accumulator for the throttled armed-phase line re-test. */
	float ShootLosRecheckElapsed = 0.f;
	/** Shoot: true once the approach has re-anchored after a dead settle. A second dead settle
	 *  means no reachable firing spot exists, so the task degrades to gunfire instead of looping.
	 *  Only checked on the non-patient path — while the player is still deciding (see
	 *  IsPlayerStillDeciding) re-anchoring is unbounded and throttled by
	 *  ConsumePatientReanchorCooldown instead. */
	bool bShootReanchorUsed = false;

	/** Count of armed-phase LoS re-breaks that returned to Approaching during a single task run.
	 *  A victim patrolling across a pillar edge can flicker the aim-point trace indefinitely,
	 *  cycling Armed->Approaching hundreds of times (each costing a full ComputeShootAnchor: worst
	 *  case 18 nav projections + 18 line traces). Capped at MaxArmedReanchors so the flicker path
	 *  degrades to normal gunfire instead of looping. The approach-path dead-settle re-anchor
	 *  (bShootReanchorUsed) is counted separately. While the player is still deciding (see
	 *  IsPlayerStillDeciding) the cap stands down and this is incremented only once per actual
	 *  re-anchor attempt (post-cooldown, in HandleArmedShootLosBreak) — not per throttled LoS
	 *  re-test — so a patient flicker sitting on the faster ShootLosRecheckInterval cadence doesn't
	 *  inflate the count relative to what actually happened. */
	int32 ArmedReanchorCount = 0;

	/** Max armed-phase LoS re-breaks before degrating to gunfire. Must stay small: each cycle is
	 *  ~0.25s of stutter-stop. Same self-limiting idiom as bShootReanchorUsed. Only enforced once the
	 *  player has already committed (or on an autonomous solo run) — see IsPlayerStillDeciding. */
	static constexpr int32 MaxArmedReanchors = 3;

	/** Seconds since the last patient-path re-anchor attempt. See ConsumePatientReanchorCooldown /
	 *  PatientReanchorCooldown. Ticks every frame alongside TaskElapsed; reset to 0 whenever a
	 *  patient re-anchor is actually consumed. Seeded to PatientReanchorCooldown (not 0) in
	 *  ExecuteTask/CleanupTask so the FIRST patient re-anchor of a task run fires immediately and
	 *  only repeats afterward are throttled. Non-patient paths don't use this — they keep their own
	 *  self-limiting counters (bShootReanchorUsed / MaxArmedReanchors). */
	float PatientReanchorCooldownElapsed = 0.f;
};
