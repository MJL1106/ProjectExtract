// Player-owned, server-authoritative consumable inventory.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ConsumableInventoryComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnStimCountChanged, int32, NewStimCount);
DECLARE_MULTICAST_DELEGATE(FOnStimUsedNative);

UCLASS(ClassGroup = "Inventory", meta = (BlueprintSpawnableComponent))
class EXTRACTION_API UConsumableInventoryComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UConsumableInventoryComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Authority-only. Returns the number of stims accepted after capacity clamping. */
	UFUNCTION(BlueprintCallable, Category = "Inventory|Consumables")
	int32 AddStims(int32 Amount);

	/** Uses a stim on authority or requests use from the owning client. */
	UFUNCTION(BlueprintCallable, Category = "Inventory|Consumables")
	bool TryUseStim();

	UFUNCTION(BlueprintPure, Category = "Inventory|Consumables")
	int32 GetStimCount() const { return StimCount; }

	UPROPERTY(BlueprintAssignable, Category = "Inventory|Consumables")
	FOnStimCountChanged OnStimCountChanged;

	/** Local-only success signal used by the owning player for animation/UI feedback. */
	FOnStimUsedNative OnStimUsedNative;

protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Inventory|Consumables", meta = (ClampMin = "1", ClampMax = "3"))
	int32 MaxStims = 3;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Inventory|Consumables", meta = (ClampMin = "0.0"))
	float HealAmount = 50.f;

private:
	UPROPERTY(ReplicatedUsing = OnRep_StimCount)
	int32 StimCount = 0;

	UFUNCTION()
	void OnRep_StimCount();

	UFUNCTION(Server, Reliable)
	void ServerTryUseStim();

	UFUNCTION(Client, Reliable)
	void ClientConfirmStimUsed();

	bool TryUseStimAuthority();
	void BroadcastStimCount();
	void NotifyOwningPlayerStimUsed();
};
