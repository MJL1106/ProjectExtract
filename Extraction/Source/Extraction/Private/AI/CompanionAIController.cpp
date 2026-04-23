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
#include "WeaponBase.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY(LogCompanionAI);

const FName ACompanionAIController::BB_PlayerActor(TEXT("PlayerActor"));
const FName ACompanionAIController::BB_PlayerNeedsRevive(TEXT("PlayerNeedsRevive"));
const FName ACompanionAIController::BB_CombatTarget(TEXT("CombatTarget"));
const FName ACompanionAIController::BB_CoverLocation(TEXT("CoverLocation"));
const FName ACompanionAIController::BB_HasCoverPosition(TEXT("HasCoverPosition"));

ACompanionAIController::ACompanionAIController()
{
	static ConstructorHelpers::FObjectFinder<UBehaviorTree> BTFinder(
		TEXT("/Game/Core/Blueprints/AI/Companion/BT_Companion"));
	if (BTFinder.Succeeded()) CompanionBehaviorTree = BTFinder.Object;

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
		UE_LOG(LogCompanionAI, Warning, TEXT("%s: No BehaviorTree assigned"), *GetName());
		return;
	}

	UBlackboardComponent* BBComp = nullptr;
	UseBlackboard(CompanionBehaviorTree->BlackboardAsset, BBComp);

	if (BBComp && CachedPlayerCharacter)
		BBComp->SetValueAsObject(BB_PlayerActor, CachedPlayerCharacter);

	RunBehaviorTree(CompanionBehaviorTree);

	// Diagnostic: log possession state so user can confirm config at a glance.
	const ACompanionCharacter* Companion = Cast<ACompanionCharacter>(InPawn);
	const FString WeaponClassName = (Companion && Companion->GetWeaponClass()) ? Companion->GetWeaponClass()->GetName() : TEXT("NONE");
	const float MaxEngageRange = Companion ? Companion->MaxEngageRange : -1.0f;
	const float SightRadius = SightConfig ? SightConfig->SightRadius : -1.0f;

	UE_LOG(LogCompanionAI, Log,
		TEXT("Possessed %s | BT=%s | WeaponClass=%s | MaxEngageRange=%.0f | Sight=%.0f"),
		*GetNameSafe(InPawn),
		*GetNameSafe(CompanionBehaviorTree),
		*WeaponClassName,
		MaxEngageRange,
		SightRadius);
}

void ACompanionAIController::OnUnPossess()
{
	if (BrainComponent)
		BrainComponent->StopLogic(TEXT("Unpossessed"));

	Super::OnUnPossess();
}
