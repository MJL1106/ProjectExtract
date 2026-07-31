// BT task (instant) -- resolves the commanded cover target, writes it to the BB CoverTarget key,
// grants the cover commit bypass, and latches the commanded hold. The existing
// BTTask_MoveToCoverPoint placed after this in the BT sequence does the walk.

#include "AI/Tasks/BTTask_CompanionTakeCover.h"
#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "AI/BlackboardKeyType_Cover.h"
#include "AI/CompanionCoverStatics.h"
#include "Companion/CompanionCharacter.h"
#include "Companion/CompanionCommandTypes.h"
#include "CompanionAIController.h"
#include "CoverSystemPublicData.h"

UBTTask_CompanionTakeCover::UBTTask_CompanionTakeCover()
{
	NodeName = TEXT("Companion Take Cover");
	// Instant task -- no tick, no abort.
	bNotifyTick = false;
}

void UBTTask_CompanionTakeCover::InitializeFromAsset(UBehaviorTree& Asset)
{
	Super::InitializeFromAsset(Asset);

	if (UBlackboardData* BBAsset = GetBlackboardAsset())
	{
		CoverTargetKey.ResolveSelectedKey(*BBAsset);
	}
}

EBTNodeResult::Type UBTTask_CompanionTakeCover::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* /*NodeMemory*/)
{
	AAIController* Controller = OwnerComp.GetAIOwner();
	if (!IsValid(Controller))
	{
		UE_LOG(LogCompanionAI, Warning, TEXT("TakeCover: ExecuteTask FAILED reason=no-controller"));
		CompanionCover::ClearCommandIfStillActive(OwnerComp, ECompanionCommand::TakeCover);
		return EBTNodeResult::Failed;
	}

	ACompanionCharacter* Companion = Cast<ACompanionCharacter>(Controller->GetPawn());
	if (!IsValid(Companion))
	{
		UE_LOG(LogCompanionAI, Warning, TEXT("TakeCover: ExecuteTask FAILED reason=no-companion"));
		CompanionCover::ClearCommandIfStillActive(OwnerComp, ECompanionCommand::TakeCover);
		return EBTNodeResult::Failed;
	}

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!BB)
	{
		UE_LOG(LogCompanionAI, Warning, TEXT("%s: TakeCover ExecuteTask FAILED reason=no-BB"), *Companion->GetName());
		CompanionCover::ClearCommandIfStillActive(OwnerComp, ECompanionCommand::TakeCover);
		return EBTNodeResult::Failed;
	}

	// Consume the one-shot commanded cover target.
	FCover Cover;
	if (!Companion->ConsumeCommandedCoverTarget(Cover))
	{
		UE_LOG(LogCompanionAI, Warning, TEXT("%s: TakeCover ExecuteTask FAILED reason=no-commanded-cover-target"), *Companion->GetName());
		CompanionCover::ClearCommandIfStillActive(OwnerComp, ECompanionCommand::TakeCover);
		return EBTNodeResult::Failed;
	}

	// Write the cover to the BB key so BTTask_MoveToCoverPoint can read it.
	BB->SetValue<UBlackboardKeyType_Cover>(CoverTargetKey.GetSelectedKeyID(), Cover);

	// Grant the one-shot cover-commit bypass so MoveToCoverPoint skips the situational trigger gate.
	Companion->SetCoverCommitGrant(true);

	// Skip the multi-threat re-rank swap so the companion goes where the player pointed.
	Companion->SetCommandedCoverSkipRerank(true);

	// Bypass the autonomous distance and floor gates so the companion walks wherever the player pointed.
	Companion->SetCommandedCoverBypass(true);

	// Latch the commanded hold so Follow holds position and cover is retained.
	Companion->SetCommandedCoverHold(Cover.Data.Location, LeashRadius);

	return EBTNodeResult::Succeeded;
}

FString UBTTask_CompanionTakeCover::GetStaticDescription() const
{
	return TEXT("Resolve commanded cover target -> CoverTarget BB key, grant commit, latch hold");
}
