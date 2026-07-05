// BT service — ticks to update all companion blackboard keys (DBNO, combat target, etc).

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTService.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "Companion/CompanionTypes.h"
#include "BTService_UpdateCompanionState.generated.h"

UCLASS()
class EXTRACTION_API UBTService_UpdateCompanionState : public UBTService
{
	GENERATED_BODY()

public:
	UBTService_UpdateCompanionState();

	virtual FString GetStaticDescription() const override;

protected:
	virtual void TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual void InitializeFromAsset(UBehaviorTree& Asset) override;

	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector PlayerActorKey;

	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector PlayerNeedsReviveKey;

	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector CombatTargetKey;

	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector HasCoverPositionKey;

	/**
	 * New-system cover point (FCover). Resolved by name "CoverTarget" in InitializeFromAsset so no BT
	 * asset edit is required. Read (not written) here — used to detect "my own wall is blocking my
	 * eye-line" so an LoS block while genuinely in cover keeps the target instead of clearing it.
	 */
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector CoverTargetKey;

	/** Toggle verbose perception/target logs under LogCompanionAI. Enable per-instance in BT. */
	UPROPERTY(EditAnywhere, Category = "Debug")
	bool bDebugLogging = false;

	UPROPERTY(EditAnywhere, Category = "Posture", meta = (ClampMin = "0.5", ClampMax = "10.0"))
	float ExploreReturnDelay = 3.f;

	/** Grace before clearing a no-cover combat target on sustained LoS block — lets the companion reposition to regain a shot instead of thrashing on brief occlusion. */
	UPROPERTY(EditAnywhere, Category = "Companion|Combat", meta = (ClampMin = "0.0"))
	float CombatTargetLosGraceSeconds = 3.0f;

private:
	float OutOfCombatTimer = 0.f;
	/** True once the weapon has been lowered since the last target/threat was lost — edge guard for
	 *  the immediate lower, robust across branch switches (stealth re-pin -> combat decay). */
	bool bLoweredOnTargetLoss = false;
	bool bWasLosBlocked = false;
	/** Accumulated time the no-cover combat target has been LoS-blocked; cleared on LoS-clear or active cover slot. */
	float OpenLosBlockedTime = 0.f;
	TWeakObjectPtr<AActor> PrevCombatTarget;
	/** One-time guard so the CoverTarget-key-unresolved warning fires at most once per instance. */
	bool bLoggedCoverKeyResolveFail = false;
};
