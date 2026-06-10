// AEnemyCharacter — single character class for all 7 enemy archetypes, driven by UEnemyArchetypeData.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "GameplayTagAssetInterface.h"
#include "GameplayTagContainer.h"
#include "GenericTeamAgentInterface.h"
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

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTakedownExecuted, AActor*, Instigator);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnMeleePerformed);

UCLASS(Blueprintable)
class EXTRACTION_API AEnemyCharacter : public ACharacter,
	public IGameplayTagAssetInterface,
	public IAIShooterInterface,
	public IGenericTeamAgentInterface
{
	GENERATED_BODY()

public:
	AEnemyCharacter();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual float TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

	// --- IGameplayTagAssetInterface ---
	virtual void GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const override;

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

	/** Fired after ApplyArchetypeData finishes registering all bolt-on components.
	 *  Called at possess time, before BP BeginPlay fires on placed pawns. BP can bind here for init FX. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Enemy|Components")
	void OnBoltOnComponentsReady();

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

	UPROPERTY(VisibleInstanceOnly, Category = "Enemy|Tags")
	FGameplayTagContainer OwnedTags;

	/** Set once when an enemy first reports this corpse to the director. */
	bool bBodyReported = false;

	/** Generic damage amount guaranteed to kill through any shield (takedown path). */
	static constexpr float TakedownDamage = 1.e6f;

	FTimerHandle DestroyTimerHandle;

	/** Resolves hitbox multiplier from the damage event's bone + damage type. */
	float GetHitboxDamageMultiplier(const FDamageEvent& DamageEvent) const;

	UFUNCTION()
	void HandleDeath();

	void DestroyAfterDeath();
};
