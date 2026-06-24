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
class UWidgetComponent;
class UEnemyAnimInstance;

// Phase 3 bolt-on components — forward-declared; headers live in Enemy/Components/ (authored by slices B/C).
class UEnemyArmourComponent;
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

/**
 * Fired when the grenadier component commits to a throw (at the start of the telegraph window).
 * Parallel to OnMeleePerformed — the anim instance binds this to play the grenade throw montage.
 * PredictedLanding and TimeToImpact mirror the UEnemyGrenadierComponent::OnGrenadeTelegraph params
 * so Blueprint listeners can drive the landing indicator without a second binding.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnGrenadeThrow, FVector, PredictedLanding, float, TimeToImpact);

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
	virtual bool CanCrouch() const override;
	virtual void GetActorEyesViewPoint(FVector& OutLocation, FRotator& OutRotation) const override;

	// --- IGameplayTagAssetInterface ---
	virtual void GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const override;

	// --- IAISightTargetInterface ---
	virtual UAISense_Sight::EVisibilityResult CanBeSeenFrom(const FCanBeSeenFromContext& Context, FVector& OutSeenLocation, int32& OutNumberOfLoSChecksPerformed, int32& OutNumberOfAsyncLosCheckRequested, float& OutSightStrength, int32* UserData = nullptr, const FOnPendingVisibilityQueryProcessedDelegate* Delegate = nullptr) override;

	// --- IAIShooterInterface ---
	virtual AActor* GetAIAimTarget() const override;
	virtual float GetAIAimSpreadDegrees() const override;
	virtual bool GetAIAimLocation(FVector& OutLocation) const override;
	virtual FVector GetAimPointForTarget(const AActor* Target) const override;

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
	 *  Returns true if a strike was committed; the montage plays and damage is applied by the contact-frame AnimNotify. */
	UFUNCTION(BlueprintCallable, Category = "Enemy|Melee")
	bool PerformMelee(AActor* Target);

	/** Applies the committed melee strike's damage to the target the swing was launched against.
	 *  Called by UAnimNotify_EnemyMeleeHit at the contact frame. No range re-check (pure timing). */
	UFUNCTION(BlueprintCallable, Category = "Enemy|Melee")
	void ApplyMeleeDamage();

	/** Forwards the grenade release to the grenadier component.
	 *  Called by UAnimNotify_EnemyGrenadeRelease at the hand-open frame of the throw montage. */
	UFUNCTION(BlueprintCallable, Category = "Enemy|Grenadier")
	void ReleaseGrenade();

	/** Fired whenever a melee strike connects — animation/FX hook for BP. */
	UPROPERTY(BlueprintAssignable, Category = "Enemy|Melee")
	FOnMeleePerformed OnMeleePerformed;

	/**
	 * Fired when the grenadier component commits to a throw (mirrors OnMeleePerformed pattern).
	 * UEnemyAnimInstance binds this in NativeInitializeAnimation to play the grenade throw montage.
	 * Only fires when bIsGrenadier is true and TryThrowAt succeeds.
	 */
	UPROPERTY(BlueprintAssignable, Category = "Enemy|Grenadier")
	FOnGrenadeThrow OnGrenadeThrow;

	/** Resolves which hit region a damage event maps to (used by armour component and internal hitbox path). */
	EHitRegion ResolveHitRegion(const FDamageEvent& DamageEvent) const;

	// --- Bolt-on component accessors (nullptr if archetype doesn't use them) ---

	UFUNCTION(BlueprintPure, Category = "Enemy|Components")
	UEnemyArmourComponent* GetArmourComponent() const { return ArmourComponent.Get(); }

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

	/**
	 * Phase 1 of a montage-deferred takedown: validates CanBeTakenDown, sets pending-death flag,
	 * broadcasts OnTakedownExecuted, snaps victim position/facing, and freezes the enemy
	 * (AI brain + movement disabled). Does NOT apply damage. Returns false if cannot start.
	 */
	UFUNCTION(BlueprintCallable, Category = "Enemy|Takedown")
	bool BeginTakedownHold(AActor* TakedownInstigator, FVector SnapLocation, float SnapYaw, float WatchdogTimeout = 5.f);

	/**
	 * Phase 2: applies lethal damage to the frozen enemy.
	 * Safe to call once; no-ops if already dead or not in a pending takedown hold.
	 */
	UFUNCTION(BlueprintCallable, Category = "Enemy|Takedown")
	void FinishTakedownKill(AActor* TakedownInstigator);

	/** Instant path (no montage): BeginTakedownHold + FinishTakedownKill in one call. */
	UFUNCTION(BlueprintCallable, Category = "Enemy|Takedown")
	bool ExecuteTakedown(AActor* TakedownInstigator);

	/**
	 * Cancels an in-progress montage-deferred hold: re-enables movement, restarts the AI brain,
	 * and clears bTakedownFrozen / bPendingTakedownDeath. Call this if the player montage is
	 * cancelled before the kill notify and the takedown should be abandoned rather than committed.
	 * No-ops if not currently held.
	 */
	UFUNCTION(BlueprintCallable, Category = "Enemy|Takedown")
	void AbortTakedownHold();

	/** Fired when a takedown begins (BeginTakedownHold succeeds) — animation/FX hook for BP. */
	UPROPERTY(BlueprintAssignable, Category = "Enemy|Takedown")
	FOnTakedownExecuted OnTakedownExecuted;

	// --- Body discovery ---

	/** First caller gets true and owns reporting this body to the director. */
	bool TryMarkBodyReported();

	// --- Corpse lifecycle ---

	/** Returns the world-space location of the ragdolled corpse (pelvis bone if simulating, else actor loc). */
	FVector GetCorpseLocation() const;

	/** Initiates the short removal countdown after a living enemy reaches the body. Safe to call multiple times. */
	void BeginCorpseRemoval();

	/** Number of living enemies currently investigating this corpse. Managed by UEnemyAwarenessComponent. */
	int32 InvestigateRefCount = 0;

	void IncrementInvestigators() { ++InvestigateRefCount; }
	void DecrementInvestigators() { InvestigateRefCount = FMath::Max(InvestigateRefCount - 1, 0); }
	bool IsBeingInvestigated() const { return InvestigateRefCount > 0; }

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

	/** Moves the held weapon between the patrol-hand and combat-hand sockets (data-driven from
	 *  the weapon's UWeaponDataAsset EnemyPatrolHandSocket / EnemyCombatHandSocket fields).
	 *  No-ops when the DA has no patrol socket set (all non-pistol archetypes).
	 *  bImmediate=false (default): KeepWorldTransform + BeginHandSwapSettle for a smooth ease.
	 *  bImmediate=true: SnapToTargetNotIncludingScale + reset settle for instant seat (firing override / death).
	 *  Authority-only, edge-guarded (idempotent on repeated same-state calls). */
	void SetWeaponHandSocket(bool bUsePatrolHand, bool bImmediate = false);

	// --- Config ---

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy|Data")
	TObjectPtr<UEnemyArchetypeData> ArchetypeData;

	// When true, this enemy detects by sight only (ignores hearing) and neither raises nor reacts to
	// the global alert. For isolating test-gym encounters; leave false in real gameplay.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy|Testing")
	bool bIsolatedEncounter = false;

	/** Debug: when true, this enemy never relocates — every AI MoveTo request is refused — but it
	 *  still rotates to aim and fires from its placed spot. For inspecting hold/fire poses in-editor.
	 *  Leave false in real gameplay. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Testing")
	bool bDebugStandAndShoot = false;

	// --- Perception: head-driven sight cone ---

	/** When true and patrolling, GetActorEyesViewPoint follows the animated head bone so the vision
	 *  cone direction tracks the idle animation (an idle that glances left actually sees left).
	 *  Disable to revert to the default capsule-centre forward vector. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Perception")
	bool bUseHeadDrivenSightCone = true;

	/** Bone name whose world transform drives the sight cone origin and direction while patrolling. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Perception")
	FName SightHeadBoneName = TEXT("head");

	/** Head bone's LOCAL axis that points out the face, in bone space. Rotated by the head bone's
	 *  world orientation each perception update to drive the sight-cone direction, so the cone tracks
	 *  the animated head. Default +Y matches the Quantum skeleton's head bone; tune if the face axis differs. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Perception")
	FVector HeadSightForwardAxis = FVector(0.f, 1.f, 0.f);

	bool IsIsolatedEncounter() const { return bIsolatedEncounter; }

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

	/** Floating awareness meter above the enemy's head. Widget self-collapses while Unaware. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy|UI")
	TObjectPtr<UWidgetComponent> AwarenessWidgetComponent;

	/** Assign the WBP_EnemyAwarenessMeter Blueprint in the enemy BP defaults. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|UI")
	TSubclassOf<UUserWidget> AwarenessWidgetClass;

	/** Maps skeleton bone names to hit regions for damage multiplier lookup. Mannequin defaults. */
	UPROPERTY(EditDefaultsOnly, Category = "Enemy|Hitbox")
	TMap<FName, EHitRegion> BoneToHitRegionMap;

private:

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy|Components", meta = (AllowPrivateAccess))
	TObjectPtr<UHealthComponent> HealthComponent;

	UPROPERTY()
	TObjectPtr<AWeaponBase> CurrentWeapon;

	/** True when the weapon is currently attached to the patrol-hand socket (DA-driven hand-swap). */
	bool bWeaponOnPatrolHand = false;

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

	// Melee target captured at swing start; damage applied by the contact-frame notify.
	TWeakObjectPtr<AActor> PendingMeleeTarget;

	// Phase 3 — bolt-on components (conditionally created in ApplyArchetypeData)
	UPROPERTY()
	TObjectPtr<UEnemyArmourComponent> ArmourComponent;

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
	bool bTakedownFrozen = false;

	/** Generic damage amount guaranteed to kill through any shield (takedown path). */
	static constexpr float TakedownDamage = 1.e6f;

	/** Seconds to delay ragdoll after a takedown kill so the BP takedown animation can play. */
	UPROPERTY(EditDefaultsOnly, Category = "Enemy|Takedown")
	float TakedownRagdollDelay = 0.8f;

	// --- Corpse lifecycle tuning ---

	/** Hard-cap lifespan (seconds) for a persisted corpse before it self-destructs if nobody investigates. */
	UPROPERTY(EditDefaultsOnly, Category = "Enemy|Corpse")
	float CorpseMaxLifespanSeconds = 120.f;

	/** Seconds after a living enemy reaches the corpse before it is destroyed (brief beat so it doesn't pop). */
	UPROPERTY(EditDefaultsOnly, Category = "Enemy|Corpse")
	float CorpseRemovalAfterReachSeconds = 2.f;

	/** Destroy delay when corpse persistence is disabled (legacy mode). */
	UPROPERTY(EditDefaultsOnly, Category = "Enemy|Corpse")
	float CorpseDisappearSeconds = 2.f;

	/** Guard against multiple BeginCorpseRemoval calls. */
	bool bCorpseRemovalStarted = false;

	/** Cached corpse location after ragdoll settles, to avoid per-tick bone lookups. */
	FVector CachedCorpseLocation = FVector::ZeroVector;
	bool bCorpseLocationCached = false;

	/** Initial delay (seconds) after ragdoll before the first settle check. */
	UPROPERTY(EditDefaultsOnly, Category = "Enemy|Corpse")
	float CorpseSettleTime = 2.f;

	/** Speed threshold (cm/s) below which the ragdoll is considered settled. */
	UPROPERTY(EditDefaultsOnly, Category = "Enemy|Corpse")
	float CorpseSettleSpeedThreshold = 10.f;

	/** Retry interval (seconds) when the ragdoll hasn't settled yet. */
	UPROPERTY(EditDefaultsOnly, Category = "Enemy|Corpse")
	float CorpseSettleRetryInterval = 0.5f;

	/** Hard ceiling (seconds from ragdoll start) after which we cache regardless of velocity. */
	UPROPERTY(EditDefaultsOnly, Category = "Enemy|Corpse")
	float CorpseSettleMaxWait = 5.f;

	/** World time when ApplyRagdoll was called — used for the hard ceiling. */
	float RagdollStartTime = 0.f;

	/** Attempts to snapshot the corpse location; retries if the ragdoll is still moving. */
	void CacheCorpseLocation();

	/** Cached result of DoesSocketExist("spine_03") — set once in BeginPlay, used per CanBeSeenFrom call. */
	bool bCachedHasChestBone = false;

	/** Non-owning ref to the anim instance — set in BeginPlay, lazily re-resolved if null (deferred anim init). */
	mutable TWeakObjectPtr<UEnemyAnimInstance> CachedAnimInstance;

	/** Suppress repeated "missing anim instance for head-cone" warnings after the first. */
	mutable bool bLoggedMissingSightAnimInstance = false;

	FTimerHandle DestroyTimerHandle;
	FTimerHandle CorpseSettleTimerHandle;
	FTimerHandle TakedownRagdollTimerHandle;
	FTimerHandle TakedownWatchdogTimerHandle;
	FTimerHandle AwarenessWidgetLinkTimerHandle;

	/** Deferred retry count for linking the awareness widget after the component creates it. */
	int32 AwarenessWidgetLinkAttempts = 0;

	/** Maximum retries for awareness widget link (0.1s interval → ~5s total). */
	static constexpr int32 MaxAwarenessLinkAttempts = 50;

	/** Deferred retry: links the awareness widget once GetUserWidgetObject() returns non-null. */
	void TryLinkAwarenessWidget();

	/** Resolves hitbox multiplier from the damage event's bone + damage type. */
	float GetHitboxDamageMultiplier(const FDamageEvent& DamageEvent) const;

	UFUNCTION()
	void HandleDeath();

	/**
	 * Re-broadcasts the grenadier component's OnGrenadeTelegraph as OnGrenadeThrow on this actor.
	 * Bound in ApplyArchetypeData when bIsGrenadier is true. Keeps UEnemyGrenadierComponent
	 * decoupled from UEnemyAnimInstance (same pattern as OnMeleePerformed forwarding).
	 */
	UFUNCTION()
	void HandleGrenadeTelegraph(FVector PredictedLanding, float TimeToImpact);

	/** Stops the grenade throw montage when a throw is cancelled mid-telegraph. */
	UFUNCTION()
	void HandleGrenadeCancelled();

	void ApplyRagdoll();
	void DestroyAfterDeath();
};
