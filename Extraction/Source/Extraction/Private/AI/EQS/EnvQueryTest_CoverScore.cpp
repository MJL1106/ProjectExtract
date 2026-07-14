// EnvQueryTest_CoverScore -- shared-formula scoring (see UCoverScoringStatics::ScoreCandidate).

#include "EnvQueryTest_CoverScore.h"
#include "AI/EnvQueryItemType_CoverBase.h"
#include "CoverScoringStatics.h"
#include "CoverGeometryStatics.h"
#include "EnvQueryContext_CombatTarget.h"
#include "EnemyCharacter.h"
#include "EnemyArchetypeData.h"
#include "EnemyAIController.h"
#include "EnemyAwarenessComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Controller.h"

static constexpr float CoverScoreDefaultCapsuleRadius = 34.f;
/** Chest height (cm) for the body-protection trace — matches BodyProtectChestHeight in the fire task. */
static constexpr float CoverScoreChestHeight = 60.f;

UEnvQueryTest_CoverScore::UEnvQueryTest_CoverScore()
{
	Cost = EEnvTestCost::High; // one body-protection trace per item when ProtectiveBonus > 0
	ValidItemType = UEnvQueryItemType_CoverBase::StaticClass();
	SetWorkOnFloatValues(true);

	TargetContext = UEnvQueryContext_CombatTarget::StaticClass();
}

void UEnvQueryTest_CoverScore::RunTest(FEnvQueryInstance& QueryInstance) const
{
	UObject* QueryOwner = QueryInstance.Owner.Get();
	if (!QueryOwner) return;

	UWorld* World = QueryInstance.World;

	// Resolve the querier pawn (owner may be the pawn or its controller).
	const APawn* QuerierPawn = Cast<APawn>(QueryOwner);
	if (!QuerierPawn)
	{
		const AController* OwnerController = Cast<AController>(QueryOwner);
		QuerierPawn = OwnerController ? OwnerController->GetPawn() : nullptr;
	}
	if (!IsValid(QuerierPawn)) return;

	const FCoverScoreParams Params = UCoverScoringStatics::GetParamsForQuerier(QueryOwner);

	// Standoff for the body-protection probe — mirrors the C++ picker paths.
	float Standoff = CoverScoreDefaultCapsuleRadius;
	const AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(QuerierPawn);
	if (Enemy)
	{
		const UCapsuleComponent* Capsule = Enemy->GetCapsuleComponent();
		const float CapsuleRadius = Capsule ? Capsule->GetScaledCapsuleRadius() : CoverScoreDefaultCapsuleRadius;
		const UEnemyArchetypeData* DA = Enemy->GetArchetypeData();
		Standoff = CapsuleRadius + (IsValid(DA) ? DA->CoverStandoffPadding : 0.f);
	}

	TArray<FVector> ContextLocations;
	if (!QueryInstance.PrepareContext(TargetContext, ContextLocations)) return;
	if (ContextLocations.IsEmpty()) return;
	const FVector ThreatLoc = ContextLocations[0];

	// Threat actor for trace-ignore (empty when the context resolved to a point — LOS lost).
	TArray<AActor*> ContextActors;
	QueryInstance.PrepareContext(TargetContext, ContextActors);
	const AActor* ThreatActor = ContextActors.IsEmpty() ? nullptr : ContextActors[0];

	const FVector PawnLoc = QuerierPawn->GetActorLocation();

	// Multi-threat exposure penalty (enemy queriers): gather extra known hostiles once per run.
	// Empty when the archetype disables the penalty — the per-candidate traces are then skipped.
	TArray<FEnemyKnownThreat> ExtraThreats;
	float MultiThreatPenalty = 1.f;
	if (Enemy)
	{
		// ThreatActor is null when the context resolved to a frozen point (LOS lost) — exclude the
		// combat target via awareness instead, or the unsighted primary re-enters as an "extra" and
		// double-penalises its own exposure term.
		const AActor* ExcludeTarget = ThreatActor;
		if (!ExcludeTarget)
		{
			const AEnemyAIController* EnemyController = Cast<AEnemyAIController>(Enemy->GetController());
			const UEnemyAwarenessComponent* Awareness = EnemyController ? EnemyController->GetAwarenessComponent() : nullptr;
			if (IsValid(Awareness)) ExcludeTarget = Awareness->GetCombatTarget();
		}
		UCoverScoringStatics::GatherEnemyExtraThreats(Enemy, ExcludeTarget, ExtraThreats);
		const UEnemyArchetypeData* DA = Enemy->GetArchetypeData();
		if (IsValid(DA)) MultiThreatPenalty = DA->MultiThreatExposurePenalty;
	}

	for (FEnvQueryInstance::ItemIterator It(this, QueryInstance); It; ++It)
	{
		const FCover CoverItem = UEnvQueryItemType_CoverBase::GetValue(
			QueryInstance.RawData.GetData() + QueryInstance.Items[It.GetIndex()].DataOffset);

		bool bBodyProtected = false;
		if (Params.ProtectiveBonus > 0.f && World)
			bBodyProtected = UCoverGeometryStatics::IsThreatCovered(World, CoverItem.Data,
				ThreatLoc, Standoff, CoverScoreChestHeight, ThreatActor, QuerierPawn);

		float Score = UCoverScoringStatics::ScoreCandidate(
			World, CoverItem.Data, PawnLoc, ThreatLoc, bBodyProtected, Params);

		Score = UCoverScoringStatics::ApplyMultiThreatPenalty(Score, World, CoverItem.Data,
			Standoff, CoverScoreChestHeight, QuerierPawn, ExtraThreats, MultiThreatPenalty);

		// Filter bounds are unused for a pure Score test; the engine min-max normalizes raw
		// scores across the candidate set, so ranking survives negative Press-flipped terms.
		It.SetScore(TestPurpose, FilterType, Score, 0.f, 1.f);
	}
}

FText UEnvQueryTest_CoverScore::GetDescriptionDetails() const
{
	return FText::FromString(TEXT("Shared cover-candidate score (weights from querier archetype DA)"));
}
