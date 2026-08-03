// BT task (latent) — suppressor role: sustained area fire at target/last-known, ammo-aware.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_EnemySuppressFire.generated.h"

UENUM()
enum class ESuppressFirePhase : uint8
{
	Fire,
	Pause,
};

UCLASS()
class EXTRACTION_API UBTTask_EnemySuppressFire : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_EnemySuppressFire();

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual uint16 GetInstanceMemorySize() const override;
	virtual FString GetStaticDescription() const override;

private:

	struct FSuppressFireMemory
	{
		ESuppressFirePhase Phase = ESuppressFirePhase::Fire;
		float PhaseTimer = 0.f;
		bool bAimOverrideActive = false;
		bool bFiring = false;
		/** True once LOS has been confirmed at least once -- a never-sighted ForceEngage'd enemy must never fire. */
		bool bEverHadLOS = false;
		/** Seconds since LOS was last true; fires while <= FireLosLostGrace. */
		float LosLostTimer = 0.f;
	};

	UPROPERTY(EditDefaultsOnly, Category = "Combat|Suppress", meta = (ClampMin = "0.5"))
	float BurstDurationMin = 3.f;

	UPROPERTY(EditDefaultsOnly, Category = "Combat|Suppress", meta = (ClampMin = "0.5"))
	float BurstDurationMax = 5.f;

	UPROPERTY(EditDefaultsOnly, Category = "Combat|Suppress", meta = (ClampMin = "0.05"))
	float BurstPauseMin = 0.3f;

	UPROPERTY(EditDefaultsOnly, Category = "Combat|Suppress", meta = (ClampMin = "0.05"))
	float BurstPauseMax = 0.8f;

	void CleanUp(UBehaviorTreeComponent& OwnerComp, FSuppressFireMemory* Mem) const;
};
