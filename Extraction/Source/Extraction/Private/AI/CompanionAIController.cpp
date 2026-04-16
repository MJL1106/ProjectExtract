// AI controller for the companion — perception, blackboard, behaviour tree.

#include "CompanionAIController.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISenseConfig_Sight.h"
#include "Perception/AISenseConfig_Hearing.h"
#include "Kismet/GameplayStatics.h"
#include "ExtractionCharacter.h"
#include "CompanionCharacter.h"

const FName ACompanionAIController::BB_PlayerActor(TEXT("PlayerActor"));
const FName ACompanionAIController::BB_PlayerNeedsRevive(TEXT("PlayerNeedsRevive"));
const FName ACompanionAIController::BB_CombatTarget(TEXT("CombatTarget"));
const FName ACompanionAIController::BB_CoverLocation(TEXT("CoverLocation"));
const FName ACompanionAIController::BB_HasCoverPosition(TEXT("HasCoverPosition"));

ACompanionAIController::ACompanionAIController()
{
	// Perception component
	PerceptionComponent = CreateDefaultSubobject<UAIPerceptionComponent>(TEXT("AIPerception"));
	SetPerceptionComponent(*PerceptionComponent);

	// Sight config
	SightConfig = CreateDefaultSubobject<UAISenseConfig_Sight>(TEXT("SightConfig"));
	SightConfig->SightRadius = 3000.0f;
	SightConfig->LoseSightRadius = 3500.0f;
	SightConfig->PeripheralVisionAngleDegrees = 90.0f;
	SightConfig->SetMaxAge(5.0f);
	SightConfig->AutoSuccessRangeFromLastSeenLocation = 500.0f;
	SightConfig->DetectionByAffiliation.bDetectEnemies = true;
	SightConfig->DetectionByAffiliation.bDetectNeutrals = true;
	SightConfig->DetectionByAffiliation.bDetectFriendlies = false;

	// Hearing config
	HearingConfig = CreateDefaultSubobject<UAISenseConfig_Hearing>(TEXT("HearingConfig"));
	HearingConfig->HearingRange = 2000.0f;
	HearingConfig->SetMaxAge(3.0f);
	HearingConfig->DetectionByAffiliation.bDetectEnemies = true;
	HearingConfig->DetectionByAffiliation.bDetectNeutrals = true;
	HearingConfig->DetectionByAffiliation.bDetectFriendlies = false;

	PerceptionComponent->ConfigureSense(*SightConfig);
	PerceptionComponent->ConfigureSense(*HearingConfig);
	PerceptionComponent->SetDominantSense(UAISense_Sight::StaticClass());
}

void ACompanionAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	CachedPlayerCharacter = Cast<AExtractionCharacter>(UGameplayStatics::GetPlayerCharacter(GetWorld(), 0));

	if (!CompanionBehaviorTree)
	{
		UE_LOG(LogTemp, Warning, TEXT("CompanionAIController: No BehaviorTree assigned"));
		return;
	}

	UBlackboardComponent* BBComp = nullptr;
	UseBlackboard(CompanionBehaviorTree->BlackboardAsset, BBComp);

	if (BBComp && CachedPlayerCharacter)
		BBComp->SetValueAsObject(BB_PlayerActor, CachedPlayerCharacter);

	RunBehaviorTree(CompanionBehaviorTree);

	UE_LOG(LogTemp, Log, TEXT("CompanionAIController possessed %s, running BT"), *GetNameSafe(InPawn));
}

void ACompanionAIController::OnUnPossess()
{
	if (BrainComponent)
		BrainComponent->StopLogic(TEXT("Unpossessed"));

	Super::OnUnPossess();
}
