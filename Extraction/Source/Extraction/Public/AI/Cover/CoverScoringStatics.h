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
struct FEnemyKnownThreat;

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

/** Positions cover candidates must keep distance from: covers hostiles are moving to (declared
 *  intents) and living hostile pawns themselves. Gathered once per selection round. */
struct FHostileAnchors
{
	TArray<FVector> CoverAnchors;
	TArray<FVector> PawnAnchors;
	bool IsEmpty() const { return CoverAnchors.IsEmpty() && PawnAnchors.IsEmpty(); }
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

	/** Extra known hostiles (beyond FocusTarget) for the multi-threat penalty — from the enemy's
	 *  awareness tracks: sighted at live position, or perceived within the archetype's
	 *  ExtraThreatMemorySeconds at the frozen last-stimulus position (aggro-switch case — the
	 *  hostile this enemy was just fighting must not vanish from its cover math). Capped by
	 *  MaxExtraThreats; left empty when the penalty is off so callers can skip the traces. */
	static void GatherEnemyExtraThreats(const AEnemyCharacter* Enemy, const AActor* FocusTarget,
		TArray<FEnemyKnownThreat>& OutThreats);

	/** Multi-threat exposure penalty on a candidate score: factor = Penalty^N where N = extra threats
	 *  the candidate fails to shield the body from (at their perceived positions). Sign-aware
	 *  (negative Press-flipped scores are pushed further down, not up). No-op when ExtraThreats is
	 *  empty. Soft — never a hard reject. */
	static float ApplyMultiThreatPenalty(float Score, const UWorld* World, const FCoverData& Data,
		float Standoff, float ChestHeight, const APawn* IgnorePawn,
		const TArray<FEnemyKnownThreat>& ExtraThreats, float Penalty);

	/** Hostile anchors for the claim-collision reject, from QuerierPawn's side (enemy vs rest —
	 *  the split the awareness/tag systems encode). Covers hostiles intend + living hostile pawns.
	 *  Shared by the EQS CoverIntent filter and the C++ picker loops so both reject identically. */
	static void GatherHostileAnchors(UWorld* World, const APawn* QuerierPawn,
		const AController* ExcludeController, FHostileAnchors& Out);

	/** True when Location is within CoverDist (2D) of a hostile-intended cover or PawnDist of a
	 *  hostile pawn. Either distance <= 0 disables that anchor set. */
	static bool IsNearHostileAnchor(const FVector& Location, const FHostileAnchors& Anchors,
		float CoverDist, float PawnDist);

	/** 0-1 fraction of sample points along the pawn->candidate route that are exposed to the
	 *  threat (clear LOS trace at chest height). Straight-line sampling — cheap by design.
	 *  InOutTraceBudget is decremented per trace; returns 0 once exhausted. */
	static float ScorePathExposure(const UWorld* World, const FVector& PawnLoc,
		const FVector& CandidateLoc, const FVector& ThreatLoc, int32 SampleCount,
		const AActor* IgnoreThreat, const AActor* IgnorePawn, int32& InOutTraceBudget);
};
