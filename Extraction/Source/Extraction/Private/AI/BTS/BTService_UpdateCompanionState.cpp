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
#include "EnemyAIController.h"        // angle-seek focus tally: who is this enemy targeting
#include "EnemyAwarenessComponent.h"  // GetCombatTarget for the focus tally
#include "GameplayTagAssetInterface.h"
#include "Kismet/GameplayStatics.h"
#include "AI/BlackboardKeyType_Cover.h" // new-system Cover-typed BB key read (replaces AAICoverSlot)
#include "CoverSystem.h"                // FCover / FCoverData for the cover-active LoS-block branch
#include "CoverReservationSubsystem.h"  // intended-cover check for the approach-window cover-active branch (Fix 4)
#include "Engine/OverlapResult.h" // FOverlapResult full definition for the proximity overlap scan
#include "EnemyDirectorSubsystem.h" // global alert level as an out-of-envelope stealth-break signal
#include "TraversalComponent.h"     // stealth-pin enforcement must not crouch mid-traversal
#include "Navigation/PathFollowingComponent.h" // ready-only threat stance yields facing to an active move
#include "HAL/IConsoleManager.h" // companion.AimLog diagnostics CVar

// companion.AimLog 1 — per-service-tick dump of everything that drives the companion's aim/facing
// (target pick + provenance, ready-only threat, takedown/route yields, focal point, low-ready).
// Diagnostic for the stuck-ADS / aims-through-walls reports; Display severity so it shows untagged.
static TAutoConsoleVariable<int32> CVarCompanionAimLog(
	TEXT("companion.AimLog"), 0,
	TEXT("1 = log companion aim/stance state each UpdateCompanionState tick."));

UBTService_UpdateCompanionState::UBTService_UpdateCompanionState()
{
	NodeName = TEXT("Update Companion State");
	Interval = 0.25f;
	RandomDeviation = 0.05f;
	bCreateNodeInstance = true;

	ReviveWindowOpenKey.SelectedKeyName = TEXT("ReviveWindowOpen");

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
		ReviveWindowOpenKey.ResolveSelectedKey(*BBAsset);
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
	const bool bPlayerDBNO = PlayerPawn && PlayerIface && PlayerIface->GetIsDBNO();
	if (PlayerPawn && PlayerIface)
	{
		BB->SetValueAsObject(PlayerActorKey.SelectedKeyName, PlayerPawn);
		BB->SetValueAsBool(PlayerNeedsReviveKey.SelectedKeyName, bPlayerDBNO);
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
	const AEnemyCharacter* AlertedThreatEnemy = nullptr; // ready-only LoS gate + AimLog diagnostics
	float BestAlertDistSq = MAX_FLT;

	// Player-commanded mode gates target acquisition (Normal: alerted-only, Combat: weapons-free,
	// Stealth: suppressed until broken). bAnyEnemyDetectedPlayer feeds the stealth-break check.
	const ECompanionMode Mode = Companion->GetMode();
	bool bAnyEnemyDetectedPlayer = false;

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
	// Defend-the-body bias: when the player is DBNO, enemies closer to the downed player
	// get a scored distance reduction so the companion prioritizes threats near the body.
	const float DBNOBiasThreatRadiusSq = bPlayerDBNO && IsValid(PlayerPawn)
		? FMath::Square(Companion->ReviveThreatRadius) : 0.f;
	const FVector DBNOPlayerLoc = IsValid(PlayerPawn) ? PlayerPawn->GetActorLocation() : FVector::ZeroVector;

	auto ConsiderCandidate = [&](AActor* Candidate, float DistSq, bool bAllowOccludedAny = true)
	{
		if (!IsValid(Candidate)) return;
		if (DistSq > AcquireRangeSq) return;

		// Apply defend-the-body scoring bias while DBNO: enemies within the revive threat
		// radius of the player have their effective distance halved for selection purposes.
		float ScoredDistSq = DistSq;
		if (bPlayerDBNO && DBNOBiasThreatRadiusSq > 0.f)
		{
			const float EnemyToPlayerDistSq = FVector::DistSquared(Candidate->GetActorLocation(), DBNOPlayerLoc);
			if (EnemyToPlayerDistSq <= DBNOBiasThreatRadiusSq)
				ScoredDistSq *= 0.5f;
		}

		if (bAllowOccludedAny && ScoredDistSq < BestAnyDistSq)
		{
			BestAnyDistSq = ScoredDistSq;
			BestAny = Candidate;
		}

		// Only trace if this candidate could become the nearest visible one.
		if (ScoredDistSq >= BestVisibleDistSq) return;

		FHitResult SelHit;
		FCollisionQueryParams SelParams(SCENE_QUERY_STAT(CompanionSelectLoS), true);
		SelParams.AddIgnoredActor(Companion);
		SelParams.AddIgnoredActor(Companion->GetCurrentWeapon());
		for (AActor* Attached : SelectIgnoredAttached)
			SelParams.AddIgnoredActor(Attached);
		const bool bSelBlocked = Companion->GetWorld()->LineTraceSingleByChannel(
			SelHit, SelectAimOrigin, AITargeting::GetSightLocation(Candidate), ECC_Visibility, SelParams);
		if (bSelBlocked && SelHit.GetActor() != Candidate) return;

		BestVisibleDistSq = ScoredDistSq;
		BestVisible = Candidate;
	};

	auto NoteAlertedThreat = [&](const AEnemyCharacter* Enemy, float DistSq)
	{
		if (!IsValid(Enemy) || !Enemy->IsAlertedForCompanionReadiness()) return;
		if (DistSq >= BestAlertDistSq) return;

		BestAlertDistSq = DistSq;
		AlertedThreatLocation = Enemy->GetActorLocation();
		AlertedThreatEnemy = Enemy;
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

		// Normal/Stealth: don't engage enemies that haven't detected the player (no first shot).
		// Combat mode is weapons-free — unaware enemies are valid targets.
		// Non-AEnemyCharacter actors with the enemy tag keep current behavior (treat as engageable).
		if (const AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Actor))
		{
			NoteAlertedThreat(Enemy, DistSq);
			if (Enemy->HasDetectedPlayer())
				bAnyEnemyDetectedPlayer = true;
			else if (Mode != ECompanionMode::Combat)
				continue;
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

				// Same mode-gated no-first-shot rule as the sight pass.
				if (const AEnemyCharacter* ProxEnemy = Cast<AEnemyCharacter>(ProxActor))
				{
					NoteAlertedThreat(ProxEnemy, ProxDistSq);
					if (ProxEnemy->HasDetectedPlayer())
						bAnyEnemyDetectedPlayer = true;
					else if (Mode != ECompanionMode::Combat)
						continue;
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
		int32 PlayerFocusedCount = 0;
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
				bAnyEnemyDetectedPlayer = true;

				// Angle-seek focus tally: aggro'd AND actually targeting the PLAYER (not the
				// companion). Aim target catches the shooter mid-burst; the awareness combat target
				// catches the aggro'd-but-still-rotating case GetAIAimTarget nulls out.
				const AEnemyAIController* ThreatAIC = Cast<AEnemyAIController>(ThreatEnemy->GetController());
				const UEnemyAwarenessComponent* ThreatAwareness = ThreatAIC ? ThreatAIC->GetAwarenessComponent() : nullptr;
				if (ThreatEnemy->GetAIAimTarget() == PlayerPawn
					|| (ThreatAwareness && ThreatAwareness->GetCombatTarget() == PlayerPawn))
					++PlayerFocusedCount;

				// Selection keyed on companion distance + companion eye-line (ConsiderCandidate) —
				// bAllowOccludedAny=false so a player-threat candidate can only enter BestVisible
				// (requires passing the eye-line trace), never the untraced BestAny fallback.
				const float CompanionDistSq = FVector::DistSquared(MyLocation, ThreatActor->GetActorLocation());
				ConsiderCandidate(ThreatActor, CompanionDistSq, /*bAllowOccludedAny=*/ false);
			}
		}
		Companion->SetPlayerFocusedEnemyCount(PlayerFocusedCount);
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

		// Target stickiness: with several visible enemies at similar range, nearest-visible flips
		// every tick as actors move — and each flip re-validates cover/aim/arc against a different
		// enemy, churning the whole combat loop (cover invalidates, re-picks, no shooting). Keep
		// the existing live target unless the new pick is meaningfully closer, provided the
		// existing one is itself still visible (one extra eye-line trace, service cadence only).
		if (IsValid(ExistingTarget) && BestVisible != ExistingTarget)
		{
			const UHealthComponent* StickHealth = ExistingTarget->FindComponentByClass<UHealthComponent>();
			const bool bExistingAlive = !StickHealth || !StickHealth->IsDead();
			const float ExistingDistSq = FVector::DistSquared(MyLocation, ExistingTarget->GetActorLocation());
			constexpr float StickinessRatioSq = 0.56f; // new pick must be ~25% closer to steal focus
			if (bExistingAlive && BestVisibleDistSq > ExistingDistSq * StickinessRatioSq)
			{
				FHitResult StickHit;
				FCollisionQueryParams StickParams(SCENE_QUERY_STAT(CompanionTargetStick), true);
				StickParams.AddIgnoredActor(Companion);
				const bool bStickBlocked = Companion->GetWorld()->LineTraceSingleByChannel(
					StickHit, Companion->GetPawnViewLocation(),
					AITargeting::GetSightLocation(ExistingTarget), ECC_Visibility, StickParams);
				if (!bStickBlocked || StickHit.GetActor() == ExistingTarget)
				{
					BestTarget = ExistingTarget;
					BestDistSq = ExistingDistSq;
				}
			}
		}
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

	// Flanker steals focus: an enemy that recently damaged the companion overrides distance
	// stickiness — being hit outranks "my current pick is closer". Eye-line gated so the
	// companion never turns to engage a shooter it can't actually see.
	if (RangeTuning && RangeTuning->FlankerResponseWindow > 0.f)
	{
		AEnemyCharacter* AttackerEnemy = Cast<AEnemyCharacter>(Companion->GetRecentAttacker(RangeTuning->FlankerResponseWindow));
		if (IsValid(AttackerEnemy) && AttackerEnemy != BestTarget)
		{
			const UHealthComponent* AttackerHealth = AttackerEnemy->GetHealthComponent();
			if (!AttackerHealth || !AttackerHealth->IsDead())
			{
				FHitResult FlankHit;
				FCollisionQueryParams FlankParams(SCENE_QUERY_STAT(CompanionFlankerSteal), true);
				FlankParams.AddIgnoredActor(Companion);
				FlankParams.AddIgnoredActor(Companion->GetCurrentWeapon());
				const bool bFlankBlocked = Companion->GetWorld()->LineTraceSingleByChannel(
					FlankHit, Companion->GetPawnViewLocation(),
					AITargeting::GetSightLocation(AttackerEnemy), ECC_Visibility, FlankParams);
				if (!bFlankBlocked || FlankHit.GetActor() == AttackerEnemy)
				{
					BestTarget = AttackerEnemy;
					BestDistSq = FVector::DistSquared(MyLocation, AttackerEnemy->GetActorLocation());
				}
			}
		}
	}

	// --- Stealth mode: break detection + auto-target suppression ---
	// Break = any enemy has detected the player ("player is spotted"). The scan flag only covers
	// the three scan envelopes, so two out-of-envelope signals widen it: global alert Loud (an
	// enemy anywhere is in open combat — e.g. a sniper beyond PlayerThreatAwarenessRadius) and
	// player DBNO (the revive sprint must not crawl at crouch speed). While unbroken, the companion
	// never auto-acquires a target and never readies up; command-driven fire (takedown/shoot pings)
	// flows through the BB command keys and is unaffected. SetMode resets the broken flag, so a
	// fresh Stealth order always starts unbroken.
	bool bStealthBreakSignal = false;
	if (Mode == ECompanionMode::Stealth)
	{
		bool bGlobalAlertLoud = false;
		if (const UEnemyDirectorSubsystem* Director = Companion->GetWorld()->GetSubsystem<UEnemyDirectorSubsystem>())
			bGlobalAlertLoud = Director->GetAlertLevel() == EGlobalAlertLevel::Loud;
		bStealthBreakSignal = bAnyEnemyDetectedPlayer || bGlobalAlertLoud || bPlayerDBNO;
	}

	if (Mode == ECompanionMode::Stealth && !Companion->IsStealthBroken())
	{
		if (bStealthBreakSignal)
		{
			Companion->SetStealthBroken(true);
		}
		else
		{
			BestTarget = nullptr;
			bHasAlertedThreat = false;
			if (ExistingTarget || BB->GetValueAsObject(CombatTargetKey.SelectedKeyName))
			{
				// Mid-fight switch into stealth: drop the target so the combat branch aborts and
				// the cover slot releases through the task's normal teardown.
				BB->ClearValue(CombatTargetKey.SelectedKeyName);
				BB->SetValueAsBool(HasCoverPositionKey.SelectedKeyName, false);
				ExistingTarget = nullptr;
			}
		}
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

		// Mirror for the anim-side aim fade (enemy bHasTargetLOS parity) — true only when the
		// eye→target trace is genuinely clear, regardless of whether the target is kept below.
		Companion->SetHasTargetLOS(!bBlocked || LosHit.GetActor() == BestTarget);

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
		Companion->SetHasTargetLOS(false);
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
	bool bReadyOnlyThreat = bHasAlertedThreat && !bHasTarget;

	// Honest knowledge: only ready up on an alerted enemy the companion can actually SEE. The
	// player-threat and proximity passes note threats by radius alone, so without this trace a
	// searcher behind a wall (e.g. a takedown neighbour scanning for the body) put the companion
	// in a pinned ADS facing at geometry it has no sight of. One trace, only in the rare
	// no-target-with-threat state, at service cadence.
	if (bReadyOnlyThreat && IsValid(AlertedThreatEnemy))
	{
		FCollisionQueryParams ReadyLosParams(SCENE_QUERY_STAT(CompanionReadyThreatLoS), true);
		ReadyLosParams.AddIgnoredActor(Companion);
		ReadyLosParams.AddIgnoredActor(Companion->GetCurrentWeapon());
		FHitResult ReadyLosHit;
		const bool bReadyLosBlocked = Companion->GetWorld()->LineTraceSingleByChannel(
			ReadyLosHit, Companion->GetPawnViewLocation(),
			AITargeting::GetSightLocation(AlertedThreatEnemy), ECC_Visibility, ReadyLosParams);
		if (bReadyLosBlocked && ReadyLosHit.GetActor() != AlertedThreatEnemy)
			bReadyOnlyThreat = false;
	}

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

	if (Companion->IsStealthActive())
	{
		// Unbroken stealth: posture pinned, weapon low, crouch enforced (route/follow re-entries
		// and the un-crouch on leaving combat cover can stand the companion back up).
		if (CurrentPosture != ECompanionPosture::Stealth)
		{
			if (bDebugLogging)
				UE_LOG(LogCompanionAI, Log, TEXT("%s: posture %s -> %s"),
					*Companion->GetName(), PostureName(CurrentPosture), PostureName(ECompanionPosture::Stealth));
			Companion->SetPosture(ECompanionPosture::Stealth);
		}

		// Enforcement must yield to systems that own aim/facing/stance right now: a commanded
		// takedown arms with weapon-up + SetFocus (stomping it drops the aim mid-hold and
		// re-crouches the knife pair out of the authored pose), traversal resizes the capsule
		// mid-vault, and route legs set their own stances.
		const UTraversalComponent* Traversal = Companion->GetTraversalComponent();
		const bool bEnforcementYields =
			Companion->IsCommandedTakedownArmed()
			|| Companion->IsCommandedTakedownExecuting()
			|| Companion->IsTakedownMontagePlaying()
			|| (IsValid(Traversal) && Traversal->IsBusy())
			|| BB->GetValueAsBool(ACompanionAIController::BB_RouteActive);
		if (!bEnforcementYields)
		{
			Companion->SetLowReadyAim(true);
			Controller->ClearFocus(EAIFocusPriority::Gameplay);
			// The sprint-break catch-up stage owns stance: re-crouching every service tick was
			// silently capping the stealth "sprint to catch up" at crouched speed — the companion
			// never visibly sprinted, it got SLOWER (plain crouch speed, not even the fast tier).
			if (Companion->GetStealthCatchup() != EStealthCatchup::Sprint
				&& !Companion->bIsCrouched && Companion->CanCrouch())
				Companion->Crouch();
		}
		OutOfCombatTimer = 0.f;
	}
	else if (bHasTarget || bReadyOnlyThreat)
	{
		if (CurrentPosture != ECompanionPosture::Combat)
		{
			if (bDebugLogging)
				UE_LOG(LogCompanionAI, Log, TEXT("%s: posture %s -> %s"),
					*Companion->GetName(), PostureName(CurrentPosture), PostureName(ECompanionPosture::Combat));
			Companion->SetPosture(ECompanionPosture::Combat);
		}
		bLoweredOnTargetLoss = false;
		if (bReadyOnlyThreat)
		{
			// Ready stance vs a searching-but-not-engaged enemy (e.g. a takedown neighbour
			// investigating the body): cover it while standing, but never hard-face it while
			// path-following — the Gameplay focal outranks the move focus and walks the
			// companion sideways in ADS at a bearing the player has no context for.
			// Yields to the same aim/focus owners as the sibling branches: an armed/executing
			// takedown (raises + SetFocus on the victim, possibly while still moving into
			// position) and route legs (own the Gameplay focal; a one-shot authored-aim focal
			// would stay cleared for the rest of the leg because the route setter is cached).
			const bool bAimOwnedElsewhere = Companion->IsCommandedTakedownArmed()
				|| Companion->IsCommandedTakedownExecuting()
				|| Companion->IsTakedownMontagePlaying()
				|| BB->GetValueAsBool(ACompanionAIController::BB_RouteActive);
			if (!bAimOwnedElsewhere)
			{
				const UPathFollowingComponent* PathFollowing = Controller->GetPathFollowingComponent();
				const bool bPathMoving = IsValid(PathFollowing)
					&& PathFollowing->GetStatus() == EPathFollowingStatus::Moving;
				if (bPathMoving)
				{
					Companion->SetLowReadyAim(true);
					Controller->ClearFocus(EAIFocusPriority::Gameplay);
				}
				else
				{
					Companion->SetLowReadyAim(false);
					Controller->SetFocalPoint(AlertedThreatLocation, EAIFocusPriority::Gameplay);
				}
			}
		}
		else
		{
			Companion->SetLowReadyAim(false);
		}
		OutOfCombatTimer = 0.f;
	}
	else if (Mode == ECompanionMode::Stealth && Companion->IsStealthBroken() && !bStealthBreakSignal)
	{
		// Broken stealth with the fight over (no target, no alerted threat, no live break signal —
		// re-pinning under a still-Loud alert or during a DBNO revive would just flap): wait out
		// the re-pin delay, then return to stealth rules. Runs regardless of current posture — a
		// break can happen without the companion ever acquiring a target (spotted player, no LoS).
		// Same immediate weapon-lower as the combat-decay branch below — waiting out the 4s re-pin
		// aimed at a corpse is the exact stale-ADS stare the edge trigger exists to kill.
		const bool bTakedownOwnsAim = Companion->IsCommandedTakedownArmed()
			|| Companion->IsCommandedTakedownExecuting()
			|| Companion->IsTakedownMontagePlaying();
		if (!bLoweredOnTargetLoss && !bTakedownOwnsAim)
		{
			Companion->SetLowReadyAim(true);
			// A stale aim target keeps the ADS pose regardless of low-ready (the anim's
			// actor-target branch bypasses it) — clear it with the same edge/yield guards.
			Companion->SetAimTarget(nullptr);
			Controller->ClearFocus(EAIFocusPriority::Gameplay);
			bLoweredOnTargetLoss = true;
		}
		OutOfCombatTimer += DeltaSeconds;
		const float RepinDelay = RangeTuning ? RangeTuning->StealthRepinDelay : ExploreReturnDelay;
		if (OutOfCombatTimer >= RepinDelay)
		{
			if (bDebugLogging)
				UE_LOG(LogCompanionAI, Log, TEXT("%s: stealth re-pin (posture %s -> %s)"),
					*Companion->GetName(), PostureName(CurrentPosture), PostureName(ECompanionPosture::Stealth));
			Companion->SetStealthBroken(false); // re-applies crouch + sprint lock
			Companion->SetPosture(ECompanionPosture::Stealth);
			// The timer keeps accruing through an armed takedown — its expiry must not stomp the
			// takedown's aim/focus either (cosmetic wobble; execute re-raises, but don't fight it).
			if (!bTakedownOwnsAim)
			{
				Companion->SetLowReadyAim(true);
				Controller->ClearFocus(EAIFocusPriority::Gameplay);
			}
			OutOfCombatTimer = 0.f;
		}
	}
	else if (!bHasTarget && CurrentPosture == ECompanionPosture::Combat)
	{
		// Lower the weapon the moment the last target is gone — the ExploreReturnDelay below is BT
		// posture stability, not a reason to hold a stale ADS pose aimed at a dead enemy's bearing.
		// Edge-guarded by bLoweredOnTargetLoss (reset whenever a target/threat is live) so the lower
		// still fires after a branch switch mid-accrual (e.g. stealth re-pin -> mode change).
		// Yields to a commanded takedown, which owns aim/focus while armed.
		const bool bTakedownOwnsAim = Companion->IsCommandedTakedownArmed()
			|| Companion->IsCommandedTakedownExecuting()
			|| Companion->IsTakedownMontagePlaying();
		if (!bLoweredOnTargetLoss && !bTakedownOwnsAim)
		{
			Companion->SetLowReadyAim(true);
			// Same stale-aim-target backstop as the stealth re-pin branch above.
			Companion->SetAimTarget(nullptr);
			Controller->ClearFocus(EAIFocusPriority::Gameplay);
			bLoweredOnTargetLoss = true;
		}

		OutOfCombatTimer += DeltaSeconds;
		if (OutOfCombatTimer >= ExploreReturnDelay)
		{
			if (bDebugLogging)
				UE_LOG(LogCompanionAI, Log, TEXT("%s: posture %s -> %s"),
					*Companion->GetName(), PostureName(CurrentPosture), PostureName(ECompanionPosture::Exploration));
			Companion->SetPosture(ECompanionPosture::Exploration);
			// Expiry mirrors the edge-lower's takedown yield — see the re-pin branch.
			if (!bTakedownOwnsAim)
			{
				Companion->SetLowReadyAim(true);
				Controller->ClearFocus(EAIFocusPriority::Gameplay);
			}
			OutOfCombatTimer = 0.f;
		}
	}

	// --- AimLog diagnostics (companion.AimLog 1) ---
	if (CVarCompanionAimLog.GetValueOnGameThread() != 0)
	{
		const TCHAR* PickSrc = !TargetAfterUpdate ? TEXT("none")
			: (TargetAfterUpdate == BestVisible) ? TEXT("visible")
			: (TargetAfterUpdate == BestAny) ? TEXT("OCCLUDED-ANY")
			: TEXT("kept");
		const UPathFollowingComponent* LogPF = Controller->GetPathFollowingComponent();
		const bool bLogMoving = IsValid(LogPF) && LogPF->GetStatus() == EPathFollowingStatus::Moving;
		UE_LOG(LogCompanionAI, Display,
			TEXT("[AimLog] mode=%d posture=%s lowReady=%d aimTarget=%s | bbTarget=%s src=%s losBlocked=%d losBlockT=%.1f | readyOnly=%d alertEnemy=%s alertDist=%.0f | moving=%d route=%d tdArmed=%d tdExec=%d tdMont=%d latch=%d | focusActor=%s focal=%s"),
			(int32)Mode, PostureName(Companion->GetPosture()), (int32)Companion->IsLowReadyAim(),
			*GetNameSafe(Companion->GetAimTarget()),
			*GetNameSafe(TargetAfterUpdate), PickSrc, (int32)bWasLosBlocked, OpenLosBlockedTime,
			(int32)bReadyOnlyThreat, *GetNameSafe(AlertedThreatEnemy),
			bHasAlertedThreat ? FMath::Sqrt(BestAlertDistSq) : -1.f,
			(int32)bLogMoving, (int32)BB->GetValueAsBool(ACompanionAIController::BB_RouteActive),
			(int32)Companion->IsCommandedTakedownArmed(), (int32)Companion->IsCommandedTakedownExecuting(),
			(int32)Companion->IsTakedownMontagePlaying(), (int32)bLoweredOnTargetLoss,
			*GetNameSafe(Controller->GetFocusActor()), *Controller->GetFocalPoint().ToCompactString());
	}

	// --- Revive window computation (threat-gated revive) ---
	if (ReviveWindowOpenKey.SelectedKeyName.IsNone() && !bLoggedReviveKeyResolveFail)
	{
		bLoggedReviveKeyResolveFail = true;
		UE_LOG(LogCompanionAI, Warning,
			TEXT("%s: ReviveWindowOpen BB key failed to resolve — revive window gating is disabled"),
			*Companion->GetName());
	}
	if (bPlayerDBNO && IsValid(PlayerPawn))
	{
		bool bReviveWindowOpen = false;

		// Latch: once the companion is mid-revive, hold the key true so the BT hold is uninterruptible.
		if (Companion->IsRevivingPlayer())
		{
			bReviveWindowOpen = true;
		}
		else
		{
			// Check for nearby threats around the downed player via a dedicated overlap.
			bool bHot = false;
			const FVector PlayerLoc = PlayerPawn->GetActorLocation();
			const float ThreatRadius = Companion->ReviveThreatRadius;

			TArray<FOverlapResult> ReviveOverlaps;
			ReviveOverlaps.Reserve(8);
			FCollisionObjectQueryParams RevObjParams(ECC_Pawn);
			FCollisionQueryParams RevParams(SCENE_QUERY_STAT(ReviveWindowThreat), false);
			RevParams.AddIgnoredActor(Companion);
			RevParams.AddIgnoredActor(PlayerPawn);

			Companion->GetWorld()->OverlapMultiByObjectType(
				ReviveOverlaps, PlayerLoc, FQuat::Identity,
				RevObjParams, FCollisionShape::MakeSphere(ThreatRadius), RevParams);

			for (const FOverlapResult& Overlap : ReviveOverlaps)
			{
				const AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Overlap.GetActor());
				if (!IsValid(Enemy)) continue;
				if (!Enemy->IsAlertedForCompanionReadiness()) continue;
				const UHealthComponent* EHP = Enemy->GetHealthComponent();
				if (EHP && EHP->IsDead()) continue;

				bHot = true;
				break;
			}

			// Also check alerted enemies beyond the radius but with LoS to the downed player
			if (!bHot)
			{
				for (AActor* Actor : PerceivedActors)
				{
					const AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Actor);
					if (!IsValid(Enemy)) continue;
					if (!Enemy->IsAlertedForCompanionReadiness()) continue;
					const UHealthComponent* EHP = Enemy->GetHealthComponent();
					if (EHP && EHP->IsDead()) continue;

					FHitResult RevLosHit;
					FCollisionQueryParams RevLosParams(SCENE_QUERY_STAT(ReviveWindowLoS), true);
					RevLosParams.AddIgnoredActor(Enemy);
					RevLosParams.AddIgnoredActor(PlayerPawn);
					const bool bLosBlocked = Companion->GetWorld()->LineTraceSingleByChannel(
						RevLosHit, Enemy->GetPawnViewLocation(), PlayerLoc, ECC_Visibility, RevLosParams);
					if (!bLosBlocked || RevLosHit.GetActor() == PlayerPawn)
					{
						bHot = true;
						break;
					}
				}
			}

			if (bHot)
			{
				ReviveSafeAccumulator = 0.f;
			}
			else
			{
				ReviveSafeAccumulator += DeltaSeconds;
			}

			// Desperation override: bleedout nearly out
			if (PlayerIface->GetBleedoutTimeRemaining() <= Companion->DesperationBleedoutThreshold)
				bReviveWindowOpen = true;
			// Grace period elapsed
			else if (ReviveSafeAccumulator >= Companion->ReviveSafeGraceSeconds)
				bReviveWindowOpen = true;
		}

		BB->SetValueAsBool(ReviveWindowOpenKey.SelectedKeyName, bReviveWindowOpen);
	}
	else
	{
		BB->SetValueAsBool(ReviveWindowOpenKey.SelectedKeyName, false);
		ReviveSafeAccumulator = 0.f;
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
