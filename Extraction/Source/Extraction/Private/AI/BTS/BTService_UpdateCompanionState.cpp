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
#include "World/DoorBase.h"              // post-combat overwatch aims at the chokepoint door
#include "World/DoorRegistrySubsystem.h" // portal-path door lookup toward the threat memory
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

namespace
{
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
						Companion->Bark(ECompanionBarkType::LostContact);
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
			// No enemies at all — combat is ending. Release cover so the companion stands up and
			// the cover slot becomes available for the next engagement. Cover is cleared only when
			// no target remains (combat ending); a live-but-temporarily-unperceived target retains
			// both the BB target and the cover slot so the CoverSwitchMonitor stays active.
			BB->SetValueAsBool(HasCoverPositionKey.SelectedKeyName, false);
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
		for (AActor* Attached : SelectIgnoredAttached)
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
			// Lower the weapon the moment the hold releases (or never engaged) — the
			// ExploreReturnDelay below is BT posture stability, not a reason to hold a stale ADS
			// pose aimed at a dead enemy's bearing. Edge-guarded by bLoweredOnTargetLoss (reset
			// whenever a target/threat is live) so the lower still fires after a branch switch
			// mid-accrual (e.g. stealth re-pin -> mode change). Yields to a commanded takedown,
			// which owns aim/focus while armed.
			if (bWaveHold && !bTakedownOwnsAim)
			{
				// Wave hold keeps the gun up through the gaps between squad spawns: the fight is
				// demonstrably not over, so the usual "last target died, lower it" edge must not fire.
				// Overwatch normally owns this, but its anchor radius can legitimately be exceeded
				// while the far wider wave leash still holds, leaving nobody to raise the weapon.
				Companion->SetLowReadyAim(false);
			}
			else if (!bLoweredOnTargetLoss && !bTakedownOwnsAim)
			{
				Companion->SetLowReadyAim(true);
				// Same stale-aim-target backstop as the stealth re-pin branch above.
				Companion->SetAimTarget(nullptr);
				Controller->ClearFocus(EAIFocusPriority::Gameplay);
				bLoweredOnTargetLoss = true;
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
			Companion->SetLowReadyAim(false); // assert combat presentation
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

			// Two-ring threat test. Inner ring (ReviveHardThreatRadius): any alerted enemy is hot
			// unconditionally — that close it would see the revive start. Outer ring (out to
			// ReviveThreatRadius): only a Combat-state enemy WITH an eye-line to the body is hot.
			// Searching enemies in the outer ring never hold the window shut — post-fight survivors
			// wandering the area blocked every quiet-scene revive until desperation.
			const float HardRadiusSq = FMath::Square(Companion->ReviveHardThreatRadius);
			for (const FOverlapResult& Overlap : ReviveOverlaps)
			{
				const AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Overlap.GetActor());
				if (!IsValid(Enemy)) continue;
				if (!Enemy->IsAlertedForCompanionReadiness()) continue;
				const UHealthComponent* EHP = Enemy->GetHealthComponent();
				if (EHP && EHP->IsDead()) continue;

				if (FVector::DistSquared(Enemy->GetActorLocation(), PlayerLoc) <= HardRadiusSq)
				{
					bHot = true;
					break;
				}

				const AEnemyAIController* RingAIC = Cast<AEnemyAIController>(Enemy->GetController());
				const UEnemyAwarenessComponent* RingAwareness = RingAIC ? RingAIC->GetAwarenessComponent() : nullptr;
				if (!RingAwareness || RingAwareness->GetAwarenessState() != EEnemyAwarenessState::Combat) continue;

				FHitResult RingLosHit;
				FCollisionQueryParams RingLosParams(SCENE_QUERY_STAT(ReviveWindowRingLoS), true);
				RingLosParams.AddIgnoredActor(Enemy);
				RingLosParams.AddIgnoredActor(PlayerPawn);
				const bool bRingLosBlocked = Companion->GetWorld()->LineTraceSingleByChannel(
					RingLosHit, Enemy->GetPawnViewLocation(), PlayerLoc, ECC_Visibility, RingLosParams);
				if (!bRingLosBlocked || RingLosHit.GetActor() == PlayerPawn)
				{
					bHot = true;
					break;
				}
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
			// Under-fire requirement: low HP alone must not abort a committed rescue — only bail while
			// actually being shot (recent attacker inside the window). 0 disables the requirement.
			const bool bUnderFire = Companion->RescueBailUnderFireWindow <= 0.f
				|| IsValid(Companion->GetRecentAttacker(Companion->RescueBailUnderFireWindow));
			const bool bBail = bLastReviveWindowOpen && bHot && !bDesperation && bUnderFire
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
		bLastReviveClaimRefused = false;
		Companion->SetRescueCommitted(false);
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
// F3 + F1 — non-combat facing arbitration
// ---------------------------------------------------------------------------

namespace
{
	// Post-combat overwatch feel constants (structural, not designer levers — those live on
	// UCompanionTuningDataAsset under Companion|PostCombatOverwatch).
	constexpr float OverwatchBreakSpeed = 60.f;        // cm/s of self-movement that counts as a real move
	constexpr float OverwatchMovingBreakTime = 0.3f;   // sustained-move seconds before the hold breaks
	constexpr float OverwatchDoorAimZOffset = 110.f;   // chest height through the doorway, not the floor pivot
	constexpr float OverwatchDoorMaxZDelta = 250.f;    // same-storey gate — the portal metric is 3D and can pick a stairwell door
	constexpr float OverwatchDoorMinDist = 200.f;      // companion standing in the doorway itself — aiming at it points at its own feet
	constexpr float OverwatchNearWallPullback = 50.f;  // aim just short of a blocking wall on the bearing fallback
	constexpr float OverwatchDoorTowardThreatMinDot = 0.1f; // door must lie roughly toward the threat, not behind us
	constexpr int32 OverwatchDoorCandidateCount = 4;   // portal candidates pulled from the registry per pick
	// Sanity floor on the blocked-trace fallback: a hit nearer than this is the ally's own cover, and
	// pulling back from it puts the aim point half a metre in front of its face. That is what made the
	// extraction VIP look like it was staring into a wall. Below this distance there is no honest
	// "cover the direction they came from" read at all, so overwatch is declined outright.
	constexpr float OverwatchMinAimStandoff = 200.f;

	// Tolerance (cm) for every "is this focal point still the one WE wrote" compare in the file:
	// overwatch end, Tier-0-yield hand-off, and watch-drop clean-up.
	constexpr float OverwatchFocalMatchTolerance = 1.f;

	// Consecutive failed aim-point refreshes before the hold is dropped. A refusal is one trace
	// sample on a ~1s cadence, so ending on the first one had no debounce at all: anything transient
	// in the firing line cycled overwatch END/START.
	constexpr int32 OverwatchRefreshFailuresToEnd = 2;

	// How many pawns the overwatch traces step past before giving up. Bounds the re-trace loop — a
	// firing line with five bodies standing in it is not a standoff measurement worth salvaging.
	constexpr int32 OverwatchTracePawnSkipLimit = 4;

	// First GEOMETRY hit along a line; pawns are stepped past instead of counted as walls. Characters
	// block ECC_Visibility, so the player or another ally crossing within the standoff made the aim
	// resolve fail and ended overwatch, then entry re-entered once they cleared — an END/START cycle
	// driven by nothing but the player's footwork. Params is by value: each skipped pawn joins its
	// ignore list before the re-trace. Returns false when the line holds nothing but pawns.
	bool TraceGeometryPastPawns(const UWorld& World, const FVector& Start, const FVector& End,
		FCollisionQueryParams Params, FHitResult& OutHit)
	{
		for (int32 Attempt = 0; Attempt <= OverwatchTracePawnSkipLimit; ++Attempt)
		{
			if (!World.LineTraceSingleByChannel(OutHit, Start, End, ECC_Visibility, Params)) return false;

			const AActor* HitActor = OutHit.GetActor();
			if (!IsValid(HitActor)) return true;

			// A weapon or other attached actor rides its owner — same "not a wall" read.
			const AActor* HitOwner = HitActor->GetOwner();
			if (!HitActor->IsA<APawn>() && !(IsValid(HitOwner) && HitOwner->IsA<APawn>())) return true;

			Params.AddIgnoredActor(HitActor);
		}
		// Out of skips: treat the last hit as geometry rather than claim a clear line.
		return true;
	}
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
	if (!bHold && bLastWaveHold) Companion.UnCrouch();

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
	const bool bWaveHold = Companion.IsWaveHoldActive();
	const float AnchorRadius = bWaveHold ? Tuning->WaveHoldLeashDistance : Tuning->OverwatchBreakDistance;

	const bool bPlayerAnchored = IsValid(PlayerPawn) && !bPlayerDBNO
		&& FVector::DistSquared(Companion.GetActorLocation(), PlayerPawn->GetActorLocation())
			<= FMath::Square(AnchorRadius);
	const bool bOwnerElsewhere = bTakedownOwnsAim || Companion.IsRevivingPlayer()
		|| Companion.GetIsCompanionDBNO();

	if (!bOverwatchActive)
	{
		// Enter only on the fresh target-loss edge (a branch switch that already lowered must not
		// re-raise the gun), standing still, player nearby, with threat memory to aim from.
		// The lowered-edge veto is waived under a wave hold: there the gun SHOULD come back up
		// between squads, so a lower from an earlier lull must not lock overwatch out for the wave.
		if ((bLoweredOnTargetLoss && !bWaveHold) || bOwnerElsewhere || !bPlayerAnchored) return false;
		if (Companion.GetVelocity().Size2D() > OverwatchBreakSpeed) return false;

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
	if (bOwnerElsewhere || !bPlayerAnchored
		|| OverwatchMovingTime > OverwatchMovingBreakTime
		|| bTimedOut)
	{
		if (bDebugLogging)
			UE_LOG(LogCompanionAI, Log, TEXT("%s: post-combat overwatch END t=%.1f (anchored=%d moveT=%.1f)"),
				*Companion.GetName(), OverwatchElapsed, (int32)bPlayerAnchored, OverwatchMovingTime);
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

	Controller.SetFocalPoint(OverwatchAimPoint, EAIFocusPriority::Gameplay);
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
		// one let anything transient in the firing line cycle overwatch END/START. Keep asserting the
		// last justified point until the refusal repeats.
		++OverwatchRefreshFailures;
		return OverwatchRefreshFailures < OverwatchRefreshFailuresToEnd;
	}

	OverwatchRefreshFailures = 0;
	OverwatchAimPoint = Refreshed;
	return true;
}

bool UBTService_UpdateCompanionState::ResolveOverwatchThreatRef(
	const ACompanionCharacter& Companion, const UCompanionTuningDataAsset* Tuning, FVector& OutThreatRef) const
{
	// Freshest threat point that isn't at our feet; else first contact; else a point pushed out along
	// the bearing of whatever memory we do have.
	const FVector CompLoc = Companion.GetActorLocation();
	const float MinDistSq = FMath::Square(Tuning->OverwatchMinThreatDist);

	if (bHasLastThreatLocation && FVector::DistSquared2D(LastThreatLocation, CompLoc) > MinDistSq)
	{
		OutThreatRef = LastThreatLocation;
		return true;
	}
	if (bHasFightFirstContact && FVector::DistSquared2D(FightFirstContactLocation, CompLoc) > MinDistSq)
	{
		OutThreatRef = FightFirstContactLocation;
		return true;
	}
	if (bHasLastThreatLocation || bHasFightFirstContact)
	{
		const FVector Src = bHasLastThreatLocation ? LastThreatLocation : FightFirstContactLocation;
		const FVector Dir = (Src - CompLoc).GetSafeNormal2D();
		if (Dir.IsNearlyZero()) return false;
		OutThreatRef = CompLoc + Dir * Tuning->OverwatchBearingDistance;
		return true;
	}

	// No fight memory at all -- wave fallback: derive from the director's spawn zone so the ally
	// covers the door the waves come through even when its own fight memory was reset.
	const UWorld* World = Companion.GetWorld();
	const UEnemyDirectorSubsystem* Director = World ? World->GetSubsystem<UEnemyDirectorSubsystem>() : nullptr;
	FVector WaveThreat;
	if (!Director || !Director->IsWaveActive() || !Director->GetWaveThreatReference(WaveThreat)) return false;
	if (FVector::DistSquared2D(WaveThreat, CompLoc) <= MinDistSq) return false;

	OutThreatRef = WaveThreat;
	return true;
}

bool UBTService_UpdateCompanionState::PickOverwatchDoorAim(
	const ACompanionCharacter& Companion, const UCompanionTuningDataAsset* Tuning,
	const FVector& ThreatRef, const FCollisionQueryParams& LosParams, FVector& OutAim) const
{
	const UWorld* World = Companion.GetWorld();
	UDoorRegistrySubsystem* DoorRegistry = World ? World->GetSubsystem<UDoorRegistrySubsystem>() : nullptr;
	if (!DoorRegistry) return false;

	const FVector CompLoc = Companion.GetActorLocation();
	TArray<ADoorBase*> Doors;
	Doors.Reserve(OverwatchDoorCandidateCount);
	DoorRegistry->CollectPortalCandidates(CompLoc, ThreatRef, OverwatchDoorCandidateCount, Doors);

	const FVector Eye = Companion.GetPawnViewLocation();
	const FVector ToThreat2D = (ThreatRef - CompLoc).GetSafeNormal2D();
	for (ADoorBase* Door : Doors)
	{
		if (!IsValid(Door)) continue;
		const FVector DoorLoc = Door->GetActorLocation();
		const float DoorDistSq = FVector::DistSquared2D(DoorLoc, CompLoc);
		if (DoorDistSq > FMath::Square(Tuning->OverwatchDoorMaxDist)) continue;
		if (DoorDistSq < FMath::Square(OverwatchDoorMinDist)) continue;
		if (FMath::Abs(DoorLoc.Z - CompLoc.Z) > OverwatchDoorMaxZDelta) continue;
		if (FVector::DotProduct((DoorLoc - CompLoc).GetSafeNormal2D(), ToThreat2D)
			< OverwatchDoorTowardThreatMinDot) continue;
		// The door panel itself may block the eye-line (closed door) — that's still a valid "cover the
		// door" aim; only a wall between us and the doorway disqualifies it. Pawns don't disqualify it
		// either: the player walking through the line must not silently re-pick the aim point.
		FCollisionQueryParams DoorLosParams = LosParams;
		DoorLosParams.AddIgnoredActor(Door);
		const FVector Aim = DoorLoc + FVector(0.f, 0.f, OverwatchDoorAimZOffset);
		FHitResult Hit;
		if (TraceGeometryPastPawns(*World, Eye, Aim, DoorLosParams, Hit)) continue;

		OutAim = Aim;
		return true;
	}
	return false;
}

bool UBTService_UpdateCompanionState::ComputeOverwatchAimPoint(
	const ACompanionCharacter& Companion, const UCompanionTuningDataAsset* Tuning, FVector& OutAimPoint) const
{
	const UWorld* World = Companion.GetWorld();
	if (!World) return false;

	FVector ThreatRef;
	if (!ResolveOverwatchThreatRef(Companion, Tuning, ThreatRef)) return false;

	FCollisionQueryParams LosParams(SCENE_QUERY_STAT(CompanionOverwatchLoS), true);
	LosParams.AddIgnoredActor(&Companion);
	LosParams.AddIgnoredActor(Companion.GetCurrentWeapon());

	// Chokepoint first: nearest same-storey door on the portal path toward the threat, visible
	// from here — "cover the door they came through".
	if (PickOverwatchDoorAim(Companion, Tuning, ThreatRef, LosParams, OutAimPoint)) return true;

	// No usable door: aim at the threat point directly; a wall on the way pulls the aim just short
	// of it, which still reads as covering the direction the fight came from — but only while the
	// wall is far enough away for that to be true. Blocked inside the standoff means the ally is
	// tucked against its own cover; there is no aim point to hold, so decline rather than plant one
	// in the geometry and hold it (with the wave-hold cap waived, that state never expired).
	// Pawn-transparent: the player or another ally stepping inside the standoff is not cover, and
	// counting them as a wall cycled overwatch END/START on nothing but footwork.
	const FVector Eye = Companion.GetPawnViewLocation();
	FHitResult Hit;
	const bool bBlocked = TraceGeometryPastPawns(*World, Eye, ThreatRef, LosParams, Hit);
	if (bBlocked && FVector::DistSquared(Hit.ImpactPoint, Eye) < FMath::Square(OverwatchMinAimStandoff))
		return false;

	OutAimPoint = bBlocked
		? Hit.ImpactPoint + (Eye - Hit.ImpactPoint).GetSafeNormal() * OverwatchNearWallPullback
		: ThreatRef;
	return true;
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
	const bool bTierZeroYield = bHasTarget || (bReadyOnlyThreat && bReadyThreatOwnsFacing)
		|| bOverwatchActive // post-fight hold owns aim/focus for its whole window
		|| Companion.IsWaveHoldActive() // wave hold owns weapon-up; ambient facing must not compete
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
		// bWatchStanceApplied covers the fight-signal fallback watch, which deliberately keeps no
		// linger (freshness is its hold, so linger is always 0 on that path) — without it a
		// fallback-watch focal survives into a route/command approach and back-walks it.
		const bool bFocalIsOurs = !bHasTarget && !bReadyOnlyThreat
			&& ((!LastAmbientFocalPoint.IsZero() && CurrentFocal.Equals(LastAmbientFocalPoint, OverwatchFocalMatchTolerance))
				|| ((WatchThreatLingerRemaining > 0.f || bWatchStanceApplied)
					&& CurrentFocal.Equals(WatchThreatLocation, OverwatchFocalMatchTolerance)));
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
	if (ComputeWatchThreat(Controller, Companion, WatchCandidateEnemy, Tuning, DeltaSeconds))
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

		if (Controller.GetFocalPointForPriority(EAIFocusPriority::Gameplay).Equals(WatchThreatLocation, OverwatchFocalMatchTolerance))
			Controller.ClearFocus(EAIFocusPriority::Gameplay);
		WatchThreatLocation = FVector::ZeroVector;
		WatchedEnemy = nullptr;
	}

	// Tier 2 (F1): ambient path look-ahead / idle attention-yaw facing.
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
	const float SignalMaxAge = Tuning ? Tuning->FightSignalMaxAge : 4.f;
	FVector AlertedLocation;
	if (Controller.GetRecentAlertedThreat(SignalMaxAge, AlertedLocation))
	{
		WatchedEnemy = nullptr;
		WatchThreatLocation = AlertedLocation;
		return true;
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
	const bool bFreshWatch = !bWatchStanceApplied;
	bWatchStanceApplied = true;
	bWatchStanceStealth = bStealthNow;

	// Fresh stealth watch = hostile spotted while sneaking — whisper it once per watch entry
	// (a mode flip mid-watch re-applies stance but is not a new sighting). Fallback watches
	// (WatchedEnemy null — fight-signal bearing only) never bark: nothing was actually spotted.
	if (bFreshWatch && bStealthNow && WatchedEnemy.IsValid())
		Companion.Bark(ECompanionBarkType::StealthSpotEnemy);

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
