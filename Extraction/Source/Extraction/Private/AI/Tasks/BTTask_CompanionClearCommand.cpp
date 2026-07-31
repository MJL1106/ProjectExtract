// BT task (instant) -- guarded clear of BB_CompanionCommand.

#include "AI/Tasks/BTTask_CompanionClearCommand.h"
#include "AI/CompanionCoverStatics.h"

UBTTask_CompanionClearCommand::UBTTask_CompanionClearCommand()
{
	NodeName = TEXT("Clear Companion Command (Guarded)");
	bNotifyTick = false;
}

EBTNodeResult::Type UBTTask_CompanionClearCommand::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* /*NodeMemory*/)
{
	CompanionCover::ClearCommandIfStillActive(OwnerComp, ExpectedCommand);
	return EBTNodeResult::Succeeded;
}

FString UBTTask_CompanionClearCommand::GetStaticDescription() const
{
	return FString::Printf(TEXT("Clear CompanionCommand if still %s"),
		*UEnum::GetValueAsString(ExpectedCommand));
}
