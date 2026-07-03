// EnvQueryTest_CoverPeekable — filter rejects covers with no usable exposure direction.
// Pure flag logic: passes if bFrontCoverCrouched OR any lean flag valid for the stance.

#include "EnvQueryTest_CoverPeekable.h"
#include "AI/EnvQueryItemType_CoverBase.h"

UEnvQueryTest_CoverPeekable::UEnvQueryTest_CoverPeekable()
{
	Cost = EEnvTestCost::Low;
	ValidItemType = UEnvQueryItemType_CoverBase::StaticClass();
	SetWorkOnFloatValues(false);
}

void UEnvQueryTest_CoverPeekable::RunTest(FEnvQueryInstance& QueryInstance) const
{
	UObject* QueryOwner = QueryInstance.Owner.Get();

	BoolValue.BindData(QueryOwner, QueryInstance.QueryID);
	const bool bWantPeekable = BoolValue.GetValue();

	for (FEnvQueryInstance::ItemIterator It(this, QueryInstance); It; ++It)
	{
		const FCover CoverItem = UEnvQueryItemType_CoverBase::GetValue(
			QueryInstance.RawData.GetData() + QueryInstance.Items[It.GetIndex()].DataOffset);

		const FCoverData& Data = CoverItem.Data;

		// A cover is peekable if ANY exposure direction exists:
		// Crouched stance: crouched lean left/right, OR front cover crouched (fire over)
		// Standing stance: standing lean left/right
		// bFrontCoverCrouched works for both (crouch->fire-over, stand->fire-over if crouched cover)
		const bool bHasAnyExposure =
			Data.bFrontCoverCrouched
			|| Data.bLeftCoverCrouched || Data.bRightCoverCrouched
			|| Data.bLeftCoverStanding || Data.bRightCoverStanding;

		It.SetScore(TestPurpose, FilterType, bHasAnyExposure, bWantPeekable);
	}
}

FText UEnvQueryTest_CoverPeekable::GetDescriptionDetails() const
{
	return FText::FromString(TEXT("Has any peekable exposure direction"));
}
