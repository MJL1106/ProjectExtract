// BT task (instant) -- guarded clear of BB_CompanionCommand. Placed after the command's work
// node so the key is cleaned up once the order resolves. Clears only if the active command still
// matches ExpectedCommand, so a command chained in during the walk is not wiped.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "Companion/CompanionCommandTypes.h"
#include "BTTask_CompanionClearCommand.generated.h"

UCLASS()
class EXTRACTION_API UBTTask_CompanionClearCommand : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_CompanionClearCommand();

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual FString GetStaticDescription() const override;

protected:
	/** The command this node owns. Clear fires only when the BB key still holds this value. */
	UPROPERTY(EditAnywhere, Category = "Command")
	ECompanionCommand ExpectedCommand = ECompanionCommand::TakeCover;
};
