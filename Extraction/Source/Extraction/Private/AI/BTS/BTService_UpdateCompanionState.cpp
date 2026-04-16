// BT service — ticks to update all companion blackboard keys (DBNO, combat target, etc).

#include "BTService_UpdateCompanionState.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISense_Sight.h"
#include "CompanionAIController.h"
#include "CompanionCharacter.h"
#include "ExtractionCharacter.h"
#include "HealthComponent.h"
#include "ExtractionTypes.h"
#include "GameplayTagAssetInterface.h"

UBTService_UpdateCompanionState::UBTService_UpdateCompanionState()
{
	NodeName = TEXT("Update Companion State");
	Interval = 0.25f;
	RandomDeviation = 0.05f;
}

void UBTService_UpdateCompanionState::TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	Super::TickNode(OwnerComp, NodeMemory, DeltaSeconds);

	ACompanionAIController* Controller = Cast<ACompanionAIController>(OwnerComp.GetAIOwner());
	if (!Controller) return;

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!BB) return;

	ACompanionCharacter* Companion = Cast<ACompanionCharacter>(Controller->GetPawn());
	if (!Companion) return;

	// --- Update PlayerNeedsRevive ---
	AExtractionCharacter* Player = Controller->GetPlayerCharacter();
	if (Player)
	{
		BB->SetValueAsBool(PlayerNeedsReviveKey.SelectedKeyName, Player->GetIsDBNO());
	}

	// --- Update CombatTarget ---
	UAIPerceptionComponent* Perception = Controller->GetPerceptionComponent();
	if (!Perception) return;

	// Validate existing target first
	AActor* ExistingTarget = Cast<AActor>(BB->GetValueAsObject(CombatTargetKey.SelectedKeyName));
	if (ExistingTarget)
	{
		UHealthComponent* TargetHealth = ExistingTarget->FindComponentByClass<UHealthComponent>();
		if (!IsValid(ExistingTarget) || (TargetHealth && TargetHealth->IsDead()))
		{
			BB->ClearValue(CombatTargetKey.SelectedKeyName);
			BB->SetValueAsBool(HasCoverPositionKey.SelectedKeyName, false);
			ExistingTarget = nullptr;
		}
	}

	// Find best target from perceived actors
	TArray<AActor*> PerceivedActors;
	Perception->GetCurrentlyPerceivedActors(UAISense_Sight::StaticClass(), PerceivedActors);

	AActor* BestTarget = nullptr;
	float BestDistSq = MAX_FLT;
	const FVector MyLocation = Companion->GetActorLocation();

	for (AActor* Actor : PerceivedActors)
	{
		if (!IsValid(Actor)) continue;

		// Must have enemy tag
		const IGameplayTagAssetInterface* TagInterface = Cast<IGameplayTagAssetInterface>(Actor);
		if (!TagInterface) continue;

		FGameplayTagContainer ActorTags;
		TagInterface->GetOwnedGameplayTags(ActorTags);
		if (!ActorTags.HasTag(TAG_Character_Enemy)) continue;

		// Must be alive
		UHealthComponent* EnemyHealth = Actor->FindComponentByClass<UHealthComponent>();
		if (EnemyHealth && EnemyHealth->IsDead()) continue;

		const float DistSq = FVector::DistSquared(MyLocation, Actor->GetActorLocation());
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			BestTarget = Actor;
		}
	}

	if (BestTarget)
	{
		// Only clear cover if target changed
		if (BestTarget != ExistingTarget)
			BB->SetValueAsBool(HasCoverPositionKey.SelectedKeyName, false);

		BB->SetValueAsObject(CombatTargetKey.SelectedKeyName, BestTarget);
	}
	else if (ExistingTarget == nullptr)
	{
		BB->ClearValue(CombatTargetKey.SelectedKeyName);
	}
}

FString UBTService_UpdateCompanionState::GetStaticDescription() const
{
	return TEXT("Updates companion BB: player DBNO, combat target from perception");
}
