// BT service — ticks to update all companion blackboard keys (DBNO, combat target, etc).

#include "BTService_UpdateCompanionState.h"
#include "AI/AITargetingStatics.h"
#include "AI/CompanionDiag.h"
#include "AI/CompanionSearchRoomPolicy.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISense_Sight.h"
#include "Perception/AISense_Hearing.h" // heard-enemy-gunfire stealth-break signal
#include "CompanionAIController.h" // for LogCompanionAI
#include "CompanionCharacter.h"
#include "WeaponBase.h"
#include "AI/CompanionTuningDataAsset.h"
#include "Companion/CompanionTypes.h"
#include "Companion/CompanionCommandTypes.h" // ECompanionCommand — non-combat facing Tier-0 yield check
#include "Character/ExtractionPlayerInterface.h"
#include "GameFramework/Pawn.h"      // APawn::GetVelocity/GetBaseAimRotation for ambient facing
#include "GameFramework/Character.h" // ACharacter::bIsCrouched for the stealth crouch-mirror
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
#include "EnemyDirectorSubsystem.h" // last combat report stamp as an out-of-envelope stealth-break event
#include "TraversalComponent.h"     // stealth-pin enforcement must not crouch mid-traversal
#include "Navigation/PathFollowingComponent.h" // ready-only threat stance yields facing to an active move
#include "HAL/IConsoleManager.h" // companion.AimLog diagnostics CVar

// companion.AimLog 1 — per-service-tick dump of everything that drives the companion's aim/facing
// (target pick + provenance, ready-only threat, takedown/route yields, focal point, low-ready).
// Diagnostic for the stuck-ADS / aims-through-walls reports; Display severity so it shows untagged.
static TAutoConsoleVariable<int32> CVarCompanionAimLog(
	TEXT("companion.AimLog"), 0,
	TEXT("1 = log companion aim/stance state each UpdateCompanionState tick."));

// companion.FireDebug is registered once in WeaponBase.cpp — re-query by name here to avoid a
// duplicate CVar registration across translation units.
static bool IsCompanionFireDebugEnabled()
{
	static const IConsoleVariable* CVar =
		IConsoleManager::Get().FindConsoleVariable(TEXT("companion.FireDebug"));
	return CVar && CVar->GetInt() != 0;
}

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

	// Downed: the retreat task owns the body until revive. Skip every combat/posture/aim/facing
	// write — a per-tick ADS/posture re-assert stomping the downed pose is a proven failure mode
	// from the player-revive rounds, and target churn is pointless while the tree sits in the
	// Downed branch.
	if (Companion->GetIsCompanionDBNO()) return;

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

	// F3 watch-threat candidate: nearest ALIVE enemy within watch range, aware or not — LoS is
	// resolved later by UpdateNonCombatFacing/ComputeWatchThreat, not here (no wasted traces on a
	// candidate that never wins the "nearest" slot).
	const AEnemyCharacter* WatchCandidateEnemy = nullptr;
	float BestWatchDistSq = MAX_FLT;
	const float WatchRangeSq = FMath::Square(RangeTuning ? RangeTuning->WatchThreatRange : 3500.f);

	// Player-commanded mode gates target acquisition (Normal: alerted-only, Combat: weapons-free,
	// Stealth: suppressed until broken). bAnyEnemyDetectedPlayer feeds the stealth-break check.
	const ECompanionMode Mode = Companion->GetMode();
	bool bAnyEnemyDetectedPlayer = false;

	// Stealth-break support: an in-envelope enemy whose Combat entry post-dates the stealth pin is
	// a NEW fight, not a stale awareness tail. Isolated-encounter enemies never report to the
	// director, so this per-enemy stamp is their only break path.
	const float StealthPinTime = Companion->GetStealthPinTime();
	bool bFreshEnvelopeCombatEntry = false;

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
		// Sight location (chest) — the ready focal feeds a pitch-preserving controller now.
		AlertedThreatLocation = AITargeting::GetSightLocation(Enemy);
		AlertedThreatEnemy = Enemy;
		bHasAlertedThreat = true;
	};

	// F3: unlike NoteAlertedThreat, this does NOT filter on IsAlertedForCompanionReadiness — an
	// unaware enemy is exactly the point (Normal mode raises the weapon at it; Stealth just rotates
	// to watch). Called before the mode-gated "no first shot" continue in both callers so unaware
	// enemies still register even when they're skipped for target acquisition.
	auto NoteWatchCandidate = [&](const AEnemyCharacter* Enemy, float DistSq)
	{
		if (!IsValid(Enemy)) return;
		if (DistSq >= WatchRangeSq) return;
		if (DistSq >= BestWatchDistSq) return;

		BestWatchDistSq = DistSq;
		WatchCandidateEnemy = Enemy;
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
			NoteWatchCandidate(Enemy, DistSq);
			if (Enemy->HasDetectedPlayer())
			{
				bAnyEnemyDetectedPlayer = true;
				bFreshEnvelopeCombatEntry |= Enemy->GetTimeEnteredCombat() > StealthPinTime;
			}
			// Exposure changes enemy sight, not permission to acquire an unaware target.
			else if (!CompanionSearchRoomPolicy::CanEngageUnawareEnemy(Mode))
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
					NoteWatchCandidate(ProxEnemy, ProxDistSq);
					if (ProxEnemy->HasDetectedPlayer())
					{
						bAnyEnemyDetectedPlayer = true;
						bFreshEnvelopeCombatEntry |= ProxEnemy->GetTimeEnteredCombat() > StealthPinTime;
					}
					// Same no-first-shot policy as the sight pass.
					else if (!CompanionSearchRoomPolicy::CanEngageUnawareEnemy(Mode))
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
				bFreshEnvelopeCombatEntry |= ThreatEnemy->GetTimeEnteredCombat() > StealthPinTime;

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
	bool bBodyChargerStealGranted = false;
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
			// Rescue commitment: while the player is DBNO, a live, still-visible target is NEVER
			// swapped for a nearer one — kill it, then take the next. Every swap re-validates
			// cover/aim/arc and stalls the clear. Exceptions: flanker-steal below, and a body
			// charger — a new pick inside the revive threat ring outranks a committed target
			// that isn't (an enemy walking up to the downed player must not be ignored).
			bool bBodyChargerSteal = false;
			if (bPlayerDBNO && DBNOBiasThreatRadiusSq > 0.f)
			{
				const bool bNewIsBodyThreat =
					FVector::DistSquared(BestVisible->GetActorLocation(), DBNOPlayerLoc) <= DBNOBiasThreatRadiusSq;
				const bool bExistingIsBodyThreat =
					FVector::DistSquared(ExistingTarget->GetActorLocation(), DBNOPlayerLoc) <= DBNOBiasThreatRadiusSq;
				bBodyChargerSteal = bNewIsBodyThreat && !bExistingIsBodyThreat;
				// Hysteresis: several enemies converging on the body flap in/out of the threat ring,
				// firing this steal several times a second — each swap re-validates cover/aim/arc
				// and the target thrashes. At most one steal per cooldown. The stamp itself is
				// deferred until after the flanker override below — a stamped-but-overridden steal
				// would suppress a genuine charger for the residual window.
				if (bBodyChargerSteal)
				{
					const float StealCooldown = RangeTuning ? RangeTuning->BodyChargerStealCooldown : 2.f;
					if (StealCooldown > 0.f && LastBodyChargerStealTime >= 0.f
						&& (Companion->GetWorld()->GetTimeSeconds() - LastBodyChargerStealTime) < StealCooldown)
						bBodyChargerSteal = false;
					else
						bBodyChargerStealGranted = true;
				}
			}
			const bool bDistanceAllowsSteal =
				(!bPlayerDBNO && BestVisibleDistSq <= ExistingDistSq * StickinessRatioSq) || bBodyChargerSteal;
			if (bExistingAlive && !bDistanceAllowsSteal)
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

	// Body-charger cooldown stamp — only when the steal actually took effect (survived the
	// flanker override above), so an overridden steal can't burn the window.
	if (bBodyChargerStealGranted && BestTarget == BestVisible)
		LastBodyChargerStealTime = Companion->GetWorld()->GetTimeSeconds();

	// --- Stealth mode: event-driven break + auto-target suppression ---
	// Break = a combat EVENT stamped after the stealth pin time (the moment this stealth stint
	// became active — fresh player order or re-pin; see GetStealthPinTime). Event sources:
	//   - director combat report: any non-isolated enemy entered Combat anywhere in the level
	//     (covers fights outside every companion scan envelope, e.g. a distant sniper)
	//   - in-envelope Combat entry newer than the pin (bFreshEnvelopeCombatEntry from the three
	//     scan passes — isolated encounters never report to the director)
	//   - player damaged since the pin (an out-of-envelope shooter actually landing hits)
	//   - companion damaged since the pin (enemy engaging the companion directly)
	//   - enemy heard by the hearing sense (weapon fire/reload within HearingRange in the last
	//     MaxAge — enemies have no footstep emitters), gated until the pin is older than MaxAge
	//     so stimuli from before a fresh order can't break it
	//   - player DBNO (immediate — the revive sprint can't crawl at stealth speed)
	// Events-vs-pin replaces the old LOW->HIGH boolean edge. That edge composited the director's
	// Loud alert LEVEL, which is a ratchet (Escalate never de-escalates) — after the level's
	// first fight the signal never read low again, the edge never re-armed, and stealth could
	// never break into combat again (nor re-pin, which required !signal). A stale
	// Combat-awareness tail still can't veto a fresh Stealth order (its stamps pre-date the pin,
	// preserving the 2026-07-11 fix), and a NEW fight erupting mid-tail now breaks too — the
	// single boolean edge missed that case.
	// While unbroken, the companion never auto-acquires a target and never readies up;
	// command-driven fire (takedown/shoot pings) flows through the BB command keys unaffected.
	bool bStealthBreakEvent = false;   // break gate: combat event newer than the pin
	bool bLiveFightSignal = false;     // re-pin hold: is a fight plausibly still on right now?
	if (Mode == ECompanionMode::Stealth)
	{
		const float StealthNow = Companion->GetWorld()->GetTimeSeconds();

		float DirectorCombatTime = -1e9f;
		if (const UEnemyDirectorSubsystem* Director = Companion->GetWorld()->GetSubsystem<UEnemyDirectorSubsystem>())
			DirectorCombatTime = Director->GetLastCombatReportTime();

		const UHealthComponent* PlayerHealth = IsValid(PlayerPawn)
			? PlayerPawn->FindComponentByClass<UHealthComponent>() : nullptr;
		const float PlayerDamageTime = PlayerHealth ? PlayerHealth->GetLastDamageWorldTime() : -1e9f;

		bool bHeardEnemy = false;
		TArray<AActor*> HeardActors;
		Perception->GetCurrentlyPerceivedActors(UAISense_Hearing::StaticClass(), HeardActors);
		for (AActor* Heard : HeardActors)
		{
			const AEnemyCharacter* HeardEnemy = Cast<AEnemyCharacter>(Heard);
			if (!IsValid(HeardEnemy)) continue;
			const UHealthComponent* HeardHealth = HeardEnemy->GetHealthComponent();
			if (HeardHealth && HeardHealth->IsDead()) continue;
			bHeardEnemy = true;
			break;
		}

		const float PinAge = StealthNow - StealthPinTime;
		const bool bCompanionHitSincePin = IsValid(Companion->GetRecentAttacker(PinAge));

		bStealthBreakEvent = bPlayerDBNO
			|| DirectorCombatTime > StealthPinTime
			|| bFreshEnvelopeCombatEntry
			|| PlayerDamageTime > StealthPinTime
			|| bCompanionHitSincePin
			|| (bHeardEnemy && PinAge > Controller->GetHearingSenseMaxAge());

		// Live-fight hold for the re-pin: engaged enemies in-envelope, gunfire audible, or any
		// combat/damage event within the re-pin window means the fight is still on.
		const float LiveFightWindow = RangeTuning ? RangeTuning->StealthRepinDelay : ExploreReturnDelay;
		bLiveFightSignal = bAnyEnemyDetectedPlayer
			|| bPlayerDBNO
			|| bHeardEnemy
			|| (StealthNow - DirectorCombatTime) < LiveFightWindow
			|| (StealthNow - PlayerDamageTime) < LiveFightWindow
			|| IsValid(Companion->GetRecentAttacker(LiveFightWindow));

		if (IsCompanionFireDebugEnabled() && !Companion->IsStealthBroken())
		{
			UE_LOG(LogCompanionDiag, Warning,
				TEXT("%s: [FireDebug] STEALTH unbroken: break=%d (dbno=%d dirCombat=%d envelope=%d playerHit=%d companionHit=%d heard=%d) pinAge=%.1f -> %s"),
				*Companion->GetName(), (int32)bStealthBreakEvent, (int32)bPlayerDBNO,
				(int32)(DirectorCombatTime > StealthPinTime), (int32)bFreshEnvelopeCombatEntry,
				(int32)(PlayerDamageTime > StealthPinTime), (int32)bCompanionHitSincePin,
				(int32)bHeardEnemy, PinAge,
				bStealthBreakEvent ? TEXT("BREAKING (targets allowed next tick)") : TEXT("holding fire (no auto-targets)"));
		}
	}

	if (Mode == ECompanionMode::Stealth && !Companion->IsStealthBroken())
	{
		if (bStealthBreakEvent)
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
		// Unbroken stealth: posture pinned, weapon low. Stance is no longer force-enforced here
		// (F4a) — the crouch-mirror below owns it, standing slow-walk by default.
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
			// Facing is now owned by UpdateNonCombatFacing's arbitration, called after this whole
			// posture chain — no ClearFocus here, or it would stomp the watch/ambient tiers the
			// instant they set a focal point this same tick.
			// Stance (crouch/stand) is owned by the crouch-mirror, not a flat force-crouch — F4a's
			// default is a standing slow-walk; only the player's own crouch pulls it down. The
			// sprint-break catch-up stage still owns stance outright (mirror returns early for it).
			UpdateStealthCrouchMirror(*Companion, PlayerPawn, RangeTuning, DeltaSeconds);
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
		// Revive hold owns aim/stance: the kneeling revive must not fight a per-tick ADS re-assert
		// (the aim layer also overrides the montage slot pose — weapon-up while kneeling).
		if (Companion->IsRevivingPlayer())
		{
			Companion->SetLowReadyAim(true);
			Companion->SetAimTarget(nullptr);
			Controller->ClearFocus(EAIFocusPriority::Gameplay);
		}
		else if (bReadyOnlyThreat)
		{
			// Ready stance vs a searching-but-not-engaged enemy (e.g. a takedown neighbour
			// investigating the body): cover it while standing, facing it even while path-moving —
			// the honest-knowledge LoS gate above (ReadyLosParams) already guarantees this enemy is
			// genuinely visible, so there's no more "aims through a wall while walking" risk the
			// old path-moving guard existed to prevent.
			// Yields to the same aim/focus owners as the sibling branches: an armed/executing
			// takedown (raises + SetFocus on the victim, possibly while still moving into
			// position), route legs (own the Gameplay focal; a one-shot authored-aim focal
			// would stay cleared for the rest of the leg because the route setter is cached),
			// and commands that own facing. Stationary Loot is the exception: it may cover the
			// threat between movement legs without back-walking the approach.
			const UPathFollowingComponent* PathFollowing = Controller->GetPathFollowingComponent();
			const bool bPathMoving = IsValid(PathFollowing)
				&& PathFollowing->GetStatus() == EPathFollowingStatus::Moving;
			const ECompanionCommand ActiveCommand = static_cast<ECompanionCommand>(
				BB->GetValueAsEnum(ACompanionAIController::BB_CompanionCommand));
			const bool bPostureOwnsFacing = CompanionSearchRoomPolicy::PostureOwnsReadyThreatFacing(
				Companion->IsStealthActive(), ActiveCommand, bPathMoving,
				Companion->IsSearchRoomExposureActive());
			const bool bAimOwnedElsewhere = Companion->IsCommandedTakedownArmed()
				|| Companion->IsCommandedTakedownExecuting()
				|| Companion->IsTakedownMontagePlaying()
				|| BB->GetValueAsBool(ACompanionAIController::BB_RouteActive)
				|| !bPostureOwnsFacing;
			if (!bAimOwnedElsewhere)
			{
				Companion->SetLowReadyAim(false);
				Controller->SetFocalPoint(AlertedThreatLocation, EAIFocusPriority::Gameplay);
			}
		}
		else
		{
			Companion->SetLowReadyAim(false);
		}
		OutOfCombatTimer = 0.f;
	}
	else if (Mode == ECompanionMode::Stealth && Companion->IsStealthBroken() && !bLiveFightSignal)
	{
		// Broken stealth with the fight over (no target, no alerted threat, no live-fight signal —
		// no engaged in-envelope enemy, no audible enemy fire, no combat/damage event within the
		// re-pin window; re-pinning mid-fight or during a DBNO revive would just flap): wait out
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
			Companion->SetStealthBroken(false); // re-applies stealth speed clamps + sprint lock (stance is the crouch-mirror's call, not this)
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
	else if (Mode == ECompanionMode::Stealth && Companion->IsStealthBroken())
	{
		// Broken stealth with a live-fight signal still high (heard fire, engaged enemy in-envelope,
		// recent combat/damage event): the re-pin accrual must restart from zero once the signal
		// drops — not resume a partial count, and not leak into the explore-return accrual below
		// (which shares the timer and would let re-pin fire up to its whole delay early).
		OutOfCombatTimer = 0.f;
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

	// --- Non-combat facing arbitration (F1 ambient + F3 watch-threats) ---
	// Runs after the posture chain settles so it sees this tick's final bHasTarget/bReadyOnlyThreat
	// and yields cleanly to every branch above that already claimed aim/focus this tick.
	UpdateNonCombatFacing(*Controller, *Companion, *BB, PlayerPawn, RangeTuning,
		bHasTarget, bReadyOnlyThreat, WatchCandidateEnemy, DeltaSeconds);

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

			// Also check enemies beyond the radius with LoS to the downed player — but only ones
			// actively IN COMBAT within ReviveLoSThreatRadius. Searching enemies parked on the
			// DBNO standoff ring keep an eye-line to the body indefinitely in open maps; counting
			// them held the window shut until desperation every single time.
			if (!bHot)
			{
				const float LoSThreatRadiusSq = FMath::Square(Companion->ReviveLoSThreatRadius);
				for (AActor* Actor : PerceivedActors)
				{
					const AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Actor);
					if (!IsValid(Enemy)) continue;
					const UHealthComponent* EHP = Enemy->GetHealthComponent();
					if (EHP && EHP->IsDead()) continue;

					const AEnemyAIController* LosAIC = Cast<AEnemyAIController>(Enemy->GetController());
					const UEnemyAwarenessComponent* LosAwareness = LosAIC ? LosAIC->GetAwarenessComponent() : nullptr;
					if (!LosAwareness || LosAwareness->GetAwarenessState() != EEnemyAwarenessState::Combat) continue;
					if (FVector::DistSquared(Enemy->GetActorLocation(), PlayerLoc) > LoSThreatRadiusSq) continue;

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

			const bool bDesperation =
				PlayerIface->GetBleedoutTimeRemaining() <= Companion->DesperationBleedoutThreshold;

			// Committed rescue: once open, the window stays LATCHED — ring enemies flipping back to
			// Combat as the companion breaks off must not yank it out mid-approach (the open/shut
			// flicker aborted every rescue ~4s in). The latch drops only on bail: threats hot AND
			// companion critically low, and desperation overrides even that (a last-ditch attempt
			// beats a guaranteed bleedout).
			const UHealthComponent* CompanionHealth = Companion->GetHealthComponent();
			const float CompanionHealthFrac = IsValid(CompanionHealth) ? CompanionHealth->GetHealthPercent() : 1.f;
			const bool bBail = bLastReviveWindowOpen && bHot && !bDesperation
				&& CompanionHealthFrac < Companion->RescueBailHealthFraction;

			if (bBail)
			{
				if (bDebugLogging)
					UE_LOG(LogCompanionAI, Log, TEXT("%s: RESCUE BAIL hp=%.0f%% — re-fighting until safer"),
						*Companion->GetName(), CompanionHealthFrac * 100.f);
			}
			else if (bLastReviveWindowOpen)
				bReviveWindowOpen = true;
			// Desperation override: bleedout nearly out
			else if (bDesperation)
				bReviveWindowOpen = true;
			// Grace period elapsed
			else if (ReviveSafeAccumulator >= Companion->ReviveSafeGraceSeconds)
				bReviveWindowOpen = true;
		}

		if (bDebugLogging && bReviveWindowOpen != bLastReviveWindowOpen)
			UE_LOG(LogCompanionAI, Log, TEXT("%s: REVIVE WINDOW %s (latched=%d bleedout=%.1fs safeAccum=%.2fs)"),
				*Companion->GetName(), bReviveWindowOpen ? TEXT("OPEN") : TEXT("SHUT"),
				(int32)Companion->IsRevivingPlayer(), PlayerIface->GetBleedoutTimeRemaining(), ReviveSafeAccumulator);
		bLastReviveWindowOpen = bReviveWindowOpen;

		// Approach damage resist rides the committed rescue (hold-phase resist is bIsRevivingPlayer).
		Companion->SetRescueCommitted(bReviveWindowOpen);

		BB->SetValueAsBool(ReviveWindowOpenKey.SelectedKeyName, bReviveWindowOpen);
	}
	else
	{
		BB->SetValueAsBool(ReviveWindowOpenKey.SelectedKeyName, false);
		ReviveSafeAccumulator = 0.f;
		bLastReviveWindowOpen = false;
		Companion->SetRescueCommitted(false);
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

// ---------------------------------------------------------------------------
// F4a — stealth crouch-mirror
// ---------------------------------------------------------------------------

void UBTService_UpdateCompanionState::UpdateStealthCrouchMirror(
	ACompanionCharacter& Companion, const APawn* PlayerPawn, const UCompanionTuningDataAsset* Tuning, float DeltaSeconds)
{
	// The sprint-break catch-up stage owns stance outright (always standing while closing the gap) —
	// re-crouching under it was the pre-F4a bug that silently capped "sprint to catch up" at crouch speed.
	if (Companion.GetStealthCatchup() == EStealthCatchup::Sprint) return;

	const ACharacter* PlayerChar = Cast<ACharacter>(PlayerPawn);
	const bool bPlayerCrouched = PlayerChar && PlayerChar->bIsCrouched;

	// Self-reconciling: a plain edge on bMirrorLastPlayerCrouched misses two cases — (a) the Sprint
	// early-return above skips this whole function, so a crouch toggle mid-sprint never gets
	// recorded; (b) a stealth re-pin after combat where the companion's ACTUAL stance was left by
	// combat's own crouch logic, not this mirror, while bMirrorLastPlayerCrouched still (correctly)
	// matches the player's unchanged state. Rolling a fresh delay whenever the companion's real
	// stance disagrees with the player's — not just on a player-state edge — catches both without
	// giving up the humanized delay.
	const bool bPlayerStateEdge = bPlayerCrouched != bMirrorLastPlayerCrouched;
	const bool bNeedsReconcile = MirrorDelayRemaining <= 0.f && bPlayerCrouched != Companion.bIsCrouched;

	if (bPlayerStateEdge || bNeedsReconcile)
	{
		bMirrorLastPlayerCrouched = bPlayerCrouched;
		const float DelayMin = bPlayerCrouched
			? (Tuning ? Tuning->StealthCrouchMirrorDelayMin : 0.35f)
			: (Tuning ? Tuning->StealthUncrouchMirrorDelayMin : 0.5f);
		const float DelayMax = bPlayerCrouched
			? (Tuning ? Tuning->StealthCrouchMirrorDelayMax : 0.8f)
			: (Tuning ? Tuning->StealthUncrouchMirrorDelayMax : 1.2f);
		MirrorDelayRemaining = FMath::FRandRange(DelayMin, DelayMax);
	}

	if (MirrorDelayRemaining <= 0.f) return;

	MirrorDelayRemaining -= DeltaSeconds;
	if (MirrorDelayRemaining > 0.f) return;

	// Through MirrorCrouch, not Crouch()/UnCrouch() directly — the ownership flag it stamps is
	// what lets the stealth teardown release this crouch without popping cover crouches.
	if (bPlayerCrouched && Companion.CanCrouch() && !Companion.bIsCrouched)
		Companion.MirrorCrouch(true);
	else if (!bPlayerCrouched && Companion.bIsCrouched)
		Companion.MirrorCrouch(false);
}

// ---------------------------------------------------------------------------
// F3 + F1 — non-combat facing arbitration
// ---------------------------------------------------------------------------

void UBTService_UpdateCompanionState::UpdateNonCombatFacing(
	ACompanionAIController& Controller, ACompanionCharacter& Companion, UBlackboardComponent& BB,
	APawn* PlayerPawn, const UCompanionTuningDataAsset* Tuning,
	bool bHasTarget, bool bReadyOnlyThreat, const AEnemyCharacter* WatchCandidateEnemy, float DeltaSeconds)
{
	// Tier 0 yield set — the complete list of systems that already own aim/focus/stance this tick.
	// Mirrors bEnforcementYields plus the combat/ready-threat/revive/DBNO/command states those
	// branches cover themselves.
	const UTraversalComponent* Traversal = Companion.GetTraversalComponent();
	const ECompanionCommand ActiveCommand = static_cast<ECompanionCommand>(
		BB.GetValueAsEnum(ACompanionAIController::BB_CompanionCommand));
	const UPathFollowingComponent* PathFollowing = Controller.GetPathFollowingComponent();
	const bool bPathMoving = IsValid(PathFollowing)
		&& PathFollowing->GetStatus() == EPathFollowingStatus::Moving;
	const bool bCommandYieldsFacing = CompanionSearchRoomPolicy::CommandYieldsThreatFacing(
		ActiveCommand, bPathMoving, Companion.IsSearchRoomExposureActive());
	const bool bReadyThreatOwnsFacing = CompanionSearchRoomPolicy::PostureOwnsReadyThreatFacing(
		Companion.IsStealthActive(), ActiveCommand, bPathMoving,
		Companion.IsSearchRoomExposureActive());
	const bool bTierZeroYield = bHasTarget || (bReadyOnlyThreat && bReadyThreatOwnsFacing)
		|| Companion.IsRevivingPlayer()
		|| Companion.IsCommandedTakedownArmed() || Companion.IsCommandedTakedownExecuting()
		|| Companion.IsTakedownMontagePlaying()
		|| (IsValid(Traversal) && Traversal->IsBusy())
		|| BB.GetValueAsBool(ACompanionAIController::BB_RouteActive)
		|| bCommandYieldsFacing
		|| Companion.GetIsCompanionDBNO();

	if (bTierZeroYield)
	{
		// Hand-off: a focal WE wrote must not persist into the new owner's approach — a stale
		// watch/ambient point behind the pawn back-walks the companion to a breach/route/command
		// move until the owner writes its own facing (the breach task only claims focus at the
		// align phase). Clear only a focal that matches one of ours: if the owner already
		// overwrote it, the compare fails and theirs is left alone.
		// Gated off the target/ready yields: WatchThreatLocation IS the enemy's location, so at a
		// watch->ready/combat transition on a near-stationary enemy the owner's brand-new focal can
		// Equals-match our stored point and get wrongly cleared (1-2 tick facing blip). Those two
		// branches own facing inside the posture chain; the back-walk this hand-off exists for is
		// the route/command/traversal approaches only.
		const FVector CurrentFocal = Controller.GetFocalPointForPriority(EAIFocusPriority::Gameplay);
		const bool bFocalIsOurs = !bHasTarget && !bReadyOnlyThreat
			&& ((!LastAmbientFocalPoint.IsZero() && CurrentFocal.Equals(LastAmbientFocalPoint, 1.f))
				|| (WatchThreatLingerRemaining > 0.f && CurrentFocal.Equals(WatchThreatLocation, 1.f)));
		if (bFocalIsOurs)
			Controller.ClearFocus(EAIFocusPriority::Gameplay);

		// Touch nothing else — the owning system already set aim/focus/stance for this tick. Only
		// clean up a scripted-aim raise we ourselves applied on a prior tick's watch stance — except
		// while a route leg is active: BTTask_CompanionFollowRoute owns ScriptedAim itself (writes it
		// both ways per Alert/Crouch leg, clears it on exit), so stomping it here on the route's
		// first yield tick would leave that leg walking weapon-down for its whole duration.
		if (bWatchStanceApplied)
		{
			if (!BB.GetValueAsBool(ACompanionAIController::BB_RouteActive))
				Companion.SetScriptedAim(false);
			bWatchStanceApplied = false;
		}
		WatchThreatLingerRemaining = 0.f;
		// A one-shot ClearFocus elsewhere in the posture chain (revive/ready-decay/re-pin/explore)
		// can land in the same tick as this yield — without resetting the cache, the next ambient
		// tick's dedup could skip re-setting the focal point that was just cleared.
		LastAmbientFocalPoint = FVector::ZeroVector;
		return;
	}

	// Tier 1 (F3): watch the nearest visible/lingering threat.
	if (ComputeWatchThreat(Companion, WatchCandidateEnemy, Tuning, DeltaSeconds))
	{
		ApplyWatchFacing(Controller, Companion);
		return;
	}

	// Watch dropped naturally (not a Tier-0 yield) — restore low-ready explicitly since nothing
	// else touches it in the steady out-of-combat state. Also release the Gameplay focal if it is
	// still ours: with ambient facing disabled (the shipped default) nothing downstream ever
	// overwrites it, so an expired watch would otherwise leave the companion staring at — and
	// back-walking its next move toward — the dead watch point forever.
	if (bWatchStanceApplied)
	{
		Companion.SetScriptedAim(false);
		Companion.SetLowReadyAim(true);
		bWatchStanceApplied = false;

		if (Controller.GetFocalPointForPriority(EAIFocusPriority::Gameplay).Equals(WatchThreatLocation, 1.f))
			Controller.ClearFocus(EAIFocusPriority::Gameplay);
		WatchThreatLocation = FVector::ZeroVector;
		WatchedEnemy = nullptr;
	}

	// Tier 2 (F1): ambient path look-ahead / idle attention-yaw facing.
	ApplyAmbientFacing(Controller, Companion, PlayerPawn, Tuning, DeltaSeconds);
}

bool UBTService_UpdateCompanionState::ComputeWatchThreat(
	const ACompanionCharacter& Companion, const AEnemyCharacter* Candidate,
	const UCompanionTuningDataAsset* Tuning, float DeltaSeconds)
{
	const float LingerSeconds = Tuning ? Tuning->WatchThreatLingerSeconds : 6.f;

	if (IsValid(Candidate))
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(CompanionWatchThreatLoS), true);
		Params.AddIgnoredActor(&Companion);
		Params.AddIgnoredActor(Companion.GetCurrentWeapon());
		FHitResult Hit;
		const bool bBlocked = Companion.GetWorld()->LineTraceSingleByChannel(
			Hit, Companion.GetPawnViewLocation(), AITargeting::GetSightLocation(Candidate), ECC_Visibility, Params);
		if (!bBlocked || Hit.GetActor() == Candidate)
		{
			WatchedEnemy = Candidate;
			// Sight location (chest), not actor centre: the controller preserves focal pitch now,
			// and this is the exact point the eye-line trace above just cleared.
			WatchThreatLocation = AITargeting::GetSightLocation(Candidate);
			WatchThreatLingerRemaining = LingerSeconds;
			return true;
		}
	}

	// Not visible (or no candidate at all) — hold the last known location through the linger
	// window: doorway-flicker hysteresis and last-known-position memory while the player retreats.
	// Only a LIVE enemy earns the memory — dead enemies are filtered before NoteWatchCandidate, so
	// "watched enemy died" reaches here identically to "retreated out of LoS", and a 6s raised
	// weapon pinned on a takedown spot is exactly the stale stare this must not produce.
	if (WatchThreatLingerRemaining > 0.f)
	{
		const AEnemyCharacter* Watched = WatchedEnemy.Get();
		const UHealthComponent* WatchedHealth = Watched ? Watched->GetHealthComponent() : nullptr;
		if (!Watched || (WatchedHealth && WatchedHealth->IsDead()))
		{
			WatchThreatLingerRemaining = 0.f;
			return false;
		}

		WatchThreatLingerRemaining = FMath::Max(0.f, WatchThreatLingerRemaining - DeltaSeconds);
		return WatchThreatLingerRemaining > 0.f;
	}

	return false;
}

void UBTService_UpdateCompanionState::ApplyWatchFacing(ACompanionAIController& Controller, ACompanionCharacter& Companion)
{
	Controller.SetFocalPoint(WatchThreatLocation, EAIFocusPriority::Gameplay);

	// Re-checked every tick (not just on first entry): a mode order landing mid-watch flips
	// IsStealthActive() without ever leaving the watch tier, and the stance must follow it —
	// otherwise ordering Stealth during a Normal-mode watch keeps the gun raised (ScriptedAim
	// raises unconditionally regardless of mode), and Stealth->Normal keeps it wrongly lowered.
	const bool bStealthNow = Companion.IsStealthActive();
	if (bWatchStanceApplied && bWatchStanceStealth == bStealthNow) return;
	bWatchStanceApplied = true;
	bWatchStanceStealth = bStealthNow;

	if (bStealthNow)
	{
		// Stealth: stays low-profile, rotates to watch only — weapon-raise would break cover
		// discipline. Explicitly lowers ScriptedAim in case we're re-applying from a Normal-mode watch.
		Companion.SetScriptedAim(false);
		Companion.SetLowReadyAim(true);
	}
	else
	{
		// Normal: weapon raised via the scripted-aim gate (posture stays Exploration — this
		// deliberately does not flip to Combat, which would drag posture-scoring/formation with it).
		Companion.SetLowReadyAim(false);
		Companion.SetScriptedAim(true);
	}
}

float UBTService_UpdateCompanionState::ComputeAttentionYaw(
	const APawn& PlayerPawn, const UCompanionTuningDataAsset* Tuning, float DeltaSeconds)
{
	constexpr float SpeedThresholdSq = 100.f * 100.f;
	const FVector PlayerVel = PlayerPawn.GetVelocity();
	const float BaseYaw = (PlayerVel.SizeSquared2D() > SpeedThresholdSq)
		? PlayerVel.Rotation().Yaw
		: PlayerPawn.GetBaseAimRotation().Yaw;

	const float ScanMax = Tuning ? Tuning->AmbientScanOffsetMaxDeg : 20.f;
	const float IntervalMin = Tuning ? Tuning->AmbientScanIntervalMin : 4.f;
	const float IntervalMax = Tuning ? Tuning->AmbientScanIntervalMax : 8.f;

	AmbientScanTimer += DeltaSeconds;
	if (AmbientScanTimer >= AmbientScanNextInterval)
	{
		AmbientScanOffsetDeg = FMath::FRandRange(-ScanMax, ScanMax);
		AmbientScanNextInterval = FMath::FRandRange(IntervalMin, IntervalMax);
		AmbientScanTimer = 0.f;
	}

	return BaseYaw + AmbientScanOffsetDeg;
}

void UBTService_UpdateCompanionState::ApplyAmbientFacing(
	ACompanionAIController& Controller, const ACompanionCharacter& Companion,
	const APawn* PlayerPawn, const UCompanionTuningDataAsset* Tuning, float DeltaSeconds)
{
	if (Tuning && !Tuning->bAmbientFacingEnabled) return;

	const UPathFollowingComponent* PathFollowing = Controller.GetPathFollowingComponent();
	const bool bPathMoving = IsValid(PathFollowing) && PathFollowing->GetStatus() == EPathFollowingStatus::Moving;
	const FVector PawnLoc = Companion.GetActorLocation();

	if (bPathMoving)
	{
		const float LookAheadDist = Tuning ? Tuning->AmbientLookAheadDistance : 300.f;
		const FVector ToTarget = (PathFollowing->GetCurrentTargetLocation() - PawnLoc).GetSafeNormal();
		if (!ToTarget.IsNearlyZero())
		{
			// View-height look-ahead — the controller preserves focal pitch, and a centre-height
			// point this close would aim the weapon visibly downward the whole move.
			FVector LookAhead = PawnLoc + ToTarget * LookAheadDist;
			LookAhead.Z = Companion.GetPawnViewLocation().Z;
			SetAmbientFocalDeduped(Controller, LookAhead);
		}
		return;
	}

	if (!IsValid(PlayerPawn)) return;

	// Don't-turn-away guard: the player is already looking at the companion — don't spin away mid-glance.
	const FVector ToCompanion = (PawnLoc - PlayerPawn->GetActorLocation()).GetSafeNormal2D();
	const float PlayerViewYaw = PlayerPawn->GetBaseAimRotation().Yaw;
	const float LookAtCompanionAngle = FMath::Abs(FMath::FindDeltaAngleDegrees(PlayerViewYaw, ToCompanion.Rotation().Yaw));
	const float ConeDeg = Tuning ? Tuning->AttentionDontTurnAwayConeDeg : 35.f;
	if (LookAtCompanionAngle <= ConeDeg) return;

	const float AttentionYaw = ComputeAttentionYaw(*PlayerPawn, Tuning, DeltaSeconds);
	const float YawError = FMath::Abs(FMath::FindDeltaAngleDegrees(Companion.GetActorRotation().Yaw, AttentionYaw));
	const float DeadzoneDeg = Tuning ? Tuning->AmbientYawDeadzoneDeg : 35.f;
	if (YawError <= DeadzoneDeg) return;

	const FVector FacingDir = FRotator(0.f, AttentionYaw, 0.f).Vector();
	FVector IdleFocal = PawnLoc + FacingDir * 1000.f;
	IdleFocal.Z = Companion.GetPawnViewLocation().Z; // level gaze — see the look-ahead note above
	SetAmbientFocalDeduped(Controller, IdleFocal);
}

void UBTService_UpdateCompanionState::SetAmbientFocalDeduped(ACompanionAIController& Controller, const FVector& Point)
{
	constexpr float DedupDistSq = 25.f * 25.f;
	if (FVector::DistSquared(LastAmbientFocalPoint, Point) < DedupDistSq) return;

	LastAmbientFocalPoint = Point;
	Controller.SetFocalPoint(Point, EAIFocusPriority::Gameplay);
}
