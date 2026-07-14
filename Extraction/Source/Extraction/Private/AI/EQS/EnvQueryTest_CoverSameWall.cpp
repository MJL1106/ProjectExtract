// EnvQueryTest_CoverSameWall — filter rejects covers not on the same wall as the BB reference cover.

#include "EnvQueryTest_CoverSameWall.h"
#include "AI/EnvQueryItemType_CoverBase.h"
#include "AI/BlackboardKeyType_Cover.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType.h"
#include "CoverGeometryStatics.h"
#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"

UEnvQueryTest_CoverSameWall::UEnvQueryTest_CoverSameWall()
{
	Cost = EEnvTestCost::Low;
	ValidItemType = UEnvQueryItemType_CoverBase::StaticClass();
	SetWorkOnFloatValues(false);
}

void UEnvQueryTest_CoverSameWall::RunTest(FEnvQueryInstance& QueryInstance) const
{
	UObject* QueryOwner = QueryInstance.Owner.Get();

	BoolValue.BindData(QueryOwner, QueryInstance.QueryID);
	const bool bWantSameWall = BoolValue.GetValue();

	// Read the reference cover from the querier's blackboard
	FCoverData RefData;
	bool bHasRef = false;

	if (APawn* Pawn = Cast<APawn>(QueryOwner))
	{
		if (AAIController* AIC = Cast<AAIController>(Pawn->GetController()))
		{
			if (UBlackboardComponent* BB = AIC->GetBlackboardComponent())
			{
				const FBlackboard::FKey KeyID = BB->GetKeyID(CoverTargetKeyName);
				if (KeyID != FBlackboard::InvalidKey)
				{
					// Verify the key type is actually UBlackboardKeyType_Cover before reading as FCover
					const TSubclassOf<UBlackboardKeyType> KeyType = BB->GetKeyType(KeyID);
					if (KeyType == UBlackboardKeyType_Cover::StaticClass())
					{
						const uint8* RawData = BB->GetKeyRawData(KeyID);
						if (RawData)
						{
							const FCover RefCover = UBlackboardKeyType_Cover::GetValue(nullptr, RawData);
							if (RefCover.IsValid())
							{
								RefData = RefCover.Data;
								bHasRef = true;
							}
						}
					}
				}
			}
		}
	}

	const float CosThreshold = FMath::Cos(FMath::DegreesToRadians(ToleranceDeg));

	// Hoist wall-normal computation out of the item loop
	const FVector RefNormal = bHasRef ? RefData.DirectionToWall.GetSafeNormal2D() : FVector::ZeroVector;

	for (FEnvQueryInstance::ItemIterator It(this, QueryInstance); It; ++It)
	{
		const FCover CoverItem = UEnvQueryItemType_CoverBase::GetValue(
			QueryInstance.RawData.GetData() + QueryInstance.Items[It.GetIndex()].DataOffset);

		bool bSameWall = true;

		if (bHasRef)
		{
			// Angular test
			const bool bAngleOK = UCoverGeometryStatics::IsSameWall(CoverItem.Data, RefData, CosThreshold);

			// Lateral band test: project item onto the wall-normal axis of the reference
			const FVector Delta = CoverItem.Data.Location - RefData.Location;
			const float NormalDist = FMath::Abs(FVector::DotProduct(Delta, RefNormal));
			const bool bBandOK = NormalDist <= NormalBandTolerance;

			bSameWall = bAngleOK && bBandOK;
		}
		// If no reference cover or wrong key type, pass all items (no-op filter)

		It.SetScore(TestPurpose, FilterType, bSameWall, bWantSameWall);
	}
}

FText UEnvQueryTest_CoverSameWall::GetDescriptionDetails() const
{
	return FText::Format(
		FText::FromString(TEXT("Same wall (tol {0} deg, band {1} cm)")),
		FText::AsNumber(ToleranceDeg),
		FText::AsNumber(NormalBandTolerance));
}
