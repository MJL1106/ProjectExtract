// UCoverScoringStatics — shared cover-candidate scoring (C++ picker paths + EQS scoring test).

#include "CoverScoringStatics.h"
#include "CoverGeometryStatics.h"
#include "CoverSystem.h"
#include "EnemyArchetypeData.h"
#include "EnemyCharacter.h"
#include "EnemyAwarenessComponent.h"
#include "EnemyPostureComponent.h"
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
	if (!IsValid(Enemy)) return FCoverScoreParams();
	return MakeParamsForEnemy(Enemy);
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
