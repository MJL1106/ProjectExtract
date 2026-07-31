// Companion-only cover helpers — see header for the sharing contract.

#include "AI/CompanionCoverStatics.h"
#include "AI/AITargetingStatics.h"
#include "AI/CompanionAIController.h"
#include "AI/CompanionTuningDataAsset.h"
#include "Companion/CompanionCharacter.h"
#include "Components/CapsuleComponent.h"
#include "SuppressionComponent.h"
#include "HealthComponent.h"
#include "CoverSystem.h"
#include "CoverGeometryStatics.h"
#include "CoverReservationSubsystem.h"
#include "AI/Cover/CoverScoringStatics.h"
#include "World/DoorRegistrySubsystem.h"
#include "ExtractionTypes.h"
#include "WeaponBase.h"
#include "AIController.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Companion/CompanionCommandTypes.h"
#include "GameplayTagAssetInterface.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISense_Sight.h"

namespace CompanionCover
{

// Few-enemy Combat gate: true when the companion is in Combat mode with fewer than the
// outnumbered threshold of known threats. Uses the same CountKnownThreats value and
// CombatOutnumberedCount the outnumbered trigger uses, so the >= path is provably untouched.
static bool IsFewEnemyCombat(const ACompanionCharacter& Companion,
	const UCompanionTuningDataAsset& Tuning, int32 KnownThreatCount)
{
	if (!Tuning.bFewEnemyCombatMobileCover) return false;
	if (!Tuning.bCombatModeStricterCommit) return false;
	if (Companion.GetMode() != ECompanionMode::Combat) return false;
	return KnownThreatCount < Tuning.CombatOutnumberedCount;
}

// Wounded-fire pairing: "is anyone actually shooting at us". The bar is deliberately the
// pressure-SPIKE bar (OR, not the trigger's AND) — lower reopens the ~8s duck-in/duck-out
// cycle the widened windows exist to stop. Release variant widens exactly like every other
// clause (window x2, one fewer hit, suppression release-scaled) so a lull between bursts
// cannot pop the companion out.
static bool HasWoundedFirePairing(const ACompanionCharacter& Companion,
	const UCompanionTuningDataAsset& Tuning, bool bForRelease)
{
	const float SuppBar = bForRelease
		? Tuning.FewEnemyWoundedCoverSuppressionFrac * Tuning.UnderFireSuppressionReleaseScale
		: Tuning.FewEnemyWoundedCoverSuppressionFrac;
	if (Companion.GetSuppression01() >= SuppBar) return true;

	const float Window = bForRelease
		? Tuning.CoverCommitUnderFireWindow * 2.f
		: Tuning.CoverCommitUnderFireWindow;
	const int32 HitsNeeded = bForRelease
		? FMath::Max(1, Tuning.FewEnemyWoundedCoverDamageHits - 1)
		: Tuning.FewEnemyWoundedCoverDamageHits;
	return Companion.GetRecentDamageCount(Window) >= HitsNeeded;
}

FCoverTriggers EvaluateTriggers(const ACompanionCharacter& Companion,
	const UCompanionTuningDataAsset& Tuning, int32 KnownThreatCount, bool bForRelease)
{
	FCoverTriggers Out;
	const bool bCombatStrict = Tuning.bCombatModeStricterCommit
		&& Companion.GetMode() == ECompanionMode::Combat;

	// Under fire = graded pressure. The old check tripped on ANY single hit in the window, which
	// made the commit gate pass near-permanently in a firefight — the 100%-cover bug. Release
	// doubles the window and needs one fewer hit — without the widening a lull between bursts pops
	// the companion out, the next burst re-commits it to a DIFFERENT point (old one on post-vacate
	// cooldown) and it cover-hops every ~8s.
	const float FireWindow = bForRelease
		? Tuning.CoverCommitUnderFireWindow * 2.f
		: Tuning.CoverCommitUnderFireWindow;
	const float Supp01 = Companion.GetSuppression01();
	const int32 RecentHits = Companion.GetRecentDamageCount(FireWindow);
	// Suppression release margin: in-cover suppression decays whenever the enemy pauses fire, so an
	// unwidened release bar pops the companion out mid-fight and the next burst re-commits it to a
	// DIFFERENT point — the same ~8s hop the window doubling exists to stop.
	const float SuppFracBase = bCombatStrict
		? Tuning.CombatUnderFireSuppressionFrac
		: Tuning.UnderFireSuppressionFrac;
	const float SuppFrac = bForRelease
		? SuppFracBase * Tuning.UnderFireSuppressionReleaseScale
		: SuppFracBase;
	if (bCombatStrict)
	{
		// Combat mode = mobile/aggressive: only heavy pressure (sustained suppression AND real
		// hits together) forces cover. Anything less and it keeps move-shooting.
		const int32 HitsNeeded = bForRelease
			? FMath::Max(1, Tuning.CombatUnderFireDamageHits - 1)
			: Tuning.CombatUnderFireDamageHits;
		Out.bUnderFire = Supp01 >= SuppFrac && RecentHits >= HitsNeeded;
	}
	else
	{
		const int32 HitsNeeded = bForRelease
			? FMath::Max(1, Tuning.UnderFireDamageHits - 1)
			: Tuning.UnderFireDamageHits;
		Out.bUnderFire = Supp01 >= SuppFrac || RecentHits >= HitsNeeded;
	}

	const float HealthThresh = bForRelease
		? FMath::Max(Tuning.CoverTriggerHealthReleaseFrac, Tuning.CoverTriggerHealthFrac)
		: Tuning.CoverTriggerHealthFrac;
	Out.bLowHealth = Companion.GetHealthFraction() < HealthThresh;

	// Few-enemy Combat: being wounded is not on its own a reason to hide from one or two enemies —
	// require live incoming fire (suppression spike or hit burst) as well. This is also the CAMP fix:
	// low health passes commit in ALL modes and blocks release via IsStrongPressure, and regen delay
	// resets on every chip hit so it never clears mid-fight. The fire pairing breaks that deadlock.
	if (Out.bLowHealth && IsFewEnemyCombat(Companion, Tuning, KnownThreatCount)
		&& !HasWoundedFirePairing(Companion, Tuning, bForRelease))
		Out.bLowHealth = false;

	// Reloading is RELEASE-ONLY: already behind cover, stay there through the reload — but never
	// run to cover from the open just because a reload started (reload on the move instead).
	// Combat mode never commits on ammo at all.
	const float AmmoThresh = bForRelease
		? FMath::Max(Tuning.CoverTriggerAmmoReleaseFrac, Tuning.CoverTriggerLowAmmoFrac)
		: Tuning.CoverTriggerLowAmmoFrac;
	if (bForRelease)
		Out.bLowAmmoOrReloading = Companion.IsReloading() || Companion.GetAmmoFraction() < AmmoThresh;
	else if (!bCombatStrict)
		Out.bLowAmmoOrReloading = Companion.GetAmmoFraction() < AmmoThresh;

	// Release keeps the trigger "active" one threat below the commit count, so the companion
	// doesn't pop out the moment a third attacker dies and duck back when it re-perceives one.
	const int32 BaseCount = bCombatStrict
		? Tuning.CombatOutnumberedCount
		: Tuning.CoverTriggerOutnumberedCount;
	const int32 CountThresh = bForRelease ? FMath::Max(2, BaseCount - 1) : BaseCount;
	Out.bOutnumbered = KnownThreatCount >= CountThresh;

	return Out;
}

int32 OutnumberedCountCap(const UCompanionTuningDataAsset& Tuning)
{
	return FMath::Max(Tuning.CoverTriggerOutnumberedCount, Tuning.CombatOutnumberedCount);
}

bool HasPressureSpiked(const ACompanionCharacter& Companion,
	const UCompanionTuningDataAsset& Tuning, int32 KnownThreatCount)
{
	// Mode-aware bars: Combat's stricter commit thresholds must apply here too, or a 3-visible-enemy
	// Combat fight (below its commit bar, so it's open-engaging and hopping) reads as "strong
	// pressure" the moment a hop lands — blocking the quick release and turning the touch point
	// into a camp.
	const bool bCombatStrict = Tuning.bCombatModeStricterCommit
		&& Companion.GetMode() == ECompanionMode::Combat;
	const int32 HitsBar = bCombatStrict ? Tuning.CombatUnderFireDamageHits : Tuning.UnderFireDamageHits;
	const int32 CountBar = bCombatStrict ? Tuning.CombatOutnumberedCount : Tuning.CoverTriggerOutnumberedCount;

	if (Companion.GetSuppression01() >= Tuning.CoverNaturalReleasePressureFrac) return true;
	if (Companion.GetRecentDamageCount(Tuning.CoverCommitUnderFireWindow) >= HitsBar) return true;
	return KnownThreatCount >= CountBar;
}

bool IsStrongPressure(const ACompanionCharacter& Companion,
	const UCompanionTuningDataAsset& Tuning, int32 KnownThreatCount)
{
	// Low health is a static condition, not a spike — it belongs only on the release-blocking
	// side (hold working cover while wounded), never on the recommit-bypass side.
	// Few-enemy Combat: the static low-health release-blocker also requires fire pairing —
	// without it the 4s Combat natural release never fires while wounded and the companion
	// camps until regen crosses 50% (the regen delay resets on every chip hit).
	if (Companion.GetHealthFraction() < Tuning.CoverTriggerHealthFrac
		&& (!IsFewEnemyCombat(Companion, Tuning, KnownThreatCount)
			|| HasWoundedFirePairing(Companion, Tuning, /*bForRelease=*/true)))
		return true;
	return HasPressureSpiked(Companion, Tuning, KnownThreatCount);
}

bool LowHealthDashAllowed(UWorld* World, const FVector& PawnLoc, const AController* Querier,
	const UCompanionTuningDataAsset& Tuning, const FCoverTriggers& Triggers)
{
	// Gate only the low-HP-alone case: pinned/outnumbered companions still get the normal commit
	// range — the danger being avoided is a wounded companion sprinting open ground for a duck spot.
	if (!Triggers.LowHealthOnly()) return true;
	if (Tuning.LowHealthCoverMaxDash <= 0.f) return true;
	return DistToNearestCover(World, PawnLoc, Tuning.LowHealthCoverMaxDash, Querier) >= 0.f;
}

int32 CountKnownThreats(AAIController* Controller, int32 Cap)
{
	if (Cap <= 0) return 0;
	UAIPerceptionComponent* Perception = Controller ? Controller->GetPerceptionComponent() : nullptr;
	if (!Perception) return 0;

	TArray<AActor*> Perceived;
	Perception->GetCurrentlyPerceivedActors(UAISense_Sight::StaticClass(), Perceived);

	int32 Count = 0;
	for (const AActor* Actor : Perceived)
	{
		if (!IsValid(Actor)) continue;

		const IGameplayTagAssetInterface* TagIface = Cast<IGameplayTagAssetInterface>(Actor);
		if (!TagIface) continue;
		FGameplayTagContainer Tags;
		TagIface->GetOwnedGameplayTags(Tags);
		if (!Tags.HasTag(TAG_Character_Enemy)) continue;

		const UHealthComponent* Health = Actor->FindComponentByClass<UHealthComponent>();
		if (Health && Health->IsDead()) continue;

		if (++Count >= Cap) break;
	}
	return Count;
}

void GatherExtraThreatActors(AAIController* Controller, const APawn* Pawn, const AActor* FocusTarget,
	int32 MaxExtra, TArray<AActor*, TInlineAllocator<8>>& OutActors)
{
	OutActors.Reset();
	if (MaxExtra <= 0 || !IsValid(Pawn)) return;

	UAIPerceptionComponent* Perception = Controller ? Controller->GetPerceptionComponent() : nullptr;
	if (!Perception) return;

	TArray<AActor*> Perceived;
	Perception->GetCurrentlyPerceivedActors(UAISense_Sight::StaticClass(), Perceived);

	// Perception can hand back null/pending-kill entries; the Sort lambda derefs by const-ref.
	Perceived.RemoveAll([](const AActor* A) { return !IsValid(A); });

	const FVector PawnLoc = Pawn->GetActorLocation();
	Perceived.Sort([PawnLoc](const AActor& A, const AActor& B)
	{
		return FVector::DistSquared(PawnLoc, A.GetActorLocation()) < FVector::DistSquared(PawnLoc, B.GetActorLocation());
	});

	for (AActor* Actor : Perceived)
	{
		if (!IsValid(Actor) || Actor == FocusTarget) continue;

		const IGameplayTagAssetInterface* TagIface = Cast<IGameplayTagAssetInterface>(Actor);
		if (!TagIface) continue;
		FGameplayTagContainer Tags;
		TagIface->GetOwnedGameplayTags(Tags);
		if (!Tags.HasTag(TAG_Character_Enemy)) continue;

		const UHealthComponent* Health = Actor->FindComponentByClass<UHealthComponent>();
		if (Health && Health->IsDead()) continue;

		OutActors.Add(Actor);
		if (OutActors.Num() >= MaxExtra) break;
	}
}

int32 CountUncoveredThreats(UWorld* World, const FCoverData& Cover, float Standoff,
	float ChestHeight, const APawn* Pawn, TArrayView<AActor* const> ExtraThreatActors)
{
	int32 Uncovered = 0;
	for (AActor* const ThreatActor : ExtraThreatActors)
	{
		if (!IsValid(ThreatActor)) continue;
		if (!UCoverGeometryStatics::IsThreatCovered(World, Cover, ThreatActor->GetActorLocation(),
			Standoff, ChestHeight, ThreatActor, Pawn))
			++Uncovered;
	}
	return Uncovered;
}

bool IsCoverCompromised(UWorld* World, const FCoverData& Cover, const AActor* Threat,
	const FVector& ThreatLoc, float ArcHalfAngleDeg, float ArcSlackDeg, float Standoff,
	float ChestHeight, const APawn* Pawn)
{
	if (!World || !IsValid(Threat)) return false;

	// Arc test: GetFireArcForward points toward the covered side — dot against to-threat direction.
	const FVector ToThreat = (ThreatLoc - Cover.Location).GetSafeNormal2D();
	const FVector FireFwd  = UCoverGeometryStatics::GetFireArcForward(Cover);
	const float Dot = FVector::DotProduct(FireFwd, ToThreat);
	if (Dot < FMath::Cos(FMath::DegreesToRadians(ArcHalfAngleDeg + ArcSlackDeg)))
		return true;

	// Body-shield test from the stable hunker position (not the live pawn chest, which oscillates
	// through the peek loop and flickers the verdict).
	return !UCoverGeometryStatics::IsThreatCovered(World, Cover, ThreatLoc, Standoff, ChestHeight, Threat, Pawn);
}

float DistToNearestCover(UWorld* World, const FVector& Point, float Radius, const AController* Querier)
{
	FVector Unused;
	if (!NearestCoverLocation(World, Point, Radius, Querier, Unused)) return -1.f;
	return FVector::Dist2D(Point, Unused);
}

bool NearestCoverLocation(UWorld* World, const FVector& Point, float Radius,
	const AController* Querier, FVector& OutLocation)
{
	if (!World || Radius <= 0.f) return false;
	ACoverSystem* CoverSys = ACoverSystem::GetCoverSystem(World);
	if (!CoverSys) return false;
	const UCoverReservationSubsystem* ResSub = World->GetSubsystem<UCoverReservationSubsystem>();
	const UDoorRegistrySubsystem* DoorRegistry = World->GetSubsystem<UDoorRegistrySubsystem>();

	TArray<FCover> Nearby;
	Nearby.Reserve(16);
	const FBoxSphereBounds Bounds(Point, FVector(Radius), Radius);
	CoverSys->GetCoverDataWithinBounds(Bounds, Nearby);

	float BestSq = -1.f;
	for (const FCover& Candidate : Nearby)
	{
		if (!Candidate.IsValid()) continue;
		const float DistSq = FVector::DistSquared2D(Point, Candidate.Data.Location);
		if (DistSq > FMath::Square(Radius)) continue; // bounds query is a box — enforce the sphere

		// A taken duck spot is no duck spot — skip points occupied/intended by anyone else.
		AController* Occupant = CoverSys->GetOccupyingController(Candidate.Handle);
		if (Occupant && Occupant != Querier) continue;
		if (IsValid(ResSub) && ResSub->IsCoverIntendedByOther(Candidate.Handle, Querier)) continue;

		// A duck spot behind a closed door is no duck spot either (same rule as the EQS
		// DoorCrossing filter) — it would inflate the low-HP dash gate's availability.
		if (IsValid(DoorRegistry) && DoorRegistry->AnyClosedDoorBlocksSegment(Point, Candidate.Data.Location)) continue;

		if (BestSq < 0.f || DistSq < BestSq)
		{
			BestSq = DistSq;
			OutLocation = Candidate.Data.Location;
		}
	}
	return BestSq >= 0.f;
}

FVector CompanionHunkerPosition(const ACompanionCharacter& Companion, const FCoverData& Data, float Standoff)
{
	const UWorld* World = Companion.GetWorld();
	const ACompanionAIController* AIC = Cast<ACompanionAIController>(Companion.GetController());
	const UCompanionTuningDataAsset* Tuning = AIC ? AIC->GetTuning() : nullptr;
	if (World && Tuning)
	{
		const UCapsuleComponent* Cap = Companion.GetCapsuleComponent();
		const float CapRadius = Cap ? Cap->GetScaledCapsuleRadius() : 34.f;
		return UCoverGeometryStatics::GetEdgeAlignedHunkerPosition(
			World, Data, Standoff, CapRadius, Tuning->CoverCornerGap, &Companion);
	}
	return UCoverGeometryStatics::GetHunkerPosition(Data, Standoff);
}

bool PathPassesNearThreat(const FVector& PawnLoc, const FVector& Dest,
	TArrayView<AActor* const> Threats, float Clearance)
{
	if (Clearance <= 0.f) return false;

	const float ClearanceSq = FMath::Square(Clearance);
	const FVector Start(PawnLoc.X, PawnLoc.Y, 0.f);
	const FVector End(Dest.X, Dest.Y, 0.f);
	for (AActor* const Threat : Threats)
	{
		if (!IsValid(Threat)) continue;
		const FVector ThreatLoc = Threat->GetActorLocation();
		if (FMath::PointDistToSegmentSquared(FVector(ThreatLoc.X, ThreatLoc.Y, 0.f), Start, End) <= ClearanceSq)
			return true;
	}
	return false;
}

/** Shared throttle for the approach-fire loop (matches the old BTTask_MoveToCoverPoint cadence). */
static constexpr float ApproachFireTickInterval = 0.1f;

void TickCoverApproachFire(ACompanionCharacter* Companion, AAIController* Controller,
	UBlackboardComponent* BB, FApproachFireState& State, float DeltaSeconds)
{
	if (!IsValid(Companion) || !IsValid(Controller) || !BB) return;

	State.TickAccum += DeltaSeconds;
	if (State.TickAccum < ApproachFireTickInterval) return;
	State.TickAccum = 0.f;

	const ACompanionAIController* CompCtrl = Cast<ACompanionAIController>(Controller);
	const UCompanionTuningDataAsset* Tuning = CompCtrl ? CompCtrl->GetTuning() : nullptr;
	AActor* Target = (Tuning && Tuning->bCoverApproachFireWhileMoving)
		? Cast<AActor>(BB->GetValueAsObject(ACompanionAIController::BB_CombatTarget)) : nullptr;

	bool bCanFire = false;
	if (IsValid(Target) && !Companion->IsReloading() && Controller->LineOfSightTo(Target))
	{
		if (AWeaponBase* W = Companion->GetCurrentWeapon())
		{
			FHitResult MuzzleHit;
			FCollisionQueryParams MuzzleParams;
			MuzzleParams.AddIgnoredActor(Companion);
			MuzzleParams.AddIgnoredActor(W);
			const bool bBlocked = Companion->GetWorld()->LineTraceSingleByChannel(MuzzleHit,
				W->GetMuzzleLocation(), Target->GetActorLocation() + FVector(0.f, 0.f, 50.f),
				ECC_Visibility, MuzzleParams);
			// A hit on the target or anything attached to it (held weapon) counts as clear.
			bCanFire = !bBlocked || MuzzleHit.GetActor() == Target
				|| (MuzzleHit.GetActor() && MuzzleHit.GetActor()->IsAttachedTo(Target));
		}
	}

	// BB retarget mid-move: aim/focus are latched per target — re-issue or fire streams at the
	// old target's position.
	if (State.bFiring && State.LatchedTarget.Get() != Target)
	{
		Companion->StopWeaponFire();
		State.bFiring = false;
	}

	if (bCanFire && !State.bFiring)
	{
		Companion->SetAimTarget(Target);
		Controller->SetFocus(Target);
		Companion->StartWeaponFire();
		State.bFiring = true;
		State.LatchedTarget = Target;
	}
	else if (!bCanFire && State.bFiring)
	{
		Companion->StopWeaponFire();
		Companion->SetAimTarget(nullptr);
		// Release the actor focus too: SetFocus(Target) re-resolves to the enemy's LIVE location
		// every tick with pitch preserved, so the torso and gun track the enemy through the wall
		// for the rest of the approach. Cleared unconditionally rather than compare-guarded: we
		// set it, we know it is ours, and no other system writes a Gameplay focal mid-approach.
		// If LOS is regained before arrival the bCanFire && !State.bFiring branch re-issues it.
		Controller->ClearFocus(EAIFocusPriority::Gameplay);
		State.bFiring = false;
	}
}

void StopCoverApproachFire(ACompanionCharacter* Companion, AAIController* Controller,
	FApproachFireState& State, bool bKeepFocus)
{
	if (IsValid(Companion))
	{
		if (State.bFiring) Companion->StopWeaponFire();
		Companion->SetAimTarget(nullptr);
	}
	// bKeepFocus is a HAND-OFF, not a licence to leak. It exists because arrival at a cover point used to
	// flick the torso off the threat in the gap between transit fire ending and the next owner taking
	// over facing, and a flick reads worse than a held torso. But the hand-off is only real while a
	// receiver exists: the intended receiver is the companion combat task, which runs off the blackboard
	// combat target and establishes no focus of its own on entry — so once that key stops naming the
	// actor this focus points at, NOBODY owns it. An actor focus re-resolves to the enemy's LIVE location
	// every tick with pitch preserved, straight through geometry, which is the "ally aims through walls
	// after the fight" defect: the target dies or clears on the frame the pawn arrives, the combat task
	// never runs, and the focus rides into FollowPlayer/overwatch tracking a hidden enemy.
	//
	// So honour bKeepFocus only while the blackboard still names the focused actor. Gated here rather
	// than flipped at the call sites: both callers want the same "hand over only if there is someone to
	// hand over to" contract, and a flag flip would have cost the arrival facing it was added for.
	if (IsValid(Controller))
	{
		bool bHandOffHasReceiver = false;
		if (bKeepFocus)
		{
			const AActor* FocusActor = Controller->GetFocusActorForPriority(EAIFocusPriority::Gameplay);
			const UBlackboardComponent* BB = Controller->GetBlackboardComponent();
			const AActor* BBTarget = BB
				? Cast<AActor>(BB->GetValueAsObject(ACompanionAIController::BB_CombatTarget)) : nullptr;
			bHandOffHasReceiver = IsValid(BBTarget) && IsValid(FocusActor) && BBTarget == FocusActor;
		}
		if (!bHandOffHasReceiver)
			Controller->ClearFocus(EAIFocusPriority::Gameplay);
	}
	State.Reset();
}

// Vertical half-extent for the ping cover search column. Cover points sit on the ground plane,
// but PingImpact can be metres above them on a tall wall. A cube of half-extent SearchRadius
// centred on the impact misses candidates below the impact. 1000cm covers ~2 storeys.
static constexpr float PingVerticalSearchHalfHeight = 1000.f;

bool FindCoverOnPingedWall(UWorld* World, const FVector& PingImpact, const FVector& PingNormal,
	float SearchRadius, const AController* Querier, const APawn* QuerierPawn,
	AActor* CombatTarget, FCover& OutCover)
{
	if (!World || !IsValid(QuerierPawn)) return false;

	ACoverSystem* CoverSys = ACoverSystem::GetCoverSystem(World);
	if (!CoverSys) return false;

	UCoverReservationSubsystem* ResSub = World->GetSubsystem<UCoverReservationSubsystem>();

	// Gather candidates in a tall vertical column around the ping impact. XY half-extent is the
	// search radius; Z is generous so a ping high on a wall still reaches ground-level points.
	// The AICS octree query uses only GetBox() (box extent), not the sphere radius.
	TArray<FCover> Candidates;
	Candidates.Reserve(32);
	const FVector ColumnExtent(SearchRadius, SearchRadius, PingVerticalSearchHalfHeight);
	const FBoxSphereBounds SearchBounds(PingImpact, ColumnExtent, ColumnExtent.Size());
	CoverSys->GetCoverDataWithinBounds(SearchBounds, Candidates);

	// Hostile anchors for claim-collision rejection (same rule as the EQS CoverIntent filter).
	FHostileAnchors PingHostileAnchors;
	const ACompanionAIController* CompAIC = Cast<ACompanionAIController>(Querier);
	const UCompanionTuningDataAsset* Tuning = CompAIC ? CompAIC->GetTuning() : nullptr;
	const bool bRejectHostileAdjacent = Tuning
		&& (Tuning->MinHostileCoverDistance > 0.f || Tuning->MinHostilePawnDistance > 0.f);
	if (bRejectHostileAdjacent)
		UCoverScoringStatics::GatherHostileAnchors(World, QuerierPawn, Querier, PingHostileAnchors);

	// Standoff for body-protection traces and corner-apex peek resolution.
	const ACharacter* QuerierChar = Cast<const ACharacter>(QuerierPawn);
	const UCapsuleComponent* Cap = QuerierChar ? QuerierChar->GetCapsuleComponent() : nullptr;
	const float CapRadius = Cap ? Cap->GetScaledCapsuleRadius() : 34.f;
	const float Standoff = CapRadius + 10.f;

	// Wall match threshold: cos(20 deg) = 0.9397. PingNormal points away from the surface, and
	// DirectionToWall points toward the wall. The matching dot is -PingNormal vs DirectionToWall:
	// a cover point "belongs to" the pinged face when its wall direction aligns with the face
	// within 20 degrees.
	static constexpr float WallMatchCosThreshold = 0.9397f;
	static constexpr float BodyProtectChestHeight = 60.f;
	const float SearchRadiusSq = SearchRadius * SearchRadius;

	// The search radius is lateral-only: cover points sit on the ground plane, so a ping high
	// on a tall wall has a vertical gap that would exhaust a 3D budget. The bounds query uses a
	// tall column (PingVerticalSearchHalfHeight Z) and the per-candidate check is DistSquared2D.
	// The ping trace range is the only hard distance limit; if the player can see it and hit it,
	// it is a legal order. The companion either paths there or the 12-second unreached-hold
	// timeout releases gracefully.
	const FVector PawnLoc = QuerierPawn->GetActorLocation();

	// ThreatLoc = actor location for body-protection traces (IsThreatCovered, ScoreCandidate).
	// ThreatSightLoc = head-height for peek LoS traces (CanPeekShoot) — centre-mass traces reject
	// candidates that could in fact shoot a standing enemy over its cover.
	const FVector ThreatLoc = IsValid(CombatTarget) ? CombatTarget->GetActorLocation() : FVector::ZeroVector;
	const FVector ThreatSightLoc = IsValid(CombatTarget) ? AITargeting::GetSightLocation(CombatTarget) : FVector::ZeroVector;
	static constexpr float StandFireEyeHeight = 140.f;

	// Scoring params for ScoreCandidate. Companion queriers get neutral defaults where
	// SideFlagWeight is 0 and ScoreCandidate's wall-end term is skipped entirely. This selector's
	// whole purpose is the end of the wall with an angle, so force it on.
	FCoverScoreParams ScoreParams = UCoverScoringStatics::GetParamsForQuerier(QuerierPawn);
	if (ScoreParams.SideFlagWeight <= 0.f) ScoreParams.SideFlagWeight = 2.f;

	// Hoisted outside the loop: the face direction from the ping does not change per candidate.
	const FVector FaceDir = (-PingNormal).GetSafeNormal2D();

	FCover BestCover;
	float BestScore = -FLT_MAX;
	int32 DbgWallMatch = 0, DbgOccupied = 0, DbgHostile = 0, DbgCombat = 0;

	for (const FCover& Candidate : Candidates)
	{
		if (!Candidate.IsValid()) continue;

		const FCoverData& Data = Candidate.Data;

		// Wall match: only keep candidates facing the same wall as the pinged surface.
		const FVector WallDir = Data.DirectionToWall.GetSafeNormal2D();
		if (FVector::DotProduct(FaceDir, WallDir) < WallMatchCosThreshold) continue;

		// Same-plane test: the angular check alone admits any parallel same-facing surface inside
		// the search sphere. Perpendicular offset from the pinged face must sit inside the bake's
		// standoff band (AICS TraceLength 100 + slack).
		static constexpr float WallPlaneToleranceCm = 150.f;
		if (FMath::Abs(FVector::DotProduct(Data.Location - PingImpact, PingNormal)) > WallPlaneToleranceCm) continue;

		// Lateral-only radius check: cover points sit on the ground plane and the ping impact can
		// be metres above them on a tall wall. 3D distance would eat the lateral allowance.
		if (FVector::DistSquared2D(PingImpact, Data.Location) > SearchRadiusSq) continue;

		// Vertical gate: the column is symmetric but the intent is one-directional -- cover sits
		// below the impact (player pings wall above the point). A stacked same-facing wall one or
		// two storeys up must not admit points from the wrong floor.
		static constexpr float PingCoverMaxAboveImpact = 150.f;
		static constexpr float PingCoverMaxBelowImpact = PingVerticalSearchHalfHeight;
		const float ZBelowImpact = PingImpact.Z - Data.Location.Z;
		if (ZBelowImpact < -PingCoverMaxAboveImpact || ZBelowImpact > PingCoverMaxBelowImpact) continue;

		++DbgWallMatch; // survived wall + plane + radius + Z -- this point is on the pinged wall

		// Occupancy gates.
		AController* Occupant = CoverSys->GetOccupyingController(Candidate.Handle);
		if (Occupant && Occupant != Querier) { ++DbgOccupied; continue; }
		if (IsValid(ResSub) && ResSub->IsCoverIntendedByOther(Candidate.Handle, Querier)) { ++DbgOccupied; continue; }
		if (IsValid(ResSub) && ResSub->IsOnPostVacateCooldown(Candidate.Handle, Querier,
			Tuning ? Tuning->CoverSwitchPostVacateCooldown : 0.f)) { ++DbgOccupied; continue; }

		// Hostile adjacency rejection.
		if (bRejectHostileAdjacent && UCoverScoringStatics::IsNearHostileAnchor(
			Data.Location, PingHostileAnchors, Tuning->MinHostileCoverDistance, Tuning->MinHostilePawnDistance))
		{ ++DbgHostile; continue; }

		if (IsValid(CombatTarget))
		{
			// With a combat target: hard-require peek-shootability and body protection.
			const ECoverHeight Height = UCoverGeometryStatics::GetCoverHeight(Data);
			const bool bCrouched = Height == ECoverHeight::Crouch;
			// Stand-height candidates: the fixed lateral offset from GetLeanPeekPosition lands
			// behind the wall -- the trace hits the cover wall itself and reports blocked. Use
			// the reach-bounded corner-marched apex instead (TryGetCornerPeekApex), which sits
			// past the wall's actual end. Falls back to the fixed offset when no reachable corner
			// exists (mid-wall on a long wall), correctly rejecting those points.
			const bool bNeedCornerApex = Height == ECoverHeight::Stand;
			const float MaxCornerReachCm = Tuning
				? Tuning->CrouchPeekMaxCornerReachCm
				: UCoverGeometryStatics::PeekApexMaxReachCm;
			if (!UCoverGeometryStatics::CanPeekShoot(World, Data, bCrouched,
				ThreatSightLoc, StandFireEyeHeight, CombatTarget, QuerierPawn,
				UCoverGeometryStatics::PeekLeanOffset, bNeedCornerApex,
				Standoff, CapRadius, UCoverGeometryStatics::PeekApexClearanceMargin,
				MaxCornerReachCm))
			{ ++DbgCombat; continue; }
			if (!UCoverGeometryStatics::IsThreatCovered(World, Data, ThreatLoc,
				Standoff, BodyProtectChestHeight, CombatTarget, QuerierPawn))
			{ ++DbgCombat; continue; }

			// Rank by the shared cover scorer with body-protection = true.
			const float Score = UCoverScoringStatics::ScoreCandidate(
				World, Data, PawnLoc, ThreatLoc, /*bBodyProtected*/ true, ScoreParams);
			if (Score > BestScore)
			{
				BestScore = Score;
				BestCover = Candidate;
			}
		}
		else
		{
			// No combat target: rank by proximity to ping impact, with a bonus for candidates
			// that have any usable lean flag so the companion never parks somewhere it can never
			// shoot from.
			const float DistSq = FVector::DistSquared(PingImpact, Data.Location);
			const bool bHasLean = Data.bLeftCoverCrouched || Data.bRightCoverCrouched
				|| Data.bLeftCoverStanding || Data.bRightCoverStanding;
			// Invert distance so higher = better. Lean bonus of 1e6 ensures lean-flagged candidates
			// always win over unflagged ones at any comparable distance.
			const float Score = -DistSq + (bHasLean ? 1e6f : 0.f);
			if (Score > BestScore)
			{
				BestScore = Score;
				BestCover = Candidate;
			}
		}
	}

	// Diagnostic: log rejection reasons when wall-matched candidates existed but all were rejected.
	if (!BestCover.IsValid() && DbgWallMatch > 0)
	{
		const float PawnToPing = FVector::Dist(PawnLoc, PingImpact);
		UE_LOG(LogCompanionAI, Warning,
			TEXT("FindCoverOnPingedWall: %d wall-matched candidates ALL rejected — occ=%d hostile=%d combat=%d (pawnToPing=%.0fcm searchRadius=%.0f)"),
			DbgWallMatch, DbgOccupied, DbgHostile, DbgCombat, PawnToPing, SearchRadius);
	}

	if (BestCover.IsValid())
	{
		OutCover = BestCover;
		return true;
	}
	return false;
}

void ClearCommandIfStillActive(UBehaviorTreeComponent& OwnerComp, ECompanionCommand ExpectedCommand)
{
	ACompanionAIController* AIC = Cast<ACompanionAIController>(OwnerComp.GetAIOwner());
	if (!IsValid(AIC)) return;
	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!BB) return;
	const ECompanionCommand Active = static_cast<ECompanionCommand>(
		BB->GetValueAsEnum(ACompanionAIController::BB_CompanionCommand));
	if (Active == ExpectedCommand)
		AIC->ClearActiveCommand();
}

} // namespace CompanionCover
