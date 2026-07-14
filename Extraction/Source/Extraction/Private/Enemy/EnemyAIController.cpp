// AI controller for enemy characters — perception, team 1, blackboard, behaviour tree.

#include "EnemyAIController.h"
#include "EnemyCharacter.h"
#include "EnemyAwarenessComponent.h"
#include "EnemyMoraleComponent.h"
#include "EnemyArchetypeData.h"
#include "PatrolRoute.h"
#include "HealthComponent.h"
#include "AICoverSlot.h"
#include "GameFramework/Character.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISenseConfig_Sight.h"
#include "Perception/AISenseConfig_Hearing.h"
#include "AITypes.h"
#include "Navigation/PathFollowingComponent.h"
#include "ExtractionTypes.h"
#include "TimerManager.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY(LogEnemyAI);

const FName AEnemyAIController::BB_CombatTarget(TEXT("CombatTarget"));
const FName AEnemyAIController::BB_LastKnownLocation(TEXT("LastKnownLocation"));
const FName AEnemyAIController::BB_InvestigateLocation(TEXT("InvestigateLocation"));
const FName AEnemyAIController::BB_AwarenessState(TEXT("AwarenessState"));
const FName AEnemyAIController::BB_HasLineOfSight(TEXT("HasLineOfSight"));
const FName AEnemyAIController::BB_TargetInRange(TEXT("TargetInRange"));
const FName AEnemyAIController::BB_CoverSlot(TEXT("CoverSlot"));
const FName AEnemyAIController::BB_HasCover(TEXT("HasCover"));
const FName AEnemyAIController::BB_PatrolRoute(TEXT("PatrolRoute"));
const FName AEnemyAIController::BB_MoraleState(TEXT("MoraleState"));
const FName AEnemyAIController::BB_ManeuverRole(TEXT("ManeuverRole"));
const FName AEnemyAIController::BB_RusherCharging(TEXT("RusherCharging"));
const FName AEnemyAIController::BB_ManeuverHoldUntil(TEXT("ManeuverHoldUntil"));
const FName AEnemyAIController::BB_PostLocation(TEXT("PostLocation"));

AEnemyAIController::AEnemyAIController()
{
	SetGenericTeamId(FGenericTeamId(1));

	PerceptionComponent = CreateDefaultSubobject<UAIPerceptionComponent>(TEXT("AIPerception"));
	SetPerceptionComponent(*PerceptionComponent);

	SightConfig = CreateDefaultSubobject<UAISenseConfig_Sight>(TEXT("SightConfig"));
	SightConfig->SightRadius = 2500.f;
	SightConfig->LoseSightRadius = 3000.f;
	SightConfig->PeripheralVisionAngleDegrees = 110.f;
	// Disabled: engine auto-success returns Visible without calling CanBeSeenFrom, bypassing the head-safe body-point gate (head-only peeks would leak). Stickiness is owned by UEnemyAwarenessComponent's contact-hold grace, not engine auto-success.
	SightConfig->AutoSuccessRangeFromLastSeenLocation = FAISystem::InvalidRange;
	SightConfig->SetMaxAge(5.f);
	SightConfig->DetectionByAffiliation.bDetectEnemies = true;
	// Neutrals are perceivable: corpses drop to NoTeam on death so allies can discover bodies.
	SightConfig->DetectionByAffiliation.bDetectNeutrals = true;
	SightConfig->DetectionByAffiliation.bDetectFriendlies = false;

	HearingConfig = CreateDefaultSubobject<UAISenseConfig_Hearing>(TEXT("HearingConfig"));
	HearingConfig->HearingRange = 2000.f;
	HearingConfig->SetMaxAge(3.f);
	HearingConfig->DetectionByAffiliation.bDetectEnemies = true;
	HearingConfig->DetectionByAffiliation.bDetectNeutrals = true;
	// Friendlies audible for ally-gunfire coordination (UEnemyAwarenessComponent::HandleAllyGunfireHeard);
	// sight stays enemies+neutrals only.
	HearingConfig->DetectionByAffiliation.bDetectFriendlies = true;

	PerceptionComponent->ConfigureSense(*SightConfig);
	PerceptionComponent->ConfigureSense(*HearingConfig);
	PerceptionComponent->SetDominantSense(UAISense_Sight::StaticClass());

	AwarenessComponent = CreateDefaultSubobject<UEnemyAwarenessComponent>(TEXT("AwarenessComponent"));
}

void AEnemyAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(InPawn);
	if (!IsValid(Enemy))
	{
		UE_LOG(LogEnemyAI, Error, TEXT("%s: possessed pawn is not AEnemyCharacter"), *GetName());
		return;
	}

	const UEnemyArchetypeData* DA = Enemy->GetArchetypeData();

	// Disabled: engine auto-success returns Visible without calling CanBeSeenFrom, bypassing the head-safe body-point gate (head-only peeks would leak). Stickiness is owned by UEnemyAwarenessComponent's contact-hold grace, not engine auto-success.
	SightConfig->AutoSuccessRangeFromLastSeenLocation = FAISystem::InvalidRange;

	// Override perception configs from DA if set
	if (IsValid(DA))
	{
		SightConfig->SightRadius                  = DA->SightRadius;
		SightConfig->LoseSightRadius              = DA->LoseSightRadius;
		// PeripheralVisionDeg is authored as a full FOV (the suspicion math halves it); the engine
		// sight config expects a half-angle from forward, so halve it here to match.
		SightConfig->PeripheralVisionAngleDegrees = DA->PeripheralVisionDeg * 0.5f;
		SightConfig->SetMaxAge(DA->SightMaxAge);
		HearingConfig->HearingRange               = DA->HearingRange;
		HearingConfig->SetMaxAge(DA->HearingMaxAge);
		PerceptionComponent->RequestStimuliListenerUpdate();
	}

	Enemy->ApplyArchetypeData();

	if (!EnemyBehaviorTree)
	{
		UE_LOG(LogEnemyAI, Error, TEXT("%s: no BehaviorTree assigned — AI will be inactive"), *GetName());
		return;
	}

	UBlackboardComponent* BBComp = nullptr;
	UseBlackboard(EnemyBehaviorTree->BlackboardAsset, BBComp);
	RunBehaviorTree(EnemyBehaviorTree);

	// Inject archetype combat subtree
	UBehaviorTreeComponent* BTComp = Cast<UBehaviorTreeComponent>(BrainComponent);
	if (IsValid(BTComp))
	{
		if (IsValid(DA) && IsValid(DA->CombatSubtree))
			BTComp->SetDynamicSubtree(TAG_BT_EnemyCombat, DA->CombatSubtree);
		else
			UE_LOG(LogEnemyAI, Warning, TEXT("%s: no CombatSubtree set on ArchetypeData"), *GetName());
	}

	// Write patrol route to BB
	if (IsValid(BBComp) && IsValid(Enemy->PatrolRoute))
		BBComp->SetValueAsObject(BB_PatrolRoute, Enemy->PatrolRoute);

	// Write guard post location to BB (always set — used by patrol task when route is absent)
	if (IsValid(BBComp))
		BBComp->SetValueAsVector(BB_PostLocation, Enemy->GetGuardPostLocation());

	// Wire awareness component
	if (IsValid(AwarenessComponent) && IsValid(BBComp))
	{
		AwarenessComponent->Initialize(BBComp, DA);
		PerceptionComponent->OnTargetPerceptionUpdated.AddUniqueDynamic(
			AwarenessComponent, &UEnemyAwarenessComponent::OnTargetPerceptionUpdated);
		AwarenessComponent->OnAwarenessStateChanged.AddUniqueDynamic(
			this, &AEnemyAIController::HandleAwarenessStateChanged);
	}

	// Phase 4: subscribe to morale state changes → write BB key
	if (UEnemyMoraleComponent* Morale = Enemy->GetMoraleComponent())
	{
		Morale->OnMoraleStateChanged.AddUniqueDynamic(this, &AEnemyAIController::HandleMoraleStateChanged);

		if (IsValid(BBComp))
			BBComp->SetValueAsEnum(BB_MoraleState, static_cast<uint8>(EMoraleState::Confident));
	}

	// Bind pawn death for controller teardown
	if (IsValid(Enemy->GetHealthComponent()))
		Enemy->GetHealthComponent()->OnDeath.AddUniqueDynamic(this, &AEnemyAIController::HandlePawnDeath);

	UE_LOG(LogEnemyAI, Verbose, TEXT("[POSSESS] %s -> %s | BT=%s | DA=%s | HasRoute=%d | BBValid=%d"),
		*GetName(), *Enemy->GetName(), *GetNameSafe(EnemyBehaviorTree), *GetNameSafe(DA),
		Enemy->PatrolRoute != nullptr ? 1 : 0, IsValid(BBComp) ? 1 : 0);
}

void AEnemyAIController::OnUnPossess()
{
	ReleaseCoverSlotIfClaimed();

	// Phase 4: unbind morale delegate before releasing the pawn
	if (APawn* PreviousPawn = GetPawn())
	{
		if (AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(PreviousPawn))
		{
			if (UEnemyMoraleComponent* Morale = Enemy->GetMoraleComponent())
				Morale->OnMoraleStateChanged.RemoveDynamic(this, &AEnemyAIController::HandleMoraleStateChanged);
		}
	}

	if (BrainComponent)
		BrainComponent->StopLogic(TEXT("Unpossessed"));

	if (UWorld* World = GetWorld())
		World->GetTimerManager().ClearAllTimersForObject(this);

	Super::OnUnPossess();
}

void AEnemyAIController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ReleaseCoverSlotIfClaimed();

	if (UWorld* World = GetWorld())
		World->GetTimerManager().ClearAllTimersForObject(this);

	Super::EndPlay(EndPlayReason);
}

void AEnemyAIController::HandlePawnDeath()
{
	if (BrainComponent)
		BrainComponent->StopLogic(TEXT("Dead"));

	ClearFocus(EAIFocusPriority::Gameplay);

	if (IsValid(AwarenessComponent))
		AwarenessComponent->HandlePawnDeath();

	if (IsValid(PerceptionComponent))
		PerceptionComponent->Deactivate();

	ReleaseCoverSlotIfClaimed();
	SetManeuverRole(EEnemyManeuverRole::None);

	// The corpse persists for body discovery but needs no controller — destroy self to avoid
	// orphaned controllers (and their perception cost) accumulating across a long session.
	UnPossess();
	Destroy();
}

void AEnemyAIController::HandleAwarenessStateChanged(EEnemyAwarenessState OldState, EEnemyAwarenessState NewState)
{
	if (OldState != EEnemyAwarenessState::Combat) return;

	// Leaving Combat — clear aim focus and cover so the BT can re-evaluate
	ClearFocus(EAIFocusPriority::Gameplay);
	ReleaseCoverSlotIfClaimed();

	// Un-crouch the pawn if it was crouching in cover
	if (APawn* ControlledPawn = GetPawn())
		if (ACharacter* Char = Cast<ACharacter>(ControlledPawn)) Char->UnCrouch();
}

void AEnemyAIController::HandleMoraleStateChanged(EMoraleState OldState, EMoraleState NewState)
{
	UBlackboardComponent* BB = GetBlackboardComponent();
	if (!BB) return;

	BB->SetValueAsEnum(BB_MoraleState, static_cast<uint8>(NewState));
	UE_LOG(LogEnemyAI, Log, TEXT("%s: morale %d -> %d"), *GetName(), static_cast<int32>(OldState), static_cast<int32>(NewState));
}

void AEnemyAIController::SetManeuverRole(EEnemyManeuverRole NewRole)
{
	UBlackboardComponent* BB = GetBlackboardComponent();
	if (!BB) return;

	BB->SetValueAsEnum(BB_ManeuverRole, static_cast<uint8>(NewRole));
}

FPathFollowingRequestResult AEnemyAIController::MoveTo(const FAIMoveRequest& MoveRequest, FNavPathSharedPtr* OutPath)
{
	if (const AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(GetPawn()))
	{
		if (Enemy->bDebugStandAndShoot)
		{
			// Debug stand-and-shoot: refuse all locomotion. Report AlreadyAtGoal (NOT Failed) so any
			// BT move task that finishes on the result treats the move as satisfied and falls through
			// to its fire logic instead of aborting the combat branch. Tasks that ignore the result
			// (EnemyMoveToCover / EnemyCombatFire) see PathFollowing stay Idle and handle it as normal.
			FPathFollowingRequestResult Result;
			Result.Code = EPathFollowingRequestResult::AlreadyAtGoal;
			return Result;
		}
	}
	return Super::MoveTo(MoveRequest, OutPath);
}

void AEnemyAIController::ReleaseCoverSlotIfClaimed()
{
	UBlackboardComponent* BB = GetBlackboardComponent();
	if (!BB) return;

	APawn* ControlledPawn = GetPawn();
	AAICoverSlot* Slot = Cast<AAICoverSlot>(BB->GetValueAsObject(BB_CoverSlot));
	if (IsValid(Slot) && IsValid(ControlledPawn) && Slot->IsClaimedBy(ControlledPawn))
	{
		Slot->Release(ControlledPawn);
		UE_LOG(LogEnemyAI, Log, TEXT("%s: released cover slot %s on teardown"), *GetName(), *Slot->GetName());
	}

	BB->SetValueAsObject(BB_CoverSlot, nullptr);
	BB->SetValueAsBool(BB_HasCover, false);
}
