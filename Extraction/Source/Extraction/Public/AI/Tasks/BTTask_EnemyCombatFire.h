// BT task — peek-fire loop: Acquire → Expose → Fire → Recover → Pause → repeat.
// Phase 4: suppression gating — suppressed enemies skip Expose and duck back from Fire.
// Bugs 1+2: while AwarenessState==Combat, the task stays InProgress (never returns Failed
// to the Selector) — pursues or re-seeks cover instead.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_EnemyCombatFire.generated.h"

class AAICoverSlot;
class AEnemyCharacter;
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
	virtual FString GetStaticDescription() const override;

private:

	struct FFireMemory
	{
		EFireTaskPhase Phase = EFireTaskPhase::Acquire;
		float PhaseTimer = 0.f;
		float BurstDuration = 0.f;
		float PauseDuration = 0.f;
		bool bSuppressCrouchedNoCover = false;
		/** World time of last suppressed-reseek-cover attempt (guards thrashing). */
		float LastReseekCoverTime = -1e9f;
		/** The cover slot claimed during SeekingCover (released on cleanup). */
		TWeakObjectPtr<AAICoverSlot> ReseekSlot;
		/** Destination stored when issuing the seek-cover MoveToLocation (arrival validation). */
		FVector ReseekArrivalPos = FVector::ZeroVector;

		// --- Part B: flank-break state ---

		/** Accumulated dwell time at the current slot (seconds since physical arrival). */
		float SlotDwellTime = 0.f;
		/** Whether the pawn has physically arrived at the current slot this task execution. */
		bool bArrivedAtSlot = false;
		/** Seconds since last compromise evaluation. */
		float CompromiseEvalTimer = 0.f;
		/** Consecutive evaluations that agreed the slot is compromised (debounce counter). */
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
	};

	/** How far (cm) to step sideways when exposing from a stand-height cover slot. */
	UPROPERTY(EditAnywhere, Category = "Combat|Cover", meta = (ClampMin = "0.0"))
	float PeekLateralOffset = 60.f;

	/** Cooldown (seconds) between attempts to find cover while suppressed in the open. */
	UPROPERTY(EditAnywhere, Category = "Combat|Suppression", meta = (ClampMin = "0.5"))
	float SuppressedReseekCooldown = 2.5f;

	void StopFireAndCleanUp(UBehaviorTreeComponent& OwnerComp, FFireMemory* Mem = nullptr) const;

	/** Shared relocate path: release current slot, find protective cover or fall back to strafe.
	 *  Used by both the compromise debounce and the Expose-LOS-timeout path. */
	void ExecuteRelocate(UBehaviorTreeComponent& OwnerComp, FFireMemory* Mem,
		AAIController* Controller, APawn* Pawn, AEnemyCharacter* Enemy,
		AActor* Target, AAICoverSlot* CurSlot, const UEnemyArchetypeData* DA,
		bool bHasLOS) const;
};
