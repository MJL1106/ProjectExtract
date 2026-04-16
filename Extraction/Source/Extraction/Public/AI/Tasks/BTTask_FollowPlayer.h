// BT task — companion follows player in formation or sprints to reach them.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "BTTask_FollowPlayer.generated.h"

UCLASS()
class EXTRACTION_API UBTTask_FollowPlayer : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_FollowPlayer();

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual FString GetStaticDescription() const override;

protected:
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector PlayerActorKey;

	/** Distance from formation point to consider arrived */
	UPROPERTY(EditAnywhere, Category = "Follow", meta = (ClampMin = "50.0"))
	float AcceptableRadius = 300.0f;

	/** Distance beyond which companion sprints to catch up */
	UPROPERTY(EditAnywhere, Category = "Follow", meta = (ClampMin = "100.0"))
	float SprintDistanceThreshold = 600.0f;

	/** Offset behind the player's forward vector */
	UPROPERTY(EditAnywhere, Category = "Follow")
	float FormationOffsetBack = 200.0f;

	/** Offset to the player's right */
	UPROPERTY(EditAnywhere, Category = "Follow")
	float FormationOffsetRight = 150.0f;

	/** Always sprint (used for revive branch) */
	UPROPERTY(EditAnywhere, Category = "Follow")
	bool bSprintToTarget = false;

	UPROPERTY(EditAnywhere, Category = "Follow", meta = (ClampMin = "100.0"))
	float WalkSpeed = 600.0f;

	UPROPERTY(EditAnywhere, Category = "Follow", meta = (ClampMin = "100.0"))
	float SprintSpeed = 900.0f;
};
