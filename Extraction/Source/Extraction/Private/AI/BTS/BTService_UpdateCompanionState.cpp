// BT service — ticks to update all companion blackboard keys (DBNO, combat target, etc).

#include "BTService_UpdateCompanionState.h"
#include "AI/CompanionDiag.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISense_Sight.h"
#include "CompanionAIController.h" // for LogCompanionAI
#include "CompanionCharacter.h"
#include "WeaponBase.h"
#include "AI/CompanionTuningDataAsset.h"
#include "Companion/CompanionTypes.h"
#include "ExtractionCharacter.h"
#include "HealthComponent.h"
#include "ExtractionTypes.h"
#include "GameplayTagAssetInterface.h"
#include "Kismet/GameplayStatics.h"
#include "AI/Cover/AICoverSlot.h"

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

	// --- Ensure PlayerActor key is set (handles spawn order race) ---
	AExtractionCharacter* Player = Controller->GetPlayerCharacter();
	if (!Player)
	{
		Player = Cast<AExtractionCharacter>(UGameplayStatics::GetPlayerCharacter(Companion->GetWorld(), 0));
		if (Player) Controller->SetPlayerCharacter(Player);
	}

	if (Player)
	{
		BB->SetValueAsObject(PlayerActorKey.SelectedKeyName, Player);
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
			if (bDebugLogging)
				UE_LOG(LogCompanionAI, Log, TEXT("%s: combat target CLEARED (was %s, dead=%d)"),
					*Companion->GetName(), *GetNameSafe(ExistingTarget),
					(int32)(TargetHealth && TargetHealth->IsDead()));
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

	// LoS filter — treat blocked target as no engageable target this tick
	if (BestTarget)
	{
		FCollisionQueryParams LosParams(SCENE_QUERY_STAT(CompanionServiceLoS), true);
		LosParams.AddIgnoredActor(Companion);
		LosParams.AddIgnoredActor(Companion->GetCurrentWeapon());
		FHitResult LosHit;
		const bool bBlocked = Companion->GetWorld()->LineTraceSingleByChannel(
			LosHit, MyLocation, BestTarget->GetActorLocation(), ECC_Visibility, LosParams);

		if (bBlocked && LosHit.GetActor() != BestTarget)
		{
			AAICoverSlot* ActiveSlot = Cast<AAICoverSlot>(BB->GetValueAsObject(ACompanionAIController::BB_CoverSlot));
			const bool bCoverSlotActive = IsValid(ActiveSlot) && ActiveSlot->IsClaimedBy(Companion);

			if (bCoverSlotActive)
			{
				if (bDebugLogging && !bWasLosBlocked)
					UE_LOG(LogCompanionAI, Log, TEXT("%s: combat target LOS blocked but cover slot active — keeping target"),
						*Companion->GetName());
				bWasLosBlocked = true;
				// Do not clear BB — cover is the reason LoS is blocked; target remains valid.
			}
			else
			{
				if (bDebugLogging && !bWasLosBlocked)
					UE_LOG(LogCompanionAI, Log, TEXT("%s: combat target LOS blocked by %s — clearing (was %s)"),
						*Companion->GetName(), *GetNameSafe(LosHit.GetActor()), *GetNameSafe(BestTarget));
				bWasLosBlocked = true;
				BestTarget = nullptr;
				// Explicitly clear BB now — the existing fallthrough only clears when ExistingTarget is also
				// null, which it isn't on the first LoS-block tick. Without this clear, the BB key value
				// stays unchanged and the Combat decorator's LowerPriority abort never fires.
				BB->ClearValue(CombatTargetKey.SelectedKeyName);
				BB->SetValueAsBool(HasCoverPositionKey.SelectedKeyName, false);
				ExistingTarget = nullptr;
			}
		}
		else
		{
			bWasLosBlocked = false;
		}
	}

	if (BestTarget)
	{
		// Only clear cover if target changed
		if (BestTarget != ExistingTarget)
		{
			BB->SetValueAsBool(HasCoverPositionKey.SelectedKeyName, false);
			if (bDebugLogging)
				UE_LOG(LogCompanionAI, Log, TEXT("%s: combat target -> %s (dist=%.0f)"),
					*Companion->GetName(), *BestTarget->GetName(), FMath::Sqrt(BestDistSq));
		}

		BB->SetValueAsObject(CombatTargetKey.SelectedKeyName, BestTarget);

		// Log first-acquisition (null -> valid) for diag.
		if (!PrevCombatTarget.IsValid() && UE_LOG_ACTIVE(LogCompanionDiag, Log))
		{
			FCollisionQueryParams AcqLosParams(SCENE_QUERY_STAT(CompanionDiagAcqLoS), true);
			AcqLosParams.AddIgnoredActor(Companion);
			FHitResult AcqLosHit;
			const bool bAcqBlocked = Companion->GetWorld()->LineTraceSingleByChannel(
				AcqLosHit, Companion->GetActorLocation(), BestTarget->GetActorLocation(), ECC_Visibility, AcqLosParams);
			const bool bAcqLos = !bAcqBlocked || (AcqLosHit.GetActor() == BestTarget);
			UE_LOG(LogCompanionDiag, Log, TEXT("%s: COMBAT-TARGET-ACQUIRED from=null to=%s dist=%.0f los=%d"),
				*Companion->GetName(), *BestTarget->GetName(), FMath::Sqrt(BestDistSq), (int32)bAcqLos);
		}
		PrevCombatTarget = BestTarget;
	}
	else
	{
		PrevCombatTarget.Reset();
		if (ExistingTarget == nullptr)
			BB->ClearValue(CombatTargetKey.SelectedKeyName);
	}

	// --- Posture transitions (server-side; SetPosture gates on HasAuthority) ---
	const ECompanionPosture CurrentPosture = Companion->GetPosture();
	const AActor* TargetAfterUpdate = Cast<AActor>(BB->GetValueAsObject(CombatTargetKey.SelectedKeyName));
	const bool bHasTarget = IsValid(TargetAfterUpdate);

	auto PostureName = [](ECompanionPosture P) -> const TCHAR*
	{
		switch (P)
		{
		case ECompanionPosture::Exploration: return TEXT("Exploration");
		case ECompanionPosture::Combat:      return TEXT("Combat");
		case ECompanionPosture::Stealth:     return TEXT("Stealth");
		default:                             return TEXT("Unknown");
		}
	};

	if (bHasTarget && CurrentPosture != ECompanionPosture::Combat && CurrentPosture != ECompanionPosture::Stealth)
	{
		if (bDebugLogging)
			UE_LOG(LogCompanionAI, Log, TEXT("%s: posture %s -> %s"),
				*Companion->GetName(), PostureName(CurrentPosture), PostureName(ECompanionPosture::Combat));
		Companion->SetPosture(ECompanionPosture::Combat);
		OutOfCombatTimer = 0.f;
	}
	else if (!bHasTarget && CurrentPosture == ECompanionPosture::Combat)
	{
		OutOfCombatTimer += DeltaSeconds;
		if (OutOfCombatTimer >= ExploreReturnDelay)
		{
			if (bDebugLogging)
				UE_LOG(LogCompanionAI, Log, TEXT("%s: posture %s -> %s"),
					*Companion->GetName(), PostureName(CurrentPosture), PostureName(ECompanionPosture::Exploration));
			Companion->SetPosture(ECompanionPosture::Exploration);
			OutOfCombatTimer = 0.f;
		}
	}
	else if (bHasTarget)
	{
		// Target re-acquired (either already in Combat or in Stealth which never auto-transitions).
		OutOfCombatTimer = 0.f;
	}

	// --- Posture-driven scoring weights + posture mirror to BB ---
	const ECompanionPosture SettledPosture = Companion->GetPosture();
	BB->SetValueAsEnum(ACompanionAIController::BB_Posture, static_cast<uint8>(SettledPosture));

	if (const UCompanionTuningDataAsset* Tuning = Controller->GetTuning())
	{
		if (const FCompanionPostureProfile* Profile = Tuning->PostureProfiles.Find(SettledPosture))
		{
			BB->SetValueAsFloat(ACompanionAIController::BB_ScoringWeight_LoSPlayer, Profile->ScoringWeight_LoSPlayer);
			BB->SetValueAsFloat(ACompanionAIController::BB_ScoringWeight_AvoidEnemy, Profile->ScoringWeight_AvoidEnemy);
			BB->SetValueAsFloat(ACompanionAIController::BB_ScoringWeight_CoverFromTarget, Profile->ScoringWeight_CoverFromTarget);
		}
	}
}

FString UBTService_UpdateCompanionState::GetStaticDescription() const
{
	return TEXT("Updates companion BB: player DBNO, combat target from perception");
}
