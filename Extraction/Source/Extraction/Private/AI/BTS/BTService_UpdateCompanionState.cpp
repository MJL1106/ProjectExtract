// BT service — ticks to update all companion blackboard keys (DBNO, combat target, etc).

#include "BTService_UpdateCompanionState.h"
#include "AI/AITargetingStatics.h"
#include "AI/CompanionDiag.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISense_Sight.h"
#include "CompanionAIController.h" // for LogCompanionAI
#include "CompanionCharacter.h"
#include "WeaponBase.h"
#include "AI/CompanionTuningDataAsset.h"
#include "Companion/CompanionTypes.h"
#include "Character/ExtractionPlayerInterface.h"
#include "HealthComponent.h"
#include "ExtractionTypes.h"
#include "EnemyCharacter.h"
#include "GameplayTagAssetInterface.h"
#include "Kismet/GameplayStatics.h"
#include "AI/Cover/AICoverSlot.h"
#include "Engine/OverlapResult.h" // FOverlapResult full definition for the proximity overlap scan

UBTService_UpdateCompanionState::UBTService_UpdateCompanionState()
{
	NodeName = TEXT("Update Companion State");
	Interval = 0.25f;
	RandomDeviation = 0.05f;
	bCreateNodeInstance = true;
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
	APawn* PlayerPawn = Controller->GetPlayerCharacter();
	if (!PlayerPawn)
	{
		PlayerPawn = Cast<APawn>(UGameplayStatics::GetPlayerCharacter(Companion->GetWorld(), 0));
		if (PlayerPawn) Controller->SetPlayerCharacter(PlayerPawn);
	}

	IExtractionPlayerInterface* PlayerIface = Cast<IExtractionPlayerInterface>(PlayerPawn);
	if (PlayerPawn && PlayerIface)
	{
		BB->SetValueAsObject(PlayerActorKey.SelectedKeyName, PlayerPawn);
		BB->SetValueAsBool(PlayerNeedsReviveKey.SelectedKeyName, PlayerIface->GetIsDBNO());
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
				UE_LOG(LogCompanionAI, Log, TEXT("%s: combat target CLEARED (was %s, dead=%d) — cover slot retained for re-score"),
					*Companion->GetName(), *GetNameSafe(ExistingTarget),
					(int32)(TargetHealth && TargetHealth->IsDead()));
			BB->ClearValue(CombatTargetKey.SelectedKeyName);
			// Do NOT clear HasCoverPosition here: if the companion holds a claimed slot, the
			// CoverSwitchMonitor will re-score it against the new target this service tick.
			// Clearing cover on every target death caused a drop into open move-shoot when
			// additional enemies were still present. Cover is cleared only when the last enemy
			// is gone (see no-target branch below) or when LoS-grace expires with no slot.
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

		// Don't engage enemies that haven't detected the player (stealth preservation).
		// Non-AEnemyCharacter actors with the enemy tag keep current behavior (treat as engageable).
		if (const AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Actor))
			if (!Enemy->HasDetectedPlayer()) continue;

		const float DistSq = FVector::DistSquared(MyLocation, Actor->GetActorLocation());

		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			BestTarget = Actor;
		}
	}

	// --- Proximity 360° awareness: detect enemies in any direction at close range ---
	// Supplements the sight cone (180° forward). Runs at the same 0.25s service cadence; one
	// small-radius sphere overlap per tick is negligible cost.
	{
		const UCompanionTuningDataAsset* ProxTuning = Controller->GetTuning();
		const float ProxRadius = ProxTuning ? ProxTuning->ProximityAwarenessRadius : 700.f;
		if (ProxRadius > 0.f)
		{
			TArray<FOverlapResult> ProxOverlaps;
			ProxOverlaps.Reserve(8);
			FCollisionObjectQueryParams ObjParams(ECC_Pawn);
			FCollisionQueryParams ProxParams(SCENE_QUERY_STAT(CompanionProximityAwareness), false);
			ProxParams.AddIgnoredActor(Companion);
			ProxParams.AddIgnoredActor(Companion->GetCurrentWeapon());

			Companion->GetWorld()->OverlapMultiByObjectType(
				ProxOverlaps, MyLocation, FQuat::Identity,
				ObjParams, FCollisionShape::MakeSphere(ProxRadius), ProxParams);

			// Fix C: skip enemies already evaluated by the sight pass — no point re-tracing them.
			TSet<AActor*> PerceivedSet;
			PerceivedSet.Reserve(PerceivedActors.Num());
			for (AActor* A : PerceivedActors)
				PerceivedSet.Add(A);

			// Fix B: match the combat task's LoS ignore list (self + weapon + attached actors).
			TArray<AActor*, TInlineAllocator<4>> ProxIgnoredAttached;
			Companion->ForEachAttachedActors([&](AActor* A) { ProxIgnoredAttached.Add(A); return true; });

			const FVector ProxAimOrigin = Companion->GetPawnViewLocation();
			for (const FOverlapResult& Overlap : ProxOverlaps)
			{
				AActor* ProxActor = Overlap.GetActor();
				if (!IsValid(ProxActor) || ProxActor == Companion) continue;
				// Fix C: already a sight-pass candidate — skip redundant LoS trace.
				if (PerceivedSet.Contains(ProxActor)) continue;

				const IGameplayTagAssetInterface* ProxTagIface = Cast<IGameplayTagAssetInterface>(ProxActor);
				if (!ProxTagIface) continue;
				FGameplayTagContainer ProxTags;
				ProxTagIface->GetOwnedGameplayTags(ProxTags);
				if (!ProxTags.HasTag(TAG_Character_Enemy)) continue;

				UHealthComponent* ProxHealth = ProxActor->FindComponentByClass<UHealthComponent>();
				if (ProxHealth && ProxHealth->IsDead()) continue;

				// Don't engage enemies that haven't detected the player (stealth preservation).
				if (const AEnemyCharacter* ProxEnemy = Cast<AEnemyCharacter>(ProxActor))
					if (!ProxEnemy->HasDetectedPlayer()) continue;

				// LoS check from eye height — must be unobstructed or hit the candidate directly.
				// Fix B: ignore list mirrors the combat task (self + weapon + attached actors).
				FHitResult ProxLosHit;
				FCollisionQueryParams ProxLosParams(SCENE_QUERY_STAT(CompanionProximityLoS), true);
				ProxLosParams.AddIgnoredActor(Companion);
				ProxLosParams.AddIgnoredActor(Companion->GetCurrentWeapon());
				for (AActor* Attached : ProxIgnoredAttached)
					ProxLosParams.AddIgnoredActor(Attached);
				const bool bProxBlocked = Companion->GetWorld()->LineTraceSingleByChannel(
					ProxLosHit, ProxAimOrigin, AITargeting::GetSightLocation(ProxActor), ECC_Visibility, ProxLosParams);
				if (bProxBlocked && ProxLosHit.GetActor() != ProxActor) continue;

				const float ProxDistSq = FVector::DistSquared(MyLocation, ProxActor->GetActorLocation());
				if (ProxDistSq < BestDistSq)
				{
					BestDistSq = ProxDistSq;
					BestTarget = ProxActor;
				}
			}
		}
	}

	// LoS filter — treat blocked target as no engageable target this tick
	if (BestTarget)
	{
		FCollisionQueryParams LosParams(SCENE_QUERY_STAT(CompanionServiceLoS), true);
		LosParams.AddIgnoredActor(Companion);
		LosParams.AddIgnoredActor(Companion->GetCurrentWeapon());
		FHitResult LosHit;
		// Spot from the eyeline (GetPawnViewLocation ~= head height), not the actor centre — keeps acquisition
		// consistent with the move-shoot fire gate so the companion doesn't acquire low but fail to fire.
		const FVector AimOrigin = Companion->GetPawnViewLocation();
		const bool bBlocked = Companion->GetWorld()->LineTraceSingleByChannel(
			LosHit, AimOrigin, AITargeting::GetSightLocation(BestTarget), ECC_Visibility, LosParams);

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
				OpenLosBlockedTime = 0.f;
				// Do not clear BB — cover is the reason LoS is blocked; target remains valid.
			}
			else
			{
				// Debounce the no-cover clear: brief occlusion (a static mesh between us and the enemy)
				// should drive a sidestep in the combat task, not a target drop + re-acquire thrash.
				// Keep the target set through the grace window so the task keeps engaging (repositioning
				// to regain LoS). Only a sustained block past the grace clears.
				OpenLosBlockedTime += DeltaSeconds;
				if (OpenLosBlockedTime < CombatTargetLosGraceSeconds)
				{
					if (bDebugLogging && !bWasLosBlocked)
						UE_LOG(LogCompanionAI, Log, TEXT("%s: combat target LOS blocked by %s — within grace, keeping target (was %s)"),
							*Companion->GetName(), *GetNameSafe(LosHit.GetActor()), *GetNameSafe(BestTarget));
					bWasLosBlocked = true;
				}
				else
				{
					if (bDebugLogging)
						UE_LOG(LogCompanionAI, Log, TEXT("%s: combat target LOS blocked by %s — grace expired, clearing (was %s)"),
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
		}
		else
		{
			bWasLosBlocked = false;
			OpenLosBlockedTime = 0.f;
		}
	}

	if (BestTarget)
	{
		// Target-change no longer clears HasCoverPosition: CoverSwitchMonitor already re-scores against
		// the current combat target every re-eval, so this clear was redundant — and it aborted in-progress
		// switch moves (MoveToCover InProgress -> AbortTask released the freshly-claimed slot -> snap-back).
		if (BestTarget != ExistingTarget && bDebugLogging)
			UE_LOG(LogCompanionAI, Log, TEXT("%s: combat target -> %s (dist=%.0f)"),
				*Companion->GetName(), *BestTarget->GetName(), FMath::Sqrt(BestDistSq));

		BB->SetValueAsObject(CombatTargetKey.SelectedKeyName, BestTarget);

		// Log first-acquisition (null -> valid) for diag.
		if (!PrevCombatTarget.IsValid() && UE_LOG_ACTIVE(LogCompanionDiag, Log))
		{
			FCollisionQueryParams AcqLosParams(SCENE_QUERY_STAT(CompanionDiagAcqLoS), true);
			AcqLosParams.AddIgnoredActor(Companion);
			FHitResult AcqLosHit;
			const bool bAcqBlocked = Companion->GetWorld()->LineTraceSingleByChannel(
				AcqLosHit, Companion->GetPawnViewLocation(), AITargeting::GetSightLocation(BestTarget), ECC_Visibility, AcqLosParams);
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
		{
			BB->ClearValue(CombatTargetKey.SelectedKeyName);
			// No enemies at all — combat is ending. Release cover so the companion stands up and
			// the cover slot becomes available for the next engagement. Cover is cleared only when
			// no target remains (combat ending); a live-but-temporarily-unperceived target retains
			// both the BB target and the cover slot so the CoverSwitchMonitor stays active.
			BB->SetValueAsBool(HasCoverPositionKey.SelectedKeyName, false);
		}
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
