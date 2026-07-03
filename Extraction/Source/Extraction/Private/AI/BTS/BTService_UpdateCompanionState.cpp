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
#include "AI/BlackboardKeyType_Cover.h" // new-system Cover-typed BB key read (replaces AAICoverSlot)
#include "CoverSystem.h"                // FCover / FCoverData for the cover-active LoS-block branch
#include "CoverReservationSubsystem.h"  // intended-cover check for the approach-window cover-active branch (Fix 4)
#include "Engine/OverlapResult.h" // FOverlapResult full definition for the proximity overlap scan

UBTService_UpdateCompanionState::UBTService_UpdateCompanionState()
{
	NodeName = TEXT("Update Companion State");
	Interval = 0.25f;
	RandomDeviation = 0.05f;
	bCreateNodeInstance = true;

	// Default the new-system cover key to the shared "CoverTarget" BB entry so no BT asset edit is
	// needed; the Cover type filter mirrors BTTask_CompanionCombat so the selector resolves correctly.
	CoverTargetKey.SelectedKeyName = TEXT("CoverTarget");
	if (GIsEditor && !HasAnyFlags(RF_ClassDefaultObject))
	{
		CoverTargetKey.AllowedTypes.Add(NewObject<UBlackboardKeyType_Cover>(this, TEXT("CoverTargetKey_Cover")));
	}
}

void UBTService_UpdateCompanionState::InitializeFromAsset(UBehaviorTree& Asset)
{
	Super::InitializeFromAsset(Asset);

	if (UBlackboardData* BBAsset = GetBlackboardAsset())
	{
		CoverTargetKey.ResolveSelectedKey(*BBAsset);
	}
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

	// --- Range thresholds for acquisition/retention ---
	const UCompanionTuningDataAsset* RangeTuning = Controller->GetTuning();
	const float MaxEngageRange = Companion->MaxEngageRange;
	const float RetentionMult = RangeTuning ? RangeTuning->EngageRangeRetentionMultiplier : 1.15f;
	const float AcquireRangeSq = MaxEngageRange * MaxEngageRange;
	const float RetainRangeSq = FMath::Square(MaxEngageRange * RetentionMult);

	// --- Update CombatTarget ---
	UAIPerceptionComponent* Perception = Controller->GetPerceptionComponent();
	if (!Perception) return;

	// Validate existing target first
	AActor* ExistingTarget = Cast<AActor>(BB->GetValueAsObject(CombatTargetKey.SelectedKeyName));
	if (ExistingTarget)
	{
		if (!IsValid(ExistingTarget))
		{
			if (bDebugLogging)
				UE_LOG(LogCompanionAI, Log, TEXT("%s: combat target CLEARED (was %s, reason=pending-kill) — cover slot retained for re-score"),
					*Companion->GetName(), *GetNameSafe(ExistingTarget));
			BB->ClearValue(CombatTargetKey.SelectedKeyName);
			ExistingTarget = nullptr;
		}
		else
		{
			UHealthComponent* TargetHealth = ExistingTarget->FindComponentByClass<UHealthComponent>();
			const float ExistingDistSq = FVector::DistSquared(Companion->GetActorLocation(), ExistingTarget->GetActorLocation());
			if ((TargetHealth && TargetHealth->IsDead()) || ExistingDistSq > RetainRangeSq)
			{
				if (bDebugLogging)
					UE_LOG(LogCompanionAI, Log, TEXT("%s: combat target CLEARED (was %s, dead=%d, dist=%.0f, retainMax=%.0f) — cover slot retained for re-score"),
						*Companion->GetName(), *GetNameSafe(ExistingTarget),
						(int32)(TargetHealth && TargetHealth->IsDead()),
						FMath::Sqrt(ExistingDistSq), FMath::Sqrt(RetainRangeSq));
				BB->ClearValue(CombatTargetKey.SelectedKeyName);
				// Do NOT clear HasCoverPosition here: if the companion holds a claimed slot, the
				// CoverSwitchMonitor will re-score it against the new target this service tick.
				// Clearing cover on every target death caused a drop into open move-shoot when
				// additional enemies were still present. Cover is cleared only when the last enemy
				// is gone (see no-target branch below) or when LoS-grace expires with no slot.
				ExistingTarget = nullptr;
			}
		}
	}

	// Find best target from perceived actors
	TArray<AActor*> PerceivedActors;
	Perception->GetCurrentlyPerceivedActors(UAISense_Sight::StaticClass(), PerceivedActors);

	// Two-tier selection: BestVisible = nearest candidate with a clear companion eye-line; BestAny =
	// nearest candidate regardless of LoS. Choosing BestVisible when one exists stops the companion
	// fixating on a wall-occluded nearest enemy while a visible farther one shoots it. BestAny is the
	// fallback that flows through the cover-keep/grace logic unchanged.
	AActor* BestVisible = nullptr;
	float BestVisibleDistSq = MAX_FLT;
	AActor* BestAny = nullptr;
	float BestAnyDistSq = MAX_FLT;
	const FVector MyLocation = Companion->GetActorLocation();
	const FVector SelectAimOrigin = Companion->GetPawnViewLocation();
	bool bHasAlertedThreat = false;
	FVector AlertedThreatLocation = FVector::ZeroVector;
	float BestAlertDistSq = MAX_FLT;

	// Shared ignore list for per-candidate eye-line traces (self + weapon + attached actors) — matches
	// the final LoS filter and the combat task's trace so acquisition and firing agree on visibility.
	TArray<AActor*, TInlineAllocator<4>> SelectIgnoredAttached;
	Companion->ForEachAttachedActors([&](AActor* A) { SelectIgnoredAttached.Add(A); return true; });

	// Considers one candidate for both tiers. Runs the eye-line trace only when the candidate could
	// improve one of the tiers (nearer than the current BestAny, or nearer than the current BestVisible),
	// so no trace is wasted on a candidate that can't win either slot.
	// bAllowOccludedAny: when true (sight + proximity channels), an untraced or trace-blocked candidate
	// can enter BestAny as a fallback. When false (player-threat channel), candidates may only be acquired
	// through the traced BestVisible path — prevents structurally-occluded enemies (e.g. one floor below
	// within the threat sphere) from entering via the BestAny fallback and being held by pressure-keep.
	auto ConsiderCandidate = [&](AActor* Candidate, float DistSq, bool bAllowOccludedAny = true)
	{
		if (!IsValid(Candidate)) return;
		if (DistSq > AcquireRangeSq) return;

		if (bAllowOccludedAny && DistSq < BestAnyDistSq)
		{
			BestAnyDistSq = DistSq;
			BestAny = Candidate;
		}

		// Only trace if this candidate could become the nearest visible one.
		if (DistSq >= BestVisibleDistSq) return;

		FHitResult SelHit;
		FCollisionQueryParams SelParams(SCENE_QUERY_STAT(CompanionSelectLoS), true);
		SelParams.AddIgnoredActor(Companion);
		SelParams.AddIgnoredActor(Companion->GetCurrentWeapon());
		for (AActor* Attached : SelectIgnoredAttached)
			SelParams.AddIgnoredActor(Attached);
		const bool bSelBlocked = Companion->GetWorld()->LineTraceSingleByChannel(
			SelHit, SelectAimOrigin, AITargeting::GetSightLocation(Candidate), ECC_Visibility, SelParams);
		if (bSelBlocked && SelHit.GetActor() != Candidate) return;

		BestVisibleDistSq = DistSq;
		BestVisible = Candidate;
	};

	auto NoteAlertedThreat = [&](const AEnemyCharacter* Enemy, float DistSq)
	{
		if (!IsValid(Enemy) || !Enemy->IsAlertedForCompanionReadiness()) return;
		if (DistSq >= BestAlertDistSq) return;

		BestAlertDistSq = DistSq;
		AlertedThreatLocation = Enemy->GetActorLocation();
		bHasAlertedThreat = true;
	};

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

		// Don't engage enemies that haven't detected the player (stealth preservation).
		// Non-AEnemyCharacter actors with the enemy tag keep current behavior (treat as engageable).
		if (const AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Actor))
		{
			NoteAlertedThreat(Enemy, DistSq);
			if (!Enemy->HasDetectedPlayer()) continue;
		}

		ConsiderCandidate(Actor, DistSq);
	}

	// --- Proximity 360° awareness: detect enemies in any direction at close range ---
	// Supplements the sight cone (180° forward). Runs at the same 0.25s service cadence; one
	// small-radius sphere overlap per tick is negligible cost.
	{
		const float ProxRadius = RangeTuning ? RangeTuning->ProximityAwarenessRadius : 700.f;
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

				const float ProxDistSq = FVector::DistSquared(MyLocation, ProxActor->GetActorLocation());

				// Don't engage enemies that haven't detected the player (stealth preservation).
				if (const AEnemyCharacter* ProxEnemy = Cast<AEnemyCharacter>(ProxActor))
				{
					NoteAlertedThreat(ProxEnemy, ProxDistSq);
					if (!ProxEnemy->HasDetectedPlayer()) continue;
				}

				// bAllowOccludedAny=false: a pure-overlap channel has no perception MaxAge to expire, so an
				// occluded pick (enemy through a floor slab / unbreached door) would re-add every tick and
				// latch. Proximity candidates must pass the eye-line trace (BestVisible) to be acquired.
				ConsiderCandidate(ProxActor, ProxDistSq, /*bAllowOccludedAny=*/ false);
			}
		}
	}

	// Player-centered threat awareness: ready on enemies pressuring the player, but only target them with companion LoS.
	{
		const float PlayerThreatRadius = RangeTuning ? RangeTuning->PlayerThreatAwarenessRadius : 3500.f;
		if (IsValid(PlayerPawn) && PlayerThreatRadius > 0.f)
		{
			TArray<FOverlapResult> ThreatOverlaps;
			ThreatOverlaps.Reserve(16);
			FCollisionObjectQueryParams ObjParams(ECC_Pawn);
			FCollisionQueryParams ThreatParams(SCENE_QUERY_STAT(CompanionPlayerThreatAwareness), false);
			ThreatParams.AddIgnoredActor(Companion);
			ThreatParams.AddIgnoredActor(Companion->GetCurrentWeapon());
			ThreatParams.AddIgnoredActor(PlayerPawn);

			Companion->GetWorld()->OverlapMultiByObjectType(
				ThreatOverlaps, PlayerPawn->GetActorLocation(), FQuat::Identity,
				ObjParams, FCollisionShape::MakeSphere(PlayerThreatRadius), ThreatParams);

			for (const FOverlapResult& Overlap : ThreatOverlaps)
			{
				AActor* ThreatActor = Overlap.GetActor();
				const AEnemyCharacter* ThreatEnemy = Cast<AEnemyCharacter>(ThreatActor);
				if (!IsValid(ThreatEnemy)) continue;

				const UHealthComponent* ThreatHealth = ThreatEnemy->GetHealthComponent();
				if (ThreatHealth && ThreatHealth->IsDead()) continue;

				const float PlayerDistSq = FVector::DistSquared(PlayerPawn->GetActorLocation(), ThreatActor->GetActorLocation());
				NoteAlertedThreat(ThreatEnemy, PlayerDistSq);
				if (!ThreatEnemy->HasDetectedPlayer()) continue;

				// Selection keyed on companion distance + companion eye-line (ConsiderCandidate) —
				// bAllowOccludedAny=false so a player-threat candidate can only enter BestVisible
				// (requires passing the eye-line trace), never the untraced BestAny fallback.
				const float CompanionDistSq = FVector::DistSquared(MyLocation, ThreatActor->GetActorLocation());
				ConsiderCandidate(ThreatActor, CompanionDistSq, /*bAllowOccludedAny=*/ false);
			}
		}
	}

	// Resolve the two-tier pick: prefer the nearest VISIBLE candidate. The visible-first swap only ever
	// happens when the new pick is genuinely visible — that's the point of BestVisible.
	//
	// Fix 1 (target identity while hunkered): when nothing is visible (BestVisible null — every enemy
	// occluded from the crouched view), do NOT blindly adopt the nearest occluded BestAny. If we already
	// hold a valid, live combat target, KEEP it (identity-preserving) and let it flow through the
	// cover-keep/grace branch below. Adopting BestAny here replaced a peeking-visible E2 with a
	// wall-occluded nearest E1 every hunker tick, then swapped back on the next peek window — the target
	// oscillated at peek cadence, dragging aim / focus / arc gates / the switch monitor with it.
	// Only fall back to BestAny when there is no valid existing target to preserve.
	AActor* BestTarget;
	float BestDistSq;
	if (BestVisible)
	{
		BestTarget = BestVisible;
		BestDistSq = BestVisibleDistSq;
	}
	else if (IsValid(ExistingTarget))
	{
		// Keep the current target's identity through the occluded window (cover-keep/grace handles it).
		BestTarget = ExistingTarget;
		BestDistSq = FVector::DistSquared(MyLocation, ExistingTarget->GetActorLocation());
	}
	else
	{
		BestTarget = BestAny;
		BestDistSq = BestAnyDistSq;
	}

	// Fix 4b: a fresh target identity gets a fresh grace window. Without this reset, an occluded new pick
	// inherits OpenLosBlockedTime accrued against the previous target and can be cleared instantly (or far
	// too early) on its first blocked tick. PrevCombatTarget holds last tick's committed pick.
	if (BestTarget && PrevCombatTarget.Get() != BestTarget)
	{
		OpenLosBlockedTime = 0.f;
		bWasLosBlocked = false;
	}

	// Final LoS filter for the selected combat target.
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
			// New-system cover-active check: on the AICS migration BB_CoverSlot is always null, so the old
			// AAICoverSlot read made this branch never taken — the moment the companion hunkered, the
			// grace expired and the target was cleared → clear/re-acquire loop. Read the Cover-typed key
			// (resolved by name "CoverTarget" in InitializeFromAsset) + the HasCoverPosition bool instead:
			// a valid claimed cover point means our own wall is the blocker, so keep the target.
			//
			// Fix 4a (approach-window): HasCoverPosition is only set on ARRIVAL, so during the walk to cover
			// an occluded final approach lasting longer than the 3s grace would clear the target and abort the
			// move. Treat cover-active as: a valid CoverTarget key AND (HasCoverPosition OR this controller has
			// an intended cover stamped in the reservation subsystem — i.e. it is en route to a claimed point).
			bool bCoverSlotActive = false;
			if (CoverTargetKey.SelectedKeyType == nullptr && !bLoggedCoverKeyResolveFail)
			{
				bLoggedCoverKeyResolveFail = true;
				UE_LOG(LogCompanionAI, Warning,
					TEXT("%s: CoverTarget BB key '%s' failed to resolve — cover-active LoS-keep is disabled, target will clear on grace expiry"),
					*Companion->GetName(), *CoverTargetKey.SelectedKeyName.ToString());
			}
			if (CoverTargetKey.SelectedKeyType != nullptr)
			{
				const FCover ActiveCover = BB->GetValue<UBlackboardKeyType_Cover>(CoverTargetKey.GetSelectedKeyID());
				const bool bHasCoverPos = BB->GetValueAsBool(HasCoverPositionKey.SelectedKeyName);
				bool bHasIntendedCover = false;
				if (UWorld* CoverWorld = Companion->GetWorld())
				{
					if (UCoverReservationSubsystem* ResSub = CoverWorld->GetSubsystem<UCoverReservationSubsystem>())
						bHasIntendedCover = ResSub->HasIntendedCover(Controller);
				}
				bCoverSlotActive = ActiveCover.IsValid() && (bHasCoverPos || bHasIntendedCover);
			}

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
				// Player-pressure keep: if the enemy is actively threatening the player, hold
				// the target through LoS blocks so the combat task's reposition hunts for an angle.
				bool bPlayerPressureKeep = false;
				const bool bRetainLosBlock = RangeTuning ? RangeTuning->bRetainPlayerThreatTargetsWhileLosBlocked : true;
				if (bRetainLosBlock && IsValid(PlayerPawn))
				{
					const float PlayerThreatRadius = RangeTuning ? RangeTuning->PlayerThreatAwarenessRadius : 3500.f;
					if (const AEnemyCharacter* BlockedEnemy = Cast<AEnemyCharacter>(BestTarget))
					{
						bPlayerPressureKeep = BlockedEnemy->HasDetectedPlayer()
							&& FVector::DistSquared(PlayerPawn->GetActorLocation(), BlockedEnemy->GetActorLocation()) <= FMath::Square(PlayerThreatRadius);
					}
				}

				if (bPlayerPressureKeep)
				{
					if (bDebugLogging && !bWasLosBlocked)
						UE_LOG(LogCompanionAI, Log, TEXT("%s: combat target LOS blocked but enemy pressuring player — keeping"),
							*Companion->GetName());
					bWasLosBlocked = true;
					OpenLosBlockedTime = 0.f;
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
	const bool bReadyOnlyThreat = bHasAlertedThreat && !bHasTarget;

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

	if (bHasTarget || bReadyOnlyThreat)
	{
		if (CurrentPosture != ECompanionPosture::Combat)
		{
			if (bDebugLogging)
				UE_LOG(LogCompanionAI, Log, TEXT("%s: posture %s -> %s"),
					*Companion->GetName(), PostureName(CurrentPosture), PostureName(ECompanionPosture::Combat));
			Companion->SetPosture(ECompanionPosture::Combat);
		}
		Companion->SetLowReadyAim(false);
		if (bReadyOnlyThreat)
			Controller->SetFocalPoint(AlertedThreatLocation, EAIFocusPriority::Gameplay);
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
			Companion->SetLowReadyAim(true);
			Controller->ClearFocus(EAIFocusPriority::Gameplay);
			OutOfCombatTimer = 0.f;
		}
	}

	// --- Posture-driven scoring weights + posture mirror to BB ---
	const ECompanionPosture SettledPosture = Companion->GetPosture();
	BB->SetValueAsEnum(ACompanionAIController::BB_Posture, static_cast<uint8>(SettledPosture));

	if (RangeTuning)
	{
		if (const FCompanionPostureProfile* Profile = RangeTuning->PostureProfiles.Find(SettledPosture))
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
