//// Copyright, (c) Sami Kangasmaa 2022


#include "AI/EnvQueryGenerator_Covers.h"
#include "EnvironmentQuery/Contexts/EnvQueryContext_Querier.h"
#include "CoverSystem.h"
#include "AI/EnvQueryItemType_CoverBase.h"
#include "Math/GenericOctree.h"
#include "Engine/World.h"

#define LOCTEXT_NAMESPACE "EnvQueryGenerator"

UEnvQueryGenerator_Covers::UEnvQueryGenerator_Covers()
{
	ItemType = UEnvQueryItemType_CoverBase::StaticClass();
	GenerateAround = UEnvQueryContext_Querier::StaticClass();

	QueryBoundSize.DefaultValue = 2048.0f;
	QueryBoundHeight.DefaultValue = 512.f;
	MaxZDeltaFromContext.DefaultValue = 250.f;

	IncludeLeftCoverStanding.DefaultValue = true;
	IncludeRightCoverStanding.DefaultValue = true;
	IncludeLeftCoverCrouched.DefaultValue = true;
	IncludeRightCoverCrouched.DefaultValue = true;
	IncludeFrontCoverCrouched.DefaultValue = true;
	IncludeOnlyCrouched.DefaultValue = false;
}

void UEnvQueryGenerator_Covers::GenerateItems(FEnvQueryInstance& QueryInstance) const
{
	UObject* QueryOwner = QueryInstance.Owner.Get();

	QueryBoundSize.BindData(QueryOwner, QueryInstance.QueryID);
	const float BoundsXY = QueryBoundSize.GetValue();

	QueryBoundHeight.BindData(QueryOwner, QueryInstance.QueryID);
	const float BoundsZ = QueryBoundHeight.GetValue();

	MaxZDeltaFromContext.BindData(QueryOwner, QueryInstance.QueryID);
	const float MaxZ = MaxZDeltaFromContext.GetValue();

	const FVector SearchBounds = FVector(BoundsXY, BoundsXY, BoundsZ);

	IncludeLeftCoverStanding.BindData(QueryOwner, QueryInstance.QueryID);
	IncludeRightCoverStanding.BindData(QueryOwner, QueryInstance.QueryID);
	IncludeLeftCoverCrouched.BindData(QueryOwner, QueryInstance.QueryID);
	IncludeRightCoverCrouched.BindData(QueryOwner, QueryInstance.QueryID);
	IncludeFrontCoverCrouched.BindData(QueryOwner, QueryInstance.QueryID);
	IncludeOnlyCrouched.BindData(QueryOwner, QueryInstance.QueryID);

	ACoverSystem* CoverSystem = ACoverSystem::GetCoverSystem(QueryInstance.World);
	if (!CoverSystem)
	{
		return;
	}

	TArray<FVector> ContextLocations;
	QueryInstance.PrepareContext(GenerateAround, ContextLocations);
	for (int32 ContextIdx = 0; ContextIdx < ContextLocations.Num(); ContextIdx++)
	{
		TArray<FCover> Covers;
		CoverSystem->GetCoversAndDataWithinBounds(FBoxCenterAndExtent(ContextLocations[ContextIdx], SearchBounds), Covers);

		FilterCovers(Covers);

		// Drop covers on another storey. The octree query box is generous in Z (and shipped query
		// assets bake their own QueryBoundHeight), so without this a querier can be handed cover a
		// floor above or below and walk off the fight to reach it.
		if (MaxZ > 0.f)
		{
			const float ContextZ = ContextLocations[ContextIdx].Z;
			Covers.RemoveAllSwap([ContextZ, MaxZ](const FCover& Cover)
			{
				return FMath::Abs(Cover.Data.Location.Z - ContextZ) > MaxZ;
			}, EAllowShrinking::No);
		}

		QueryInstance.AddItemData<UEnvQueryItemType_CoverBase>(Covers);
	}
}

FText UEnvQueryGenerator_Covers::GetDescriptionTitle() const
{
	return FText::Format(LOCTEXT("DescriptionGenerateAroundContext", "{0}: generate around {1}"),
		Super::GetDescriptionTitle(), UEnvQueryTypes::DescribeContext(GenerateAround));
};

FText UEnvQueryGenerator_Covers::GetDescriptionDetails() const
{
	FText Desc = FText::Format(LOCTEXT("SimpleGridDescription", "Extent: {0}, {0}, {1}"),
		FText::FromString(QueryBoundSize.ToString()),
		FText::FromString(QueryBoundHeight.ToString()));
	return Desc;
}

void UEnvQueryGenerator_Covers::FilterCovers(TArray<FCover>& InOut) const
{
	const bool bIncludeLeftCoverStanding = IncludeLeftCoverStanding.GetValue();
	const bool bIncludeRightCoverStanding = IncludeRightCoverStanding.GetValue();
	const bool bIncludeLeftCoverCrouched = IncludeLeftCoverCrouched.GetValue();
	const bool bIncludeRightCoverCrouched = IncludeRightCoverCrouched.GetValue();
	const bool bIncludeFrontCoverCrouched = IncludeFrontCoverCrouched.GetValue();
	const bool bIncludeOnlyCrouched = IncludeOnlyCrouched.GetValue();

	for (int32 Idx = InOut.Num() - 1; Idx >= 0; Idx--)
	{
		const FCover& CoverRef = InOut[Idx];
		const FCoverData& DataRef = CoverRef.Data;

		bool bPass = false;

		if (bIncludeLeftCoverStanding && DataRef.bLeftCoverStanding)
			bPass = true;
		if (bIncludeRightCoverStanding && DataRef.bRightCoverStanding)
			bPass = true;
		if (bIncludeLeftCoverCrouched && DataRef.bLeftCoverCrouched)
			bPass = true;
		if (bIncludeRightCoverCrouched && DataRef.bRightCoverCrouched)
			bPass = true;
		if (bIncludeFrontCoverCrouched && DataRef.bFrontCoverCrouched)
			bPass = true;

		if (bIncludeOnlyCrouched && !DataRef.bCrouchedCover)
		{
			bPass = false;
		}

		if (!bPass)
		{
			InOut.RemoveAtSwap(Idx);
		}
	}
}

#undef LOCTEXT_NAMESPACE