// BT task — peek-fire loop: Acquire → Expose → Fire → Recover → Pause → repeat.
// Phase 4: suppression gating — suppressed enemies skip Expose and duck back from Fire.
// Bugs 1+2: while AwarenessState==Combat, the task stays InProgress (never returns Failed
// to the Selector) — pursues or re-seeks cover instead.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_EnemyCombatFire.generated.h"

class AAICoverSlot;

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
	};

	/** How far (cm) to step sideways when exposing from a stand-height cover slot. */
	UPROPERTY(EditAnywhere, Category = "Combat|Cover", meta = (ClampMin = "0.0"))
	float PeekLateralOffset = 60.f;

	/** Cooldown (seconds) between attempts to find cover while suppressed in the open. */
	UPROPERTY(EditAnywhere, Category = "Combat|Suppression", meta = (ClampMin = "0.5"))
	float SuppressedReseekCooldown = 2.5f;

	void StopFireAndCleanUp(UBehaviorTreeComponent& OwnerComp, FFireMemory* Mem = nullptr) const;
};
