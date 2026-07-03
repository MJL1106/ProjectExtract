// EnvQueryTest_CoverSideFlag -- directional scoring: threat-facing side flag = 1.0,
// opposite-only = OppositeSideScore, no flags = 0.

#include "EnvQueryTest_CoverSideFlag.h"
#include "AI/EnvQueryItemType_CoverBase.h"
#include "CoverGeometryStatics.h"
#include "EnvQueryContext_CombatTarget.h"

UEnvQueryTest_CoverSideFlag::UEnvQueryTest_CoverSideFlag()
{
	Cost = EEnvTestCost::Low;
	ValidItemType = UEnvQueryItemType_CoverBase::StaticClass();
	SetWorkOnFloatValues(true);

	TargetContext = UEnvQueryContext_CombatTarget::StaticClass();
	OppositeSideScore.DefaultValue = 0.35f;
}

void UEnvQueryTest_CoverSideFlag::RunTest(FEnvQueryInstance& QueryInstance) const
{
	UObject* QueryOwner = QueryInstance.Owner.Get();

	OppositeSideScore.BindData(QueryOwner, QueryInstance.QueryID);
	const float OppositeScore = OppositeSideScore.GetValue();

	TArray<FVector> ContextLocations;
	if (!QueryInstance.PrepareContext(TargetContext, ContextLocations)) return;
	if (ContextLocations.IsEmpty()) return;

	const FVector ThreatLoc = ContextLocations[0];

	for (FEnvQueryInstance::ItemIterator It(this, QueryInstance); It; ++It)
	{
		const FCover CoverItem = UEnvQueryItemType_CoverBase::GetValue(
			QueryInstance.RawData.GetData() + QueryInstance.Items[It.GetIndex()].DataOffset);

		const bool bCrouched = UCoverGeometryStatics::GetCoverHeight(CoverItem.Data) == ECoverHeight::Crouch;
		const bool bThreatFacing = UCoverGeometryStatics::HasThreatFacingSideFlag(
			CoverItem.Data, ThreatLoc, bCrouched);

		if (bThreatFacing)
		{
			It.SetScore(TestPurpose, FilterType, 1.f, 0.f, 1.f);
			continue;
		}

		// Check if there's any side flag at all (opposite side only)
		const bool bAnyFlag = bCrouched
			? (CoverItem.Data.bLeftCoverCrouched || CoverItem.Data.bRightCoverCrouched)
			: (CoverItem.Data.bLeftCoverStanding || CoverItem.Data.bRightCoverStanding);

		const float Score = bAnyFlag ? OppositeScore : 0.f;
		It.SetScore(TestPurpose, FilterType, Score, 0.f, 1.f);
	}
}

FText UEnvQueryTest_CoverSideFlag::GetDescriptionDetails() const
{
	return FText::Format(
		FText::FromString(TEXT("Threat-facing side flag (opposite={0})")),
		FText::AsNumber(OppositeSideScore.DefaultValue));
}
