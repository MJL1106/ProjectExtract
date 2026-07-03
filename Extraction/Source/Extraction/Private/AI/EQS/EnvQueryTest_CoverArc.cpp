// EnvQueryTest_CoverArc — filter/score covers by fire-arc alignment toward a context actor.
// Cheap dot-product math — no traces. Mirrors the task's arc gate (widened arc = base + slack).

#include "EnvQueryTest_CoverArc.h"
#include "AI/EnvQueryItemType_CoverBase.h"
#include "CoverGeometryStatics.h"
#include "EnvQueryContext_CombatTarget.h"

UEnvQueryTest_CoverArc::UEnvQueryTest_CoverArc()
{
	Cost = EEnvTestCost::Low;
	ValidItemType = UEnvQueryItemType_CoverBase::StaticClass();
	SetWorkOnFloatValues(false);

	TargetContext = UEnvQueryContext_CombatTarget::StaticClass();
	ArcHalfAngleDeg.DefaultValue = 75.f;
}

void UEnvQueryTest_CoverArc::RunTest(FEnvQueryInstance& QueryInstance) const
{
	UObject* QueryOwner = QueryInstance.Owner.Get();

	ArcHalfAngleDeg.BindData(QueryOwner, QueryInstance.QueryID);
	const float HalfAngle = ArcHalfAngleDeg.GetValue();
	const float ArcCosThreshold = FMath::Cos(FMath::DegreesToRadians(HalfAngle));

	TArray<FVector> ContextLocations;
	if (!QueryInstance.PrepareContext(TargetContext, ContextLocations)) return;
	if (ContextLocations.IsEmpty()) return;

	// Use the first context location (single combat target).
	const FVector TargetLoc = ContextLocations[0];

	BoolValue.BindData(QueryOwner, QueryInstance.QueryID);
	const bool bWantInArc = BoolValue.GetValue();

	for (FEnvQueryInstance::ItemIterator It(this, QueryInstance); It; ++It)
	{
		const FCover CoverItem = UEnvQueryItemType_CoverBase::GetValue(
			QueryInstance.RawData.GetData() + QueryInstance.Items[It.GetIndex()].DataOffset);

		const FVector ToTarget2D = (TargetLoc - CoverItem.Data.Location).GetSafeNormal2D();
		const FVector FireFwd    = UCoverGeometryStatics::GetFireArcForward(CoverItem.Data);
		const float   Dot        = FVector::DotProduct(FireFwd, ToTarget2D);
		const bool    bInArc     = Dot >= ArcCosThreshold;

		It.SetScore(TestPurpose, FilterType, bInArc, bWantInArc);
	}
}

FText UEnvQueryTest_CoverArc::GetDescriptionDetails() const
{
	return FText::Format(
		FText::FromString(TEXT("Fire arc half-angle: {0}°")),
		FText::AsNumber(ArcHalfAngleDeg.DefaultValue));
}
