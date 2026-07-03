// BT task — peek-fire loop: Acquire → Expose → Fire → Recover → Pause → repeat.
// Phase 4: suppression gating — suppressed enemies skip Expose and duck back from Fire.
// Bugs 1+2: while AwarenessState==Combat, the task stays InProgress (never returns Failed
// to the Selector) — pursues or re-seeks cover instead.
// P2 AICS migration: cover source changed from AAICoverSlot to FCoverHandle/FCoverData.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "CoverSystemPublicData.h"
#include "AI/Cover/CoverPoseTypes.h"
#include "BTTask_EnemyCombatFire.generated.h"

class ACoverSystem;
class AEnemyCharacter;
class UCoverReservationSubsystem;
class UEnemyArchetypeData;

UENUM()
enum class EFireTaskPhase : uint8
{
	Acquire,
	Expose,
	Fire,
	Recover,
	Pause,
	/** Pursuing target: moving toward last known location while in combat. */
	Pursuing,
	/** Re-seeking cover while suppressed without cover. */
	SeekingCover,
};

UCLASS()
class EXTRACTION_API UBTTask_EnemyCombatFire : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_EnemyCombatFire();

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual uint16 GetInstanceMemorySize() const override;
	virtual void InitializeMemory(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTMemoryInit::Type InitType) const override;
	virtual void CleanupMemory(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTMemoryClear::Type CleanupType) const override;
	virtual FString GetStaticDescription() const override;
	virtual void InitializeFromAsset(UBehaviorTree& Asset) override;

private:

	struct FFireMemory
	{
		EFireTaskPhase Phase = EFireTaskPhase::Acquire;
		float PhaseTimer = 0.f;
		float BurstDuration = 0.f;
		float PauseDuration = 0.f;
		/** World time of last suppressed-reseek-cover attempt (guards thrashing). */
		float LastReseekCoverTime = -1e9f;
		/** AICS cover handle for cover obtained during SeekingCover (replaces old ReseekSlot). */
		FCoverHandle ReseekCover;
		/** Snapshot of the cover data at time of pick (used when the live read fails). */
		FCoverData ReseekCoverData;
		/** Destination stored when issuing the seek-cover MoveToLocation (arrival validation). */
		FVector ReseekArrivalPos = FVector::ZeroVector;

		// Stall detection for the SeekingCover transit — recover if the relocate move stops progressing.
		float SeekStallBestDist = TNumericLimits<float>::Max();
		float SeekStallAccum = 0.f;

		// --- Part B: flank-break state ---

		/** Accumulated dwell time at the current cover point (seconds since physical arrival). */
		float SlotDwellTime = 0.f;
		/** Whether the pawn has physically arrived at the current cover point this task execution. */
		bool bArrivedAtSlot = false;
		/** Seconds since last compromise evaluation. */
		float CompromiseEvalTimer = 0.f;
		/** Consecutive evaluations that agreed the cover is compromised (debounce counter). */
		int32 CompromiseConsecutiveCount = 0;
		/** World time when the last flank-triggered relocate completed (per-enemy cooldown). */
		float LastRelocateCompletedTime = -1e9f;
		/** Seconds LOS has been continuously absent during an active burst (Fire phase). */
		float NoLosGraceTimer = 0.f;
		/** Seconds Expose has waited for LOS before opening fire (anti-stuck cap). */
		float ExposeLosWaitTimer = 0.f;
		/** Consecutive Expose-phase timeouts with no LOS (never fired). Reset when fire opens. */
		int32 ExposeLosTimeoutCount = 0;
		/** True when a compromise was detected during Fire; deferred commit to next safe phase. */
		bool bRelocatePending = false;
		/** Completed peek-fire cycles (Fire→Recover) at the current cover. Relocate/shuffle are
		 *  held until at least one full cycle has run — commit to a point before moving on. */
		int32 PeekCyclesAtCover = 0;

		/** True once a side-peek corner hop has been attempted from the current cover point —
		 *  prevents ping-ponging when no same-wall point carries the wanted side flag. */
		bool bSidePeekHopTried = false;

		/** True while a drift-correct nudge back to the hunker is in flight. The move steers the
		 *  pawn toward the wall; this flag drives a per-tick re-assert of the back-to-cover facing
		 *  so the body stays tucked facing out instead of swinging 180°. Cleared on arrival. */
		bool bDriftCorrecting = false;
		/** Cover data captured when the drift move was issued — supplies the WallYaw for the
		 *  per-tick facing re-assert without a fresh BB read. */
		FCoverData DriftFacingCover;
		/** Hunker destination of the active drift move (2D arrival test for the facing re-assert). */
		FVector DriftArrivalPos = FVector::ZeroVector;

		// --- Blind-fire state (Feature A) ---

		/** True once the per-suppression-episode roll has been made. Reset when suppression clears. */
		bool bBlindFireDecided = false;
		/** Result of the per-episode roll: true = blind-fire, false = hide. */
		bool bBlindFireChosen = false;
		/** True while the enemy is actively blind-firing (burst in progress). */
		bool bBlindFiringNow = false;
		/** Remaining blind-fire burst time. */
		float BlindFireBurstTimer = 0.f;
		/** Tracks whether we were suppressed last tick (edge-detect for episode reset). */
		bool bWasSuppressedLastTick = false;

		// --- Cached subsystem pointers (PERF #8) ---
		TWeakObjectPtr<ACoverSystem> CachedCoverSys;
		TWeakObjectPtr<UCoverReservationSubsystem> CachedResSub;
		/** World-time of last mis-wiring warning log (throttle). */
		float LastMiswiringLogTime = -10.f;
	};

	/** BB key holding the Cover type (FCover via AICS plugin). Default name "CoverTarget". */
	UPROPERTY(EditAnywhere, Category = "Cover")
	FBlackboardKeySelector CoverTargetKey;

	/** Cooldown (seconds) between attempts to find cover while suppressed in the open. */
	UPROPERTY(EditAnywhere, Category = "Combat|Suppression", meta = (ClampMin = "0.5"))
	float SuppressedReseekCooldown = 2.5f;

	void StopFireAndCleanUp(UBehaviorTreeComponent& OwnerComp, FFireMemory* Mem = nullptr) const;

	/** Shared relocate path: vacate current cover, find protective cover or fall back to strafe.
	 *  Used by both the compromise debounce and the Expose-LOS-timeout path. */
	void ExecuteRelocate(UBehaviorTreeComponent& OwnerComp, FFireMemory* Mem,
		AAIController* Controller, APawn* Pawn, AEnemyCharacter* Enemy,
		AActor* Target, const FCoverHandle& CurCoverHandle, const FCoverData& CurCoverData,
		const UEnemyArchetypeData* DA, bool bHasLOS) const;

	/** Attempt to find cover via AICS bounds query, write it to the CoverTarget BB key,
	 *  issue MoveToLocation, and enter SeekingCover.
	 *  Returns true on success (phase set to SeekingCover). Does NOT stamp LastReseekCoverTime. */
	bool TryReseekCover(UBehaviorTreeComponent& OwnerComp, FFireMemory* Mem,
		AAIController* Controller, APawn* Pawn, AEnemyCharacter* Enemy, AActor* Target,
		const UEnemyArchetypeData* DA, bool bHasLOS) const;

	/** Clear blind-fire spread/pose state. Called on every exit path that ends or interrupts blind fire. */
	void ClearBlindFireState(AEnemyCharacter* Enemy, FFireMemory* Mem) const;

	/** Find a neighbouring cover point on the same wall for shuffle (mirrors companion's FindShuffleCover). */
	FCover FindShuffleCover(UWorld* World, const APawn* Pawn, const FCover& CurrentCover,
		const FVector& ThreatLocation, const UEnemyArchetypeData* DA, AController* Controller,
		AActor* Target) const;

	/** enemy.ForceCoverPeekSide support: find the SIDE-EXTREME same-wall cover point toward Side
	 *  (the wall end), preferring one baked with the matching lean flag. Natural combat never
	 *  hops — side peeks are positional (end points carry the flags; mid-wall goes over-top).
	 *  Occupancy gates mirror FindShuffleCover. Returns invalid when already end-most. */
	FCover FindSidePeekCover(UWorld* World, const APawn* Pawn, const FCover& CurrentCover,
		ECoverLean Side, const FVector& ThreatLocation, const UEnemyArchetypeData* DA,
		AController* Controller, AActor* Target) const;

	/** Read the current cover from the CoverTarget BB key. Returns an invalid FCover when unset. */
	FCover ReadCoverFromBB(const UBlackboardComponent* BB) const;

	/** Write a new cover to the CoverTarget BB key. */
	void WriteCoverToBB(UBlackboardComponent* BB, const FCover& Cover) const;

	/** Clear the CoverTarget BB key. */
	void ClearCoverBB(UBlackboardComponent* BB) const;
};
