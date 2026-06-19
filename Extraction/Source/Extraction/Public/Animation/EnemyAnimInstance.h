// Anim instance for enemy characters — locomotion, aim offset, combat montages, and delegate-driven reactions.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "ExtractionTypes.h"
#include "EnemyTypes.h"
#include "EnemyAnimInstance.generated.h"

class AEnemyCharacter;
class AWeaponBase;
class UCharacterMovementComponent;
class UHealthComponent;
class UAnimMontage;
class UEnemyAwarenessComponent;
class USkeletalMeshComponent;

UCLASS()
class EXTRACTION_API UEnemyAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	virtual void NativeInitializeAnimation() override;
	virtual void NativeUninitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

	// --- Montage Helpers ---

	UFUNCTION(BlueprintCallable, Category = "Enemy|Animation")
	void PlayFireMontage(float PlayRate = 1.f);

	UFUNCTION(BlueprintCallable, Category = "Enemy|Animation")
	void StopFireMontage(float BlendOutTime = 0.2f);

	UFUNCTION(BlueprintCallable, Category = "Enemy|Animation")
	void PlayReloadMontage(float PlayRate = 1.f);

	UFUNCTION(BlueprintCallable, Category = "Enemy|Animation")
	void PlayHitReactMontage(float PlayRate = 1.f);

	/**
	 * Additive hit flinch — not gated by bInCombat; a light twitch visible while the character
	 * fires. NOTE: the montage MUST be authored on a dedicated additive/layered slot in the ABP
	 * AnimGraph (separate from the fire-loop slot) — C++ cannot enforce slot routing. This is
	 * part of the in-engine wiring checklist for this feature.
	 */
	UFUNCTION(BlueprintCallable, Category = "Enemy|Animation")
	void PlayHitReactFlinch(float PlayRate = 1.f);

	UFUNCTION(BlueprintCallable, Category = "Enemy|Animation")
	void PlayDeathMontage(float PlayRate = 1.f);

	UFUNCTION(BlueprintCallable, Category = "Enemy|Animation")
	void PlayMeleeMontage(float PlayRate = 1.f);

	UFUNCTION(BlueprintCallable, Category = "Enemy|Animation")
	void PlayGrenadeMontage(float PlayRate = 1.f);

	UFUNCTION(BlueprintCallable, Category = "Enemy|Animation")
	void StopGrenadeMontage(float BlendOutTime = 0.2f);

protected:
	// --- Cached Refs ---

	UPROPERTY(BlueprintReadOnly, Category = "Enemy|Animation")
	TObjectPtr<AEnemyCharacter> OwningEnemy;

	UPROPERTY(BlueprintReadOnly, Category = "Enemy|Animation")
	TObjectPtr<UCharacterMovementComponent> MovementComponent;

	UPROPERTY(BlueprintReadOnly, Category = "Enemy|Animation")
	TObjectPtr<UHealthComponent> HealthComponent;

	// --- Locomotion ---

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Enemy|Animation|Locomotion")
	float Speed = 0.f;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Enemy|Animation|Locomotion")
	float Direction = 0.f;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Enemy|Animation|Locomotion")
	float NormalizedSpeed = 0.f;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Enemy|Animation|Locomotion")
	bool bIsInAir = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Enemy|Animation|Locomotion")
	bool bIsFalling = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Enemy|Animation|Locomotion")
	bool bHasVelocity = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Enemy|Animation|Locomotion")
	bool bIsAccelerating = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Enemy|Animation|Locomotion")
	bool bIsCrouched = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Enemy|Animation|Locomotion")
	bool bIsAlive = true;

	/**
	 * True when the enemy is in a non-combat awareness state and not actively aiming.
	 * The ABP uses this to select the relaxed-carry locomotion path (patrol idle / patrol walk)
	 * over the shouldered-alert path. Computed each frame from existing awareness + aim signals.
	 */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Enemy|Animation|Locomotion")
	bool bIsPatrolling = false;

	// --- Aim Offset ---

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Enemy|Animation|AimOffset")
	float AimPitch = 0.f;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Enemy|Animation|AimOffset")
	float AimYaw = 0.f;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Enemy|Animation|AimOffset")
	bool bIsAiming = false;

	// --- Combat ---

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Enemy|Animation|Combat")
	bool bIsFiring = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Enemy|Animation|Combat")
	bool bIsReloading = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Enemy|Animation|Combat")
	bool bInCombat = false;

	/** True while the USuppressionComponent reports this enemy as suppressed. The ABP blends a
	 *  cower / hunker layer when this is true. Polled each frame — no delegate needed. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Enemy|Animation|Combat")
	bool bIsSuppressed = false;

	// --- Weapon Animation Type ---

	/**
	 * Weapon animation family read from the equipped weapon's UWeaponDataAsset each frame.
	 * The ABP selects grip blendspace variant and montage set based on this value.
	 * Defaults to Rifle so unmodified weapon DAs leave enemies unchanged.
	 */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Enemy|Animation|Weapon")
	EEnemyWeaponAnimType WeaponAnimType = EEnemyWeaponAnimType::Rifle;

	// --- Left-Hand IK ---

	/**
	 * World-space transform of the equipped weapon's left-hand grip socket, updated each frame
	 * when bHasLeftHandIK is true. The ABP's Two Bone IK node reads this directly.
	 * Socket is queried on the ThirdPersonGripMesh (the visible Infima visual actor mesh).
	 * Resets to FTransform::Identity when the socket is absent or LeftHandGripSocket is NAME_None.
	 */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Enemy|Animation|IK")
	FTransform LeftHandIKTarget;

	/**
	 * True when the equipped weapon has a valid LeftHandGripSocket on its ThirdPersonGripMesh.
	 * Cached on weapon equip. The ABP gates its Two Bone IK node on this flag.
	 */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Enemy|Animation|IK")
	bool bHasLeftHandIK = false;

	// --- Montage Assets (designer-assigned on the ABP) ---
	// These legacy single-field slots are kept for backward compatibility.
	// When the equipped weapon's DA has a matching FEnemyWeaponAnimSet slot filled in, that
	// takes priority. When null, the anim instance falls back to these ABP-level values.

	UPROPERTY(EditDefaultsOnly, Category = "Enemy|Animation|Montages")
	TObjectPtr<UAnimMontage> FireMontage;

	UPROPERTY(EditDefaultsOnly, Category = "Enemy|Animation|Montages")
	TObjectPtr<UAnimMontage> ReloadMontage;

	UPROPERTY(EditDefaultsOnly, Category = "Enemy|Animation|Montages")
	TObjectPtr<UAnimMontage> HitReactMontage;

	UPROPERTY(EditDefaultsOnly, Category = "Enemy|Animation|Montages")
	TObjectPtr<UAnimMontage> DeathMontage;

	UPROPERTY(EditDefaultsOnly, Category = "Enemy|Animation|Montages")
	TObjectPtr<UAnimMontage> MeleeMontage;

	UPROPERTY(EditDefaultsOnly, Category = "Enemy|Animation|Montages")
	TObjectPtr<UAnimMontage> TakedownReactionMontage;

	// Single-shot fire montage — plays via OnWeaponFired delegate for weapons whose fire
	// duration is too short for the loop-montage rising-edge to catch (snipers, shotguns).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Animation|Montages")
	TObjectPtr<UAnimMontage> SingleFireMontage;

	/** Grenade throw montage — upper-body slot; plays when OnGrenadeThrow fires. */
	UPROPERTY(EditDefaultsOnly, Category = "Enemy|Animation|Montages")
	TObjectPtr<UAnimMontage> GrenadeMontage;

	UPROPERTY(EditDefaultsOnly, Category = "Enemy|Animation|Montages")
	FName FireMontageLoopSection = TEXT("Default");

	/**
	 * Socket on the enemy skeleton to blend the weapon toward while the fire-loop montage plays.
	 * Leave NAME_None to disable fire-alignment for this ABP.
	 * Set to "WeaponSocket_Fire" (or equivalent) on the ABP defaults.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|FireAlign")
	FName FireAlignSocketName = NAME_None;

	/** Interpolation speed (1/s) for the fire-align offset blend. Higher = snappier. Tunable per ABP. */
	UPROPERTY(EditDefaultsOnly, Category = "Weapon|FireAlign")
	float FireAlignBlendSpeed = 12.f;

	// --- Aim Offset Helper ---

	void UpdateAimOffset(const FVector& ToTarget, const FRotator& ActorRot);

	// --- Resolved-set helpers ---
	// Return the effective montage for each slot: per-weapon DA set first, ABP fallback second.

	bool IsHoldFireActive() const;
	UAnimMontage* GetEffectiveFireLoopMontage() const;
	UAnimMontage* GetEffectiveFireSingleMontage() const;
	UAnimMontage* GetEffectiveReloadMontage() const;
	UAnimMontage* GetEffectiveMeleeMontage() const;
	UAnimMontage* GetEffectiveGrenadeMontage() const;
	UAnimMontage* GetEffectiveDeathMontage() const;
	UAnimMontage* GetEffectiveHitReactFlinchMontage() const;

private:
	// --- Delegate handlers ---

	UFUNCTION()
	void HandleHitReact(EHitRegion Region);

	UFUNCTION()
	void HandleMeleePerformed();

	UFUNCTION()
	void HandleTakedown(AActor* Instigator);

	UFUNCTION()
	void HandleGrenadeThrow(FVector PredictedLanding, float TimeToImpact);

	// --- Per-shot delegate for single-fire weapons (sniper, shotgun) ---

	UFUNCTION()
	void HandleWeaponFired();

	// --- Hold-fire notify — auto-routed from the "FireHold" skeleton notify in the single-fire montage ---

	UFUNCTION()
	void AnimNotify_FireHold();

	UPROPERTY(Transient)
	TWeakObjectPtr<AWeaponBase> BoundFireWeapon;

	// --- Perf: cached awareness component — resolved at init + weapon rebind, not per frame ---
	TWeakObjectPtr<UEnemyAwarenessComponent> CachedAwarenessComponent;

	// --- Perf: IK socket validity cached on weapon rebind ---
	// True when the current weapon's grip socket exists on its ThirdPersonGripMesh.
	bool bGripSocketValid = false;
	// Weak ref to the mesh that owns the grip socket (ThirdPersonGripMesh at equip time).
	TWeakObjectPtr<USkeletalMeshComponent> CachedGripMesh;
	// The socket name resolved at equip time (copied from DA to avoid per-frame DA access).
	FName CachedGripSocketName = NAME_None;

	// --- Auto-trigger tracking ---

	bool bPrevIsFiring = false;
	bool bPrevIsReloading = false;

	// --- Hold-fire state ---

	/** True when the single-fire montage is paused at the FireHold notify (aimed, waiting to shoot). */
	bool bHoldFireHeld = false;
	/** True while the montage is resuming past the FireHold notify to play the recoil/cycle. */
	bool bHoldFireAdvancing = false;

	// --- Fire-align tracking ---

	/** Current interpolated alpha (0=rest, 1=fire pose). Driven per-frame toward the target. */
	float FireAlignAlpha = 0.f;

	/** True once SetupFireAlign has been called successfully for the current weapon. Reset on weapon rebind. */
	bool bFireAlignSetup = false;
};
