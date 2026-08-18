// EnvQueryTest_CoverPeekable — filter rejects covers with no usable exposure direction.
// Pure flag logic: passes if bFrontCoverCrouched OR any lean flag valid for the stance.

#include "EnvQueryTest_CoverPeekable.h"
#include "AI/EnvQueryItemType_CoverBase.h"
#include "CoverGeometryStatics.h"
#include "EnemyTestKnobs.h"

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

	// enemy.ForceCoverHeight debug filter (0=auto, 1=crouch-only, 2=stand-only)
	const int32 ForcedHeight = GetForceCoverHeightLevel();

	for (FEnvQueryInstance::ItemIterator It(this, QueryInstance); It; ++It)
	{
		const FCover CoverItem = UEnvQueryItemType_CoverBase::GetValue(
			QueryInstance.RawData.GetData() + QueryInstance.Items[It.GetIndex()].DataOffset);

		const FCoverData& Data = CoverItem.Data;

		// Debug height filter: reject mismatched stance points (derived height, not raw flag)
		const ECoverHeight DerivedH = UCoverGeometryStatics::GetCoverHeight(Data);
		if (ForcedHeight == 1 && DerivedH != ECoverHeight::Crouch) { It.SetScore(TestPurpose, FilterType, false, bWantPeekable); continue; }
		if (ForcedHeight == 2 && DerivedH != ECoverHeight::Stand)  { It.SetScore(TestPurpose, FilterType, false, bWantPeekable); continue; }

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
