// Player-owned, server-authoritative consumable inventory.
// A stim use is a committed window, not an instant: the count is spent immediately, the heal
// is driven by UAnimNotify_StimHeal on the injection montage (designer places the needle-hit
// frame), and the owning player locks fire/ADS/reload for the duration (AExtractionPlayer
// gates on IsUsingStim). If the montage has no StimHeal notify, the heal lands at window end
// as a backstop (FinishStimUse).

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ConsumableInventoryComponent.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogConsumables, Log, All);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnStimCountChanged, int32, NewStimCount);
DECLARE_MULTICAST_DELEGATE(FOnStimUsedNative);
DECLARE_MULTICAST_DELEGATE(FOnStimUseEndedNative);

/** Why a stim press was refused. Only the reasons the player can act on are surfaced —
 *  a press during an injection already in progress is a double-tap and stays silent. */
UENUM(BlueprintType)
enum class EStimUseRefusal : uint8
{
	NoStims,
	FullHealth
};

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

	/** Authority-only. Overwrites the stim count (checkpoint restore). Broadcasts the change. */
	UFUNCTION(BlueprintCallable, Category = "Inventory|Consumables")
	void SetStimCount(int32 NewCount);

	/** Uses a stim on authority or requests use from the owning client. */
	UFUNCTION(BlueprintCallable, Category = "Inventory|Consumables")
	bool TryUseStim();

	/** Ends an injection early (DBNO, death). The stim itself is already spent, so a cancel
	 *  forfeits the heal. Idempotent. AUTHORITY ONLY -- the count, the heal and the use-window
	 *  timer live on the server, so a client-side cancel would reopen the local fire/ADS gate
	 *  while the server ran the injection to completion anyway. Logs and no-ops off authority. */
	UFUNCTION(BlueprintCallable, Category = "Inventory|Consumables")
	void CancelStimUse();

	/** Local teardown ONLY, for mirroring a cancel the authority has ALREADY made — the owning
	 *  client's DBNO OnRep lands ahead of the server's ClientCancelStimUse. Deliberately not
	 *  BlueprintCallable: using this to cancel a live injection desyncs the lockout from the
	 *  server's timers, which is what CancelStimUse is for. */
	void CancelStimUseLocally();

	/** Re-broadcasts the current count on demand. The starting stims are granted from BeginPlay,
	 *  long before any HUD widget exists, so the widget's construct has to pull the value —
	 *  OnStimCountChanged fired into an empty delegate and nothing re-broadcasts. */
	UFUNCTION(BlueprintCallable, Category = "Inventory|Consumables")
	void BroadcastStimCountNow();

	/** Applies the pending heal immediately (idempotent via bStimHealPending).
	 *  Intended for UAnimNotify_StimHeal so the designer can place the heal moment on the
	 *  injection montage. FinishStimUse calls ApplyStimHeal at window end as the backstop
	 *  when no notify fires. Authority-only; logs Verbose and no-ops off authority. */
	UFUNCTION(BlueprintCallable, Category = "Inventory|Consumables")
	void ApplyStimHealNow();

	/** True for the whole committed injection window. Drives the player's fire/ADS/reload gate. */
	UFUNCTION(BlueprintPure, Category = "Inventory|Consumables")
	bool IsUsingStim() const { return bStimUseActive; }

	UFUNCTION(BlueprintPure, Category = "Inventory|Consumables")
	int32 GetStimCount() const { return StimCount; }

	/** Pouch capacity — the HUD slot draws Count/Max, so it needs the denominator too. */
	UFUNCTION(BlueprintPure, Category = "Inventory|Consumables")
	int32 GetMaxStims() const { return MaxStims; }

	/** Length of the committed injection window. Drives the HUD slot's cooldown sweep, which must
	 *  match the real lockout rather than a duplicated constant. */
	UFUNCTION(BlueprintPure, Category = "Inventory|Consumables")
	float GetStimUseDuration() const { return StimUseDurationSeconds; }

	UPROPERTY(BlueprintAssignable, Category = "Inventory|Consumables")
	FOnStimCountChanged OnStimCountChanged;

	/** Local-only. Fires at the START of a use so the montage opens with the animation. */
	FOnStimUsedNative OnStimUsedNative;

	/** Local-only. Fires when a use finishes or is cancelled — the injector prop hides here. */
	FOnStimUseEndedNative OnStimUseEndedNative;

#if WITH_DEV_AUTOMATION_TESTS
	void TestSetStimCount(int32 Count) { StimCount = FMath::Clamp(Count, 0, MaxStims); }
#endif

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Inventory|Consumables", meta = (ClampMin = "1", ClampMax = "3"))
	int32 MaxStims = 3;

	/** Granted on BeginPlay through AddStims (authority, player pawn only). 0 = start empty. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Inventory|Consumables", meta = (ClampMin = "0", ClampMax = "3"))
	int32 StartingStims = 1;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Inventory|Consumables", meta = (ClampMin = "0.0"))
	float HealAmount = 50.f;

	/** Length of the committed injection window — matches the injection montage. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Inventory|Consumables", meta = (ClampMin = "0.0"))
	float StimUseDurationSeconds = 3.4f;

	/** Toast raised on the owning client when the key is pressed with an empty pouch. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Inventory|Consumables")
	FText NoStimsMessage = NSLOCTEXT("Loot", "StimNone", "No stims");

	/** Toast raised on the owning client when a stim would heal nothing. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Inventory|Consumables")
	FText FullHealthMessage = NSLOCTEXT("Loot", "StimFullHealth", "Already at full health");

private:
	/** SetTimer never fires on a rate of zero -- clamp the use-window duration above it. */
	static constexpr float MinStimUseDuration = 0.01f;

	UPROPERTY(ReplicatedUsing = OnRep_StimCount)
	int32 StimCount = 0;

	/** Not replicated: set on authority when the use starts, and on the owning client from the
	 *  same confirm RPC that fires OnStimUsedNative, so both sides gate off one flag. */
	bool bStimUseActive = false;

	/** Authority-only: the heal is still owed. Cleared once applied, or dropped on cancel. */
	bool bStimHealPending = false;

	FTimerHandle StimUseTimerHandle;

	UFUNCTION()
	void OnRep_StimCount();

	UFUNCTION(Server, Reliable)
	void ServerTryUseStim();

	UFUNCTION(Client, Reliable)
	void ClientConfirmStimUsed();

	UFUNCTION(Client, Reliable)
	void ClientCancelStimUse();

	UFUNCTION(Client, Reliable)
	void ClientStimUseRefused(EStimUseRefusal Reason);

	bool TryUseStimAuthority();
	void GrantStartingStims();
	void BroadcastStimCount();

	void NotifyOwningPlayerStimUsed();
	void NotifyOwningPlayerRefused(EStimUseRefusal Reason);
	void HandleStimUseStartedLocally();
	void ShowRefusalToast(EStimUseRefusal Reason) const;

	/** Shared lockout clock (flag + end timer), run on whichever side calls it. */
	void StartStimUseClock();

	/** Authority-only: marks the heal as pending. The actual heal is applied either by
	 *  UAnimNotify_StimHeal (designer-placed on the montage) or by FinishStimUse (backstop). */
	void MarkStimHealPending();

	void ApplyStimHeal();
	void FinishStimUse();
	void EndStimUse();
	void ClearStimTimers();
};
