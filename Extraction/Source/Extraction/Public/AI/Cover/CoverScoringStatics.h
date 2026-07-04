// UCoverScoringStatics — the single cover-candidate scoring implementation shared by the C++
// relocate/reseek paths and the EQS scoring test. Weights live in FCoverScoreParams, sourced
// from the querier's UEnemyArchetypeData so both paths react to the same tuning.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CoverSystemPublicData.h"
#include "CoverScoringStatics.generated.h"

class AEnemyCharacter;
class UEnemyArchetypeData;
class UEnemyAwarenessComponent;

/** Tunable term weights for cover-candidate scoring. Defaults reproduce the pre-unification
 *  C++ formula exactly; every post-unification term defaults off (weight 0). */
USTRUCT(BlueprintType)
struct EXTRACTION_API FCoverScoreParams
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cover|Scoring")
	float ProximityWeight = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cover|Scoring")
	float DistanceBandWeight = 0.7f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cover|Scoring")
	float FlatBonus = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cover|Scoring")
	float IdealDistMin = 500.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cover|Scoring")
	float IdealDistRange = 700.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cover|Scoring")
	float ProtectiveBonus = 2.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cover|Scoring")
	float SideFlagWeight = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cover|Scoring")
	float SideFlagOppositeScore = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cover|Scoring")
	float RetreatViabilityWeight = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cover|Scoring")
	float RetreatViabilityRadius = 600.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cover|Scoring")
	float StickinessMargin = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cover|Scoring")
	float MaxSearchRadius = 1200.f;
};

UCLASS()
class EXTRACTION_API UCoverScoringStatics : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	/** Build params from an archetype DataAsset. Null DA returns defaults. */
	static FCoverScoreParams MakeParams(const UEnemyArchetypeData* DA);

	/** MakeParams + posture band shift. Press prefers covers nearer the threat (band floor shifted
	 *  down, band term flipped to penalise distance); FallBack pushes the band floor out so deeper
	 *  covers win. No-op unless the archetype enables the posture system. */
	static FCoverScoreParams MakeParamsForEnemy(const AEnemyCharacter* Enemy);

	/** Resolve params from an EQS querier / BT owner (AEnemyCharacter or its controller).
	 *  Non-enemy owners (companion queries) get neutral defaults. */
	static FCoverScoreParams GetParamsForQuerier(const UObject* Querier);

	/** The shared scoring formula. bBodyProtected is computed by the caller (it already needs the
	 *  trace for its accept gate). World is only used by weight-gated terms (retreat viability) and
	 *  may be null when those weights are 0. */
	static float ScoreCandidate(const UWorld* World, const FCoverData& Data,
		const FVector& PawnLoc, const FVector& ThreatLoc, bool bBodyProtected,
		const FCoverScoreParams& Params);

	/** Free (unoccupied, not the candidate itself) cover points within Radius of Data.Location.
	 *  Capped early — callers only care about >= 1. */
	static int32 CountNearbyFreeCovers(const UWorld* World, const FCoverData& Data, float Radius);

	/** The position an enemy should treat the threat as occupying: live while sighted, frozen
	 *  LastKnownLocation once LOS is lost. Falls back to the live location when awareness is null. */
	static FVector GetPerceivedThreatLocation(const AActor* Threat,
		const UEnemyAwarenessComponent* Awareness, bool& bOutSighted);

	/** 0-1 fraction of sample points along the pawn->candidate route that are exposed to the
	 *  threat (clear LOS trace at chest height). Straight-line sampling — cheap by design.
	 *  InOutTraceBudget is decremented per trace; returns 0 once exhausted. */
	static float ScorePathExposure(const UWorld* World, const FVector& PawnLoc,
		const FVector& CandidateLoc, const FVector& ThreatLoc, int32 SampleCount,
		const AActor* IgnoreThreat, const AActor* IgnorePawn, int32& InOutTraceBudget);
};
