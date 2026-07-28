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
#include "World/LootTypes.h"
#include "EnemyCharacter.generated.h"

class UHealthComponent;
class UFootstepNoiseComponent;
class UEnemyArchetypeData;
class AWeaponBase;
class APatrolRoute;
class AEnemyAIController;
class UWidgetComponent;
class UEnemyAnimInstance;

class ATakedownVolume;

// Phase 3 bolt-on components — forward-declared; headers live in Enemy/Components/ (authored by slices B/C).
class UEnemyArmourComponent;
class UEnemyGrenadierComponent;
class USquadAuraComponent;
class UEnemySniperTelegraphComponent;
class ALootPickup;

// Cover pose (AICS migration — default subobject)
class UCoverPoseComponent;

// Phase 4 — suppression & morale (default subobjects, not bolt-ons)
class USuppressionComponent;
class UEnemyMoraleComponent;
class UEnemyPostureComponent;

// Phase 5 — squad coordination
class UEnemySquadSubsystem;
class UEnemySquad;
class UAmmoDropTableDataAsset;
class UNiagaraSystem;

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

	// --- Target LOS flag (written by BTService_EnemyCombat, read by anim instance) ---

	void SetHasTargetLOS(bool bInLOS) { bHasTargetLOS = bInLOS; }
	bool HasTargetLOS() const { return bHasTargetLOS; }

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

	// --- Cover pose (AICS migration — default subobject, always present) ---

	UFUNCTION(BlueprintPure, Category = "Enemy|Components")
	UCoverPoseComponent* GetCoverPoseComponent() const { return CoverPoseComponent; }

	// --- Phase 4: suppression & morale accessors (default subobjects — always present) ---

	UFUNCTION(BlueprintPure, Category = "Enemy|Components")
	USuppressionComponent* GetSuppressionComponent() const { return SuppressionComponent; }

	UFUNCTION(BlueprintPure, Category = "Enemy|Components")
	UEnemyMoraleComponent* GetMoraleComponent() const { return MoraleComponent; }

	UFUNCTION(BlueprintPure, Category = "Enemy|Components")
	UEnemyPostureComponent* GetPostureComponent() const { return PostureComponent; }

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

	/** True when alive, Unaware, and the instigator is within range in the rear arc (design §4).
	 *  bIgnoreRangeAndArc=true for ranged takedowns (e.g. companion shoot) — skips melee proximity/rear-arc gate but still requires Unaware. */
	UFUNCTION(BlueprintPure, Category = "Enemy|Takedown")
	bool CanBeTakenDown(const AActor* TakedownInstigator, bool bIgnoreRangeAndArc = false) const;

	/** True when this enemy is both Unaware AND inside at least one ATakedownVolume.
	 *  Consumed by teammate A's ping classification and any companion BT task. */
	UFUNCTION(BlueprintPure, Category = "Enemy|Takedown")
	bool IsTakedownEligible() const;

	/** True when this enemy's awareness state indicates it has detected the player
	 *  (Combat). Returns false for Unaware, Suspicious, and Searching — and false
	 *  if the controller or awareness component is missing (unknown = don't engage).
	 *  Plain C++ (no UFUNCTION) so Live Coding can hot-patch it; only the companion BT service calls it. */
	bool HasDetectedPlayer() const;

	/** True when this enemy has engaged the COMPANION itself: it is targeting the companion now, has
	 *  damaged it within MemorySeconds, or holds live suspicion/search knowledge of it. The companion
	 *  side of HasDetectedPlayer — together they answer "is this enemy part of a fight we are in".
	 *  The damage clause deliberately bypasses the sight cloak: an enemy that has landed hits on the
	 *  companion has demonstrated it can see it, whatever the cloak says it is permitted to perceive.
	 *  Plain C++ (no UFUNCTION) so Live Coding can hot-patch it, matching HasDetectedPlayer. */
	bool HasEngagedCompanion(const AActor* Companion, float MemorySeconds) const;

	/** World seconds of this enemy's most recent transition into Combat awareness, or a large
	 *  negative sentinel if never / component missing. Companion stealth-break compares this
	 *  against the stealth pin time to distinguish a NEW fight from a stale Combat-state tail. */
	float GetTimeEnteredCombat() const;

	/** True when this enemy is alert enough for the companion to ready its weapon.
	 *  Searching and Combat count; Suspicious does not. */
	bool IsAlertedForCompanionReadiness() const;

	/** Instigator-aware eligibility. Same as IsTakedownEligible(), except a victim RESERVED by this
	 *  instigator also passes while it has only drifted to Searching.
	 *
	 *  Why: the global alert ladder is a ratchet. One missed player shot escalates to Loud, which
	 *  wakes every Unaware enemy to Searching — including the pocket victim the companion is already
	 *  lined up on — and the strict Unaware rule then fails the in-flight takedown permanently. An
	 *  already-committed kill must survive that; a victim that reaches Combat genuinely has not. */
	bool IsTakedownEligibleFor(const AActor* TakedownInstigator) const;

	/** Claim this enemy as an in-flight takedown victim. Grandfathers it against the alert-ratchet
	 *  wake-up (see IsTakedownEligibleFor). One reservation at a time — last claim wins. */
	void ReserveForTakedown(AActor* TakedownInstigator);

	/** Release a reservation. No-op unless TakedownInstigator currently holds it. */
	void ClearTakedownReservation(const AActor* TakedownInstigator);

	/** True when TakedownInstigator currently holds this enemy's takedown reservation. */
	bool IsTakedownReservedBy(const AActor* TakedownInstigator) const;

	/** True when ANYONE holds a takedown reservation on this enemy. Used to keep an in-flight
	 *  takedown victim asleep — e.g. the proximity body notice must not wake the very enemy the
	 *  companion is lined up on, or the takedown it is about to land turns into a whiff. */
	bool IsReservedForTakedown() const { return TakedownReservedBy.IsValid(); }

	/** Called by ATakedownVolume on overlap begin/end. Increments/decrements an internal
	 *  counter so overlapping more than one volume is handled correctly. */
	void SetInTakedownVolume(bool bInVolume);


	/**
	 * Phase 1 of a montage-deferred takedown: validates CanBeTakenDown, sets pending-death flag,
	 * broadcasts OnTakedownExecuted, snaps victim position/facing, and freezes the enemy
	 * (AI brain + movement disabled). Does NOT apply damage. Returns false if cannot start.
	 */
	/** bIgnoreRangeAndArc=true for ranged takedowns (e.g. companion shoot) — forwarded to CanBeTakenDown. */
	UFUNCTION(BlueprintCallable, Category = "Enemy|Takedown")
	bool BeginTakedownHold(AActor* TakedownInstigator, FVector SnapLocation, float SnapYaw, float WatchdogTimeout = 5.f, bool bIgnoreRangeAndArc = false);

	/**
	 * Phase 2: applies lethal damage to the frozen enemy.
	 * Safe to call once; no-ops if already dead or not in a pending takedown hold.
	 */
	UFUNCTION(BlueprintCallable, Category = "Enemy|Takedown")
	void FinishTakedownKill(AActor* TakedownInstigator);

	/** Instant path (no montage): BeginTakedownHold + FinishTakedownKill in one call.
	 *  bIgnoreRangeAndArc=true for ranged takedowns (e.g. companion shoot) — also keeps current facing instead of spinning toward instigator. */
	UFUNCTION(BlueprintCallable, Category = "Enemy|Takedown")
	bool ExecuteTakedown(AActor* TakedownInstigator, bool bIgnoreRangeAndArc = false);

	/**
	 * Cancels an in-progress montage-deferred hold: re-enables movement, restarts the AI brain,
	 * and clears bTakedownFrozen / bPendingTakedownDeath. Call this if the player montage is
	 * cancelled before the kill notify and the takedown should be abandoned rather than committed.
	 * No-ops if not currently held.
	 */
	UFUNCTION(BlueprintCallable, Category = "Enemy|Takedown")
	void AbortTakedownHold();

	/** Set true by the takedown instigator once its finisher montage is confirmed playing,
	 *  so the victim ragdolls immediately on kill (no reaction-beat delay / pose snap). */
	void SetTakedownWasMontageDriven(bool bDriven) { bTakedownWasMontageDriven = bDriven; }

	/** True from BeginTakedownHold until the kill lands (or the hold aborts) — the enemy is
	 *  frozen in a finisher and shouldn't react to the world (barks, morale theatrics). */
	bool IsTakedownPending() const { return bPendingTakedownDeath; }

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

	/** Central ammo-drop economy table (chance/amount per weapon category). Assigned once on the
	 *  base enemy BP — null disables death drops for this enemy. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Data")
	TObjectPtr<UAmmoDropTableDataAsset> AmmoDropTable;

	/** When true, death ammo drops use AmmoDropCategoryOverride instead of the weapon DA's anim
	 *  type. The Rusher's SMG is keyed Rifle for animation selection — this lets it still drop
	 *  SMG-category ammo. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Data")
	bool bOverrideAmmoDropCategory = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Data", meta = (EditCondition = "bOverrideAmmoDropCategory"))
	EEnemyWeaponAnimType AmmoDropCategoryOverride = EEnemyWeaponAnimType::SMG;

	/** Guaranteed loot dropped as a physical pickup on death (e.g. keycards). Authored per placed instance. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy|Loot")
	TArray<FLootGrant> DeathLoot;

	/** Blueprint class to spawn for death loot. Assigned on the base enemy BP. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Loot")
	TSubclassOf<ALootPickup> LootPickupClass;

	// When true, this enemy detects by sight only (ignores hearing) and neither raises nor reacts to
	// the global alert. For isolating test-gym encounters; leave false in real gameplay.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy|Testing")
	bool bIsolatedEncounter = false;

	/** Debug: when true, this enemy never relocates — every AI MoveTo request is refused — but it
	 *  still rotates to aim and fires from its placed spot. For inspecting hold/fire poses in-editor.
	 *  Leave false in real gameplay. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Testing")
	bool bDebugStandAndShoot = false;

	/** Debug: when true, this enemy immediately and persistently treats the player as a detected
	 *  combat target — no perception required. Engages silently: does not alert the Director or
	 *  broadcast to the squad, so other enemies stay unaffected. Composes with bDebugStandAndShoot
	 *  (both ticked = stands planted, firing at the player from spawn). Leave false in real gameplay. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Testing")
	bool bDebugAutoEngagePlayer = false;

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

	/** True while standing inside at least one ATakedownVolume. Drives awareness "muffling": a pocket
	 *  enemy ignores gunfire, walking and reload noise so taking one down doesn't cascade to its
	 *  neighbours — but a sprint footstep and a level-wide Loud alert still wake it. Distinct from the
	 *  bIsolatedEncounter test flag, which is fully deaf. */
	bool IsInTakedownVolume() const { return TakedownVolumeRefCount > 0; }

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

	/** Blood burst spawned at the bullet impact point on point-damage hits.
	 *  Assign the kit's NS_Smoke_Blood_System in the enemy BP defaults. */
	UPROPERTY(EditDefaultsOnly, Category = "Enemy|FX")
	TObjectPtr<UNiagaraSystem> BloodImpactFX;

	/** Uniform scale applied to BloodImpactFX on head-region hits — headshots bleed bigger. */
	UPROPERTY(EditDefaultsOnly, Category = "Enemy|FX", meta = (ClampMin = "1.0"))
	float HeadshotBloodScale = 1.6f;

private:

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy|Components", meta = (AllowPrivateAccess))
	TObjectPtr<UHealthComponent> HealthComponent;

	/** Audible surface-aware footsteps only — AI-noise emission is disabled at construction. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy|Components", meta = (AllowPrivateAccess))
	TObjectPtr<UFootstepNoiseComponent> FootstepAudioComponent;

	UPROPERTY()
	TObjectPtr<AWeaponBase> CurrentWeapon;

	/** True when the weapon is currently attached to the patrol-hand socket (DA-driven hand-swap). */
	bool bWeaponOnPatrolHand = false;

	/** Spawns BloodImpactFX at the hit location for point-damage events (kit enemy-blood behaviour).
	 *  Head-region hits scale the burst by HeadshotBloodScale. */
	void SpawnBloodImpactFX(const FDamageEvent& DamageEvent, EHitRegion HitRegion) const;

	/** True while the combat service's eye-to-target trace is clear. AI-side only (not replicated). */
	bool bHasTargetLOS = false;

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

	// Cover pose — AICS migration (default subobject, every enemy gets it)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy|Components", meta = (AllowPrivateAccess))
	TObjectPtr<UCoverPoseComponent> CoverPoseComponent;

	// Phase 4 — default subobjects (every enemy gets both)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy|Components", meta = (AllowPrivateAccess))
	TObjectPtr<USuppressionComponent> SuppressionComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy|Components", meta = (AllowPrivateAccess))
	TObjectPtr<UEnemyMoraleComponent> MoraleComponent;

	// Posture (pressure/advance — default subobject, gated per-archetype by bPostureSystemEnabled)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy|Components", meta = (AllowPrivateAccess))
	TObjectPtr<UEnemyPostureComponent> PostureComponent;

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

	/** True if the current/last takedown hold was driven by a finisher montage (watchdog>0),
	 *  vs an instant kill. Montage-driven kills ragdoll immediately (no reaction-beat delay). */
	bool bTakedownWasMontageDriven = false;

	/** True when a ranged (shoot) takedown — ragdoll uses the shorter RangedTakedownRagdollDelay
	 *  instead of the grapple-reaction-beat TakedownRagdollDelay. */
	bool bTakedownRagdollImmediate = false;

	/** Number of ATakedownVolumes currently containing this enemy. Positive = in at least one volume. */
	int32 TakedownVolumeRefCount = 0;

	/** Who has claimed this enemy as an in-flight takedown victim (weak — the instigator can die
	 *  mid-approach). Drives the grandfather exemption in IsTakedownEligibleFor / CanBeTakenDown. */
	TWeakObjectPtr<AActor> TakedownReservedBy;

	/** Generic damage amount guaranteed to kill through any shield (takedown path). */
	static constexpr float TakedownDamage = 1.e6f;

	/** Seconds to delay ragdoll after a takedown kill so the BP takedown animation can play. */
	UPROPERTY(EditDefaultsOnly, Category = "Enemy|Takedown")
	float TakedownRagdollDelay = 0.8f;

	/** Shorter ragdoll delay for ranged (shoot) takedowns — the enemy drops near-instantly
	 *  instead of standing frozen for the full grapple-reaction beat. */
	UPROPERTY(EditDefaultsOnly, Category = "Enemy|Takedown", meta = (ClampMin = "0.0"))
	float RangedTakedownRagdollDelay = 0.05f;

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
	/** Overload that accepts an already-resolved region — avoids a second ResolveHitRegion call. */
	float GetHitboxDamageMultiplier(const FDamageEvent& DamageEvent, EHitRegion Region) const;

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

	/** Authority-only death-drop roll: chance from AmmoDropTable keyed by the held weapon's
	 *  category; spawns an AAmmoPickup next to the corpse on success. */
	void TrySpawnAmmoDrop();

	/** Authority-only: makes the corpse's held gun collectable. Spawns the mapped weapon pickup
	 *  (WeaponDropTable keyed by weapon class) invisible and attached to the weapon actor, with a
	 *  rolled partial mag + reserve. Weapons with no table entry offer nothing. */
	void SetupCorpseWeaponPickup();

	/** Authority-only: spawns a loot pickup with DeathLoot contents next to the corpse. */
	void TrySpawnDeathLoot();

	/** Finds a clear lateral + ground-snapped spawn location for death loot. */
	FVector FindDeathLootSpawnLocation() const;
};
