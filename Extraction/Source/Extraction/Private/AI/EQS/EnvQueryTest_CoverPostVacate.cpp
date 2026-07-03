// EnvQueryTest_CoverPostVacate — filter rejects covers on post-vacate cooldown for the querier.

#include "EnvQueryTest_CoverPostVacate.h"
#include "AI/EnvQueryItemType_CoverBase.h"
#include "CoverReservationSubsystem.h"
#include "AIController.h"
#include "Engine/World.h"

UEnvQueryTest_CoverPostVacate::UEnvQueryTest_CoverPostVacate()
{
	Cost = EEnvTestCost::Low;
	ValidItemType = UEnvQueryItemType_CoverBase::StaticClass();
	SetWorkOnFloatValues(false);

	CooldownSeconds.DefaultValue = 6.f;
}

void UEnvQueryTest_CoverPostVacate::RunTest(FEnvQueryInstance& QueryInstance) const
{
	UObject* QueryOwner = QueryInstance.Owner.Get();

	CooldownSeconds.BindData(QueryOwner, QueryInstance.QueryID);
	const float Cooldown = CooldownSeconds.GetValue();

	AController* QuerierController = nullptr;
	if (APawn* Pawn = Cast<APawn>(QueryOwner))
		QuerierController = Pawn->GetController();

	UCoverReservationSubsystem* ResSub = QueryInstance.World ? QueryInstance.World->GetSubsystem<UCoverReservationSubsystem>() : nullptr;

	BoolValue.BindData(QueryOwner, QueryInstance.QueryID);
	const bool bWantFree = BoolValue.GetValue();

	for (FEnvQueryInstance::ItemIterator It(this, QueryInstance); It; ++It)
	{
		const FCover CoverItem = UEnvQueryItemType_CoverBase::GetValue(
			QueryInstance.RawData.GetData() + QueryInstance.Items[It.GetIndex()].DataOffset);

		bool bIsFree = true;
		if (IsValid(ResSub) && IsValid(QuerierController))
		{
			bIsFree = !ResSub->IsOnPostVacateCooldown(CoverItem.Handle, QuerierController, Cooldown);
		}

		It.SetScore(TestPurpose, FilterType, bIsFree, bWantFree);
	}
}

FText UEnvQueryTest_CoverPostVacate::GetDescriptionDetails() const
{
	return FText::Format(
		FText::FromString(TEXT("Post-vacate cooldown: {0}s")),
		FText::AsNumber(CooldownSeconds.DefaultValue));
}
