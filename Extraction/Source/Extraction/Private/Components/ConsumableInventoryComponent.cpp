// UConsumableInventoryComponent implementation.

#include "Components/ConsumableInventoryComponent.h"
#include "Character/ExtractionPlayerInterface.h"
#include "Components/HealthComponent.h"
#include "GameFramework/Actor.h"
#include "Net/UnrealNetwork.h"

UConsumableInventoryComponent::UConsumableInventoryComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UConsumableInventoryComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION(UConsumableInventoryComponent, StimCount, COND_OwnerOnly);
}

int32 UConsumableInventoryComponent::AddStims(const int32 Amount)
{
	AActor* Owner = GetOwner();
	if (!IsValid(Owner) || !Owner->HasAuthority() || Amount <= 0) return 0;

	const int32 PreviousCount = StimCount;
	StimCount = FMath::Clamp(StimCount + Amount, 0, MaxStims);
	const int32 Added = StimCount - PreviousCount;
	if (Added > 0)
	{
		BroadcastStimCount();
		Owner->ForceNetUpdate();
	}

	return Added;
}

bool UConsumableInventoryComponent::TryUseStim()
{
	AActor* Owner = GetOwner();
	if (!IsValid(Owner)) return false;

	if (!Owner->HasAuthority())
	{
		ServerTryUseStim();
		return false;
	}

	return TryUseStimAuthority();
}

void UConsumableInventoryComponent::ServerTryUseStim_Implementation()
{
	TryUseStimAuthority();
}

bool UConsumableInventoryComponent::TryUseStimAuthority()
{
	AActor* Owner = GetOwner();
	if (!IsValid(Owner) || !Owner->HasAuthority() || StimCount <= 0) return false;

	IExtractionPlayerInterface* Player = Cast<IExtractionPlayerInterface>(Owner);
	UHealthComponent* Health = Player ? Player->GetHealthComponent() : nullptr;
	if (!IsValid(Health) || Health->IsDead() || Player->GetIsDBNO()) return false;
	if (Health->GetCurrentHealth() >= Health->GetMaxHealth()) return false;

	Health->Heal(HealAmount);
	--StimCount;
	BroadcastStimCount();
	Owner->ForceNetUpdate();
	NotifyOwningPlayerStimUsed();
	return true;
}

void UConsumableInventoryComponent::OnRep_StimCount()
{
	BroadcastStimCount();
}

void UConsumableInventoryComponent::BroadcastStimCount()
{
	OnStimCountChanged.Broadcast(StimCount);
}

void UConsumableInventoryComponent::NotifyOwningPlayerStimUsed()
{
	if (GetNetMode() == NM_Standalone)
	{
		OnStimUsedNative.Broadcast();
		return;
	}

	ClientConfirmStimUsed();
}

void UConsumableInventoryComponent::ClientConfirmStimUsed_Implementation()
{
	OnStimUsedNative.Broadcast();
}
