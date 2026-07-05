// EnvQueryTest_CoverAllySpacing — score/filter by min 2D distance to squad allies + intended covers.

#include "EnvQueryTest_CoverAllySpacing.h"
#include "AI/EnvQueryItemType_CoverBase.h"
#include "CoverReservationSubsystem.h"
#include "EnemyCharacter.h"
#include "EnemyAIController.h"
#include "EnemySquad.h"
#include "EnemySquadSubsystem.h"
#include "HealthComponent.h"
#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Engine/World.h"

/** Score for items too close to an ally. */
static constexpr float CrowdedScore = 0.2f;
/** Score for items with adequate spacing. */
static constexpr float ClearScore = 1.0f;

UEnvQueryTest_CoverAllySpacing::UEnvQueryTest_CoverAllySpacing()
{
	Cost = EEnvTestCost::Low;
	ValidItemType = UEnvQueryItemType_CoverBase::StaticClass();
	SetWorkOnFloatValues(true);

	MinSpacing.DefaultValue = 300.f;
	FloatValueMin.DefaultValue = CrowdedScore;
	FloatValueMax.DefaultValue = ClearScore;
}

void UEnvQueryTest_CoverAllySpacing::RunTest(FEnvQueryInstance& QueryInstance) const
{
	UObject* QueryOwner = QueryInstance.Owner.Get();

	MinSpacing.BindData(QueryOwner, QueryInstance.QueryID);
	FloatValueMin.BindData(QueryOwner, QueryInstance.QueryID);
	FloatValueMax.BindData(QueryOwner, QueryInstance.QueryID);
	const float Spacing = MinSpacing.GetValue();
	const float MinThresholdValue = FloatValueMin.GetValue();
	const float MaxThresholdValue = FloatValueMax.GetValue();

	// Gather ally positions using the same mechanism as BTTask_EnemyMoveToCover::ScoreSlotsWithSpacing
	TArray<FVector> AllyPoints;

	APawn* QuerierPawn = Cast<APawn>(QueryOwner);

	if (IsValid(QuerierPawn))
	{
		AController* QuerierController = QuerierPawn->GetController();

		AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(QuerierPawn);
		int32 EstimatedCount = 0;

		// Estimate total ally points for a single reserve
		UEnemySquad* Squad = nullptr;
		if (IsValid(Enemy))
		{
			UEnemySquadSubsystem* SquadSub = QuerierPawn->GetWorld()->GetSubsystem<UEnemySquadSubsystem>();
			Squad = SquadSub ? SquadSub->GetSquadFor(Enemy) : nullptr;
			if (Squad) EstimatedCount += Squad->GetMembers().Num();
		}

		UCoverReservationSubsystem* ResSub = QuerierPawn->GetWorld()->GetSubsystem<UCoverReservationSubsystem>();
		AllyPoints.Reserve(EstimatedCount + 8); // +slack for intended covers

		if (IsValid(Enemy) && Squad)
		{
			const TArray<TWeakObjectPtr<AEnemyCharacter>>& Members = Squad->GetMembers();
			for (const TWeakObjectPtr<AEnemyCharacter>& M : Members)
			{
				AEnemyCharacter* Ally = M.Get();
				if (!IsValid(Ally) || Ally == QuerierPawn) continue;
				UHealthComponent* AllyHP = Ally->GetHealthComponent();
				if (IsValid(AllyHP) && AllyHP->IsDead()) continue;

				FVector Pos = Ally->GetActorLocation();

				// Prefer the ally's current cover slot location when available
				AEnemyAIController* AllyAIC = Cast<AEnemyAIController>(Ally->GetController());
				if (AllyAIC)
				{
					UBlackboardComponent* AllyBB = AllyAIC->GetBlackboardComponent();
					if (AllyBB)
					{
						AActor* AllySlot = Cast<AActor>(AllyBB->GetValueAsObject(AEnemyAIController::BB_CoverSlot));
						if (IsValid(AllySlot)) Pos = AllySlot->GetActorLocation();
					}
				}
				AllyPoints.Add(Pos);
			}
		}

		// Also add intended cover positions from the reservation subsystem. No class filter:
		// a cover ANYONE is moving to is bad spacing regardless of side — the old
		// AEnemyAIController-only filter let enemy picks stack onto companion destinations.
		if (IsValid(ResSub))
		{
			TArray<FCover> IntendedCovers;
			ResSub->GetIntendedCovers(IntendedCovers, QuerierController);
			for (const FCover& IC : IntendedCovers)
			{
				AllyPoints.Add(IC.Data.Location);
			}
		}
	}

	for (FEnvQueryInstance::ItemIterator It(this, QueryInstance); It; ++It)
	{
		const FCover CoverItem = UEnvQueryItemType_CoverBase::GetValue(
			QueryInstance.RawData.GetData() + QueryInstance.Items[It.GetIndex()].DataOffset);

		float ItemScore = ClearScore;

		if (Spacing > 0.f)
		{
			const FVector ItemLoc = CoverItem.Data.Location;
			for (const FVector& AllyPos : AllyPoints)
			{
				if (FVector::Dist2D(ItemLoc, AllyPos) < Spacing)
				{
					ItemScore = CrowdedScore;
					break;
				}
			}
		}

		It.SetScore(TestPurpose, FilterType, ItemScore, MinThresholdValue, MaxThresholdValue);
	}
}

FText UEnvQueryTest_CoverAllySpacing::GetDescriptionDetails() const
{
	return FText::Format(
		FText::FromString(TEXT("Min ally spacing: {0} cm")),
		FText::AsNumber(MinSpacing.DefaultValue));
}
