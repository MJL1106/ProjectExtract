// AEnemyCharacter — single character class for all 7 enemy archetypes, driven by UEnemyArchetypeData.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "GameplayTagAssetInterface.h"
#include "GameplayTagContainer.h"
#include "GenericTeamAgentInterface.h"
#include "Perception/AISightTargetInterface.h"
#include "AIShooterInterface.h"
#include "ExtractionTypes.h"
#include "EnemyTypes.h"
#include "EnemyCharacter.generated.h"

class UHealthComponent;
class UEnemyArchetypeData;
class AWeaponBase;
class APatrolRoute;
class AEnemyAIController;

// Phase 3 bolt-on components — forward-declared; headers live in Enemy/Components/ (authored by slices B/C).
class UEnemyArmourComponent;
class UEnemyShieldComponent;
class UEnemyGrenadierComponent;
class USquadAuraComponent;
class UEnemySniperTelegraphComponent;

// Phase 4 — suppression & morale (default subobjects, not bolt-ons)
class USuppressionComponent;
class UEnemyMoraleComponent;

// Phase 5 — squad coordination
class UEnemySquadSubsystem;
class UEnemySquad;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTakedownExecuted, AActor*, Instigator);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnMeleePerformed);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnEnemyHitReact, EHitRegion, Region);

UCLASS(Blueprintable)
class EXTRACTION_API AEnemyCharacter : public ACharacter,
	public IGameplayTagAssetInterface,
	public IAISightTargetInterface,
	public IAIShooterInterface,
	public IGenericTeamAgentInterface
{
	GENERATED_BODY()

public:
	AEnemyCharacter();

	virtual void PostInitializeComponents() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual float TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

	// --- IGameplayTagAssetInterface ---
	virtual void GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const override;

	// --- IAISightTargetInterface ---
	virtual UAISense_Sight::EVisibilityResult CanBeSeenFrom(const FCanBeSeenFromContext& Context, FVector& OutSeenLocation, int32& OutNumberOfLoSChecksPerformed, int32& OutNumberOfAsyncLosCheckRequested, float& OutSightStrength, int32* UserData = nullptr, const FOnPendingVisibilityQueryProcessedDelegate* Delegate = nullptr) override;

	// --- IAIShooterInterface ---
	virtual AActor* GetAIAimTarget() const override;
	virtual float GetAIAimSpreadDegrees() const override;
	virtual bool GetAIAimLocation(FVector& OutLocation) const override;

	// --- IGenericTeamAgentInterface ---
	virtual FGenericTeamId GetGenericTeamId() const override;

	// --- Aim API ---

	/** Sets the actor to aim at. Resets the settle timer on a new target; clears on nullptr. */
	UFUNCTION(BlueprintCallable, Category = "Enemy|Combat")
	void SetAimTarget(AActor* NewTarget);

	/** Overrides the world-space aim point used by the weapon when no aim target actor is set.
	 *  Used by BTTask_HeavySuppress to fire at LastKnownLocation. */
	UFUNCTION(BlueprintCallable, Category = "Enemy|Combat")
	void SetAimLocationOverride(FVector Location);

	/** Clears a previously set aim location override. */
	UFUNCTION(BlueprintCallable, Category = "Enemy|Combat")
	void ClearAimLocationOverride();

	/** Multiplier applied to the final spread value. Set/cleared by USquadAuraComponent. */
	UFUNCTION(BlueprintCallable, Category = "Enemy|Combat")
	void SetCommandSpreadMultiplier(float Multiplier);

	/** Additive spread (degrees) stacked on top of the natural spread.
	 *  BT tasks set this while in a special state and clear it on exit/abort. */
	UFUNCTION(BlueprintCallable, Category = "Enemy|Combat")
	void SetExtraSpreadDegrees(float Degrees);

	/** Attempts a melee attack on Target. Enforces range from DA and internal cooldown.
	 *  Returns true if the attack connected (damage was applied). */
	UFUNCTION(BlueprintCallable, Category = "Enemy|Melee")
	bool PerformMelee(AActor* Target);

	/** Fired whenever a melee strike connects — animation/FX hook for BP. */
	UPROPERTY(BlueprintAssignable, Category = "Enemy|Melee")
	FOnMeleePerformed OnMeleePerformed;

	/** Resolves which hit region a damage event maps to (used by armour component and internal hitbox path). */
	EHitRegion ResolveHitRegion(const FDamageEvent& DamageEvent) const;

	// --- Bolt-on component accessors (nullptr if archetype doesn't use them) ---

	UFUNCTION(BlueprintPure, Category = "Enemy|Components")
	UEnemyArmourComponent* GetArmourComponent() const { return ArmourComponent.Get(); }

	UFUNCTION(BlueprintPure, Category = "Enemy|Components")
	UEnemyShieldComponent* GetShieldComponent() const { return ShieldComponent.Get(); }

	UFUNCTION(BlueprintPure, Category = "Enemy|Components")
	UEnemyGrenadierComponent* GetGrenadierComponent() const { return GrenadierComponent.Get(); }

	UFUNCTION(BlueprintPure, Category = "Enemy|Components")
	USquadAuraComponent* GetSquadAuraComponent() const { return SquadAuraComp.Get(); }

	UFUNCTION(BlueprintPure, Category = "Enemy|Components")
	UEnemySniperTelegraphComponent* GetSniperTelegraphComponent() const { return SniperTelegraphComp.Get(); }

	// --- Phase 4: suppression & morale accessors (default subobjects — always present) ---

	UFUNCTION(BlueprintPure, Category = "Enemy|Components")
	USuppressionComponent* GetSuppressionComponent() const { return SuppressionComponent; }

	UFUNCTION(BlueprintPure, Category = "Enemy|Components")
	UEnemyMoraleComponent* GetMoraleComponent() const { return MoraleComponent; }

	/** Broadcast when alive and taking damage — region resolved from the damage event. BP/ABP binds for flinch montages. */
	UPROPERTY(BlueprintAssignable, Category = "Enemy|Combat")
	FOnEnemyHitReact OnHitReact;

	/** Fired after ApplyArchetypeData finishes registering all bolt-on components.
	 *  Called at possess time, before BP BeginPlay fires on placed pawns. BP can bind here for init FX. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Enemy|Components")
	void OnBoltOnComponentsReady();

	// --- Guard post ---

	/** World-space location of this enemy's guard post.
	 *  Returns GuardPostOverride when bOverrideGuardPost is true, otherwise the location captured at BeginPlay. */
	UFUNCTION(BlueprintPure, Category = "Enemy|Patrol")
	FVector GetGuardPostLocation() const;

	/** Yaw (degrees) captured at BeginPlay — used as the base sweep direction for guard-scan. */
	UFUNCTION(BlueprintPure, Category = "Enemy|Patrol")
	float GetInitialPostYaw() const { return InitialPostYaw; }

	/** When enabled, the guard returns to GuardPostOverride instead of the spawn location. */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Enemy|Patrol")
	bool bOverrideGuardPost = false;

	/** World-space override for the guard post (only used when bOverrideGuardPost is true).
	 *  MakeEditWidget lets designers drag it in the viewport. */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Enemy|Patrol",
		meta = (MakeEditWidget, EditCondition = "bOverrideGuardPost"))
	FVector GuardPostOverride = FVector::ZeroVector;

	// --- Move speed ---

	UFUNCTION(BlueprintCallable, Category = "Enemy|Movement")
	void SetMoveSpeedMode(EEnemyMoveSpeedMode Mode);

	// --- Silent takedown ---

	/** True when alive, Unaware, and the instigator is within range in the rear arc (design §4). */
	UFUNCTION(BlueprintPure, Category = "Enemy|Takedown")
	bool CanBeTakenDown(const AActor* TakedownInstigator) const;

	/** Performs a silent instant kill if CanBeTakenDown passes. Returns whether it executed. */
	UFUNCTION(BlueprintCallable, Category = "Enemy|Takedown")
	bool ExecuteTakedown(AActor* TakedownInstigator);

	/** Fired on a successful takedown — animation/FX hook for BP. */
	UPROPERTY(BlueprintAssignable, Category = "Enemy|Takedown")
	FOnTakedownExecuted OnTakedownExecuted;

	// --- Body discovery ---

	/** First caller gets true and owns reporting this body to the director. */
	bool TryMarkBodyReported();

	/** True if TakeDamage was called within the last Window seconds. Used by sniper relocate-on-damaged. */
	UFUNCTION(BlueprintPure, Category = "Enemy|Combat")
	bool WasDamagedRecently(float Window) const;

	// --- Archetype ---

	/** Called by the controller after possession; sets speeds and initialises health from DA. */
	void ApplyArchetypeData();

	UFUNCTION(BlueprintPure, Category = "Enemy|Data")
	const UEnemyArchetypeData* GetArchetypeData() const { return ArchetypeData; }

	UFUNCTION(BlueprintPure, Category = "Enemy|Components")
	UHealthComponent* GetHealthComponent() const { return HealthComponent; }

	UFUNCTION(BlueprintPure, Category = "Enemy|Weapon")
	AWeaponBase* GetCurrentWeapon() const { return CurrentWeapon.Get(); }

	// --- Config ---

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy|Data")
	TObjectPtr<UEnemyArchetypeData> ArchetypeData;

	/** Designer-assigned squad identifier. Enemies with the same SquadId share sightings and coordinate.
	 *  NAME_None = squadless (radius-based morale fallback, no coordination). */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Enemy|Squad")
	FName SquadId;

	/** Returns this enemy's squad (nullptr if squadless or subsystem unavailable). */
	UFUNCTION(BlueprintPure, Category = "Enemy|Squad")
	UEnemySquad* GetSquad() const;

	/** Patrol route assigned in the level for this character. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Enemy|Patrol")
	TObjectPtr<APatrolRoute> PatrolRoute;

	/** Mesh socket to attach the spawned weapon to. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Weapon")
	FName WeaponSocket = TEXT("WeaponSocket");

	/** Maps skeleton bone names to hit regions for damage multiplier lookup. Mannequin defaults. */
	UPROPERTY(EditDefaultsOnly, Category = "Enemy|Hitbox")
	TMap<FName, EHitRegion> BoneToHitRegionMap;

private:

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy|Components", meta = (AllowPrivateAccess))
	TObjectPtr<UHealthComponent> HealthComponent;

	UPROPERTY()
	TObjectPtr<AWeaponBase> CurrentWeapon;

	TWeakObjectPtr<AActor> CurrentAimTarget;
	TWeakObjectPtr<AController> LastDamageInstigator;

	/** World time at which the current aim target was set. Used to compute settle alpha without Tick. */
	float AimStartWorldTime = -1e9f;
	float LastDamageWorldTime = -1e9f;

	// Bug 5a: re-aim settle grace — prevents settle timer reset on same-target re-acquire within a window.
	TWeakObjectPtr<AActor> LastSettleTarget;
	float LastAimClearWorldTime = -1e9f;
	/** Saved AimStartWorldTime to restore if the same target is re-acquired within grace. */
	float SavedAimStartWorldTime = -1e9f;

	/** Grace period (seconds) after clearing aim within which re-acquiring the same target
	 *  restores the prior settle progress instead of resetting it. */
	UPROPERTY(EditDefaultsOnly, Category = "Enemy|Combat")
	float ReAimSettleGrace = 1.5f;

	// Phase 3 — spread modifiers
	float CommandSpreadMultiplier = 1.f;
	float ExtraSpreadDegrees = 0.f;

	// Phase 3 — aim location override (used when no aim target actor is set)
	bool bHasAimLocationOverride = false;
	FVector AimLocationOverride = FVector::ZeroVector;

	// Phase 3 — melee cooldown
	float LastMeleeWorldTime = -1e9f;

	// Phase 3 — bolt-on components (conditionally created in ApplyArchetypeData)
	UPROPERTY()
	TObjectPtr<UEnemyArmourComponent> ArmourComponent;

	UPROPERTY()
	TObjectPtr<UEnemyShieldComponent> ShieldComponent;

	UPROPERTY()
	TObjectPtr<UEnemyGrenadierComponent> GrenadierComponent;

	UPROPERTY()
	TObjectPtr<USquadAuraComponent> SquadAuraComp;

	UPROPERTY()
	TObjectPtr<UEnemySniperTelegraphComponent> SniperTelegraphComp;

	// Phase 4 — default subobjects (every enemy gets both)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy|Components", meta = (AllowPrivateAccess))
	TObjectPtr<USuppressionComponent> SuppressionComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy|Components", meta = (AllowPrivateAccess))
	TObjectPtr<UEnemyMoraleComponent> MoraleComponent;

	UPROPERTY(VisibleInstanceOnly, Category = "Enemy|Tags")
	FGameplayTagContainer OwnedTags;

	/** World-space location captured at BeginPlay (authority only). Fallback guard post when no override is set. */
	FVector InitialPostLocation = FVector::ZeroVector;

	/** Yaw captured at BeginPlay (degrees). Base direction for guard-scan sweep. */
	float InitialPostYaw = 0.f;

	/** Set once when an enemy first reports this corpse to the director. */
	bool bBodyReported = false;

	bool bPendingTakedownDeath = false;

	/** Generic damage amount guaranteed to kill through any shield (takedown path). */
	static constexpr float TakedownDamage = 1.e6f;

	/** Seconds to delay ragdoll after a takedown kill so the BP takedown animation can play. */
	UPROPERTY(EditDefaultsOnly, Category = "Enemy|Takedown")
	float TakedownRagdollDelay = 0.8f;

	FTimerHandle DestroyTimerHandle;
	FTimerHandle TakedownRagdollTimerHandle;

	/** Resolves hitbox multiplier from the damage event's bone + damage type. */
	float GetHitboxDamageMultiplier(const FDamageEvent& DamageEvent) const;

	UFUNCTION()
	void HandleDeath();

	void ApplyRagdoll();
	void DestroyAfterDeath();
};
