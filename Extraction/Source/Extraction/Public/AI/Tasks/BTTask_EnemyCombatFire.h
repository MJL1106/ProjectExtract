// BT task — peek-fire loop: Acquire → Expose → Fire → Recover → Pause → repeat.
// Phase 4: suppression gating — suppressed enemies skip Expose and duck back from Fire.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_EnemyCombatFire.generated.h"

UENUM()
enum class EFireTaskPhase : uint8
{
	Acquire,
	Expose,
	Fire,
	Recover,
	Pause,
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
	};

	/** How far (cm) to step sideways when exposing from a stand-height cover slot. */
	UPROPERTY(EditAnywhere, Category = "Combat|Cover", meta = (ClampMin = "0.0"))
	float PeekLateralOffset = 60.f;

	void StopFireAndCleanUp(UBehaviorTreeComponent& OwnerComp, FFireMemory* Mem = nullptr) const;
};
