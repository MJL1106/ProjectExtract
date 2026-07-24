// Companion-only cover helpers — see header for the sharing contract.

#include "AI/CompanionCoverStatics.h"
#include "AI/CompanionAIController.h"
#include "AI/CompanionTuningDataAsset.h"
#include "Companion/CompanionCharacter.h"
#include "Components/CapsuleComponent.h"
#include "SuppressionComponent.h"
#include "HealthComponent.h"
#include "CoverSystem.h"
#include "CoverGeometryStatics.h"
#include "CoverReservationSubsystem.h"
#include "World/DoorRegistrySubsystem.h"
#include "ExtractionTypes.h"
#include "WeaponBase.h"
#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "GameplayTagAssetInterface.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISense_Sight.h"

namespace CompanionCover
{

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
	if (Companion.GetHealthFraction() < Tuning.CoverTriggerHealthFrac) return true;
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
	if (!bKeepFocus && IsValid(Controller))
		Controller->ClearFocus(EAIFocusPriority::Gameplay);
	State.Reset();
}

} // namespace CompanionCover
