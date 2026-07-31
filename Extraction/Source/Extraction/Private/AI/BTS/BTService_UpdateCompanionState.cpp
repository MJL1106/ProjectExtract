// BT service — ticks to update all companion blackboard keys (DBNO, combat target, etc).

#include "BTService_UpdateCompanionState.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "AI/AITargetingStatics.h"
#include "AI/CompanionDiag.h"
#include "AI/CompanionSearchRoomPolicy.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISense_Sight.h"
#include "Perception/AISense_Hearing.h" // heard-enemy-gunfire stealth-break signal
#include "CompanionAIController.h" // for LogCompanionAI
#include "CompanionCharacter.h"
#include "Companion/CompanionBarkTypes.h"
#include "EnemyArchetypeData.h" // specialist-archetype bark context
#include "WeaponBase.h"
#include "AI/CompanionTuningDataAsset.h"
#include "Companion/CompanionTypes.h"
#include "Companion/CompanionCommandTypes.h" // ECompanionCommand — non-combat facing Tier-0 yield check
#include "Companion/CompanionRoute.h"        // route facing reference geometry (Tier 2 non-combat facing)
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
#include "EngineUtils.h"            // TActorIterator for combat-enemy proximity scan
#include "TraversalComponent.h"     // stealth-pin enforcement must not crouch mid-traversal
#include "Navigation/PathFollowingComponent.h" // ready-only threat stance yields facing to an active move
#include "AI/CompanionAimValidation.h" // the one seam every threat-derived aim bearing is proven through
#include "HAL/IConsoleManager.h" // companion.AimLog diagnostics CVar
#include "DrawDebugHelpers.h"    // companion.RouteFacingDebug heading arrow

// companion.AimLog 1 — per-service-tick dump of everything that drives the companion's aim/facing
// (target pick + provenance, ready-only threat, takedown/route yields, focal point, low-ready).
// Diagnostic for the stuck-ADS / aims-through-walls reports; Display severity so it shows untagged.
static TAutoConsoleVariable<int32> CVarCompanionAimLog(
	TEXT("companion.AimLog"), 0,
	TEXT("1 = log companion aim/stance state each UpdateCompanionState tick."));

// companion.RouteFacingDebug 1 — draws the heading the route facing tier is actually applying (cyan
// along the route line, magenta while the backtrack latch holds). Its own CVar rather than a re-use
// of companion.RouteDebug, which CompanionRoute.cpp already registers — registering the same name
// twice asserts. Read via GetValueOnGameThread() AT THE POINT OF USE, never through a cached
// IConsoleVariable*: a Live Coding patch left exactly that cached pointer dangling and crashed
// ACompanionRoute::BeginPlay on 2026-06-26 (see CompanionRoute.cpp:53-57).
static TAutoConsoleVariable<int32> CVarCompanionRouteFacingDebug(
	TEXT("companion.RouteFacingDebug"), 0,
	TEXT("1 = draw the companion's applied route-facing heading (cyan = route, magenta = backtracking)."),
	ECVF_Cheat);

// companion.ReviveLog 1 — per-tick revive-window state (body heat, fight-live, hot-dwell, bleedout).
// Read via GetValueOnGameThread() at the point of use, never through a cached IConsoleVariable*:
// a Live Coding patch left exactly that cached pointer dangling (see comment above).
static TAutoConsoleVariable<int32> CVarCompanionReviveLog(
	TEXT("companion.ReviveLog"), 0,
	TEXT("1 = log companion revive-window gating state each tick."));

// companion.FireDebug is registered once in WeaponBase.cpp — re-query by name here to avoid a
// duplicate CVar registration across translation units.
static bool IsCompanionFireDebugEnabled()
{
	static const IConsoleVariable* CVar =
		IConsoleManager::Get().FindConsoleVariable(TEXT("companion.FireDebug"));
	return CVar && CVar->GetInt() != 0;
}

namespace
{
	// Tolerance (cm) for every "is this focal point still the one WE wrote" compare in the file:
	// overwatch end, Tier-0-yield hand-off, route-facing release, watch-drop clean-up, and the
	// sprint-yield releases. Declared up here rather than beside the overwatch constants because
	// TickNode's ready-threat arm needs it too and sits above them.
	constexpr float OverwatchFocalMatchTolerance = 1.f;

	// Player-relative bearing tag for EnemyDirection bark variants (Left/Right/Front/High) — the
	// lines name a direction, so an untagged random pick could call the wrong side.
	FName DirectionBarkContext(const APawn* PlayerPawn, const AActor* Target)
	{
		if (!IsValid(PlayerPawn) || !IsValid(Target)) return NAME_None;

		const FVector ToTarget = Target->GetActorLocation() - PlayerPawn->GetActorLocation();
		// ~24 degrees of elevation reads as "up high" at typical engagement ranges.
		if (ToTarget.Z > ToTarget.Size2D() * 0.45f) return FName(TEXT("High"));

		const float YawDelta = FMath::FindDeltaAngleDegrees(
			PlayerPawn->GetBaseAimRotation().Yaw, ToTarget.Rotation().Yaw);
		if (FMath::Abs(YawDelta) <= 45.f) return FName(TEXT("Front"));
		return YawDelta < 0.f ? FName(TEXT("Left")) : FName(TEXT("Right"));
	}

	// Specialist tag for EnemyArchetype bark variants; NAME_None for baseline infantry (no callout).
	FName SpecialistBarkContext(const AActor* Target)
	{
		const AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Target);
		const UEnemyArchetypeData* DA = IsValid(Enemy) ? Enemy->GetArchetypeData() : nullptr;
		if (!IsValid(DA)) return NAME_None;

		switch (DA->Archetype)
		{
		case EEnemyArchetype::Sniper:    return FName(TEXT("Sniper"));
		case EEnemyArchetype::Heavy:     return FName(TEXT("Heavy"));
		case EEnemyArchetype::Grenadier: return FName(TEXT("Grenadier"));
		case EEnemyArchetype::Rusher:    return FName(TEXT("Rusher"));
		default: return NAME_None;
		}
	}
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
	TRACE_CPUPROFILER_EVENT_SCOPE(Extraction_AI_CompanionStateService);

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

		// The player going down is the companion's own crisis — one reaction per down stint.
		if (bPlayerDBNO && !bPlayerDownBarked)
		{
			bPlayerDownBarked = true;
			Companion->Bark(ECompanionBarkType::PlayerDownReaction);
		}
		else if (!bPlayerDBNO)
			bPlayerDownBarked = false;

		// Low-HP warning, edge-triggered with hysteresis so it can't nag every service tick.
		if (!bPlayerDBNO)
		{
			if (const UHealthComponent* PlayerHealth = PlayerIface->GetHealthComponent())
			{
				constexpr float HurtWarnFraction = 0.35f;
				constexpr float HurtRearmFraction = 0.6f;
				const float PlayerHealthFrac = PlayerHealth->GetHealthPercent();
				if (PlayerHealthFrac > 0.f && PlayerHealthFrac <= HurtWarnFraction && !bPlayerHurtBarked)
				{
					bPlayerHurtBarked = true;
					Companion->Bark(ECompanionBarkType::PlayerHurtWarning);
				}
				else if (PlayerHealthFrac >= HurtRearmFraction)
					bPlayerHurtBarked = false;
			}
		}
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
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Extraction_AI_CompanionPerceptionGather);
		Perception->GetCurrentlyPerceivedActors(UAISense_Sight::StaticClass(), PerceivedActors);
	}

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

	// Widened acquisition: an enemy that has engaged, damaged, or is actively hunting the COMPANION
	// counts as a valid target even when it has never detected the player. Without this the companion
	// ignored a Searching enemy that had just shot it — "don't start fights the player is avoiding"
	// was being applied to a fight the enemy had already started.
	//
	// bWidenGate is Stealth's SECOND structural layer, not a convenience: the absolute veto further
	// down (BestTarget = nullptr while stealth is unbroken) is the first. Both stay — Stealth must be
	// byte-for-byte unchanged, and forcing the widened clause false here means an enemy that damaged
	// the companion in a previous mode can never even enter selection while sneaking.
	const bool bWidenGate = (Mode != ECompanionMode::Stealth);
	const float EngagedCompanionMemory = RangeTuning ? RangeTuning->EngagedCompanionMemorySeconds : 4.f;

	// Combat mode is weapons-free, so the widened clause can never change its answer there. Hoisted
	// so both scan loops can short-circuit the whole permission test in that mode: HasEngagedCompanion
	// is a controller cast, two TMap::Finds and a cloak check that may reach the Director subsystem,
	// and running it per unaware candidate only to discard the result is pure waste.
	const bool bWeaponsFree = CompanionSearchRoomPolicy::CanEngageUnawareEnemy(Mode);

	// Shared ignore list for per-candidate eye-line traces (self + weapon + attached actors) — matches
	// the final LoS filter and the combat task's trace so acquisition and firing agree on visibility.
	//
	// Plus every same-team pawn, taken from the weapon's own friendly-fire list: AWeaponBase excludes
	// team-mates from the hitscan outright, so a round passes straight THROUGH the player and the other
	// companion. A team-mate standing on the line is therefore not an obstruction to a shot, and
	// dropping a candidate for one was a false negative. Widened here as well as in the combat task
	// because the two are required to agree — leaving acquisition friendly-blind while firing is not
	// would let the VIP engage targets it could never have selected. Inline capacity covers
	// attachments plus a single-player team (player + up to two companions) without a heap alloc.
	TArray<AActor*, TInlineAllocator<8>> SelectFireTraceIgnored;
	Companion->ForEachAttachedActors([&](AActor* A) { SelectFireTraceIgnored.Add(A); return true; });
	if (AWeaponBase* SelectWeapon = Companion->GetCurrentWeapon())
		SelectFireTraceIgnored.Append(SelectWeapon->GetFriendlyFireIgnoreList());

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
		for (AActor* Attached : SelectFireTraceIgnored)
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

	TRACE_CPUPROFILER_EVENT_MANUAL_START("Extraction_AI_CompanionPerceivedCandidateScan");
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

		// Defensive/Stealth: don't engage enemies that haven't detected the player (no first shot) —
		// unless they have already engaged the COMPANION, which is a fight the player is not avoiding.
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
			// Exposure changes enemy sight, not permission to acquire an unaware target. Two
			// short-circuits keep the widened clause cheap: this else already restricts it to
			// candidates that fail today, and !bWeaponsFree drops it entirely in Combat mode, where
			// CanAcquireTarget would return true on its own clause regardless.
			else if (!bWeaponsFree && !CompanionSearchRoomPolicy::CanAcquireTarget(Mode, false,
				bWidenGate && Enemy->HasEngagedCompanion(Companion, EngagedCompanionMemory)))
				continue;
		}

		ConsiderCandidate(Actor, DistSq);
	}
	TRACE_CPUPROFILER_EVENT_MANUAL_END();

	// --- Proximity 360° awareness: detect enemies in any direction at close range ---
	// Supplements the sight cone (180° forward). Runs at the same 0.25s service cadence; one
	// small-radius sphere overlap per tick is negligible cost.
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Extraction_AI_CompanionProximityScan);

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
					// Same no-first-shot policy — and the same companion-aware widening, with the
					// same Combat-mode short-circuit — as the sight pass. This is the channel that
					// catches an enemy that shot the companion from outside its 180-degree sight cone.
					else if (!bWeaponsFree && !CompanionSearchRoomPolicy::CanAcquireTarget(Mode, false,
						bWidenGate && ProxEnemy->HasEngagedCompanion(Companion, EngagedCompanionMemory)))
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
		TRACE_CPUPROFILER_EVENT_SCOPE(Extraction_AI_CompanionPlayerThreatScan);

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
	TRACE_CPUPROFILER_EVENT_MANUAL_START("Extraction_AI_CompanionTargetResolution");
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
	TRACE_CPUPROFILER_EVENT_MANUAL_END();

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
		TRACE_CPUPROFILER_EVENT_SCOPE(Extraction_AI_CompanionStealthChecks);

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

	// Publish the live-fight signal: consumed by the watch-tier fallback below and the follow
	// task's slot bias (fight-aware follow). Refreshed every service tick while any alerted enemy
	// is known near the party, so consumers treat freshness as the hold. Deliberately after the
	// stealth gate — unbroken stealth zeroes bHasAlertedThreat and must not go fight-postured.
	if (bHasAlertedThreat)
		Controller->NoteAlertedThreat(AlertedThreatLocation);

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
		TRACE_CPUPROFILER_EVENT_SCOPE(Extraction_AI_CompanionFinalTargetLOS);

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
						// Suppress the bark when the LoS blocker is the companion's own wall —
						// measured to the impact point (a long wall's pivot can sit far away).
						const float SelfOccludeRadius = RangeTuning ? RangeTuning->SelfOcclusionBarkSuppressRadius : 250.f;
						const bool bSelfOccluded = SelfOccludeRadius > 0.f && LosHit.bBlockingHit
							&& FVector::Dist(AimOrigin, LosHit.ImpactPoint) <= SelfOccludeRadius;
						if (!bSelfOccluded)
							Companion->Bark(ECompanionBarkType::LostContact);
						else if (bDebugLogging)
							UE_LOG(LogCompanionAI, Log, TEXT("%s: LostContact bark suppressed — self-occluded by %s at %.0fcm"),
								*Companion->GetName(), *GetNameSafe(LosHit.GetActor()), FVector::Dist(AimOrigin, LosHit.ImpactPoint));
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

	// Takedown commitment override — outranks stickiness, the flanker steal and the body-charger
	// steal. A commanded takedown that tore down with its victim still alive latches that victim on
	// the companion; without this the companion dropped the enemy it was already lined up on the
	// instant the player's missed shot woke the level, joined the player's fight, and never went
	// back. Self-discharges the moment the victim dies (or on the task's hold timeout), so "finish
	// yours, then help me" falls out for free. Placed last so nothing downstream can re-steal it.
	if (AActor* ForcedTarget = Companion->GetForcedCombatTarget())
	{
		if (ForcedTarget != BestTarget)
		{
			if (bDebugLogging)
				UE_LOG(LogCompanionAI, Log, TEXT("%s: takedown commitment holds %s (over %s)"),
					*Companion->GetName(), *ForcedTarget->GetName(), *GetNameSafe(BestTarget));
			BestTarget = ForcedTarget;
			BestDistSq = FVector::DistSquared(MyLocation, ForcedTarget->GetActorLocation());

			// The main LOS filter above ran against the old BestTarget; refresh the anim-side
			// mirror for this override so the non-cover aim fade has an honest signal. The ignore
			// set must match SelectFireTraceIgnored (self + weapon + attached actors + team-mates)
			// so acquisition and firing agree on visibility -- the comment at ~308-321 states that
			// agreement is load-bearing.
			FCollisionQueryParams ForcedLosParams(SCENE_QUERY_STAT(CompanionForcedTargetLoS), true);
			ForcedLosParams.AddIgnoredActor(Companion);
			ForcedLosParams.AddIgnoredActor(Companion->GetCurrentWeapon());
			for (AActor* Attached : SelectFireTraceIgnored)
				ForcedLosParams.AddIgnoredActor(Attached);
			FHitResult ForcedLosHit;
			const bool bForcedBlocked = Companion->GetWorld()->LineTraceSingleByChannel(
				ForcedLosHit, Companion->GetPawnViewLocation(),
				AITargeting::GetSightLocation(ForcedTarget), ECC_Visibility, ForcedLosParams);
			Companion->SetHasTargetLOS(!bForcedBlocked || ForcedLosHit.GetActor() == ForcedTarget);
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

		// Mid-fight target switch — call the new threat out (specialists by name, else a bearing).
		// First acquisition is silent here; the posture flip below owns the contact call.
		if (BestTarget != ExistingTarget && ExistingTarget != nullptr)
		{
			const FName Specialist = SpecialistBarkContext(BestTarget);
			if (!Specialist.IsNone())
				Companion->Bark(ECompanionBarkType::EnemyArchetype, Specialist);
			else
				Companion->Bark(ECompanionBarkType::EnemyDirection, DirectionBarkContext(PlayerPawn, BestTarget));
		}

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
			// No enemies at all -- combat is ending. Release cover so the companion stands up and
			// the cover slot becomes available for the next engagement. Cover is cleared only when
			// no target remains (combat ending); a live-but-temporarily-unperceived target retains
			// both the BB target and the cover slot so the CoverSwitchMonitor stays active.
			//
			// Exception: a commanded cover hold IS "holding cover with no combat target" by
			// definition. Clearing the cover keys here would wipe the order's own state within a
			// quarter second of it being issued, causing MoveToCoverPoint to read invalid cover
			// and the entire TakeCover branch to silently fail.
			if (!Companion->IsCommandedCoverHoldActive())
			{
				BB->SetValueAsBool(HasCoverPositionKey.SelectedKeyName, false);
				if (CoverTargetKey.SelectedKeyType != nullptr)
					BB->ClearValue(CoverTargetKey.GetSelectedKeyID());
			}
		}
	}

	TRACE_CPUPROFILER_EVENT_MANUAL_START("Extraction_AI_CompanionPostureAndFacing");

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
		// Same ignore set as the selection trace above so acquisition, firing and this agree on
		// visibility. Load-bearing now that this flag also gates the wave hold: a companion's own
		// attached actor blocking the line would otherwise read as "sees nothing" and release the hold.
		for (AActor* Attached : SelectFireTraceIgnored)
			ReadyLosParams.AddIgnoredActor(Attached);
		FHitResult ReadyLosHit;
		const bool bReadyLosBlocked = Companion->GetWorld()->LineTraceSingleByChannel(
			ReadyLosHit, Companion->GetPawnViewLocation(),
			AITargeting::GetSightLocation(AlertedThreatEnemy), ECC_Visibility, ReadyLosParams);
		if (bReadyLosBlocked && ReadyLosHit.GetActor() != AlertedThreatEnemy)
			bReadyOnlyThreat = false;
	}

	// Recomputed unconditionally (not inside the decay branch) so the hold clears the tick a wave
	// ends no matter which posture branch the companion is sitting in. Placed after the threat
	// resolution above because the quiet-release timer keys off "nothing known", not just "no target".
	//
	// Fed bReadyOnlyThreat, NOT the raw bHasAlertedThreat: the raw flag is radius-only and sees
	// through walls, so an ally with no eye-line to the fight reset the quiet timer every tick and
	// the release could never fire — the extraction VIP froze at cover, weapon up, for a whole
	// defence wave. bReadyOnlyThreat is the same signal with the LoS trace above applied, and
	// UpdateWaveHold ORs it with bHasTarget anyway, so nothing is lost by using the honest one.
	const bool bWaveHold = UpdateWaveHold(
		*Companion, PlayerPawn, RangeTuning, bHasTarget, bReadyOnlyThreat, DeltaSeconds);

	// --- Commanded cover hold release ---
	// Release when: (a) player moves beyond the leash from the cover anchor, (b) companion
	// goes DBNO, or (c) a different command is issued (the BB write already happened above).
	// Modelled on the wave-hold leash above.
	if (Companion->IsCommandedCoverHoldActive())
	{
		bool bReleaseCoverHold = false;

		// (a) Player leash break.
		if (IsValid(PlayerPawn))
		{
			const float LeashSq = FMath::Square(Companion->GetCommandedCoverHoldLeash());
			const float DistSq = FVector::DistSquared(
				PlayerPawn->GetActorLocation(), Companion->GetCommandedCoverHoldAnchor());
			if (DistSq > LeashSq) bReleaseCoverHold = true;
		}

		// (b) Companion DBNO — unreachable here (TickNode early-returns at :160 on DBNO), but
		// kept as a backstop. The primary DBNO release is in EnterDBNO()'s teardown block.
		if (Companion->GetIsCompanionDBNO()) bReleaseCoverHold = true;

		// (c) Different command issued (the command BB key will be something other than TakeCover
		// or None when a new command has been sent).
		{
			const uint8 CurrentCmd = BB->GetValueAsEnum(ACompanionAIController::BB_CompanionCommand);
			const bool bCmdIsTakeCover = CurrentCmd == static_cast<uint8>(ECompanionCommand::TakeCover);
			const bool bCmdIsNone = CurrentCmd == static_cast<uint8>(ECompanionCommand::None);
			if (!bCmdIsTakeCover && !bCmdIsNone) bReleaseCoverHold = true;
		}

		// (d) Never reached the cover — the hold must not park the companion in the open.
		// MoveToCoverPoint can decline/fail on several paths the commit grant does not bypass.
		{
			static constexpr float HoldArriveRadius = 250.f;
			static constexpr float HoldArriveTimeout = 12.f;
			if (FVector::DistSquared2D(Companion->GetActorLocation(), Companion->GetCommandedCoverHoldAnchor())
				> FMath::Square(HoldArriveRadius))
			{
				CommandedCoverHoldUnreachedTime += DeltaSeconds;
				if (CommandedCoverHoldUnreachedTime > HoldArriveTimeout) bReleaseCoverHold = true;
			}
			else
			{
				CommandedCoverHoldUnreachedTime = 0.f;
			}
		}

		// (e) Combat grace: once a fight starts the hold is honoured for N seconds then released.
		// Also releases when combat ends after the grace was armed (target appeared then died) --
		// without this, a post-fight companion freezes at stale cover with no release path.
		if (bHasTarget)
		{
			Companion->StampCommandedCoverCombatStart();
			if (CommandedCoverCombatGraceSeconds > 0.f)
			{
				const float CombatStart = Companion->GetCommandedCoverCombatStartTime();
				if (CombatStart > 0.f && GetWorld()
					&& (GetWorld()->GetTimeSeconds() - CombatStart) > CommandedCoverCombatGraceSeconds)
				{
					bReleaseCoverHold = true;
				}
			}
		}
		else if (Companion->IsCommandedCoverCombatGraceArmed())
		{
			// Combat was seen during this hold and has now ended. Release unconditionally --
			// no task is maintaining the pose, and none of the other release conditions can fire.
			bReleaseCoverHold = true;
		}

		if (bReleaseCoverHold)
		{
			Companion->ClearCommandedCoverHold();
			CommandedCoverHoldUnreachedTime = 0.f;
			if (bDebugLogging)
				UE_LOG(LogCompanionAI, Log, TEXT("%s: commanded cover hold RELEASED"), *Companion->GetName());
		}
	}

	// --- Fight threat memory (feeds the post-combat overwatch aim pick) ---
	// First-contact records once per fight (flag survives mid-fight target churn and LoS gaps —
	// PrevCombatTarget resets on every unperceived tick, so it can't gate this); the rolling
	// last-threat point tracks whichever target is current. Both clear only when the fight fully
	// ends (Exploration flip / stealth re-pin).
	if (bHasTarget)
	{
		LastThreatLocation = TargetAfterUpdate->GetActorLocation();
		bHasLastThreatLocation = true;
		if (!bHasFightFirstContact)
		{
			FightFirstContactLocation = LastThreatLocation;
			bHasFightFirstContact = true;
			// Snapshot the director's corpse FIFO so ResolveRecentCorpseCentroid can filter
			// to kills from THIS fight only. Without this, a previous room's 6 bodies outnumber
			// the current fight's 2 and drag the centroid back through the wall.
			const UWorld* FightWorld = Companion->GetWorld();
			const UEnemyDirectorSubsystem* FightDirector = FightWorld
				? FightWorld->GetSubsystem<UEnemyDirectorSubsystem>() : nullptr;
			PreFightCorpses = FightDirector ? FightDirector->GetCorpses() : TArray<TWeakObjectPtr<AEnemyCharacter>>();
		}
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

	// True only when the combat-decay branch below evaluates this tick — any other branch winning
	// (new target, ready threat, stealth re-pin) means a new owner has aim/focus, so a still-active
	// overwatch hold must release without touching focus (see the safety end after the chain).
	bool bCombatDecayRanThisTick = false;

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

			// Contact call for the fresh fight — a crowd beats a specialist warning beats the
			// generic contact. One bark; the arbitration gap eats anything stacked after it.
			constexpr int32 MultipleContactsThreshold = 3;
			int32 PerceivedEnemyCount = 0;
			for (const AActor* Perceived : PerceivedActors)
				if (Cast<AEnemyCharacter>(Perceived)) ++PerceivedEnemyCount;

			const FName Specialist = SpecialistBarkContext(TargetAfterUpdate);
			if (PerceivedEnemyCount >= MultipleContactsThreshold)
				Companion->Bark(ECompanionBarkType::MultipleContacts);
			else if (!Specialist.IsNone())
				Companion->Bark(ECompanionBarkType::EnemyArchetype, Specialist);
			else
				Companion->Bark(ECompanionBarkType::ContactCombat);
		}
		bLoweredOnTargetLoss = false;
		// A live target/threat ends any post-fight hold outright, and the focal has to be released
		// HERE rather than assumed gone: of the three arms below only the revive one clears it
		// unconditionally — the ready-threat arm writes a focal only when !bAimOwnedElsewhere, and
		// the plain combat arm touches posture alone. Released before those arms run so an owner that
		// does write its own focal overwrites a cleared channel, and compare-guarded so this is a
		// no-op on the arms that already claimed it. Dropped here rather than left to expire because
		// the flag must never outlive the arm edge that set it: posture can flip to Exploration on
		// the same tick a longer hold is still nominally running, after which the decay branch that
		// owns the falling edge stops evaluating entirely.
		if (bWeaponUpHoldAsserted)
		{
			bWeaponUpHoldAsserted = false;
			ReleaseWeaponUpHoldFocal(*Controller);
		}
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
				// Travelling yields facing to travel. Travelling = sprinting OR closing a follow gap
				// (IsStrafingForFocus returns false for both). The locomotion blendspace's only
				// sprint-speed sample is forward-only, so a travelling companion with a threat focal
				// held plays running-forwards legs under a side-on body. Releasing the Gameplay focal
				// hands facing to path-following's Move-priority focus, which is the direction of
				// travel. Compare-guarded like every other focal release here so a focal another
				// system wrote is never stomped. The weapon stays up either way.
				// NOT gated on the live-combat actor-target focus: that is owned by the combat task's
				// own SetFocus(Actor) and must never be released by this tier.
				const bool bTravellingToClose = !Companion->IsStrafingForFocus()
					&& (Companion->IsSprinting() || Companion->IsFollowCatchupPace());
				if (bTravellingToClose)
				{
					if (Controller->GetFocalPointForPriority(EAIFocusPriority::Gameplay)
						.Equals(AlertedThreatLocation, OverwatchFocalMatchTolerance))
						Controller->ClearFocus(EAIFocusPriority::Gameplay);
				}
				else
					Controller->SetFocalPoint(AlertedThreatLocation, EAIFocusPriority::Gameplay);
			}
		}
		else
		{
			Companion->SetLowReadyAim(false);
		}
		OutOfCombatTimer = 0.f;

		// Steady-voice reassurance under sustained pressure (heavy suppression, or the squad's
		// guns concentrated on the player). Attempt-paced; the bark set's cooldown paces further.
		if (const UWorld* BarkWorld = Companion->GetWorld())
		{
			constexpr float ReassuranceAttemptInterval = 20.f;
			constexpr float ReassuranceSuppressionThreshold = 0.5f;
			constexpr int32 ReassurancePlayerFocusThreshold = 2;
			const float NowSeconds = BarkWorld->GetTimeSeconds();
			if (NowSeconds >= NextReassuranceBarkTime
				&& (Companion->GetSuppression01() > ReassuranceSuppressionThreshold
					|| Companion->GetPlayerFocusedEnemyCount() >= ReassurancePlayerFocusThreshold))
			{
				NextReassuranceBarkTime = NowSeconds + ReassuranceAttemptInterval;
				Companion->Bark(ECompanionBarkType::Reassurance);
			}
		}
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
			ResetFightThreatMemory(); // fight fully over — next acquisition starts a new fight
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
		bCombatDecayRanThisTick = true;
		const bool bTakedownOwnsAim = Companion->IsCommandedTakedownArmed()
			|| Companion->IsCommandedTakedownExecuting()
			|| Companion->IsTakedownMontagePlaying();

		// Post-fight overwatch: instead of dropping the gun the frame the last enemy dies, hold it
		// on the chokepoint/bearing the threat came from until the player moves on (or the timer
		// cap). While it owns aim, both the immediate lower and the Exploration decay are paused —
		// the hold must not be cut short by the posture flip at ExploreReturnDelay < MaxTime.
		const bool bOverwatchOwnsAim = UpdatePostCombatOverwatch(
			*Controller, *Companion, PlayerPawn, RangeTuning, bTakedownOwnsAim, bPlayerDBNO, DeltaSeconds);

		if (!bOverwatchOwnsAim)
		{
			// Drop the STARE the moment the hold releases (or never engaged) — the ExploreReturnDelay
			// below is BT posture stability, not a reason to keep pointing at a dead enemy's bearing.
			// Edge-guarded by bLoweredOnTargetLoss (reset whenever a target/threat is live) so the
			// edge still fires after a branch switch mid-accrual (e.g. stealth re-pin -> mode change).
			// Yields to a commanded takedown, which owns aim/focus while armed.
			if (bWaveHold && !bTakedownOwnsAim)
			{
				// Wave hold keeps the gun up through the gaps between squad spawns: the fight is
				// demonstrably not over, so the usual "last target died, lower it" edge must not fire.
				// Overwatch normally owns this, but its anchor radius can legitimately be exceeded
				// while the far wider wave leash still holds, leaving nobody to raise the weapon.
				//
				// "Nobody to aim" was literal, and it is what made the allies-aim-through-walls report
				// visible: this arm raised the weapon and wrote NO focal at all, so the gun came up over
				// whatever the combat task had last left in the focus channel — historically a live
				// SetFocus(Actor) on the enemy, which the controller re-resolves to that pawn's CURRENT
				// position every tick, pitch preserved, straight through geometry. Worst for the
				// extractee, the ally most likely to sit outside the overwatch anchor while inside the
				// wave leash. Arm the same forward focal the post-fight hold uses instead: it points
				// down the pawn's own forward, it is what the anim's bFocusLive gate actually needs, and
				// it is already in the Tier-0 hand-off compare set.
				//
				// Travelling is refused for the same reason the post-fight hold refuses it — the focal
				// is a FIXED world point, and travelling carries the body past it and then turns it
				// around to keep facing it. The raise itself is unchanged either way.
				const bool bWaveTravelling = Companion->IsSprinting() || Companion->IsFollowCatchupPace();
				if (bWaveTravelling && bWeaponUpHoldAsserted)
				{
					bWeaponUpHoldAsserted = false;
					ReleaseWeaponUpHoldFocal(*Controller);
				}
				else if (!bWaveTravelling && !bWeaponUpHoldAsserted)
				{
					bWeaponUpHoldAsserted = true;
					// Stale actor-aim backstop, mirrors the lower path and overwatch entry: the anim's
					// actor-target branch bypasses low-ready entirely, so a leftover aim target is its
					// own through-wall stare regardless of what the focal says.
					Companion->SetAimTarget(nullptr);
					ArmWeaponUpHoldFocal(*Controller, *Companion);
					// Deliberately NOT stamping WeaponUpHoldStartTime: this raise is governed by the
					// wave hold, not by GetWeaponUpHoldSeconds. Leaving the stamp at its sentinel means
					// that when the wave hold drops and the sibling arm below inherits the flag, the
					// elapsed compare reads "long expired" and releases cleanly — the documented safe
					// reading of a stale stamp.
				}
				Companion->SetLowReadyAim(false);
			}
			else if (!bTakedownOwnsAim)
			{
				const UWorld* HoldWorld = Companion->GetWorld();
				const float HoldNow = IsValid(HoldWorld) ? HoldWorld->GetTimeSeconds() : 0.f;
				// No world means no clock, so no hold — fall back to the plain edge-lower rather than
				// start a timer that can never expire.
				const float HoldSeconds = IsValid(HoldWorld)
					? GetWeaponUpHoldSeconds(*Companion, RangeTuning) : 0.f;

				// Post-fight weapon-up hold. The aim-target clear and the bLoweredOnTargetLoss latch
				// still fire on the FIRST no-target tick, exactly as the plain lower this replaces
				// did. Both are load-bearing:
				//   - UpdatePostCombatOverwatch's entry veto reads the latch, so moving it later
				//     would silently re-open overwatch entry for the whole decay window;
				//   - a stale aim target forces the ADS pose regardless of low-ready (the anim's
				//     actor-target branch bypasses it — see the stealth re-pin branch above), so
				//     keeping the target IS the "stares at where the body dropped" complaint.
				// What does NOT happen on that edge any more is a bare ClearFocus: the anim raises
				// the weapon only on IsScriptedAiming() or (Combat && !IsLowReadyAim() && bFocusLive),
				// and bFocusLive is simply "the Gameplay focal is a valid location". Clearing it here
				// destroyed the hold's own precondition, so with ambient facing shipped disabled and
				// no route reference installed the gun never actually came up. ArmWeaponUpHoldFocal
				// gives the hold a facing of its own instead — straight out along the pawn's forward,
				// which keeps the weapon up WITHOUT re-pointing it at the dead enemy's bearing.
				if (!bLoweredOnTargetLoss)
				{
					bLoweredOnTargetLoss = true;
					WeaponUpHoldStartTime = HoldNow;
					Companion->SetAimTarget(nullptr);

					// Never arm the hold on a travelling companion (sprinting OR closing a follow
					// gap). Its focal is a FIXED world point sampled once from the pawn's forward, so
					// travelling carries the body past it within a second and then turns it around to
					// keep facing it — at those speeds the locomotion blendspace blends into the
					// forward-only sample. A travelling companion faces travel.
					bWeaponUpHoldAsserted = HoldSeconds > 0.f
						&& !Companion->IsSprinting() && !Companion->IsFollowCatchupPace();
					if (bWeaponUpHoldAsserted)
					{
						ArmWeaponUpHoldFocal(*Controller, *Companion);
					}
					else
					{
						// No hold configured (Stealth, or a designer zeroing the lever): the
						// original single-edge lower, unchanged.
						Controller->ClearFocus(EAIFocusPriority::Gameplay);
						Companion->SetLowReadyAim(true);
					}
				}

				// Posture is written only while the hold is live, plus exactly one write on the
				// falling edge — after which this branch goes silent for the rest of the decay
				// window. Asserting it every tick regardless fought ApplyWatchFacing, which sets
				// low-ready false for a Defensive watch and is itself edge-guarded: the pair produced
				// two transitions per service tick, i.e. eight OnLowReadyAimChanged broadcasts a
				// second into whatever Blueprint graphs bind that delegate.
				if (bWeaponUpHoldAsserted)
				{
					// Travelling STARTING mid-hold ends it, for the same reason the arm edge refuses
					// one (see above): the hold's focal is a fixed point that travelling immediately
					// runs past. Released through the normal path so the focal drop stays compare-guarded.
					if (Companion->IsSprinting() || Companion->IsFollowCatchupPace())
					{
						bWeaponUpHoldAsserted = false;
						Companion->SetLowReadyAim(true);
						ReleaseWeaponUpHoldFocal(*Controller);
					}
					// HoldSeconds > 0 is re-tested, not assumed from the arm edge: if the world goes
					// null on a LATER tick both HoldNow and HoldSeconds collapse to 0, and the elapsed
					// compare alone reads (0 - StartTime) < 0 as TRUE against any real stamp — the hold
					// would then assert weapon-up forever and never reach its own release.
					else if (HoldSeconds > 0.f && (HoldNow - WeaponUpHoldStartTime) < HoldSeconds)
					{
						Companion->SetLowReadyAim(false);
					}
					else
					{
						bWeaponUpHoldAsserted = false;
						Companion->SetLowReadyAim(true);
						ReleaseWeaponUpHoldFocal(*Controller);
					}
				}
			}

			// Posture stays Combat for the whole wave. Zeroed rather than merely left un-accrued: a
			// banked accrual would fire the instant the wave ended and drop the ally to Exploration
			// on the same tick the final kill landed. Falls through to the rest of the tick (facing
			// arbitration, scoring weights, the profiler scope END) — no early return here.
			if (bWaveHold)
			{
				OutOfCombatTimer = 0.f;
			}
			else
			{
				OutOfCombatTimer += DeltaSeconds;
				if (OutOfCombatTimer >= ExploreReturnDelay)
				{
					if (bDebugLogging)
						UE_LOG(LogCompanionAI, Log, TEXT("%s: posture %s -> %s"),
							*Companion->GetName(), PostureName(CurrentPosture), PostureName(ECompanionPosture::Exploration));
					Companion->SetPosture(ECompanionPosture::Exploration);
					// Only after a real fight (first-contact memory set) — an alert blip that never
					// engaged doesn't earn an "all clear".
					if (bHasFightFirstContact)
						Companion->Bark(ECompanionBarkType::AreaClear);
					// Expiry mirrors the edge-lower's takedown yield — see the re-pin branch.
					if (!bTakedownOwnsAim)
					{
						Companion->SetLowReadyAim(true);
						Controller->ClearFocus(EAIFocusPriority::Gameplay);
					}
					OutOfCombatTimer = 0.f;
					ResetFightThreatMemory();
				}
			}
		}
	}

	// Overwatch safety end: the hold only ever lives inside the combat-decay branch (or the
	// wave-hold extension below). If any other branch ran this tick (new target, ready threat,
	// stealth re-pin, stealth pin) AND the wave-hold extension didn't claim the tick, that owner
	// has aim/focus -- drop our scripted-aim raise and state. The focal release inside the end is
	// compare-guarded against our own aim point, so an owner that already wrote theirs is untouched.
	//
	// Wave-hold overwatch extension: when the wave hold is active, no target, and the combat-decay
	// branch did NOT run this tick (posture decayed to Exploration or similar), still run overwatch
	// so the ally covers the door between squads instead of standing lifeless. The overwatch's own
	// entry conditions (player anchored, not moving, aim point computable) keep it sane.
	bool bWaveOverwatchRanThisTick = false;
	if (bWaveHold && !bHasTarget && !bCombatDecayRanThisTick)
	{
		const bool bTakedownOwnsAim = Companion->IsCommandedTakedownArmed()
			|| Companion->IsCommandedTakedownExecuting()
			|| Companion->IsTakedownMontagePlaying();

		const bool bOwnsAim = UpdatePostCombatOverwatch(
			*Controller, *Companion, PlayerPawn, RangeTuning, bTakedownOwnsAim, bPlayerDBNO, DeltaSeconds);
		if (bOwnsAim)
		{
			bWaveOverwatchRanThisTick = true;
			// Only raise when the hold is actively asserting a bearing. When
			// OverwatchRefreshFailures > 0 the hold is in its holding-lowered state (aim point
			// failed validation, debouncing before ending). Re-raising here would undo the
			// SetLowReadyAim(true) that UpdatePostCombatOverwatch just wrote, flipping it true->false
			// every tick and firing ~8 OnLowReadyAimChanged broadcasts per second.
			if (OverwatchRefreshFailures == 0)
				Companion->SetLowReadyAim(false);
		}
	}

	// Wave-hold overwatch release edge: the extension stopped owning aim (wave ended, leash break,
	// player DBNO, moving-break) while posture is NOT Combat -- the decay-branch lower can't run
	// in Exploration, so we must restore low-ready and clear focus ourselves.
	if (bWaveOverwatchOwnedAimLastTick && !bWaveOverwatchRanThisTick && !bCombatDecayRanThisTick)
	{
		const ECompanionPosture CurrentPostureCheck = Companion->GetPosture();
		const bool bTakedownOwnsAimNow = Companion->IsCommandedTakedownArmed()
			|| Companion->IsCommandedTakedownExecuting()
			|| Companion->IsTakedownMontagePlaying();
		if (!bHasTarget && CurrentPostureCheck != ECompanionPosture::Combat && !bTakedownOwnsAimNow)
		{
			Companion->SetLowReadyAim(true);
			Companion->SetAimTarget(nullptr);
			Controller->ClearFocus(EAIFocusPriority::Gameplay);
			bLoweredOnTargetLoss = true;
		}
	}
	bWaveOverwatchOwnedAimLastTick = bWaveOverwatchRanThisTick;

	if (bOverwatchActive && !bCombatDecayRanThisTick && !bWaveOverwatchRanThisTick)
		EndPostCombatOverwatch(*Controller, *Companion, bHasTarget || bReadyOnlyThreat);

	// --- Downtime chatter (priority-0 ambient; the subsystem's lull gate drops anything that
	// would land near real combat audio) ---
	if (CurrentPosture == ECompanionPosture::Exploration && !bHasTarget && !bReadyOnlyThreat
		&& !Companion->IsStealthActive() && !bPlayerDBNO)
	{
		if (const UWorld* AmbientWorld = Companion->GetWorld())
		{
			const float NowSeconds = AmbientWorld->GetTimeSeconds();
			if (NextAmbientBarkTime < 0.f)
			{
				// First tick: push the first attempt out a full interval — no chatter on spawn.
				NextAmbientBarkTime = NowSeconds + AmbientAttemptIntervalSeconds;
			}
			else if (NowSeconds >= NextAmbientBarkTime)
			{
				NextAmbientBarkTime = NowSeconds + AmbientAttemptIntervalSeconds;
				const bool bPlayerMoving = IsValid(PlayerPawn)
					&& PlayerPawn->GetVelocity().SizeSquared2D() > FMath::Square(150.f);
				Companion->Bark(bPlayerMoving ? ECompanionBarkType::Following : ECompanionBarkType::IdleAmbient);
			}
		}
	}

	// --- Non-combat facing arbitration (F1 ambient + F3 watch-threats) ---
	// Runs after the posture chain settles so it sees this tick's final bHasTarget/bReadyOnlyThreat
	// and yields cleanly to every branch above that already claimed aim/focus this tick.
	UpdateNonCombatFacing(*Controller, *Companion, *BB, PlayerPawn, RangeTuning,
		bHasTarget, bReadyOnlyThreat, WatchCandidateEnemy, DeltaSeconds);

	// --- Stance backstop ---
	// Runs after the posture chain so it sees this tick's settled bHasTarget. See UpdateStanceBackstop.
	UpdateStanceBackstop(*Companion, *BB, bHasTarget, DeltaSeconds);

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
	TRACE_CPUPROFILER_EVENT_MANUAL_END();

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
		TRACE_CPUPROFILER_EVENT_SCOPE(Extraction_AI_CompanionReviveWindow);

		bool bReviveWindowOpen = false;

		// Nearest living hostile to the BODY, published on the companion at the end of this block for
		// the rescue approach's sprint-vs-jog decision (BTTask_FollowPlayer). Negative = none found /
		// not measured this tick. Filled from the sweep below rather than a second enemy scan.
		float NearestThreatDist = -1.f;

		// Heat terms for diagnostics -- declared outer so the transition log can read them.
		// All-false on the mid-revive latch path (no sweep runs there); reads correctly as "no
		// threat evaluated" alongside NearestThreatDist's -1.
		bool bBodyHot = false;
		bool bFightLive = false;
		bool bDesperation = false;

		// Latch: once the companion is mid-revive, hold the key true so the BT hold is uninterruptible.
		if (Companion->IsRevivingPlayer())
		{
			bReviveWindowOpen = true;
		}
		else
		{
			// Check for nearby threats around the downed player via a dedicated overlap.
			const FVector PlayerLoc = PlayerPawn->GetActorLocation();
			const float ThreatRadius = Companion->ReviveThreatRadius;

			TArray<FOverlapResult> ReviveOverlaps;
			ReviveOverlaps.Reserve(8);
			FCollisionObjectQueryParams RevObjParams(ECC_Pawn);
			FCollisionQueryParams RevParams(SCENE_QUERY_STAT(ReviveWindowThreat), false);
			RevParams.AddIgnoredActor(Companion);
			RevParams.AddIgnoredActor(PlayerPawn);

			// One sweep, two consumers: the two-ring safety test below and the rescue approach's
			// urgency read. Sized to cover whichever reaches further so neither under-samples -- the
			// ring loop re-tests ThreatRadius per candidate (below), so a wider sphere cannot widen
			// the safety rings. The rescue sprint gate's release band is SprintThreatRadius x 1.25
			// (RescueSprintThreatReleaseScale), so the sweep must cover the release-scaled value or
			// the hysteresis band falls outside the sweep and the threat gate releases early.
			const float SprintThreatRadius = RangeTuning ? RangeTuning->ReviveSprintThreatRadius : 0.f;
			constexpr float SprintThreatReleaseScale = 1.25f;
			const float SprintReleaseBand = SprintThreatRadius * SprintThreatReleaseScale;
			Companion->GetWorld()->OverlapMultiByObjectType(
				ReviveOverlaps, PlayerLoc, FQuat::Identity,
				RevObjParams, FCollisionShape::MakeSphere(FMath::Max(ThreatRadius, SprintReleaseBand)), RevParams);

			// Nearest living hostile to the body. Alert state is deliberately NOT filtered here (the
			// safety rings below do filter it): an unaware enemy standing over the body is every
			// reason to run, and it becomes aware the instant the revive starts. Its own pass because
			// the ring loop breaks out the moment it finds a reason to call the area hot.
			for (const FOverlapResult& Overlap : ReviveOverlaps)
			{
				const AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Overlap.GetActor());
				if (!IsValid(Enemy)) continue;
				const UHealthComponent* NearHP = Enemy->GetHealthComponent();
				if (NearHP && NearHP->IsDead()) continue;

				const float NearDist = FVector::Dist(Enemy->GetActorLocation(), PlayerLoc);
				if (NearestThreatDist < 0.f || NearDist < NearestThreatDist) NearestThreatDist = NearDist;
			}

			// Two-ring threat test. Inner ring (ReviveHardThreatRadius): any alerted enemy is hot
			// unconditionally — that close it would see the revive start. Outer ring (out to
			// ReviveThreatRadius): only a Combat-state enemy WITH an eye-line to the body is hot.
			// Searching enemies in the outer ring never hold the window shut — post-fight survivors
			// wandering the area blocked every quiet-scene revive until desperation.
			const float HardRadiusSq = FMath::Square(Companion->ReviveHardThreatRadius);
			const float ThreatRadiusSq = FMath::Square(ThreatRadius);
			for (const FOverlapResult& Overlap : ReviveOverlaps)
			{
				const AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Overlap.GetActor());
				if (!IsValid(Enemy)) continue;
				if (!Enemy->IsAlertedForCompanionReadiness()) continue;
				const UHealthComponent* EHP = Enemy->GetHealthComponent();
				if (EHP && EHP->IsDead()) continue;

				// The sphere may reach past ReviveThreatRadius when the sprint radius is the larger of
				// the two — re-test here so the safety rings stay exactly the size they were authored.
				const float EnemyDistSq = FVector::DistSquared(Enemy->GetActorLocation(), PlayerLoc);
				if (EnemyDistSq > ThreatRadiusSq) continue;

				if (EnemyDistSq <= HardRadiusSq)
				{
					bBodyHot = true;
					break;
				}

				const AEnemyAIController* RingAIC = Cast<AEnemyAIController>(Enemy->GetController());
				const UEnemyAwarenessComponent* RingAwareness = RingAIC ? RingAIC->GetAwarenessComponent() : nullptr;
				if (!RingAwareness) continue;
				if (RingAwareness->GetAwarenessState() != EEnemyAwarenessState::Combat) continue;

				// DBNO handoff clause. When the player drops, UpdateCombat hands surviving shooters to an
				// ally (EnemyAwarenessComponent.cpp:1187-1208): they stay in Combat but their eye-lines move
				// OFF the body, so the trace below answers "no" for an enemy 8m from the body that is actively
				// fighting. Safe below the strict-Combat filter: only Combat-state enemies reach this point,
				// so a post-fight wanderer (Searching/Suspicious/Unaware) with a stale combat-target pointer
				// is already filtered out. Matches any ally, not just this companion, because the handoff
				// can target the armed VIP.
				if (Cast<ACompanionCharacter>(RingAwareness->GetCombatTarget())) { bBodyHot = true; break; }

				FHitResult RingLosHit;
				FCollisionQueryParams RingLosParams(SCENE_QUERY_STAT(ReviveWindowRingLoS), true);
				RingLosParams.AddIgnoredActor(Enemy);
				RingLosParams.AddIgnoredActor(PlayerPawn);
				const bool bRingLosBlocked = Companion->GetWorld()->LineTraceSingleByChannel(
					RingLosHit, Enemy->GetPawnViewLocation(), PlayerLoc, ECC_Visibility, RingLosParams);
				if (!bRingLosBlocked || RingLosHit.GetActor() == PlayerPawn)
				{
					bBodyHot = true;
					break;
				}
			}

			// Also check enemies beyond the radius with LoS to the downed player — but only ones
			// actively IN COMBAT within ReviveLoSThreatRadius. Searching enemies parked on the
			// DBNO standoff ring keep an eye-line to the body indefinitely in open maps; counting
			// them held the window shut until desperation every single time.
			if (!bBodyHot)
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
						bBodyHot = true;
						break;
					}
				}
			}

			// --- Companion-centric fight term (ENTRY gate only) ---
			// Every term above measures the BODY. The enemy side deliberately moves the fight off the body
			// when the player drops (EnemyAwarenessComponent::UpdateCombat, :1178-1218), so a firefight
			// raging on the companion 25m away reads the body as cold and the window opens mid-engagement.
			//
			// Bleedout-floored: unqualified, a director wave feeding continuous handoffs pins this true and
			// the ONLY opening arm left is desperation -- a sprint across open ground into a live wave with a
			// few seconds of slack. The floor releases the term while there is still budget to spend.
			// Ordering invariant: ReviveFightLiveBleedoutFloor > ReviveSprintBleedoutThreshold (DA, 25) >
			// DesperationBleedoutThreshold (12). Below the sprint threshold you get a jogging rescue.
			const float BleedoutRemaining = PlayerIface->GetBleedoutTimeRemaining();
			bFightLive = false;
			if (Companion->ReviveFightLiveBleedoutFloor <= 0.f
				|| BleedoutRemaining <= 0.f
				|| BleedoutRemaining > Companion->ReviveFightLiveBleedoutFloor)
			{
				// TargetAfterUpdate, NOT BestTarget: BestTarget is nulled by the stealth veto (:781) and the
				// LoS-grace clear (:915) while the BB key -- what BR_Combat's decorator actually reads --
				// survives (:1013-1033). BestDistSq is never reassigned on either path, so it is stale
				// whenever BestTarget is null; recomputed here from the cached MyLocation (:265).
				// No LoS term: HasTargetLOS() is false through cover occlusion (which :862-870 explicitly
				// treats as a live fight), so requiring it would flicker this at peek cadence.
				const float SelfEngageSq = FMath::Square(Companion->ReviveSelfEngageRadius);
				bFightLive =
					(Companion->ReviveSelfEngageRadius > 0.f && IsValid(TargetAfterUpdate)
						&& FVector::DistSquared(MyLocation, TargetAfterUpdate->GetActorLocation()) <= SelfEngageSq)
					|| IsValid(Companion->GetRecentAttacker(Companion->ReviveContactWindow))
					|| (Companion->ReviveSuppressionThreshold > 0.f
						&& Companion->GetSuppression01() >= Companion->ReviveSuppressionThreshold);
			}

			// ENTRY and EXIT heat are deliberately different signals.
			const bool bEntryHot = bBodyHot || bFightLive;

			if (bEntryHot) ReviveSafeAccumulator = 0.f;
			else           ReviveSafeAccumulator += DeltaSeconds;

			// Bounded latch. Only BODY heat can shut a committed window: breaking cover and sprinting into
			// the open is GUARANTEED to draw fire, so letting bFightLive close it makes the approach cancel
			// itself -- open, sprint, get shot, shut in the open (losing RescueApproachDamageMultiplier at the
			// worst possible moment), fight, re-open. Being shot mid-approach is not new information; it is
			// the cost of the commitment. What legitimately aborts a rescue is the DESTINATION going hot.
			//
			// Unwinds in real time instead of snapping to zero: a ring-straddling enemy would otherwise
			// re-zero the dwell every other tick and the latch could never release. Same idiom and reason as
			// UpdateWaveHoldQuietTimer (:2297-2305).
			if (bLastReviveWindowOpen)
			{
				if (bBodyHot) ReviveHotAccumulator += DeltaSeconds;
				else          ReviveHotAccumulator = FMath::Max(0.f, ReviveHotAccumulator - DeltaSeconds);
			}
			else
			{
				ReviveHotAccumulator = 0.f; // fresh commit gets a fresh dwell
			}

			bDesperation = BleedoutRemaining <= Companion->DesperationBleedoutThreshold;

			const UHealthComponent* CompanionHealth = Companion->GetHealthComponent();
			const float CompanionHealthFrac = IsValid(CompanionHealth) ? CompanionHealth->GetHealthPercent() : 1.f;
			const bool bUnderFire = Companion->RescueBailUnderFireWindow <= 0.f
				|| IsValid(Companion->GetRecentAttacker(Companion->RescueBailUnderFireWindow));

			// Fast exit: critically low AND actually being shot.
			const bool bBail = bLastReviveWindowOpen && bBodyHot && !bDesperation && bUnderFire
				&& CompanionHealthFrac < Companion->RescueBailHealthFraction;

			// Slow exit: the body has been hot for a sustained window. This is what bounds the latch.
			const bool bHotDwellAbort = bLastReviveWindowOpen && !bDesperation
				&& Companion->ReviveAbortHotSeconds > 0.f
				&& ReviveHotAccumulator >= Companion->ReviveAbortHotSeconds;

			if (bBail || bHotDwellAbort)
			{
				if (bDebugLogging || CVarCompanionReviveLog.GetValueOnGameThread() != 0)
					UE_LOG(LogCompanionAI, Log, TEXT("%s: RESCUE %s hp=%.0f%% hotDwell=%.2fs -- re-fighting until safer"),
						*Companion->GetName(), bBail ? TEXT("BAIL") : TEXT("HOT-ABORT"),
						CompanionHealthFrac * 100.f, ReviveHotAccumulator);
				// Re-arm the full entry grace: a window that just closed must not re-open on safe time banked
				// before the abort. Removes the need for a separate re-open cooldown.
				ReviveHotAccumulator = 0.f;
				ReviveSafeAccumulator = 0.f;
			}
			else if (bLastReviveWindowOpen)
				bReviveWindowOpen = true;
			else if (bDesperation)
				bReviveWindowOpen = true;
			else if (ReviveSafeAccumulator >= Companion->ReviveSafeGraceSeconds)
				bReviveWindowOpen = true;
		}

		// Single-reviver arbitration. The primary companion and the armed VIP run THIS service against
		// the same body with independent commit flags neither reads from the other, so without a shared
		// claim both windows open, both snap to the identical authored pair offset and both kneel.
		// Gated here rather than in BTTask_RevivePlayer on purpose: claiming at the task would let both
		// allies sprint in and one bail on arrival, which still reads as two allies converging.
		if (bReviveWindowOpen)
		{
			const bool bClaimed = PlayerIface->TryClaimRevive(Companion);

			// A companion already mid-hold never yields the window: the task is seated and playing the
			// paired anims, and pulling the key there would abort a revive seconds from finishing.
			const bool bRefused = !bClaimed && !Companion->IsRevivingPlayer();
			if (bRefused)
			{
				bReviveWindowOpen = false;
				if (bDebugLogging && !bLastReviveClaimRefused)
					UE_LOG(LogCompanionAI, Log, TEXT("%s: revive claim REFUSED (held by %s) — staying in the fight"),
						*Companion->GetName(), *GetNameSafe(PlayerIface->GetReviveClaimant()));
			}
			bLastReviveClaimRefused = bRefused;
		}
		else
		{
			bLastReviveClaimRefused = false;
			// Bail, threats hot, or never opened — drop the hold so a still-capable ally can commit.
			// Holder-scoped inside, so a companion that never held it can't free the winner's claim.
			PlayerIface->ReleaseReviveClaim(Companion);
		}

		if ((bDebugLogging || CVarCompanionReviveLog.GetValueOnGameThread() != 0) && bReviveWindowOpen != bLastReviveWindowOpen)
			UE_LOG(LogCompanionAI, Log, TEXT("%s: REVIVE WINDOW %s (latched=%d bleedout=%.1fs safeAccum=%.2fs hotDwell=%.2fs bodyHot=%d fightLive=%d desp=%d)"),
				*Companion->GetName(), bReviveWindowOpen ? TEXT("OPEN") : TEXT("SHUT"),
				(int32)Companion->IsRevivingPlayer(), PlayerIface->GetBleedoutTimeRemaining(),
				ReviveSafeAccumulator, ReviveHotAccumulator,
				(int32)bBodyHot, (int32)bFightLive, (int32)bDesperation);
		bLastReviveWindowOpen = bReviveWindowOpen;

		// Approach damage resist rides the committed rescue (hold-phase resist is bIsRevivingPlayer).
		Companion->SetRescueCommitted(bReviveWindowOpen);

		// Stays -1 on the mid-revive latch path (no sweep runs there): the approach is over by then,
		// so there is nothing left for the pace decision to read and a stale value would be worse.
		Companion->SetNearestThreatToDownedPlayer(NearestThreatDist);

		BB->SetValueAsBool(ReviveWindowOpenKey.SelectedKeyName, bReviveWindowOpen);
	}
	else
	{
		BB->SetValueAsBool(ReviveWindowOpenKey.SelectedKeyName, false);
		ReviveSafeAccumulator = 0.f;
		ReviveHotAccumulator = 0.f;
		bLastReviveWindowOpen = false;
		bLastReviveClaimRefused = false;
		Companion->SetRescueCommitted(false);
		Companion->SetNearestThreatToDownedPlayer(-1.f);
		// Player back up (or the pawn swapped out) — ExitDBNO already wipes the claim, but a
		// possession change would otherwise strand this companion's hold on the old body.
		if (PlayerIface) PlayerIface->ReleaseReviveClaim(Companion);
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(Extraction_AI_CompanionScoringWeights);

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
// Stance backstop
// ---------------------------------------------------------------------------

void UBTService_UpdateCompanionState::UpdateStanceBackstop(
	ACompanionCharacter& Companion, UBlackboardComponent& BB, bool bHasTarget, float DeltaSeconds)
{
	// Cheapest gate first — nothing to reconcile while standing, and the reads below are not free.
	if (!Companion.bIsCrouched)
	{
		UnownedCrouchTime = 0.f;
		return;
	}

	// A held cover point is stance ownership: the combat task crouches at cover and the service
	// deliberately KEEPS the cover keys through a target death so the switch monitor can re-score the
	// slot. Both keys are checked — HasCoverPosition is the arrival flag, CoverTarget is the claim,
	// and either one alone can be live during a commit.
	const bool bCoverHeld = BB.GetValueAsBool(HasCoverPositionKey.SelectedKeyName)
		|| (CoverTargetKey.SelectedKeyType != nullptr
			&& BB.GetValue<UBlackboardKeyType_Cover>(CoverTargetKey.GetSelectedKeyID()).IsValid());

	// Stealth is an owner in its own right, not just via bCrouchOwnedByStealth: the crouch-mirror
	// self-reconciles a stance it did not itself apply (see UpdateStealthCrouchMirror), so standing
	// the companion up here would only race the mirror's next reconcile.
	const bool bStanceOwned = bHasTarget
		|| bCoverHeld
		|| Companion.IsStanceOwnedElsewhere()
		|| Companion.IsCrouchOwnedByStealth()
		|| Companion.IsStealthActive();

	if (bStanceOwned)
	{
		UnownedCrouchTime = 0.f;
		return;
	}

	UnownedCrouchTime += DeltaSeconds;
	if (UnownedCrouchTime < StanceReconcileDebounceSeconds) return;

	// Attempt the uncrouch. If UnCrouch is refused (no headroom under geometry), bIsCrouched stays
	// true and this branch would re-fire every tick with a log each time. Gate the log on the first
	// attempt; the timer keeps growing past the threshold, so bFirstAttempt stays false on retries
	// and the retry is silent. Once the uncrouch succeeds, the timer resets to zero.
	const bool bFirstAttempt = (UnownedCrouchTime - DeltaSeconds) < StanceReconcileDebounceSeconds;
	if (bFirstAttempt)
		UE_LOG(LogCompanionAI, Log, TEXT("%s: stance backstop — crouched with no owner for %.2fs, standing up"),
			*Companion.GetName(), StanceReconcileDebounceSeconds);
	Companion.UnCrouch();

	// If the uncrouch succeeded, reset the timer. If it was refused, keep it at the threshold so
	// the next tick retries without re-logging (bFirstAttempt will be false).
	if (!Companion.bIsCrouched)
		UnownedCrouchTime = 0.f;
}

// ---------------------------------------------------------------------------
// F3 + F1 — non-combat facing arbitration
// ---------------------------------------------------------------------------

namespace
{
	// Post-combat overwatch feel constants (structural, not designer levers — those live on
	// UCompanionTuningDataAsset under Companion|PostCombatOverwatch).
	constexpr float OverwatchBreakSpeed = 60.f;        // cm/s of self-movement that counts as a real move
	constexpr float OverwatchMovingBreakTime = 0.3f;   // sustained-move seconds before the hold breaks
	constexpr float OverwatchCorpseAimZOffset = 110.f; // chest height over the pile, not the floor the bodies lie on
	// Slack (cm) for the blocked-trace LOS check: a hit essentially AT the threat point (floor/prop
	// under a ground-level spawn-zone reference) counts as seen, not as a wall. Beyond this the
	// trace genuinely hit blocking geometry and the aim point is declined.
	constexpr float OverwatchAimLosSlack = 150.f;

	// OverwatchFocalMatchTolerance — the shared focal-compare tolerance — now lives in the anonymous
	// namespace at the top of the file: TickNode's ready-threat sprint-yield needs it and sits above
	// this block.

	// Route facing reference fallbacks — used only when the controller has no tuning asset. Each one
	// mirrors the shipped default on UCompanionTuningDataAsset (Companion|Facing), so a missing asset
	// behaves like an unedited one instead of silently changing the feel.
	constexpr float RouteFacingLookAheadFallback = 800.f;      // cm along the route line; this IS the corner sweep
	constexpr float RouteFacingMaxDistanceFallback = 2000.f;   // 2D range gate to the route (0 = unlimited)
	constexpr float RouteFacingMaxZDeltaFallback = 250.f;      // storey gate, separate from the 2D one (0 = unlimited)
	constexpr float RouteFacingMinHeadingSpeedFallback = 120.f; // cm/s below which travel direction is not honest
	constexpr float RouteFacingBacktrackEnterDotFallback = -0.15f; // travel vs route alignment that latches backtracking
	constexpr float RouteFacingBacktrackExitDotFallback = 0.35f;   // and the wider one that releases it

	// companion.RouteFacingDebug arrow geometry. The lifetime spans one service tick (Interval 0.25 +
	// RandomDeviation 0.05) so the arrow reads as continuous rather than strobing.
	constexpr float RouteFacingDebugArrowSize = 24.f;
	constexpr float RouteFacingDebugThickness = 3.f;
	constexpr float RouteFacingDebugLifetime = 0.35f;

	// Consecutive failed aim-point refreshes before the hold is dropped. A refusal is one trace
	// sample on a ~1s cadence, so ending on the first one had no debounce at all: anything transient
	// in the firing line cycled overwatch END/START.
	constexpr int32 OverwatchRefreshFailuresToEnd = 2;

	// TraceGeometryPastPawns and its pawn-skip limit moved to CompanionAim (Public/AI +
	// Private/AI/CompanionAimValidation) — it is now THE seam every threat-derived bearing is proven
	// through, and it must stay off the visible-enemy paths (it has no "the hit IS the target"
	// exemption; see the header note there).
}

// ---------------------------------------------------------------------------
// Post-fight weapon-up hold
// ---------------------------------------------------------------------------

float UBTService_UpdateCompanionState::GetWeaponUpHoldSeconds(
	const ACompanionCharacter& Companion, const UCompanionTuningDataAsset* Tuning) const
{
	// Fallbacks mirror the tuning asset's own defaults, so a missing asset behaves like an unedited
	// one rather than silently disabling the hold.
	constexpr float CombatHoldFallback = 3.f;
	constexpr float DefensiveHoldFallback = 1.f;

	switch (Companion.GetMode())
	{
	case ECompanionMode::Combat:
		return Tuning ? Tuning->CombatWeaponUpHoldSeconds : CombatHoldFallback;
	case ECompanionMode::Normal: // "Defensive" on screen; the enumerator name is unchanged
		return Tuning ? Tuning->DefensiveWeaponUpHoldSeconds : DefensiveHoldFallback;
	default:
		// Stealth never raises on a target loss — holding a firing line is the one thing it must not
		// do. (The combat-decay branch is not even reachable while stealth is pinned; this is the
		// structural statement of intent, and zero collapses the caller to a plain single-edge lower.)
		return 0.f;
	}
}

void UBTService_UpdateCompanionState::ArmWeaponUpHoldFocal(
	ACompanionAIController& Controller, const ACompanionCharacter& Companion)
{
	// Distance is not a feel lever: the Z override below forces the gaze level, so this only has to
	// be far enough out that ordinary formation shuffling can't swing the yaw. Deliberately NOT a
	// tuning field — there is nothing here a designer would want to tune.
	constexpr float WeaponUpHoldFocalDistance = 800.f;

	// The pawn's CURRENT forward, sampled once on the arm edge. Not the threat bearing (that is the
	// stare being removed) and not a running re-sample (that would chase the body yaw it is itself
	// driving, since the companion runs bUseControllerDesiredRotation).
	WeaponUpHoldFocalPoint = Companion.GetActorLocation()
		+ Companion.GetActorForwardVector() * WeaponUpHoldFocalDistance;
	WeaponUpHoldFocalPoint.Z = Companion.GetPawnViewLocation().Z;
	Controller.SetFocalPoint(WeaponUpHoldFocalPoint, EAIFocusPriority::Gameplay);
}

void UBTService_UpdateCompanionState::ReleaseWeaponUpHoldFocal(ACompanionAIController& Controller)
{
	// Compare-guarded like every other focal release here. The route facing tier overwrites this
	// point later in the same tick whenever a reference is installed, so on that path the compare
	// fails and the tier's own focal is left alone — the two compose without either clearing the
	// other, and the hardened dedup in SetNonCombatFocalDeduped re-asserts if this clear does land.
	if (Controller.GetFocalPointForPriority(EAIFocusPriority::Gameplay)
		.Equals(WeaponUpHoldFocalPoint, OverwatchFocalMatchTolerance))
	{
		Controller.ClearFocus(EAIFocusPriority::Gameplay);
	}
	WeaponUpHoldFocalPoint = FVector::ZeroVector;
}

bool UBTService_UpdateCompanionState::UpdateWaveHold(
	ACompanionCharacter& Companion, const APawn* PlayerPawn, const UCompanionTuningDataAsset* Tuning,
	bool bHasTarget, bool bThreatKnown, float DeltaSeconds)
{
	const UWorld* World = Companion.GetWorld();
	const UEnemyDirectorSubsystem* Director = World ? World->GetSubsystem<UEnemyDirectorSubsystem>() : nullptr;

	// IsWaveActive stays true across the gaps BETWEEN squad spawns — that is the entire point of
	// reading it. It only drops once every squad has spawned AND the last member is dead, or the
	// wave is cancelled, so a cancelled wave releases the hold for free.
	const bool bWaveActive = Director && Director->IsWaveActive();
	const FName ActiveWaveId = Director ? Director->GetActiveWaveId() : NAME_None;

	// Per-wave block, reset on wave IDENTITY rather than activity alone. TickNode returns early for a
	// DBNO companion, so an ally downed during the defence and revived after the next wave started
	// never observes an inactive tick — the ceiling latch survived into the new wave and it ran
	// hold-less from its first tick. Going down during a defence is the encounter's failure state, so
	// that path is not exotic. All four members share this one reset contract.
	if (!bWaveActive || ActiveWaveId != WaveHoldWaveId)
	{
		WaveHoldWaveId = ActiveWaveId;
		bEngagedThisWave = false;
		WaveHoldQuietTimer = 0.f;
		WaveHoldBlindHeldTime = 0.f;
		bWaveHoldCeilingReleased = false;
	}
	else if (bHasFightFirstContact) bEngagedThisWave = true;

	// Knowledge must be LoS-verified (see the header note on bThreatKnown) — the radius-only signal
	// sees through walls and pinned every release on for an ally with no eye-line to the fight.
	const bool bKnowledgeLive = bHasTarget || bThreatKnown;
	const bool bQuietTooLong = UpdateWaveHoldQuietTimer(bKnowledgeLive, bEngagedThisWave, Tuning, DeltaSeconds);
	const bool bCombatStale = IsWaveCombatStale(Companion, Tuning, Director);

	// Hard ceiling — the last-resort backstop, independent of both releases above. Each of those keys
	// off a signal that can be pinned high for the whole wave (the quiet timer by any known threat,
	// the stale timer by any live detected enemy inside the leash), so between them they can leave no
	// exit at all: that is how an ally with no eye-line to the fight stood at cover for an entire
	// defence. It counts BLIND held time only, so a healthy wave can never reach it.
	//
	// Latched, not a live compare: without the latch the first knowing tick zeroes the blind timer and
	// the hold re-arms immediately, flickering the ally between hold and follow instead of letting it
	// walk back to the player. Cleared with the rest of the per-wave block above.
	if (Tuning && Tuning->WaveHoldMaxBlindHoldSeconds > 0.f
		&& WaveHoldBlindHeldTime >= Tuning->WaveHoldMaxBlindHoldSeconds)
	{
		bWaveHoldCeilingReleased = true;
	}

	const bool bHold = bWaveActive
		&& bEngagedThisWave
		&& !bQuietTooLong
		&& !bCombatStale
		&& !bWaveHoldCeilingReleased
		&& Tuning && Tuning->bEnableWaveHold
		// Not CanFire(): that goes false on an empty magazine, and an ally must keep its hold
		// through a reload rather than stroll home mid-mag-swap.
		&& Companion.IsCombatReady()
		// Stealth's whole point is not holding a firing line.
		&& Companion.GetMode() != ECompanionMode::Stealth
		// Deliberately measured to the PLAYER, not the follow leader — do NOT chain this one to match
		// the overwatch anchor above. This leash asks "has the player left me behind", and that has to
		// stay a direct measurement: routed through the primary a secondary could hold at cover
		// indefinitely while the player walked away, as long as the primary stayed near it, which is
		// exactly the abandonment the leash exists to stop. The chained-offset problem that forced the
		// overwatch change does not arise here either — 2500cm against a VIP resting ~700cm out is
		// better than 3x margin, so nothing is sitting on the threshold.
		&& IsValid(PlayerPawn)
		&& FVector::DistSquared(Companion.GetActorLocation(), PlayerPawn->GetActorLocation())
			<= FMath::Square(Tuning->WaveHoldLeashDistance);

	// Accrues only while the hold is BLIND, and zeroes on any live knowledge. Accruing on hold time
	// alone counted a healthy fight as stuck time: 4 squads at ~8s cadence run 40-90s wall clock,
	// almost all of it held, so a normal defence tripped the 45s ceiling and the latch then disabled
	// the hold for the rest of the wave for every ally — abandoning cover in each squad gap,
	// un-crouching mid-defence, and collapsing overwatch's anchor from the wave leash back to the
	// 800cm break distance with the 6s cap re-armed.
	if (bKnowledgeLive) WaveHoldBlindHeldTime = 0.f;
	else if (bHold) WaveHoldBlindHeldTime += DeltaSeconds;

	if (bDebugLogging && bHold != bLastWaveHold)
		UE_LOG(LogCompanionAI, Log, TEXT("%s: WAVE HOLD %s (waveActive=%d engaged=%d quiet=%.1fs blindHeld=%.1fs ceiling=%d)"),
			*Companion.GetName(), bHold ? TEXT("ON") : TEXT("OFF"),
			(int32)bWaveActive, (int32)bEngagedThisWave, WaveHoldQuietTimer,
			WaveHoldBlindHeldTime, (int32)bWaveHoldCeilingReleased);

	// Stand up on release. The hold parks the ally wherever the combat teardown left it, which after
	// a cover engagement is crouched. Nothing downstream un-crouches a companion that is merely
	// following, so without this it walks the rest of the level in a permanent crouch.
	// Skip if a commanded cover hold is still active -- popping the crouch would pull the companion
	// out of its commanded cover position.
	if (!bHold && bLastWaveHold && !Companion.IsCommandedCoverHoldActive()) Companion.UnCrouch();

	bLastWaveHold = bHold;

	Companion.SetWaveHoldActive(bHold);
	return bHold;
}

bool UBTService_UpdateCompanionState::UpdateWaveHoldQuietTimer(
	bool bKnowledgeLive, bool bWaveEngaged, const UCompanionTuningDataAsset* Tuning, float DeltaSeconds)
{
	const float ReleaseSeconds = Tuning ? Tuning->WaveHoldQuietReleaseSeconds : 0.f;
	if (ReleaseSeconds <= 0.f)
	{
		// Release disabled. Park the timer so the wave-hold debug log doesn't report a climbing
		// counter that nothing reads.
		WaveHoldQuietTimer = 0.f;
		return false;
	}

	// The timer must not accrue before the ally's first engagement in this wave. The pre-engagement
	// window (FirstSquadDelaySeconds 2s + squad travel from the spawn zone) is blind by definition,
	// and without this gate the timer banks enough time to trip the release before the wave's first
	// contact, disabling the hold for the entire wave.
	if (!bWaveEngaged)
	{
		WaveHoldQuietTimer = 0.f;
		return false;
	}

	// Holding at cover is only right while the fight is genuinely paused mid-wave. Once nothing at all
	// is known for this long the ally is standing in an emptied room facing the last fight while the
	// player pushes on, which reads as frozen. Drop the hold, let Follow bring it forward.
	//
	// The timer UNWINDS in real time instead of snapping to zero on the first knowing tick. Re-arming
	// the hold calls StopMovement() on the follow task wherever the ally happens to be, so a single
	// tick of knowledge re-arming it turned a flickering eye-line into a stop-go stutter across the
	// whole defence, with an UnCrouch() on every release edge. The likeliest source of that flicker is
	// an alerted enemy in Searching state: NoteAlertedThreat accepts it, but Normal mode (which the
	// extraction VIP is pinned to) skips it for acquisition, so it appears and vanishes as knowledge
	// without ever becoming a target.
	//
	// Saturates on the ACCRUAL branch only: once the release trips, re-arming always costs the full
	// sustained-knowledge window. The saturate must not run on the unwind branch; a knowing tick that
	// leaves the timer still >= ReleaseSeconds would snap it back to the ceiling and the timer could
	// never actually fall.
	// Tuning is known non-null here; a null one yields ReleaseSeconds 0 and returns above.
	const float Ceiling = ReleaseSeconds + FMath::Max(0.f, Tuning->WaveHoldQuietRearmSeconds);
	if (bKnowledgeLive)
	{
		WaveHoldQuietTimer = FMath::Max(0.f, WaveHoldQuietTimer - DeltaSeconds);
	}
	else
	{
		WaveHoldQuietTimer = FMath::Min(WaveHoldQuietTimer + DeltaSeconds, Ceiling);
		if (WaveHoldQuietTimer >= ReleaseSeconds) WaveHoldQuietTimer = Ceiling;
	}
	return WaveHoldQuietTimer >= ReleaseSeconds;
}

bool UBTService_UpdateCompanionState::IsWaveCombatStale(
	const ACompanionCharacter& Companion, const UCompanionTuningDataAsset* Tuning,
	const UEnemyDirectorSubsystem* Director) const
{
	// Stale-combat backstop: if no fight has been reported to the director for
	// WaveHoldStaleCombatReleaseSeconds the hold drops regardless of IsWaveActive(). Covers stuck
	// enemies (a member alive but never fighting) and blocked-spawn soft-locks alike. The default
	// 20s exceeds the 8s squad cadence so a healthy wave never trips it.
	const UWorld* World = Companion.GetWorld();
	if (!Tuning || !Director || !World) return false;
	if (Tuning->WaveHoldStaleCombatReleaseSeconds <= 0.f) return false;
	if ((World->GetTimeSeconds() - Director->GetLastCombatReportTime())
		< Tuning->WaveHoldStaleCombatReleaseSeconds) return false;

	// Suppressed when a live detected enemy is within WaveHoldLeashDistance of the companion (mirrors
	// ExtracteeCharacter::FindNearestCombatEnemy). A distant stuck holdout must NOT pin the signal —
	// that's the exact case the release exists for.
	const float RelevanceRadiusSq = FMath::Square(Tuning->WaveHoldLeashDistance);
	const FVector CompanionLoc = Companion.GetActorLocation();
	for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
	{
		const AEnemyCharacter* Enemy = *It;
		if (!IsValid(Enemy)) continue;
		const UHealthComponent* HP = Enemy->GetHealthComponent();
		if (HP && HP->IsDead()) continue;
		if (!Enemy->HasDetectedPlayer()) continue;
		if (FVector::DistSquared(CompanionLoc, Enemy->GetActorLocation()) <= RelevanceRadiusSq)
			return false;
	}
	return true;
}

bool UBTService_UpdateCompanionState::IsDirectorWaveActive(const ACompanionCharacter& Companion) const
{
	// Queried live rather than cached on a member: the same call chain already resolves this
	// subsystem for the wave threat fallback, the tick is 0.25s, and a cached per-tick copy would
	// create a silent "must be refreshed before overwatch runs" ordering invariant for no gain.
	const UWorld* World = Companion.GetWorld();
	const UEnemyDirectorSubsystem* Director = World ? World->GetSubsystem<UEnemyDirectorSubsystem>() : nullptr;
	return Director && Director->IsWaveActive();
}

bool UBTService_UpdateCompanionState::UpdatePostCombatOverwatch(
	ACompanionAIController& Controller, ACompanionCharacter& Companion, const APawn* PlayerPawn,
	const UCompanionTuningDataAsset* Tuning, bool bTakedownOwnsAim, bool bPlayerDBNO, float DeltaSeconds)
{
	if (!Tuning || !Tuning->bEnablePostCombatOverwatch)
	{
		if (bOverwatchActive) EndPostCombatOverwatch(Controller, Companion, false);
		return false;
	}

	// Wave hold widens the anchor to its own leash: during a defence the ally is meant to stay
	// planted well beyond the normal overwatch radius, and without this overwatch would drop out at
	// OverwatchBreakDistance while the hold kept the ally standing there with nothing aiming it.
	//
	// NOTE: WaveHoldLeashDistance is measured against a DIFFERENT actor in each of its two consumers, and
	// that is intentional. UpdateWaveHold measures it to the PLAYER, because the question there is "has
	// the player left me behind" and routing it through the primary would let a secondary hold at cover
	// while the player walked away. Here it is a radius on the leader-anchored distance below, because
	// this gate asks "am I still posted with what I form up on". A VIP can therefore be inside one and
	// outside the other; the widening is only ever permissive (2500 vs 800), so the failure mode is
	// overwatch persisting slightly past the hold, never the reverse. If the two ever need to disagree
	// by more than that, give this path its own tunable rather than re-pointing either measurement.
	const bool bWaveHold = Companion.IsWaveHoldActive();
	const float AnchorRadius = bWaveHold ? Tuning->WaveHoldLeashDistance : Tuning->OverwatchBreakDistance;

	// Measured to the companion's FORMATION LEADER, not the player. This gate asks "am I still posted
	// with the thing I form up on" — for a primary the resolved leader IS the player, so its gate is
	// unchanged by construction. The VIP forms up on the PRIMARY, and measuring it to the player
	// instead chained two formation offsets into one budget: the primary sits ~403cm out
	// (FormationOffsetBack 350 / Right 200) and the VIP lands ~700cm from the player once the mirror
	// and the zeroed back-bias are applied, against an OverwatchBreakDistance of 800. It passed
	// standing still and failed on any lag, cover detour or player step — which is why the VIP held
	// weapon-up only sometimes while the primary did it reliably. Anchoring on the leader restores the
	// VIP's real margin (~350-400cm, the same as the primary's) instead of widening the radius for
	// everyone. The player fallback covers the pre-publish window before follow has run once.
	const AActor* AnchorActor = Controller.GetFollowLeader();
	if (!IsValid(AnchorActor)) AnchorActor = PlayerPawn;

	// bPlayerDBNO stays player-keyed on purpose: a downed player ends overwatch no matter who the
	// companion is anchored on, because the rescue branch owns the pawn from that moment.
	const bool bFormationAnchored = IsValid(AnchorActor) && !bPlayerDBNO
		&& FVector::DistSquared(Companion.GetActorLocation(), AnchorActor->GetActorLocation())
			<= FMath::Square(AnchorRadius);
	const bool bOwnerElsewhere = bTakedownOwnsAim || Companion.IsRevivingPlayer()
		|| Companion.GetIsCompanionDBNO();

	if (!bOverwatchActive)
	{
		// Wave-only ENTRY gate. The same held bearing reads completely differently either side of it:
		// between squads of a Director wave it is covering the door, after an ordinary room clear it
		// is staring at a corpse. Deliberately placed here and not in the bEnablePostCombatOverwatch
		// early-out above — that path calls EndPostCombatOverwatch, which would snap the weapon down
		// on the frame of a wave's own last kill. An already-running hold therefore always finishes
		// on its normal terms (cap, break distance, movement) even if the wave ends underneath it.
		// A wave hold cannot outlive an inactive wave (UpdateWaveHold requires IsWaveActive), so the
		// hold path needs no separate waiver here.
		if (Tuning->bOverwatchWaveOnly && !IsDirectorWaveActive(Companion))
		{
			// Clear the retry throttle on the refusal. Left alone, a failed entry late in wave N
			// leaves OverwatchRefreshFailures > 0, and the FIRST entry attempt of wave N+1 then eats
			// a full OverwatchAimRefreshInterval of throttle before it is even allowed to try.
			OverwatchRefreshFailures = 0;
			OverwatchAimRefreshTimer = 0.f;
			return false;
		}

		// Enter only on the fresh target-loss edge (a branch switch that already lowered must not
		// re-raise the gun), standing still, posted with the formation leader, with threat memory to
		// aim from.
		// The lowered-edge veto is waived under a wave hold: there the gun SHOULD come back up
		// between squads, so a lower from an earlier lull must not lock overwatch out for the wave.
		if ((bLoweredOnTargetLoss && !bWaveHold) || bOwnerElsewhere || !bFormationAnchored) return false;
		if (Companion.GetVelocity().Size2D() > OverwatchBreakSpeed) return false;
		// Travelling flag as well as velocity: the request lands a tick before the speed does, and
		// entering a hold that the very next tick's travelling break would tear down is pure churn.
		// Catch-up pace is included alongside sprint for the same reason.
		if (Companion.IsSprinting() || Companion.IsFollowCatchupPace()) return false;

		// Throttle failed entry retries to the refresh cadence. With the standoff refusal a companion
		// tucked against cover fails ComputeOverwatchAimPoint and retries the full portal-candidate
		// sort plus up to 25 line traces every service tick (~4Hz) for as long as it stays in that
		// state. OverwatchRefreshFailures > 0 means a prior entry already failed; the first attempt
		// after a fight or after EndPostCombatOverwatch resets to 0 is always immediate.
		if (OverwatchRefreshFailures > 0 && Tuning->OverwatchAimRefreshInterval > 0.f)
		{
			OverwatchAimRefreshTimer += DeltaSeconds;
			if (OverwatchAimRefreshTimer < Tuning->OverwatchAimRefreshInterval) return false;
			OverwatchAimRefreshTimer = 0.f;
		}

		if (!ComputeOverwatchAimPoint(Companion, Tuning, OverwatchAimPoint))
		{
			++OverwatchRefreshFailures;
			return false;
		}
		bOverwatchActive = true;
		OverwatchElapsed = 0.f;
		OverwatchMovingTime = 0.f;
		OverwatchAimRefreshTimer = 0.f;
		OverwatchRefreshFailures = 0;
		Companion.SetAimTarget(nullptr); // stale actor-aim backstop, mirrors the lower path
		Companion.SetLowReadyAim(false);
		Companion.SetScriptedAim(true);
		if (bDebugLogging)
			UE_LOG(LogCompanionAI, Log, TEXT("%s: post-combat overwatch START aim=%s"),
				*Companion.GetName(), *OverwatchAimPoint.ToCompactString());
	}

	OverwatchElapsed += DeltaSeconds;
	OverwatchMovingTime = Companion.GetVelocity().Size2D() > OverwatchBreakSpeed
		? OverwatchMovingTime + DeltaSeconds : 0.f;
	// During a wave hold the max-time cap is waived: the chokepoint aim persists for the whole hold
	// so the ally looks like it is covering the door. Without the waiver overwatch expired at 6s and
	// ambient facing (F1/F3) took over, leaving the ally weapon-up aimed at nothing. The waiver is
	// safe now that bWaveHold is in the Tier-0 yield set — new targets still break overwatch via the
	// safety end (bCombatDecayRanThisTick), and the waived lowered-edge veto lets it re-enter with a
	// fresh aim point once the next squad dies. On hold release the normal 6s cap resumes.
	const bool bTimedOut = !bWaveHold && OverwatchElapsed >= Tuning->PostCombatOverwatchMaxTime;
	// Travelling breaks the hold IMMEDIATELY rather than waiting out OverwatchMovingBreakTime.
	// A third of a second of side-on travel is exactly the forward-legs-under-a-sideways-body tell
	// the travelling-yields-facing rule exists to remove, and the sustained-move break was authored
	// for a walk-away, not a run. Catch-up pace is included: 550-650 is above the strafe cap and
	// would blend into the forward-sprint clip just as a full sprint does.
	const bool bTravelling = Companion.IsSprinting() || Companion.IsFollowCatchupPace();
	if (bOwnerElsewhere || !bFormationAnchored
		|| bTravelling
		|| OverwatchMovingTime > OverwatchMovingBreakTime
		|| bTimedOut)
	{
		if (bDebugLogging)
			UE_LOG(LogCompanionAI, Log, TEXT("%s: post-combat overwatch END t=%.1f (anchored=%d moveT=%.1f travelling=%d)"),
				*Companion.GetName(), OverwatchElapsed, (int32)bFormationAnchored, OverwatchMovingTime,
				(int32)bTravelling);
		EndPostCombatOverwatch(Controller, Companion, false);
		return false;
	}

	// The aim point used to be resolved once on entry and re-asserted verbatim for the rest of the
	// window, so it went stale the moment the ally or the fight moved. Re-resolving it can also fail
	// outright (the standoff refusal in ComputeOverwatchAimPoint) — an ally that can no longer
	// justify a firing line must drop the hold, not keep pointing at the last point it could. The
	// refresh debounces those refusals itself; only a sustained one gets here.
	if (!RefreshOverwatchAimPoint(Companion, Tuning, DeltaSeconds))
	{
		if (bDebugLogging)
			UE_LOG(LogCompanionAI, Log, TEXT("%s: post-combat overwatch END t=%.1f (aim point no longer resolvable)"),
				*Companion.GetName(), OverwatchElapsed);
		EndPostCombatOverwatch(Controller, Companion, false);
		return false;
	}

	// Debounce the END, not the POSE. OverwatchRefreshFailures stays non-zero from the first refusal
	// until a refresh succeeds, so this covers the whole debounce window — including the ticks between
	// refresh attempts, where the refresh itself early-returns true. The hold keeps its window (that is
	// the LOS-flicker hysteresis the debounce exists for) but stops asserting a bearing it can no
	// longer justify: previously the ally held a now-invalid aim for ~2s, which for "must never happen"
	// is simply the bug with a timer on it. Returning true keeps ownership, so the facing arbitration
	// still Tier-0-yields and nothing else writes a focal in the gap.
	if (OverwatchRefreshFailures > 0)
	{
		Companion.SetScriptedAim(false);
		Companion.SetLowReadyAim(true);
		if (Controller.GetFocalPointForPriority(EAIFocusPriority::Gameplay)
			.Equals(OverwatchAimPoint, OverwatchFocalMatchTolerance))
		{
			Controller.ClearFocus(EAIFocusPriority::Gameplay);
		}
		return true;
	}

	Controller.SetFocalPoint(OverwatchAimPoint, EAIFocusPriority::Gameplay);
	// Re-asserted every tick rather than only on entry, so a refusal that dropped the pose above comes
	// back up when the bearing clears. Free to repeat: both setters are change-guarded (SetLowReadyAim
	// early-outs and only then broadcasts), and while bOverwatchActive holds, the facing arbitration
	// Tier-0-yields, so there is no edge-guarded stance for this to fight.
	Companion.SetLowReadyAim(false);
	Companion.SetScriptedAim(true);
	return true;
}

bool UBTService_UpdateCompanionState::RefreshOverwatchAimPoint(
	const ACompanionCharacter& Companion, const UCompanionTuningDataAsset* Tuning, float DeltaSeconds)
{
	// 0 disables the refresh entirely — the entry point is then held for the whole window.
	if (!Tuning || Tuning->OverwatchAimRefreshInterval <= 0.f) return true;

	OverwatchAimRefreshTimer += DeltaSeconds;
	if (OverwatchAimRefreshTimer < Tuning->OverwatchAimRefreshInterval) return true;

	OverwatchAimRefreshTimer = 0.f;

	FVector Refreshed;
	if (!ComputeOverwatchAimPoint(Companion, Tuning, Refreshed))
	{
		// Debounced. A refusal is one trace sample on a ~1s cadence, so ending the hold on the first
		// one let anything transient in the firing line cycle overwatch END/START. The hold therefore
		// survives one refusal — but the last justified point is NOT re-asserted meanwhile: the caller
		// reads OverwatchRefreshFailures and drops to low-ready for as long as it is non-zero. Holding
		// a bearing that can no longer be proven open was the defect, debounce or not.
		++OverwatchRefreshFailures;
		return OverwatchRefreshFailures < OverwatchRefreshFailuresToEnd;
	}

	OverwatchRefreshFailures = 0;
	OverwatchAimPoint = Refreshed;
	return true;
}

void UBTService_UpdateCompanionState::ResolveOverwatchThreatCandidates(
	const ACompanionCharacter& Companion, const UCompanionTuningDataAsset* Tuning,
	TArray<FVector, TInlineAllocator<5>>& OutCandidates) const
{
	if (!Tuning) return;

	// Ordered list of pre-existing threat memories, each with the at-our-feet floor applied. The
	// caller validates each in turn and takes the first whose bearing is provably open, so a blocked
	// corpse centroid no longer kills the entire chain.
	const FVector CompLoc = Companion.GetActorLocation();
	const float MinDistSq = FMath::Square(Tuning->OverwatchMinThreatDist);

	// 1. Corpse centroid -- the pile of enemies that just died.
	FVector CorpseCentroid;
	if (ResolveRecentCorpseCentroid(Companion, Tuning, CorpseCentroid)
		&& FVector::DistSquared2D(CorpseCentroid, CompLoc) > MinDistSq)
	{
		OutCandidates.Add(CorpseCentroid);
	}

	// 2. Last threat location -- freshest "threat was here" point.
	if (bHasLastThreatLocation && FVector::DistSquared2D(LastThreatLocation, CompLoc) > MinDistSq)
		OutCandidates.Add(LastThreatLocation);

	// 3. First contact location -- where this fight started.
	if (bHasFightFirstContact && FVector::DistSquared2D(FightFirstContactLocation, CompLoc) > MinDistSq)
		OutCandidates.Add(FightFirstContactLocation);

	// 4. Bearing push-out -- a point along the direction of whatever memory exists.
	// OverwatchMinThreatDist floor applied for consistency with candidates 1-3 and 5 (header
	// contract). Safe at defaults (OverwatchBearingDistance 1200 >> OverwatchMinThreatDist 400);
	// without the floor a designer lowering BearingDistance below MinThreatDist would produce a
	// candidate the other four already rejected.
	if (bHasLastThreatLocation || bHasFightFirstContact)
	{
		const FVector Src = bHasLastThreatLocation ? LastThreatLocation : FightFirstContactLocation;
		const FVector Dir = (Src - CompLoc).GetSafeNormal2D();
		if (!Dir.IsNearlyZero())
		{
			const FVector PushOut = CompLoc + Dir * Tuning->OverwatchBearingDistance;
			if (FVector::DistSquared2D(PushOut, CompLoc) > MinDistSq)
				OutCandidates.Add(PushOut);
		}
	}

	// 5. Wave spawn reference -- the direction waves come through, even when own fight memory was
	// reset (the ally covers the door the next squad emerges from).
	const UWorld* World = Companion.GetWorld();
	const UEnemyDirectorSubsystem* Director = World ? World->GetSubsystem<UEnemyDirectorSubsystem>() : nullptr;
	FVector WaveThreat;
	if (Director && Director->IsWaveActive() && Director->GetWaveThreatReference(WaveThreat)
		&& FVector::DistSquared2D(WaveThreat, CompLoc) > MinDistSq)
	{
		OutCandidates.Add(WaveThreat);
	}
}

void UBTService_UpdateCompanionState::CollectQualifyingCorpseLocations(
	const ACompanionCharacter& Companion, const UCompanionTuningDataAsset* Tuning,
	TArray<FVector, TInlineAllocator<10>>& OutLocations, int32& OutNearestIdx) const
{
	const UWorld* World = Companion.GetWorld();
	const UEnemyDirectorSubsystem* Director = World ? World->GetSubsystem<UEnemyDirectorSubsystem>() : nullptr;
	if (!Director) return;

	const FVector CompLoc = Companion.GetActorLocation();
	const float RadiusSq = FMath::Square(Tuning->OverwatchCorpseSearchRadius);
	const float MaxZDelta = Tuning->OverwatchCorpseMaxZDelta;

	float NearestDistSq = MAX_FLT;
	for (const TWeakObjectPtr<AEnemyCharacter>& Entry : Director->GetCorpses())
	{
		const AEnemyCharacter* Corpse = Entry.Get();
		if (!IsValid(Corpse)) continue;
		// Recency: skip corpses that existed before this fight started.
		if (PreFightCorpses.Contains(Entry)) continue;
		const FVector CorpseLoc = Corpse->GetCorpseLocation();
		// 2D distance gate (the old 3D test let a floor-spanning sphere drag the centroid).
		const float Dist2DSq = FVector::DistSquared2D(CorpseLoc, CompLoc);
		if (Dist2DSq > RadiusSq) continue;
		// Storey gate: same lesson as FollowMaxZDelta and RouteFacingMaxZDelta.
		if (MaxZDelta > 0.f && FMath::Abs(CorpseLoc.Z - CompLoc.Z) > MaxZDelta) continue;
		const int32 Idx = OutLocations.Add(CorpseLoc);
		if (Dist2DSq < NearestDistSq) { NearestDistSq = Dist2DSq; OutNearestIdx = Idx; }
	}
}

bool UBTService_UpdateCompanionState::ResolveRecentCorpseCentroid(
	const ACompanionCharacter& Companion, const UCompanionTuningDataAsset* Tuning, FVector& OutCentroid) const
{
	if (!Tuning || Tuning->OverwatchCorpseSearchRadius <= 0.f) return false;

	TArray<FVector, TInlineAllocator<10>> Locations;
	int32 NearestIdx = INDEX_NONE;
	CollectQualifyingCorpseLocations(Companion, Tuning, Locations, NearestIdx);
	if (Locations.Num() == 0) return false;

	// Spread reject: a centroid of two separate clusters lands in the wall between them. When any
	// pair exceeds the threshold, fall back to the single nearest corpse.
	bool bUseCentroid = true;
	const float MaxSpreadSq = FMath::Square(Tuning->OverwatchCorpseMaxSpread);
	if (Locations.Num() > 1 && MaxSpreadSq > 0.f)
	{
		for (int32 i = 0; i < Locations.Num() && bUseCentroid; ++i)
			for (int32 j = i + 1; j < Locations.Num(); ++j)
				if (FVector::DistSquared2D(Locations[i], Locations[j]) > MaxSpreadSq)
				{ bUseCentroid = false; break; }
	}

	if (bUseCentroid)
	{
		FVector Sum = FVector::ZeroVector;
		for (const FVector& Loc : Locations) Sum += Loc;
		OutCentroid = Sum / static_cast<float>(Locations.Num());
	}
	else
	{
		OutCentroid = Locations[NearestIdx];
	}
	OutCentroid.Z += OverwatchCorpseAimZOffset;
	return true;
}

bool UBTService_UpdateCompanionState::ValidateThreatAimPoint(
	const ACompanionCharacter& Companion, const FVector& Point, FVector& OutValidated) const
{
	const UWorld* World = Companion.GetWorld();
	if (!World) return false;

	// Build the full ignore set: self, weapon, attached actors, friendly-fire list. Same ignore set as
	// the ready-threat gate in TickNode, and load-bearing for the same reason: a companion's own
	// attached actor sitting on the line otherwise reads as "sees nothing" and the bearing is wrongly
	// declined. Team-mates come from the weapon's own friendly-fire list — rounds pass straight
	// THROUGH them (AWeaponBase excludes them from the hitscan), so an ally standing on the line is
	// not an obstruction to a shot either.
	FCollisionQueryParams LosParams(SCENE_QUERY_STAT(CompanionThreatAimLoS), true);
	LosParams.AddIgnoredActor(&Companion);
	AWeaponBase* Weapon = Companion.GetCurrentWeapon();
	LosParams.AddIgnoredActor(Weapon);
	Companion.ForEachAttachedActors([&LosParams](AActor* Attached)
	{
		LosParams.AddIgnoredActor(Attached);
		return true;
	});
	if (IsValid(Weapon))
	{
		for (AActor* Friendly : Weapon->GetFriendlyFireIgnoreList())
			LosParams.AddIgnoredActor(Friendly);
	}

	return ValidateThreatAimPoint(Companion, Point, LosParams, OutValidated);
}

bool UBTService_UpdateCompanionState::ValidateThreatAimPoint(
	const ACompanionCharacter& Companion, const FVector& Point,
	const FCollisionQueryParams& PrebuiltParams, FVector& OutValidated) const
{
	const UWorld* World = Companion.GetWorld();
	if (!World) return false;

	// Pawn-transparent: the player or another ally crossing the line is not geometry; counting them as
	// a wall cycled overwatch END/START on nothing but footwork. A hit essentially AT the point (the
	// floor or a prop under a ground-level reference) counts as seen; anything further back is a wall.
	FHitResult Hit;
	const bool bBlocked = CompanionAim::TraceGeometryPastPawns(
		*World, Companion.GetPawnViewLocation(), Point, PrebuiltParams, Hit);
	if (bBlocked && FVector::DistSquared(Hit.ImpactPoint, Point) > FMath::Square(OverwatchAimLosSlack))
		return false;

	OutValidated = Point;
	return true;
}

bool UBTService_UpdateCompanionState::ComputeOverwatchAimPoint(
	const ACompanionCharacter& Companion, const UCompanionTuningDataAsset* Tuning, FVector& OutAimPoint) const
{
	// Walk the ordered candidate list and take the FIRST whose bearing is provably open. A blocked
	// corpse centroid (ally behind a crate, or the centroid itself inside a wall from cluster spread)
	// no longer kills the chain -- it falls through to last-threat, first-contact, bearing push-out,
	// then the wave spawn reference. False only when EVERY candidate is refused or no memory exists.
	//
	// This is NOT a search for an arbitrary open direction (the sweep-arc facing was scrapped). Every
	// candidate comes from a concrete threat memory; validation merely filters which one is honest.
	TArray<FVector, TInlineAllocator<5>> Candidates;
	ResolveOverwatchThreatCandidates(Companion, Tuning, Candidates);

	// Build the ignore set ONCE for the whole candidate pass. The set depends only on the companion
	// (self, weapon, attached actors, friendly-fire list), which is const across candidates.
	FCollisionQueryParams LosParams(SCENE_QUERY_STAT(CompanionThreatAimLoS), true);
	LosParams.AddIgnoredActor(&Companion);
	AWeaponBase* Weapon = Companion.GetCurrentWeapon();
	LosParams.AddIgnoredActor(Weapon);
	Companion.ForEachAttachedActors([&LosParams](AActor* Attached)
	{
		LosParams.AddIgnoredActor(Attached);
		return true;
	});
	if (IsValid(Weapon))
	{
		for (AActor* Friendly : Weapon->GetFriendlyFireIgnoreList())
			LosParams.AddIgnoredActor(Friendly);
	}

	for (const FVector& Candidate : Candidates)
	{
		if (ValidateThreatAimPoint(Companion, Candidate, LosParams, OutAimPoint))
			return true;
	}
	return false;
}

void UBTService_UpdateCompanionState::EndPostCombatOverwatch(
	ACompanionAIController& Controller, ACompanionCharacter& Companion, bool bAnotherOwnerHasFocus)
{
	Companion.SetScriptedAim(false);

	// Clear the focal too, not just the scripted raise. Nothing downstream does it in the states this
	// ends in: under a wave hold the decay branch only restores low-ready, and UpdateNonCombatFacing
	// Tier-0-yields on IsWaveHoldActive(), so the anim's focal-driven aim kept the ally pointing at
	// the dead overwatch point and "declining overwatch" only stopped the point being RE-asserted.
	// Skipped when another system already owns focus this tick: GetFocalPointForPriority resolves a
	// SetFocus(Actor) to GetFocalPointOnActor, a real world location, so a near-stationary enemy's
	// position can Equals-match the overwatch point and clear the combat task's just-written focus.
	// The identical hazard is guarded at the Tier-0-yield hand-off in UpdateNonCombatFacing.
	if (!bAnotherOwnerHasFocus
		&& Controller.GetFocalPointForPriority(EAIFocusPriority::Gameplay)
			.Equals(OverwatchAimPoint, OverwatchFocalMatchTolerance))
	{
		Controller.ClearFocus(EAIFocusPriority::Gameplay);
	}

	bOverwatchActive = false;
	OverwatchElapsed = 0.f;
	OverwatchMovingTime = 0.f;
	OverwatchAimRefreshTimer = 0.f;
	OverwatchRefreshFailures = 0;
}

void UBTService_UpdateCompanionState::ResetFightThreatMemory()
{
	bHasFightFirstContact = false;
	bHasLastThreatLocation = false;
	PreFightCorpses.Reset();
}

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
	// Split in two so the wave hold can be told apart from every OTHER yield reason. The wave hold is
	// the one member of this set that does not necessarily write a facing of its own — it just holds
	// the gun up — so when it is the only reason we yielded, the posture chain's forward focal IS the
	// facing and the hand-off below must leave it alone. Every other member either writes its own
	// focal or is a state where ours must not survive.
	//
	// bActiveAimOwnerYields is the subset of members that ACTIVELY WRITE a facing this tick: target,
	// ready-threat, overwatch, revive, takedown, traversal, DBNO. Excluded: BB_RouteActive (a
	// HoldAtFinal park keeps it true indefinitely without writing any focal -- the route task is done)
	// and bCommandYieldsFacing (a command may yield facing without actively writing one this tick).
	// The full set (bOtherOwnerYields) is still used for the Tier-0-yield decision and the hand-off
	// cleanup; the narrow set gates only bWaveHoldOwnsForwardFocal, where the question is "will
	// another owner overwrite the wave hold's focal this tick", not "should non-combat facing yield".
	const bool bActiveAimOwnerYields = bHasTarget || (bReadyOnlyThreat && bReadyThreatOwnsFacing)
		|| bOverwatchActive // post-fight hold owns aim/focus for its whole window
		|| Companion.IsRevivingPlayer()
		|| Companion.IsCommandedTakedownArmed() || Companion.IsCommandedTakedownExecuting()
		|| Companion.IsTakedownMontagePlaying()
		|| (IsValid(Traversal) && Traversal->IsBusy())
		|| Companion.GetIsCompanionDBNO();
	const bool bOtherOwnerYields = bActiveAimOwnerYields
		|| BB.GetValueAsBool(ACompanionAIController::BB_RouteActive)
		|| bCommandYieldsFacing;
	// Wave hold owns weapon-up; ambient facing must not compete.
	const bool bTierZeroYield = bOtherOwnerYields || Companion.IsWaveHoldActive();

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
		// bWatchStanceApplied covers the fight-signal fallback watch, which deliberately keeps no
		// linger (freshness is its hold, so linger is always 0 on that path) — without it a
		// fallback-watch focal survives into a route/command approach and back-walks it.
		// The post-fight hold's forward focal joins the compare set for the same reason: it is sampled
		// ONCE from the pawn's forward at the arm edge, so it is a FIXED world point. It reads as
		// "straight ahead" only on that tick — a command or route move starting mid-hold walks the
		// companion past it within a second or two, and bUseControllerDesiredRotation then turns the
		// body to face a point behind it. That is exactly the back-walk this hand-off exists to stop.
		// Note the route task's own focal setter is a pure distance dedup with no live-focal check, so
		// at a stationary waypoint dwell it would skip re-asserting and our stale point would override
		// the designer's authored waypoint aim for the rest of the hold.
		// One exception to the whole hand-off: the WAVE HOLD's forward focal. IsWaveHoldActive() is
		// itself in this yield set, so during a wave hold there is no other facing owner to hand off
		// TO — the hold's own raise is the facing. Clearing it here would drop the focal on the same
		// tick the posture chain armed it, every tick, and the anim's bFocusLive gate would never see
		// a valid focal, so the gun would never actually come up.
		//
		// Gated on bActiveAimOwnerYields (not the full bOtherOwnerYields) because BB_RouteActive and
		// bCommandYieldsFacing can be true WITHOUT actively writing a focal this tick. A HoldAtFinal
		// park keeps BB_RouteActive true indefinitely (CompanionCommandComponent.cpp:81-82) while the
		// route task is done walking and writes nothing — under those conditions bOtherOwnerYields was
		// true, this exemption was false, and the focal was cleared-and-re-armed every tick, so the
		// anim's bFocusLive gate never saw a valid focal and the gun never came up. The narrowed set
		// still contains every system that would overwrite the wave hold's focal: combat target,
		// ready-threat, overwatch, revive, takedown, traversal, DBNO.
		const bool bWaveHoldOwnsForwardFocal = bWeaponUpHoldAsserted
			&& Companion.IsWaveHoldActive() && !bActiveAimOwnerYields;
		const bool bFocalIsOurs = !bHasTarget && !bReadyOnlyThreat && !bWaveHoldOwnsForwardFocal
			&& ((!LastNonCombatFocalPoint.IsZero() && CurrentFocal.Equals(LastNonCombatFocalPoint, OverwatchFocalMatchTolerance))
				|| ((WatchThreatLingerRemaining > 0.f || bWatchStanceApplied)
					&& CurrentFocal.Equals(WatchThreatLocation, OverwatchFocalMatchTolerance))
				|| (bWeaponUpHoldAsserted
					&& CurrentFocal.Equals(WeaponUpHoldFocalPoint, OverwatchFocalMatchTolerance)));
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
		// can land in the same tick as this yield — without resetting the cache, the next non-combat
		// tick's dedup could skip re-setting the focal point that was just cleared.
		LastNonCombatFocalPoint = FVector::ZeroVector;
		// The route tier no longer owns anything: the hand-off above already released our focal if it
		// was still ours, and the backtrack latch must not survive an owner's move (a route/command
		// approach is exactly the case that drives velocity against the reference line).
		bRouteFacingOwnsFocal = false;
		bRouteFacingBacktracking = false;
		// Same for the post-fight hold — it has yielded its facing to a real aim owner, which is the
		// correct outcome. Only the facing is surrendered; posture and the bLoweredOnTargetLoss latch
		// stay exactly where the decay branch left them, since this block owns neither.
		// Skipped while the wave hold owns the forward focal (see above): there the yield is caused by
		// the hold itself, so tearing its state down here would fight the posture chain every tick.
		if (!bWaveHoldOwnsForwardFocal)
		{
			bWeaponUpHoldAsserted = false;
			WeaponUpHoldFocalPoint = FVector::ZeroVector;
		}
		return;
	}

	// Tier 1 (F3): watch the nearest visible/lingering threat.
	if (ComputeWatchThreat(Controller, Companion, WatchCandidateEnemy, Tuning, DeltaSeconds))
	{
		// The watch tier writes its focal directly rather than through the shared dedup, so the cache
		// must be invalidated here: left stale, a later route-facing release would compare-match the
		// watch focal and clear it, and the route tier's own dedup could skip re-asserting on resume.
		LastNonCombatFocalPoint = FVector::ZeroVector;
		bRouteFacingOwnsFocal = false;
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

		if (Controller.GetFocalPointForPriority(EAIFocusPriority::Gameplay).Equals(WatchThreatLocation, OverwatchFocalMatchTolerance))
			Controller.ClearFocus(EAIFocusPriority::Gameplay);
		WatchThreatLocation = FVector::ZeroVector;
		WatchedEnemy = nullptr;
	}

	// Tier 2: face the way this level section runs, when a route has been installed as its facing
	// reference. Below Tier 0 by construction — it is the quietest thing the companion can be doing,
	// and every system that owns aim outranks it.
	if (ApplyRouteFacingReference(Controller, Companion, Tuning)) return;

	// Tier 3 (F1): ambient path look-ahead / idle attention-yaw facing. Ships disabled
	// (bAmbientFacingEnabled) and stays present — it is the no-reference fallback, not dead code.
	ApplyAmbientFacing(Controller, Companion, PlayerPawn, Tuning, DeltaSeconds);
}

bool UBTService_UpdateCompanionState::ComputeWatchThreat(
	const ACompanionAIController& Controller, const ACompanionCharacter& Companion,
	const AEnemyCharacter* Candidate, const UCompanionTuningDataAsset* Tuning, float DeltaSeconds)
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
		}
		else
		{
			WatchThreatLingerRemaining = FMath::Max(0.f, WatchThreatLingerRemaining - DeltaSeconds);
			if (WatchThreatLingerRemaining > 0.f)
				return true;
		}
	}

	// Fight-aware follow fallback: a live fight is on near the party but no enemy is in eye-line
	// (and no linger memory holds) — watch the alerted-threat bearing published this service tick.
	// The signal refreshes every tick while the fight is live, so freshness IS the hold; no linger
	// bookkeeping. WatchedEnemy stays null: there is no actor to track, only a bearing. This is
	// what stops a trailing companion (the extractee behind a wall) strolling through a firefight.
	//
	// The signal itself is PURE RADIUS -- NoteAlertedThreat publishes unconditionally, with no
	// line-of-sight trace anywhere behind it -- so it is the one watch source that can name a point
	// inside a wall. The facing is accepted unconditionally: this branch exists precisely for the ally
	// behind a wall, so refusing it dropped the facing entirely and the ally strolled through the
	// firefight. ApplyWatchFacing never raises on it (bFallbackWatch) -- a lowered weapon pointed
	// at an unseen bearing is the honest pose, and that is the only constraint this signal needs.
	const float SignalMaxAge = Tuning ? Tuning->FightSignalMaxAge : 4.f;
	FVector AlertedLocation;
	if (Controller.GetRecentAlertedThreat(SignalMaxAge, AlertedLocation))
	{
		WatchedEnemy = nullptr;
		// The facing is ALWAYS accepted: this branch exists for the trailing ally behind a wall, so
		// refusing it dropped the facing entirely and the ally strolled through the firefight.
		// ValidateThreatAimPoint's only output is OutValidated = Point (the input unchanged), so both
		// validated and refused branches yielded AlertedLocation — the call was a full ignore-set
		// rebuild + up to 5 line traces per tick for no information. Assigned directly.
		WatchThreatLocation = AlertedLocation;
		return true;
	}

	return false;
}

void UBTService_UpdateCompanionState::ApplyWatchFacing(ACompanionAIController& Controller, ACompanionCharacter& Companion)
{
	// Travelling yields facing to travel: a watched threat is worth turning the head for at a walk,
	// never worth running side-on for. Travelling = sprinting OR closing a follow gap (same widened
	// condition as the ready-threat tier and IsStrafingForFocus). Releasing our focal hands facing to
	// path-following's Move-priority focus. Compare-guarded so a focal another system wrote this tick
	// is untouched. Stance below is unaffected — the companion keeps the weapon up (or stays
	// low-profile in stealth) while it runs.
	const bool bTravellingToClose = !Companion.IsStrafingForFocus()
		&& (Companion.IsSprinting() || Companion.IsFollowCatchupPace());
	if (bTravellingToClose)
	{
		if (Controller.GetFocalPointForPriority(EAIFocusPriority::Gameplay)
			.Equals(WatchThreatLocation, OverwatchFocalMatchTolerance))
			Controller.ClearFocus(EAIFocusPriority::Gameplay);
	}
	else
		Controller.SetFocalPoint(WatchThreatLocation, EAIFocusPriority::Gameplay);

	// Re-checked every tick (not just on first entry): a mode order landing mid-watch flips
	// IsStealthActive() without ever leaving the watch tier, and the stance must follow it —
	// otherwise ordering Stealth during a Normal-mode watch keeps the gun raised (ScriptedAim
	// raises unconditionally regardless of mode), and Stealth->Normal keeps it wrongly lowered.
	//
	// The fight-signal fallback (WatchedEnemy null) joins that key for exactly the same reason. A
	// single watch slides visible -> linger -> fallback without ever leaving this tier, so keying the
	// re-apply on stealth alone let the fallback INHERIT the raise the visible watch had applied —
	// a weapon left up on a radius-only bearing, which is one of the ways the ally ended up aiming
	// through a wall. bFallbackWatch reuses the exact WatchedEnemy.IsValid() test the stealth bark
	// below already discriminates on, so there is one discriminator here, not two.
	const bool bStealthNow = Companion.IsStealthActive();
	const bool bFallbackWatch = !WatchedEnemy.IsValid();
	if (bWatchStanceApplied && bWatchStanceStealth == bStealthNow
		&& bWatchStanceFallback == bFallbackWatch) return;
	const bool bFreshWatch = !bWatchStanceApplied;
	bWatchStanceApplied = true;
	bWatchStanceStealth = bStealthNow;
	bWatchStanceFallback = bFallbackWatch;

	// Fresh stealth watch = hostile spotted while sneaking — whisper it once per watch entry
	// (a mode flip mid-watch re-applies stance but is not a new sighting). Fallback watches
	// (WatchedEnemy null — fight-signal bearing only) never bark: nothing was actually spotted.
	if (bFreshWatch && bStealthNow && !bFallbackWatch)
		Companion.Bark(ECompanionBarkType::StealthSpotEnemy);

	if (bStealthNow)
	{
		// Stealth: stays low-profile, rotates to watch only — weapon-raise would break cover
		// discipline. Explicitly lowers ScriptedAim in case we're re-applying from a Normal-mode watch.
		Companion.SetScriptedAim(false);
		Companion.SetLowReadyAim(true);
	}
	else if (bFallbackWatch)
	{
		// Fight-signal fallback: FACE, do not raise. Its point comes from the pure-radius alerted
		// threat signal, so nothing behind it has ever confirmed a sighting — the ally is turning
		// toward a fight it can hear, not covering a target it can see. The facing and the hold stay
		// (that is what stops the extractee strolling through a firefight, and the in-code intent of
		// this branch); only the raise is dropped, because raising here is what "aiming through the
		// wall" actually looks like on screen. Weapon-down and rotated is the honest pose.
		Companion.SetScriptedAim(false);
		Companion.SetLowReadyAim(true);
	}
	else
	{
		// Normal, with a real enemy watched (visible this tick, or held inside its linger window —
		// last-known-position aim, which is correct behaviour and the doorway-flicker hysteresis):
		// weapon raised via the scripted-aim gate (posture stays Exploration — this deliberately
		// does not flip to Combat, which would drag posture-scoring/formation with it).
		Companion.SetLowReadyAim(false);
		Companion.SetScriptedAim(true);
	}
}

// ---------------------------------------------------------------------------
// Tier 2 — route facing reference
// ---------------------------------------------------------------------------

bool UBTService_UpdateCompanionState::ApplyRouteFacingReference(
	ACompanionAIController& Controller, const ACompanionCharacter& Companion,
	const UCompanionTuningDataAsset* Tuning)
{
	// Master switch first, and it must RELEASE rather than simply return: a designer switching the
	// feature off mid-section otherwise freezes the companion on the last heading it applied.
	if (Tuning && !Tuning->bRouteFacingReferenceEnabled)
	{
		ReleaseRouteFacingFocal(Controller);
		return false;
	}

	// One reference is live at a time; the controller owns install/replace/clear. A section with no
	// reference falls straight through to today's behaviour.
	const ACompanionRoute* Route = Controller.GetFacingReferenceRoute();
	if (!IsValid(Route))
	{
		ReleaseRouteFacingFocal(Controller);
		return false;
	}

	// Travelling yields facing to travel. Travelling = sprinting OR closing a follow gap (same
	// widened condition as the ready-threat and watch-threat tiers). Returning TRUE (rather than
	// falling through) is deliberate: it claims the tick so Tier 3 doesn't write a focal in our
	// place. Path-following's Move-priority focus then turns the body down the path. Gated behind
	// the "a reference is actually installed" check above so a plain travelling companion with no
	// route never suppresses ambient facing.
	const bool bTravellingToClose = !Companion.IsStrafingForFocus()
		&& (Companion.IsSprinting() || Companion.IsFollowCatchupPace());
	if (bTravellingToClose)
	{
		ReleaseRouteFacingFocal(Controller);
		return true;
	}

	const FVector PawnLoc = Companion.GetActorLocation();
	const float LookAhead = Route->GetFacingReferenceLookAhead(
		Tuning ? Tuning->RouteFacingLookAheadDistance : RouteFacingLookAheadFallback);

	// RouteHeading is the only output we face by. The look-ahead point and the projection are inputs
	// to the range gate and the debug arrow — never to the focal; see the anchor note below.
	FVector RouteHeading, LookAheadPoint, Projection;
	if (!Route->GetFacingReferenceAim(PawnLoc, LookAhead, RouteHeading, LookAheadPoint, Projection)
		|| !IsRouteFacingInRange(PawnLoc, Projection, Tuning))
	{
		ReleaseRouteFacingFocal(Controller);
		return false;
	}

	// Running back against the route to catch the player up: face the way we are actually going.
	const bool bBacktracking = UpdateRouteBacktrackLatch(Companion, RouteHeading, Tuning);
	const FVector FacingDir = bBacktracking
		? Companion.GetVelocity().GetSafeNormal2D()
		: RouteHeading.GetSafeNormal2D();
	if (FacingDir.IsNearlyZero())
	{
		// No honest direction this tick — a degenerate heading, or the backtrack latch still held
		// while the companion has stopped. Release rather than invent one: it then holds its current
		// yaw, which is precisely what it does today with no reference installed.
		ReleaseRouteFacingFocal(Controller);
		return false;
	}

	// ANCHORED AT THE PAWN, not at the route's look-ahead point. Facing that point instead rotates
	// the heading by the companion's own lateral offset from the line — 68 degrees at the default
	// 2000 cm range gate, i.e. the wall beside the corridor rather than down it, which is the exact
	// bug this feature exists to fix — and it reverses once the companion walks past the final
	// waypoint. The route tells us WHICH WAY the section runs; where we happen to be standing must
	// not change that.
	FVector Focal = PawnLoc + FacingDir * LookAhead;
	Focal.Z = Companion.GetPawnViewLocation().Z; // level gaze — the controller preserves focal pitch
	SetNonCombatFocalDeduped(Controller, Focal);
	bRouteFacingOwnsFocal = true;

#if ENABLE_DRAW_DEBUG
	if (CVarCompanionRouteFacingDebug.GetValueOnGameThread() != 0)
	{
		if (const UWorld* DebugWorld = Companion.GetWorld())
			DrawDebugDirectionalArrow(DebugWorld, Companion.GetPawnViewLocation(), Focal,
				RouteFacingDebugArrowSize, bBacktracking ? FColor::Magenta : FColor::Cyan,
				false, RouteFacingDebugLifetime, 0, RouteFacingDebugThickness);
	}
#endif

	return true;
}

bool UBTService_UpdateCompanionState::IsRouteFacingInRange(
	const FVector& PawnLocation, const FVector& RoutePoint, const UCompanionTuningDataAsset* Tuning) const
{
	const float MaxDist = Tuning ? Tuning->RouteFacingMaxDistance : RouteFacingMaxDistanceFallback;
	if (MaxDist > 0.f && FVector::Dist2D(PawnLocation, RoutePoint) > MaxDist) return false;

	// A separate storey gate, not a 3D distance test. DemoMap is a stacked skyscraper: a plain 3D
	// radius keeps a floor-2 reference live while the companion walks floor 1 and points it down a
	// corridor over its head. Same lesson already baked into FollowMaxZDelta.
	const float MaxZDelta = Tuning ? Tuning->RouteFacingMaxZDelta : RouteFacingMaxZDeltaFallback;
	return MaxZDelta <= 0.f || FMath::Abs(PawnLocation.Z - RoutePoint.Z) <= MaxZDelta;
}

bool UBTService_UpdateCompanionState::UpdateRouteBacktrackLatch(
	const ACompanionCharacter& Companion, const FVector& RouteHeading, const UCompanionTuningDataAsset* Tuning)
{
	const FVector Velocity = Companion.GetVelocity();
	const float MinSpeed = Tuning ? Tuning->RouteFacingMinHeadingSpeed : RouteFacingMinHeadingSpeedFallback;

	// Below the gate there is no honest travel direction to compare against, so the latch is left
	// exactly where it is. Resetting it on a momentary stop would swing facing back to the route line
	// for one tick and away again the instant the companion re-accelerated — the very flap the
	// hysteresis below exists to prevent.
	if (Velocity.Size2D() < MinSpeed) return bRouteFacingBacktracking;

	const float Alignment = FVector::DotProduct(Velocity.GetSafeNormal2D(), RouteHeading.GetSafeNormal2D());
	const float EnterDot = Tuning ? Tuning->RouteFacingBacktrackEnterDot : RouteFacingBacktrackEnterDotFallback;
	const float ExitDot = Tuning ? Tuning->RouteFacingBacktrackExitDot : RouteFacingBacktrackExitDotFallback;

	// Asymmetric by design: entering needs travel clearly OPPOSED to the route, leaving needs it
	// clearly WITH it. A single threshold flapped every time a lateral formation adjustment swung the
	// velocity across it, and every flip is a visible yaw swing — body yaw follows the Gameplay focal
	// because the companion runs bUseControllerDesiredRotation.
	bRouteFacingBacktracking = bRouteFacingBacktracking ? (Alignment < ExitDot) : (Alignment <= EnterDot);
	return bRouteFacingBacktracking;
}

void UBTService_UpdateCompanionState::ReleaseRouteFacingFocal(ACompanionAIController& Controller)
{
	// Drop the latch on every release path: whatever ended the reference (out of range, another
	// storey, a cleared trigger) also ends the catch-up read, and a stale latch would face the next
	// reference backwards for a tick on resume.
	bRouteFacingBacktracking = false;
	if (!bRouteFacingOwnsFocal) return;
	bRouteFacingOwnsFocal = false;

	// Compare-guarded like every other focal release in this file: only clear a focal that is still
	// the one we wrote, so an owner that already claimed it this tick is left untouched.
	if (Controller.GetFocalPointForPriority(EAIFocusPriority::Gameplay)
		.Equals(LastNonCombatFocalPoint, OverwatchFocalMatchTolerance))
	{
		Controller.ClearFocus(EAIFocusPriority::Gameplay);
	}
	LastNonCombatFocalPoint = FVector::ZeroVector;
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
	// Null tuning = no DA assigned: treat as disabled rather than running the tier unconfigured.
	if (!Tuning || !Tuning->bAmbientFacingEnabled) return;

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
			SetNonCombatFocalDeduped(Controller, LookAhead);
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
	SetNonCombatFocalDeduped(Controller, IdleFocal);
}

void UBTService_UpdateCompanionState::SetNonCombatFocalDeduped(
	ACompanionAIController& Controller, const FVector& Point)
{
	constexpr float DedupDistSq = 25.f * 25.f;

	// Distance alone is not a sufficient dedup. Five bare ClearFocus(Gameplay) calls live outside
	// this arbitration (the combat task's teardown and its per-tick clear, the cover statics, the
	// explore task, the follow-route task), and any of them can land on a tick this function does NOT
	// Tier-0-yield on. After one of those the focal is gone while the cache still holds the point we
	// last wrote, so a pure distance test never re-asserts and a companion standing still keeps a
	// dead yaw for as long as it stays put. Re-assert unless the controller's LIVE focal is still
	// the one we cached.
	const bool bPointUnchanged = FVector::DistSquared(LastNonCombatFocalPoint, Point) < DedupDistSq;
	const bool bFocalStillOurs = Controller.GetFocalPointForPriority(EAIFocusPriority::Gameplay)
		.Equals(LastNonCombatFocalPoint, OverwatchFocalMatchTolerance);
	if (bPointUnchanged && bFocalStillOurs) return;

	LastNonCombatFocalPoint = Point;
	Controller.SetFocalPoint(Point, EAIFocusPriority::Gameplay);
}
