// UConsumableInventoryComponent implementation.

#include "Components/ConsumableInventoryComponent.h"
#include "Character/ExtractionPlayerInterface.h"
#include "Components/HealthComponent.h"
#include "ExtractionTypes.h"
#include "Game/MissionInventorySubsystem.h"
#include "GameFramework/Actor.h"
#include "GameplayTagAssetInterface.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY(LogConsumables);

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

void UConsumableInventoryComponent::BeginPlay()
{
	Super::BeginPlay();

	GrantStartingStims();
}

void UConsumableInventoryComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearStimTimers();
	bStimUseActive = false;
	bStimHealPending = false;

	Super::EndPlay(EndPlayReason);
}

void UConsumableInventoryComponent::GrantStartingStims()
{
	AActor* Owner = GetOwner();
	if (!IsValid(Owner) || !Owner->HasAuthority() || StartingStims <= 0) return;

	// Player pouch only. The companion shares IExtractionPlayerInterface, so the player tag is
	// the discriminator if this component is ever attached somewhere else.
	const IGameplayTagAssetInterface* Tagged = Cast<IGameplayTagAssetInterface>(Owner);
	if (!Tagged || !Tagged->HasMatchingGameplayTag(TAG_Character_Player)) return;

	AddStims(StartingStims);
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

void UConsumableInventoryComponent::SetStimCount(const int32 NewCount)
{
	AActor* Owner = GetOwner();
	if (!IsValid(Owner) || !Owner->HasAuthority()) return;

	const int32 Clamped = FMath::Clamp(NewCount, 0, MaxStims);
	if (Clamped == StimCount) return;

	StimCount = Clamped;
	BroadcastStimCount();
	Owner->ForceNetUpdate();
}

bool UConsumableInventoryComponent::TryUseStim()
{
	AActor* Owner = GetOwner();
	if (!IsValid(Owner)) return false;

	// Silent: a second press mid-injection is a double-tap, not a refusal worth a toast.
	if (bStimUseActive) return false;

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
	if (!IsValid(Owner) || !Owner->HasAuthority() || bStimUseActive) return false;

	IExtractionPlayerInterface* Player = Cast<IExtractionPlayerInterface>(Owner);
	UHealthComponent* Health = Player ? Player->GetHealthComponent() : nullptr;
	if (!IsValid(Health) || Health->IsDead() || Player->GetIsDBNO()) return false;

	if (StimCount <= 0)
	{
		UE_LOG(LogConsumables, Verbose, TEXT("'%s': stim refused — pouch empty."), *GetNameSafe(Owner));
		NotifyOwningPlayerRefused(EStimUseRefusal::NoStims);
		return false;
	}

	if (Health->GetCurrentHealth() >= Health->GetMaxHealth())
	{
		UE_LOG(LogConsumables, Verbose, TEXT("'%s': stim refused — already at full health."), *GetNameSafe(Owner));
		NotifyOwningPlayerRefused(EStimUseRefusal::FullHealth);
		return false;
	}

	// Spend on commit, heal on the needle hit: the injection can't be spammed and can't be
	// cancelled into a free heal.
	--StimCount;
	BroadcastStimCount();
	Owner->ForceNetUpdate();

	StartStimUseClock();
	MarkStimHealPending();
	NotifyOwningPlayerStimUsed();
	return true;
}

void UConsumableInventoryComponent::CancelStimUse()
{
	const AActor* Owner = GetOwner();
	if (!IsValid(Owner) || !Owner->HasAuthority())
	{
		UE_LOG(LogConsumables, Warning, TEXT("'%s': CancelStimUse ignored — authority only. A client cancel "
			"would only unlock the local gate while the server's timers ran on."), *GetNameSafe(Owner));
		return;
	}

	// Sent before the local teardown, while a use is still known to be running.
	if (bStimUseActive && GetNetMode() != NM_Standalone)
		ClientCancelStimUse();

	CancelStimUseLocally();
}

void UConsumableInventoryComponent::CancelStimUseLocally()
{
	bStimHealPending = false;
	EndStimUse();
}

void UConsumableInventoryComponent::StartStimUseClock()
{
	UWorld* World = GetWorld();
	if (!World) return;

	bStimUseActive = true;
	const float Duration = FMath::Max(StimUseDurationSeconds, MinStimUseDuration);
	World->GetTimerManager().SetTimer(
		StimUseTimerHandle, this, &UConsumableInventoryComponent::FinishStimUse, Duration, false);
}

void UConsumableInventoryComponent::MarkStimHealPending()
{
	bStimHealPending = true;
}

void UConsumableInventoryComponent::ApplyStimHealNow()
{
	const AActor* Owner = GetOwner();
	if (!IsValid(Owner) || !Owner->HasAuthority())
	{
		UE_LOG(LogConsumables, Verbose, TEXT("'%s': ApplyStimHealNow ignored off authority."), *GetNameSafe(Owner));
		return;
	}

	ApplyStimHeal();
}

void UConsumableInventoryComponent::ApplyStimHeal()
{
	if (!bStimHealPending) return;
	bStimHealPending = false;

	IExtractionPlayerInterface* Player = Cast<IExtractionPlayerInterface>(GetOwner());
	UHealthComponent* Health = Player ? Player->GetHealthComponent() : nullptr;
	if (!IsValid(Health) || Health->IsDead()) return;

	Health->Heal(HealAmount);
}

void UConsumableInventoryComponent::FinishStimUse()
{
	// Backstop: if no StimHeal notify fired during the montage, the heal lands here at window
	// end. ApplyStimHeal is idempotent via bStimHealPending, so a notify that already fired
	// makes this a no-op.
	ApplyStimHeal();
	EndStimUse();
}

void UConsumableInventoryComponent::EndStimUse()
{
	if (!bStimUseActive) return;

	bStimUseActive = false;
	bStimHealPending = false;
	ClearStimTimers();
	OnStimUseEndedNative.Broadcast();
}

void UConsumableInventoryComponent::ClearStimTimers()
{
	const UWorld* World = GetWorld();
	if (!World) return;

	World->GetTimerManager().ClearTimer(StimUseTimerHandle);
}

void UConsumableInventoryComponent::OnRep_StimCount()
{
	BroadcastStimCount();
}

void UConsumableInventoryComponent::BroadcastStimCount()
{
	OnStimCountChanged.Broadcast(StimCount);
}

void UConsumableInventoryComponent::BroadcastStimCountNow()
{
	BroadcastStimCount();
}

void UConsumableInventoryComponent::NotifyOwningPlayerStimUsed()
{
	if (GetNetMode() == NM_Standalone)
	{
		HandleStimUseStartedLocally();
		return;
	}

	ClientConfirmStimUsed();
}

void UConsumableInventoryComponent::ClientConfirmStimUsed_Implementation()
{
	HandleStimUseStartedLocally();
}

void UConsumableInventoryComponent::ClientCancelStimUse_Implementation()
{
	CancelStimUseLocally();
}

void UConsumableInventoryComponent::HandleStimUseStartedLocally()
{
	// On a listen server this lands on the object that already started the authority clock.
	if (!bStimUseActive) StartStimUseClock();

	OnStimUsedNative.Broadcast();
}

void UConsumableInventoryComponent::NotifyOwningPlayerRefused(EStimUseRefusal Reason)
{
	if (GetNetMode() == NM_Standalone)
	{
		ShowRefusalToast(Reason);
		return;
	}

	ClientStimUseRefused(Reason);
}

void UConsumableInventoryComponent::ClientStimUseRefused_Implementation(EStimUseRefusal Reason)
{
	ShowRefusalToast(Reason);
}

void UConsumableInventoryComponent::ShowRefusalToast(EStimUseRefusal Reason) const
{
	UWorld* World = GetWorld();
	UMissionInventorySubsystem* Inventory = World ? World->GetSubsystem<UMissionInventorySubsystem>() : nullptr;
	if (!IsValid(Inventory)) return;

	Inventory->OnLootNotify.Broadcast(Reason == EStimUseRefusal::NoStims ? NoStimsMessage : FullHealthMessage);
}
