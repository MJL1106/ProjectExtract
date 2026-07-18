// Player-owned bridge between authoritative weapon state and first-person presentation.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Core/ExtractionTypes.h"
#include "Data/PlayerWeaponPresentationTypes.h"
#include "PlayerWeaponPresentationComponent.generated.h"

class AExtractionPlayer;
class AWeaponBase;
class APlayerWeaponAttachmentView;
class APlayerWeaponView;
struct FStreamableHandle;
class UPlayerWeaponAttachmentDefinition;
class UPlayerWeaponPresentationProfile;
class UWeaponComponent;
enum class EPlayerWeaponSeatPolicy : uint8;

enum class EPlayerWeaponViewSuppressionReason : uint8
{
	None			= 0,
	Manual			= 1 << 0,
	Traversal		= 1 << 1,
	Takedown		= 1 << 2,
	DBNO			= 1 << 3,
	Reviving		= 1 << 4,
	BeingRevived	= 1 << 5,
};
ENUM_CLASS_FLAGS(EPlayerWeaponViewSuppressionReason);

DECLARE_MULTICAST_DELEGATE_OneParam(FOnPresentedWeaponChangedNative, AWeaponBase*);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnPresentedWeaponViewChangedNative, APlayerWeaponView*);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnPresentedBooleanStateChangedNative, bool);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnPresentationActiveChangedNative, bool);
DECLARE_MULTICAST_DELEGATE(FOnPresentedShotNative);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnPresentedAmmoChangedNative, int32, int32);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnPresentedReloadPhaseChangedNative, EWeaponReloadPhase);
DECLARE_MULTICAST_DELEGATE_OneParam(
	FOnPresentedSightChangedNative, const FPlayerWeaponResolvedSight&);

/**
 * Event-driven owner of the player's weapon presentation state.
 * Gameplay authority remains in UWeaponComponent and AWeaponBase.
 */
UCLASS(ClassGroup=(Weapon))
class EXTRACTION_API UPlayerWeaponPresentationComponent : public UActorComponent
{
	GENERATED_BODY()

public:

	UPlayerWeaponPresentationComponent();

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Re-evaluates local presentation eligibility after possession/controller changes. */
	void RefreshPresentation();

	FOnPresentationActiveChangedNative OnPresentationActiveChangedNative;
	FOnPresentedWeaponChangedNative OnPresentedWeaponChangedNative;
	FOnPresentedWeaponViewChangedNative OnPresentedWeaponViewChangedNative;
	FOnPresentedBooleanStateChangedNative OnPresentedAimingChangedNative;
	FOnPresentedBooleanStateChangedNative OnPresentedTriggerChangedNative;
	FOnPresentedShotNative OnPresentedShotNative;
	FOnPresentedAmmoChangedNative OnPresentedAmmoChangedNative;
	FOnPresentedReloadPhaseChangedNative OnPresentedReloadPhaseChangedNative;
	FOnPresentedSightChangedNative OnPresentedSightChangedNative;

	UFUNCTION(BlueprintPure, Category = "Weapon|Presentation")
	AWeaponBase* GetCurrentWeapon() const { return CurrentWeapon.Get(); }

	UFUNCTION(BlueprintPure, Category = "Weapon|Presentation")
	bool IsPresentationActive() const { return bPresentationActive; }

	UFUNCTION(BlueprintPure, Category = "Weapon|Presentation")
	bool IsAiming() const { return bIsAiming; }

	UFUNCTION(BlueprintPure, Category = "Weapon|Presentation")
	bool IsTriggerHeld() const { return bTriggerHeld; }

	UFUNCTION(BlueprintPure, Category = "Weapon|Presentation")
	int32 GetCurrentAmmo() const { return CurrentAmmo; }

	UFUNCTION(BlueprintPure, Category = "Weapon|Presentation")
	int32 GetReserveAmmo() const { return ReserveAmmo; }

	UFUNCTION(BlueprintPure, Category = "Weapon|Presentation")
	EWeaponReloadPhase GetLastReloadPhase() const { return LastReloadPhase; }

	UFUNCTION(BlueprintPure, Category = "Weapon|Presentation")
	bool HasObservedReloadPhase() const { return bHasObservedReloadPhase; }

	UFUNCTION(BlueprintPure, Category = "Weapon|Presentation|View")
	APlayerWeaponView* GetActiveWeaponView() const { return ActiveWeaponView; }

	UFUNCTION(BlueprintPure, Category = "Weapon|Presentation|ADS")
	APlayerWeaponAttachmentView* GetActiveOpticView() const
	{
		return ActiveOpticView;
	}

	UFUNCTION(BlueprintPure, Category = "Weapon|Presentation|ADS")
	FName GetSelectedOpticId() const { return SelectedOpticId; }

	UFUNCTION(BlueprintPure, Category = "Weapon|Presentation|ADS")
	FPlayerWeaponResolvedSight GetResolvedSight() const { return ResolvedSight; }

	/** Reconstructs the single canonical aim anchor consumed by the kit adapter. */
	UFUNCTION(BlueprintPure, Category = "Weapon|Presentation|ADS")
	bool GetResolvedAimSourceWorld(FTransform& OutAimSourceWorld) const;

	UFUNCTION(BlueprintCallable, Category = "Weapon|Presentation|ADS")
	bool SetSelectedOpticId(FName OpticId);

	UFUNCTION(BlueprintPure, Category = "Weapon|Presentation|View")
	bool HasCachedViewPlacement() const { return bHasCachedViewPlacement; }

	void SetWeaponViewHidden(bool bHidden);
	void SetWeaponViewSuppressed(
		EPlayerWeaponViewSuppressionReason Reason, bool bSuppressed);

#if WITH_DEV_AUTOMATION_TESTS
	void SetWeaponViewCreatedHookForTesting(
		TFunction<void(APlayerWeaponView&)> Hook)
	{
		WeaponViewCreatedHookForTesting = MoveTemp(Hook);
	}
	bool AreVisibilitySourcesBoundForTesting() const
	{
		return bVisibilitySourcesBound;
	}
#endif

	static bool ResolveViewPlacement(
		const APlayerWeaponView& View,
		EPlayerWeaponSeatPolicy SeatPolicy,
		FTransform& OutPlacement);

protected:

	virtual void BeginPlay() override;

private:

	void DiscoverAndBindWeaponComponent();
	void UnbindWeaponComponent();

	void HandleWeaponChanged(AWeaponBase* Weapon);
	void HandleAimingChanged(bool bNewAiming);
	void HandleTriggerChanged(bool bNewTriggerHeld);
	void HandleShot();
	void HandleAmmoChanged(int32 NewCurrentAmmo, int32 NewReserveAmmo);
	void HandleReloadPhaseChanged(EWeaponReloadPhase Phase);

	bool CanRouteLocalPresentation() const;
	void SetPresentationActive(bool bNewActive);
	void PublishPresentationSnapshot();
	void RouteLegacyWeaponChanged(AWeaponBase* Weapon);
	void RouteLegacyAimingChanged(bool bNewAiming);
	void ClearLegacyWeaponPresentation();
	void SynchronizeWeaponView();
	void RequestWeaponViewLoad(
		AWeaponBase& Weapon,
		const UPlayerWeaponPresentationProfile& Profile);
	void HandleWeaponViewClassLoaded(
		TWeakObjectPtr<AWeaponBase> RequestedWeapon,
		FSoftObjectPath RequestedClassPath,
		uint32 RequestGeneration);
	void CancelPendingWeaponViewLoad();
	void DestroyWeaponView();
	void SynchronizeWeaponSight();
	void InitializeSightSelection(
		AWeaponBase& Weapon,
		UPlayerWeaponPresentationProfile& Profile);
	bool CommitIronSight(const UPlayerWeaponPresentationProfile& Profile);
	bool CommitIronSightFallback(
		const UPlayerWeaponPresentationProfile& Profile);
	bool SelectOpticDefinition(
		AWeaponBase& Weapon,
		UPlayerWeaponPresentationProfile& Profile,
		const UPlayerWeaponAttachmentDefinition& Definition);
	bool SpawnAndCommitOptic(
		AWeaponBase& Weapon,
		UPlayerWeaponPresentationProfile& Profile,
		const UPlayerWeaponAttachmentDefinition& Definition,
		TSubclassOf<APlayerWeaponAttachmentView> ViewClass,
		const FSoftObjectPath& ViewClassPath);
	APlayerWeaponAttachmentView* CreateOpticView(
		TSubclassOf<APlayerWeaponAttachmentView> ViewClass);
	bool PlaceOpticView(APlayerWeaponAttachmentView& View) const;
	bool ResolveIronSight(
		const UPlayerWeaponPresentationProfile& Profile,
		FPlayerWeaponResolvedSight& OutSight) const;
	bool ResolveOpticSight(
		const UPlayerWeaponAttachmentDefinition& Definition,
		const APlayerWeaponAttachmentView& View,
		const UPlayerWeaponPresentationProfile& Profile,
		FPlayerWeaponResolvedSight& OutSight) const;
	bool GetHandSocketWorld(FTransform& OutTransform) const;
	void CommitResolvedSight(const FPlayerWeaponResolvedSight& Sight);
	void ClearResolvedSight();
	void RecoverFromFailedOpticSelection(
		const UPlayerWeaponPresentationProfile& Profile,
		FName FailedOpticId);
	void DestroyOpticView();
	void RequestOpticViewLoad(
		AWeaponBase& Weapon,
		UPlayerWeaponPresentationProfile& Profile,
		const UPlayerWeaponAttachmentDefinition& Definition);
	void HandleOpticViewClassLoaded(
		TWeakObjectPtr<AWeaponBase> RequestedWeapon,
		TWeakObjectPtr<UPlayerWeaponPresentationProfile> RequestedProfile,
		FName LoadedOpticId,
		FSoftObjectPath RequestedClassPath,
		uint32 RequestGeneration);
	void CancelPendingOpticViewLoad();
	bool SpawnWeaponView(
		AWeaponBase& Weapon,
		const UPlayerWeaponPresentationProfile& Profile,
		TSubclassOf<APlayerWeaponView> ViewClass,
		const FSoftObjectPath& ViewClassPath);
	APlayerWeaponView* CreateWeaponView(
		TSubclassOf<APlayerWeaponView> ViewClass);
	bool CacheViewPlacement(
		const APlayerWeaponView& View,
		const UPlayerWeaponPresentationProfile& Profile,
		const FSoftObjectPath& ViewClassPath);
	bool AttachWeaponView(APlayerWeaponView& View) const;
	void ResetCachedViewPlacement();
	void BindVisibilitySources();
	void UnbindVisibilitySources();
	void ApplyWeaponViewVisibility();
	void HandleTraversalStarted(
		ETraversalType Type, float PlayRate,
		FVector ObstacleLocation, FVector LandingLocation);
	void HandleTraversalEnded();

	UFUNCTION()
	void HandleDBNOStateChanged(bool bNewIsDBNO, float BleedoutDuration);

	UPlayerWeaponPresentationProfile* ResolveProfile(
		const AWeaponBase* Weapon) const;

	UPROPERTY(Transient)
	TObjectPtr<UWeaponComponent> WeaponComponent;

	UPROPERTY(Transient)
	TWeakObjectPtr<AWeaponBase> CurrentWeapon;

	UPROPERTY(Transient)
	TObjectPtr<APlayerWeaponView> ActiveWeaponView;

	UPROPERTY(Transient)
	TObjectPtr<APlayerWeaponAttachmentView> ActiveOpticView;

	UPROPERTY(Transient)
	FPlayerWeaponResolvedSight ResolvedSight;

	/** Validated class retained for the current weapon across local view recreation. */
	UPROPERTY(Transient)
	TObjectPtr<UClass> CachedLoadedViewClass;

	TWeakObjectPtr<AExtractionPlayer> PlayerOwner;
	TWeakObjectPtr<AWeaponBase> LastRoutedWeapon;
	TWeakObjectPtr<AWeaponBase> ViewWeapon;
	TWeakObjectPtr<AWeaponBase> SightWeapon;
	TWeakObjectPtr<UPlayerWeaponPresentationProfile> SightProfile;
	TWeakObjectPtr<AWeaponBase> PendingViewLoadWeapon;
	TWeakObjectPtr<AWeaponBase> PendingOpticLoadWeapon;
	TWeakObjectPtr<UPlayerWeaponPresentationProfile> PendingOpticLoadProfile;
	TSharedPtr<FStreamableHandle> PendingViewLoad;
	TSharedPtr<FStreamableHandle> PendingOpticLoad;
	FSoftObjectPath PendingViewClassPath;
	FSoftObjectPath PendingOpticClassPath;
	FSoftObjectPath ActiveOpticClassPath;
	FSoftObjectPath CachedLoadedViewClassPath;
	FSoftObjectPath CachedPlacementViewClassPath;
	FTransform CachedViewPlacement = FTransform::Identity;
	uint32 ViewLoadGeneration = 0;
	uint32 OpticLoadGeneration = 0;
	FName PendingOpticId = NAME_None;
	/** Requested preference; committed selection remains SelectedOpticId. */
	FName RequestedOpticId = NAME_None;
	FName SelectedOpticId = NAME_None;

	int32 CurrentAmmo = 0;
	int32 ReserveAmmo = 0;
	EWeaponReloadPhase LastReloadPhase = EWeaponReloadPhase::Completed;

	bool bIsAiming = false;
	bool bTriggerHeld = false;
	bool bHasObservedReloadPhase = false;
	bool bBoundToWeaponComponent = false;
	bool bPresentationActive = false;
	bool bHasRoutedAiming = false;
	bool bLastRoutedAiming = false;
	bool bHasCachedViewPlacement = false;
	bool bVisibilitySourcesBound = false;
	bool bLegacyPresentationCleared = false;
	bool bForceNextSightPublish = false;
	EPlayerWeaponViewSuppressionReason ViewSuppressionReasons =
		EPlayerWeaponViewSuppressionReason::None;

#if WITH_DEV_AUTOMATION_TESTS
	TFunction<void(APlayerWeaponView&)> WeaponViewCreatedHookForTesting;
#endif
};
