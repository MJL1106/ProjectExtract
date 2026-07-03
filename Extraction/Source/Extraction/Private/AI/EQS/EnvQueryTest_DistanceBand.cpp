// EnvQueryTest_DistanceBand -- score/filter by 2D distance with a band preference.
// Score 1.0 inside [BandMin, BandMax]; linear falloff to 0 over one band-width outside.

#include "EnvQueryTest_DistanceBand.h"
#include "EnvironmentQuery/Items/EnvQueryItemType_VectorBase.h"
#include "EnvironmentQuery/Contexts/EnvQueryContext_Querier.h"

UEnvQueryTest_DistanceBand::UEnvQueryTest_DistanceBand()
{
	Cost = EEnvTestCost::Low;
	ValidItemType = UEnvQueryItemType_VectorBase::StaticClass();
	SetWorkOnFloatValues(true);

	DistanceTo = UEnvQueryContext_Querier::StaticClass();
	BandMin.DefaultValue = 500.f;
	BandMax.DefaultValue = 1200.f;

	// Mirror the CoverAllySpacing pattern: bind inherited filter thresholds.
	FloatValueMin.DefaultValue = 0.f;
	FloatValueMax.DefaultValue = 1.f;
}

void UEnvQueryTest_DistanceBand::RunTest(FEnvQueryInstance& QueryInstance) const
{
	UObject* QueryOwner = QueryInstance.Owner.Get();

	BandMin.BindData(QueryOwner, QueryInstance.QueryID);
	BandMax.BindData(QueryOwner, QueryInstance.QueryID);
	FloatValueMin.BindData(QueryOwner, QueryInstance.QueryID);
	FloatValueMax.BindData(QueryOwner, QueryInstance.QueryID);

	const float MinDist = BandMin.GetValue();
	const float MaxDist = FMath::Max(BandMax.GetValue(), MinDist);
	const float BandWidth = FMath::Max(MaxDist - MinDist, 1.f);
	const float MinThresholdValue = FloatValueMin.GetValue();
	const float MaxThresholdValue = FloatValueMax.GetValue();

	// Gather context locations
	TArray<FVector> ContextLocations;
	if (!QueryInstance.PrepareContext(DistanceTo, ContextLocations))
		return;

	for (FEnvQueryInstance::ItemIterator It(this, QueryInstance); It; ++It)
	{
		const FVector ItemLoc = GetItemLocation(QueryInstance, It);

		// Find minimum 2D distance to any context location
		float BestDist2D = TNumericLimits<float>::Max();
		for (const FVector& CtxLoc : ContextLocations)
		{
			const float D = FVector::Dist2D(ItemLoc, CtxLoc);
			if (D < BestDist2D)
				BestDist2D = D;
		}

		// Score: 1.0 inside band, linear falloff outside
		float Score;
		if (BestDist2D < MinDist)
		{
			Score = FMath::Max(1.f - (MinDist - BestDist2D) / BandWidth, 0.f);
		}
		else if (BestDist2D > MaxDist)
		{
			Score = FMath::Max(1.f - (BestDist2D - MaxDist) / BandWidth, 0.f);
		}
		else
		{
			Score = 1.f;
		}

		It.SetScore(TestPurpose, FilterType, Score, MinThresholdValue, MaxThresholdValue);
	}
}

FText UEnvQueryTest_DistanceBand::GetDescriptionTitle() const
{
	return FText::FromString(FString::Printf(TEXT("%s: Distance Band"), *Super::GetDescriptionTitle().ToString()));
}

FText UEnvQueryTest_DistanceBand::GetDescriptionDetails() const
{
	return FText::Format(
		FText::FromString(TEXT("Band [{0}, {1}] cm")),
		FText::AsNumber(BandMin.DefaultValue),
		FText::AsNumber(BandMax.DefaultValue));
}
