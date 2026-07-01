// BT task -- companion coordinated takedown. Approaches victim (knife: crouched sneak),
// arms the takedown on CompanionCharacter, then waits for the player's commit signal.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "Companion/CompanionCommandTypes.h"
#include "BTTask_CompanionTakedown.generated.h"

class ACompanionCharacter;

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

private:
	enum class EPhase : uint8
	{
		Approaching,    // Knife: moving to stab anchor
		Armed,          // In position, waiting for player commit
		Executing,      // Takedown in progress (montage / shot)
		Done,
	};

	void CleanupTask(ACompanionCharacter* Companion);

	/** Try +angle, -angle, 0 behind the victim; return the first nav-reachable anchor. */
	FVector ComputeKnifeAnchor(const AActor* Victim) const;

	/** Single-candidate helper for ComputeKnifeAnchor. */
	FVector ComputeAnchorCandidate(const FVector& VictimLoc, const FVector& Behind, float AngleDeg) const;

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
};
