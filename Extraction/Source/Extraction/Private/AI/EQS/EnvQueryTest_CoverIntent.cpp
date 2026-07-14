// EnvQueryTest_CoverIntent — filter rejects covers intended by another agent or too close to a hostile.

#include "EnvQueryTest_CoverIntent.h"
#include "AI/EnvQueryItemType_CoverBase.h"
#include "CoverReservationSubsystem.h"
#include "CoverScoringStatics.h"
#include "EnemyArchetypeData.h"
#include "EnemyCharacter.h"
#include "AIController.h"
#include "Engine/World.h"

UEnvQueryTest_CoverIntent::UEnvQueryTest_CoverIntent()
{
	Cost = EEnvTestCost::Low;
	ValidItemType = UEnvQueryItemType_CoverBase::StaticClass();
	SetWorkOnFloatValues(false);

	MinHostileDistance.DefaultValue = 250.f;
	MinHostilePawnDistance.DefaultValue = 150.f;
}

void UEnvQueryTest_CoverIntent::RunTest(FEnvQueryInstance& QueryInstance) const
{
	UObject* QueryOwner = QueryInstance.Owner.Get();
	UWorld* World = QueryInstance.World;
	if (!World) return;

	MinHostileDistance.BindData(QueryOwner, QueryInstance.QueryID);
	MinHostilePawnDistance.BindData(QueryOwner, QueryInstance.QueryID);
	float CoverDist = MinHostileDistance.GetValue();
	float PawnDist = MinHostilePawnDistance.GetValue();

	BoolValue.BindData(QueryOwner, QueryInstance.QueryID);
	const bool bWantClear = BoolValue.GetValue();

	APawn* QuerierPawn = Cast<APawn>(QueryOwner);
	AController* QuerierController = QuerierPawn ? QuerierPawn->GetController() : Cast<AController>(QueryOwner);

	// Enemy queriers: radii come from the archetype DA — one source of truth with the C++ picker loops.
	if (const AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(QuerierPawn))
	{
		if (const UEnemyArchetypeData* DA = Enemy->GetArchetypeData())
		{
			CoverDist = DA->MinHostileCoverDistance;
			PawnDist = DA->MinHostilePawnDistance;
		}
	}

	UCoverReservationSubsystem* ResSub = World->GetSubsystem<UCoverReservationSubsystem>();

	FHostileAnchors Anchors;
	if ((CoverDist > 0.f || PawnDist > 0.f) && IsValid(QuerierPawn))
		UCoverScoringStatics::GatherHostileAnchors(World, QuerierPawn, QuerierController, Anchors);

	for (FEnvQueryInstance::ItemIterator It(this, QueryInstance); It; ++It)
	{
		const FCover CoverItem = UEnvQueryItemType_CoverBase::GetValue(
			QueryInstance.RawData.GetData() + QueryInstance.Items[It.GetIndex()].DataOffset);

		bool bClear = true;

		if (IsValid(ResSub) && ResSub->IsCoverIntendedByOther(CoverItem.Handle, QuerierController))
			bClear = false;

		if (bClear && UCoverScoringStatics::IsNearHostileAnchor(CoverItem.Data.Location, Anchors, CoverDist, PawnDist))
			bClear = false;

		It.SetScore(TestPurpose, FilterType, bClear, bWantClear);
	}
}

FText UEnvQueryTest_CoverIntent::GetDescriptionDetails() const
{
	return FText::Format(
		FText::FromString(TEXT("Reject intended-by-other; hostile cover/pawn distance {0}/{1} cm (enemy DA overrides)")),
		FText::AsNumber(MinHostileDistance.DefaultValue),
		FText::AsNumber(MinHostilePawnDistance.DefaultValue));
}
