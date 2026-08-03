// EnvQueryTest_CoverDoorCrossing — filter rejects covers whose direct line from the querier
// crosses a closed door. Mirrors the C++ picker loops' AnyClosedDoorBlocksSegment reject.

#include "EnvQueryTest_CoverDoorCrossing.h"
#include "AI/EnvQueryItemType_CoverBase.h"
#include "World/DoorRegistrySubsystem.h"
#include "AIController.h"
#include "Engine/World.h"

UEnvQueryTest_CoverDoorCrossing::UEnvQueryTest_CoverDoorCrossing()
{
	Cost = EEnvTestCost::Low;
	ValidItemType = UEnvQueryItemType_CoverBase::StaticClass();
	SetWorkOnFloatValues(false);
}

void UEnvQueryTest_CoverDoorCrossing::RunTest(FEnvQueryInstance& QueryInstance) const
{
	UObject* QueryOwner = QueryInstance.Owner.Get();
	UWorld* World = QueryInstance.World;
	if (!World) return;

	const UDoorRegistrySubsystem* DoorRegistry = World->GetSubsystem<UDoorRegistrySubsystem>();
	if (!DoorRegistry) return;

	const APawn* QuerierPawn = Cast<APawn>(QueryOwner);
	if (!QuerierPawn)
		if (const AController* QuerierController = Cast<AController>(QueryOwner))
			QuerierPawn = QuerierController->GetPawn();
	if (!IsValid(QuerierPawn)) return;

	const FVector QuerierLoc = QuerierPawn->GetActorLocation();

	BoolValue.BindData(QueryOwner, QueryInstance.QueryID);
	const bool bWantClear = BoolValue.GetValue();

	for (FEnvQueryInstance::ItemIterator It(this, QueryInstance); It; ++It)
	{
		const FCover CoverItem = UEnvQueryItemType_CoverBase::GetValue(
			QueryInstance.RawData.GetData() + QueryInstance.Items[It.GetIndex()].DataOffset);

		const bool bClear = !DoorRegistry->AnyClosedDoorBlocksSegment(QuerierLoc, CoverItem.Data.Location);
		It.SetScore(TestPurpose, FilterType, bClear, bWantClear);
	}
}

FText UEnvQueryTest_CoverDoorCrossing::GetDescriptionDetails() const
{
	return FText::FromString(TEXT("Reject covers whose direct line from the querier crosses a closed door"));
}
