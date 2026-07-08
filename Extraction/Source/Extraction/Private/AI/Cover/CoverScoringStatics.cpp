// UCoverScoringStatics — shared cover-candidate scoring (C++ picker paths + EQS scoring test).

#include "CoverScoringStatics.h"
#include "CoverGeometryStatics.h"
#include "CoverReservationSubsystem.h"
#include "CoverSystem.h"
#include "HealthComponent.h"
#include "EngineUtils.h"
#include "EnemyArchetypeData.h"
#include "EnemyCharacter.h"
#include "EnemyAIController.h"
#include "EnemyAwarenessComponent.h"
#include "EnemyPostureComponent.h"
#include "Companion/CompanionCharacter.h"
#include "AI/CompanionAIController.h"
#include "AI/CompanionTuningDataAsset.h"
#include "GameFramework/Controller.h"
#include "Engine/World.h"

/** Chest height (cm) for path-exposure sample traces — matches BodyProtectChestHeight in the fire task. */
static constexpr float PathExposureChestHeight = 60.f;
/** Candidates closer than this (cm) to the scored point count as the same point for retreat viability. */
static constexpr float RetreatSelfExclusionDist = 50.f;

FCoverScoreParams UCoverScoringStatics::MakeParams(const UEnemyArchetypeData* DA)
{
	FCoverScoreParams Params;
	if (!IsValid(DA)) return Params;

	Params.ProximityWeight        = DA->CoverScoreProximityWeight;
	Params.DistanceBandWeight     = DA->CoverScoreDistanceBandWeight;
	Params.FlatBonus              = DA->CoverScoreFlatBonus;
	Params.IdealDistMin           = DA->CoverScoreIdealDistMin;
	Params.IdealDistRange         = DA->CoverScoreIdealDistRange;
	Params.ProtectiveBonus        = DA->ProtectiveCoverScoreBonus;
	Params.SideFlagWeight         = DA->CoverScoreSideFlagWeight;
	Params.SideFlagOppositeScore  = DA->CoverScoreSideFlagOpposite;
	Params.RetreatViabilityWeight = DA->CoverScoreRetreatViabilityWeight;
	Params.RetreatViabilityRadius = DA->CoverScoreRetreatViabilityRadius;
	Params.StickinessMargin       = DA->CoverScoreStickinessMargin;
	Params.MaxSearchRadius        = DA->CoverSearchRadius;
	return Params;
}

FCoverScoreParams UCoverScoringStatics::MakeParamsForEnemy(const AEnemyCharacter* Enemy)
{
	if (!IsValid(Enemy)) return FCoverScoreParams();
	const UEnemyArchetypeData* DA = Enemy->GetArchetypeData();
	FCoverScoreParams Params = MakeParams(DA);

	// Downed-player standoff: while a hostile player is DBNO, every enemy cover pick avoids the
	// ring around the body — set before the posture gate so posture-disabled archetypes get it too.
	if (IsValid(DA) && DA->DBNOStandoffRadius > 0.f)
	{
		if (const APawn* Downed = UEnemyAwarenessComponent::FindDownedPlayerPawn(Enemy))
		{
			Params.DBNOAvoidLocation = Downed->GetActorLocation();
			Params.DBNOAvoidRadius = DA->DBNOStandoffRadius;
		}
	}

	if (!IsValid(DA) || !DA->bPostureSystemEnabled) return Params;

	const UEnemyPostureComponent* Posture = Enemy->GetPostureComponent();
	if (!IsValid(Posture)) return Params;

	switch (Posture->GetPosture())
	{
	case EEnemyPosture::Press:
		// Prefer covers NEARER the threat: shift the band floor down and flip the band term so
		// distance now penalises — far candidates clamp to the largest penalty.
		Params.IdealDistMin = FMath::Max(0.f, Params.IdealDistMin + DA->PosturePressDistMinDelta);
		Params.DistanceBandWeight = -Params.DistanceBandWeight;
		break;
	case EEnemyPosture::FallBack:
		Params.IdealDistMin += DA->PostureFallBackDistMinDelta;
		break;
	default:
		break;
	}
	return Params;
}

FCoverScoreParams UCoverScoringStatics::GetParamsForQuerier(const UObject* Querier)
{
	const AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Querier);
	if (!Enemy)
	{
		const AController* Controller = Cast<AController>(Querier);
		if (Controller) Enemy = Cast<AEnemyCharacter>(Controller->GetPawn());
	}
	if (IsValid(Enemy)) return MakeParamsForEnemy(Enemy);

	// Companion querier: neutral defaults, except Combat mode applies the advance shift (the enemy
	// Press mechanic) so cover picks gain ground toward the threat instead of holding depth.
	const ACompanionCharacter* Companion = Cast<ACompanionCharacter>(Querier);
	if (!Companion)
	{
		const AController* Controller = Cast<AController>(Querier);
		if (Controller) Companion = Cast<ACompanionCharacter>(Controller->GetPawn());
	}

	FCoverScoreParams Params;
	if (IsValid(Companion) && Companion->GetMode() == ECompanionMode::Combat)
	{
		const ACompanionAIController* CompAIC = Cast<ACompanionAIController>(Companion->GetController());
		if (const UCompanionTuningDataAsset* Tuning = CompAIC ? CompAIC->GetTuning() : nullptr)
		{
			Params.IdealDistMin = FMath::Max(0.f, Params.IdealDistMin + Tuning->CombatModeAdvanceDistMinDelta);
			Params.DistanceBandWeight = -Params.DistanceBandWeight;
		}
	}
	return Params;
}

float UCoverScoringStatics::ScoreCandidate(const UWorld* World, const FCoverData& Data,
	const FVector& PawnLoc, const FVector& ThreatLoc, bool bBodyProtected,
	const FCoverScoreParams& Params)
{
	const float DistToPawn   = FVector::Dist(PawnLoc, Data.Location);
	const float ProxScore    = FMath::Clamp(1.f - DistToPawn / Params.MaxSearchRadius, 0.f, 1.f);
	const float DistToTarget = FVector::Dist(Data.Location, ThreatLoc);
	const float DistScore    = FMath::Clamp((DistToTarget - Params.IdealDistMin) / Params.IdealDistRange, 0.f, 1.f);

	float Score = Params.ProximityWeight * ProxScore
	            + Params.DistanceBandWeight * DistScore
	            + Params.FlatBonus;

	if (bBodyProtected)
		Score += Params.ProtectiveBonus;

	if (Params.SideFlagWeight > 0.f)
	{
		const bool bCrouched = UCoverGeometryStatics::GetCoverHeight(Data) == ECoverHeight::Crouch;
		if (UCoverGeometryStatics::HasThreatFacingSideFlag(Data, ThreatLoc, bCrouched))
			Score += Params.SideFlagWeight;
		else if (Data.bLeftCoverCrouched || Data.bRightCoverCrouched || Data.bLeftCoverStanding || Data.bRightCoverStanding)
			Score += Params.SideFlagWeight * Params.SideFlagOppositeScore;
	}

	if (Params.RetreatViabilityWeight > 0.f && World
		&& CountNearbyFreeCovers(World, Data, Params.RetreatViabilityRadius) >= 1)
		Score += Params.RetreatViabilityWeight;

	if (Params.DBNOAvoidRadius > 0.f
		&& FVector::DistSquared2D(Data.Location, Params.DBNOAvoidLocation) < FMath::Square(Params.DBNOAvoidRadius))
		Score = ApplyScorePenalty(Score, Params.DBNOAvoidPenalty);

	return Score;
}

int32 UCoverScoringStatics::CountNearbyFreeCovers(const UWorld* World, const FCoverData& Data, float Radius)
{
	if (!World) return 0;
	ACoverSystem* CoverSys = ACoverSystem::GetCoverSystem(const_cast<UWorld*>(World));
	if (!CoverSys) return 0;

	TArray<FCover> Nearby;
	Nearby.Reserve(16);
	const FBoxSphereBounds Bounds(Data.Location, FVector(Radius), Radius);
	CoverSys->GetCoverDataWithinBounds(Bounds, Nearby);

	int32 Count = 0;
	for (const FCover& Candidate : Nearby)
	{
		if (!Candidate.IsValid()) continue;
		if (FVector::Dist(Candidate.Data.Location, Data.Location) < RetreatSelfExclusionDist) continue;
		if (CoverSys->GetOccupyingController(Candidate.Handle)) continue;
		++Count;
		break; // callers only care about >= 1
	}
	return Count;
}

FVector UCoverScoringStatics::GetPerceivedThreatLocation(const AActor* Threat,
	const UEnemyAwarenessComponent* Awareness, bool& bOutSighted)
{
	bOutSighted = true;
	if (!IsValid(Threat)) return FVector::ZeroVector;
	if (!IsValid(Awareness) || Awareness->GetCombatTarget() != Threat)
		return Threat->GetActorLocation();

	bOutSighted = Awareness->HasLOSToTarget();
	return bOutSighted ? Threat->GetActorLocation() : Awareness->GetLastKnownLocation();
}

void UCoverScoringStatics::GatherEnemyExtraThreats(const AEnemyCharacter* Enemy, const AActor* FocusTarget,
	TArray<FEnemyKnownThreat>& OutThreats)
{
	OutThreats.Reset();
	if (!IsValid(Enemy)) return;

	const UEnemyArchetypeData* DA = Enemy->GetArchetypeData();
	if (!IsValid(DA) || DA->MultiThreatExposurePenalty >= 1.f || DA->MaxExtraThreats <= 0) return;

	const AEnemyAIController* Controller = Cast<AEnemyAIController>(Enemy->GetController());
	const UEnemyAwarenessComponent* Awareness = Controller ? Controller->GetAwarenessComponent() : nullptr;
	if (!IsValid(Awareness)) return;

	Awareness->GetExtraKnownThreats(FocusTarget, DA->MaxExtraThreats, DA->ExtraThreatMemorySeconds, OutThreats);
}

float UCoverScoringStatics::ApplyMultiThreatPenalty(float Score, const UWorld* World, const FCoverData& Data,
	float Standoff, float ChestHeight, const APawn* IgnorePawn,
	const TArray<FEnemyKnownThreat>& ExtraThreats, float Penalty)
{
	if (!World || ExtraThreats.IsEmpty()) return Score;

	// Floor above 0 so a 0 penalty stays a (steep) soft penalty, and the negative branch can divide.
	const float ClampedPenalty = FMath::Clamp(Penalty, 0.01f, 1.f);
	if (ClampedPenalty >= 1.f) return Score;

	int32 Uncovered = 0;
	for (const FEnemyKnownThreat& Threat : ExtraThreats)
	{
		if (!Threat.Actor) continue;
		// Perceived position: live when sighted, frozen last-stimulus otherwise (gather decides).
		if (!UCoverGeometryStatics::IsThreatCovered(World, Data, Threat.Location,
			Standoff, ChestHeight, Threat.Actor, IgnorePawn))
			++Uncovered;
	}
	if (Uncovered == 0) return Score;

	const float Factor = FMath::Pow(ClampedPenalty, static_cast<float>(Uncovered));
	// Press posture flips the band weight, so raw scores can be negative — multiply would then
	// IMPROVE an exposed candidate. Scale away from zero on the negative side instead.
	return ApplyScorePenalty(Score, Factor);
}

float UCoverScoringStatics::ApplyScorePenalty(float Score, float Penalty)
{
	const float ClampedPenalty = FMath::Clamp(Penalty, 0.01f, 1.f);
	return Score >= 0.f ? Score * ClampedPenalty : Score / ClampedPenalty;
}

void UCoverScoringStatics::GatherHostileAnchors(UWorld* World, const APawn* QuerierPawn,
	const AController* ExcludeController, FHostileAnchors& Out)
{
	Out.CoverAnchors.Reset();
	Out.PawnAnchors.Reset();
	if (!World || !IsValid(QuerierPawn)) return;

	const bool bEnemyQuerier = QuerierPawn->IsA<AEnemyCharacter>();

	for (TActorIterator<APawn> It(World); It; ++It)
	{
		APawn* Other = *It;
		if (!IsValid(Other) || Other == QuerierPawn) continue;
		if (Other->IsA<AEnemyCharacter>() == bEnemyQuerier) continue; // same side
		const UHealthComponent* HC = Other->FindComponentByClass<UHealthComponent>();
		if (HC && HC->IsDead()) continue;
		Out.PawnAnchors.Add(Other->GetActorLocation());
	}

	if (UCoverReservationSubsystem* ResSub = World->GetSubsystem<UCoverReservationSubsystem>())
	{
		TArray<TPair<APawn*, FCover>> IntentOwners;
		ResSub->GetIntendedCoverOwners(IntentOwners, ExcludeController);
		for (const TPair<APawn*, FCover>& Entry : IntentOwners)
		{
			const APawn* OwnerPawn = Entry.Key;
			if (!IsValid(OwnerPawn)) continue;
			if (OwnerPawn->IsA<AEnemyCharacter>() == bEnemyQuerier) continue; // same side
			Out.CoverAnchors.Add(Entry.Value.Data.Location);
		}
	}
}

bool UCoverScoringStatics::IsNearHostileAnchor(const FVector& Location, const FHostileAnchors& Anchors,
	float CoverDist, float PawnDist)
{
	if (CoverDist > 0.f)
	{
		for (const FVector& Anchor : Anchors.CoverAnchors)
			if (FVector::Dist2D(Location, Anchor) < CoverDist) return true;
	}
	if (PawnDist > 0.f)
	{
		for (const FVector& Anchor : Anchors.PawnAnchors)
			if (FVector::Dist2D(Location, Anchor) < PawnDist) return true;
	}
	return false;
}

float UCoverScoringStatics::ScorePathExposure(const UWorld* World, const FVector& PawnLoc,
	const FVector& CandidateLoc, const FVector& ThreatLoc, int32 SampleCount,
	const AActor* IgnoreThreat, const AActor* IgnorePawn, int32& InOutTraceBudget)
{
	if (!World || SampleCount <= 0) return 0.f;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(CoverPathExposure), false);
	if (IgnoreThreat) Params.AddIgnoredActor(IgnoreThreat);
	if (IgnorePawn)   Params.AddIgnoredActor(IgnorePawn);

	int32 Exposed = 0;
	int32 Traced = 0;
	for (int32 i = 0; i < SampleCount; ++i)
	{
		if (InOutTraceBudget <= 0) break;
		--InOutTraceBudget;
		++Traced;
		const float Alpha = static_cast<float>(i + 1) / static_cast<float>(SampleCount + 1);
		const FVector Sample = FMath::Lerp(PawnLoc, CandidateLoc, Alpha) + FVector(0.f, 0.f, PathExposureChestHeight);
		// Not blocked = the threat can see this point on the route.
		if (!World->LineTraceTestByChannel(Sample, ThreatLoc, ECC_Visibility, Params))
			++Exposed;
	}
	return Traced > 0 ? static_cast<float>(Exposed) / static_cast<float>(Traced) : 0.f;
}
