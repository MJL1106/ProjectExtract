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
	 *  (prevents starving follow/combat if the player never commits). */
	UPROPERTY(EditAnywhere, Category = "Takedown", meta = (ClampMin = "1.0"))
	float ArmedHoldTimeout = 10.f;

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

	/** Max time (seconds) across ALL shoot repositioning before aborting to normal gunfire. */
	UPROPERTY(EditAnywhere, Category = "Takedown|Shoot", meta = (ClampMin = "1.0"))
	float ShootApproachTimeout = 8.f;

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
	 *  means no reachable firing spot exists, so the task degrades to gunfire instead of looping. */
	bool bShootReanchorUsed = false;

	/** Count of armed-phase LoS re-breaks that returned to Approaching during a single task run.
	 *  A victim patrolling across a pillar edge can flicker the aim-point trace indefinitely,
	 *  cycling Armed->Approaching hundreds of times (each costing a full ComputeShootAnchor: worst
	 *  case 18 nav projections + 18 line traces). Capped at MaxArmedReanchors so the flicker path
	 *  degrades to normal gunfire instead of looping. The approach-path dead-settle re-anchor
	 *  (bShootReanchorUsed) is counted separately. */
	int32 ArmedReanchorCount = 0;

	/** Max armed-phase LoS re-breaks before degrating to gunfire. Must stay small: each cycle is
	 *  ~0.25s of stutter-stop. Same self-limiting idiom as bShootReanchorUsed. */
	static constexpr int32 MaxArmedReanchors = 3;
};
