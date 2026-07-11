// Anim instance for the AI companion — drives locomotion, aim offset, and combat montages.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Movement/TraversalTypes.h"
#include "Companion/CompanionTypes.h"
#include "AI/Cover/CoverSlotTypes.h"
#include "AI/Cover/CoverPoseTypes.h"
#include "Enemy/EnemyTypes.h"
#include "CompanionAnimInstance.generated.h"

class UCoverPoseComponent;

class ACompanionCharacter;
class AWeaponBase;
class UCharacterMovementComponent;
class UAnimMontage;

UCLASS()
class EXTRACTION_API UCompanionAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	virtual void NativeInitializeAnimation() override;
	virtual void NativeUninitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

	// --- Montage Helpers ---

	UFUNCTION(BlueprintCallable, Category = "Companion|Animation")
	void PlayFireMontage(float PlayRate = 1.f);

	UFUNCTION(BlueprintCallable, Category = "Companion|Animation")
	void StopFireMontage(float BlendOutTime = 0.2f);

	UFUNCTION(BlueprintCallable, Category = "Companion|Animation")
	void PlayReloadMontage(float PlayRate = 1.f);

	UFUNCTION(BlueprintCallable, Category = "Companion|Animation")
	void PlayHitReactMontage(float PlayRate = 1.f);

	UFUNCTION(BlueprintCallable, Category = "Companion|Animation")
	void PlayDeathMontage(float PlayRate = 1.f);

	UFUNCTION(BlueprintCallable, Category = "Animation|Actions")
	float PlayTraversalMontage(ETraversalType Type, float PlayRate = 1.f);

	/** Returns true if a montage asset is configured for the given traversal type. Cheap — pointer check only. */
	UFUNCTION(BlueprintPure, Category = "Animation|Actions")
	bool HasMontageForType(ETraversalType Type) const;

	/** Returns the montage asset for the given traversal type, or nullptr if unset. Used to bind the
	 *  end-delegate to the correct montage even when another montage (e.g. fire) is currently active. */
	UFUNCTION(BlueprintPure, Category = "Animation|Actions")
	UAnimMontage* GetMontageForType(ETraversalType Type) const;

	// --- Left-Hand IK ---

	/**
	 * World-space transform of the equipped weapon's left-hand grip socket, updated each frame
	 * when bHasLeftHandIK is true. The ABP's Two Bone IK node reads this directly.
	 * Auto-disables during reloads so the left hand follows the reload montage (mag grab).
	 */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Companion|Weapon|LeftHandIK")
	FTransform LeftHandIKTarget;

	/**
	 * True when the equipped weapon has a valid LeftHandGripSocket on its ThirdPersonGripMesh
	 * AND we are not reloading. The ABP gates its Two Bone IK node on this flag.
	 */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Companion|Weapon|LeftHandIK")
	bool bHasLeftHandIK = false;

	// --- Recoil Output (ABP reads via spine_03 Modify Bone) ---

	/**
	 * Additive spine rotation output. Positive Pitch = upward kick (sign negated in UpdateRecoilSolver
	 * same as the enemy). ABP applies this with a component-space Transform(Modify)Bone on spine_03.
	 */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Companion|Animation|Recoil")
	FRotator RecoilSpineRotation = FRotator::ZeroRotator;

	/**
	 * Additive spine translation output (cm, component space). Backward = -Y in spine_03 component
	 * space (companion mesh is also yawed -90 deg). ABP applies via Translation channel of the same
	 * Modify Bone node as RecoilSpineRotation.
	 */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Companion|Animation|Recoil")
	FVector RecoilSpineOffset = FVector::ZeroVector;

	/**
	 * Captured spine_01 component-space rotation for the cover-reload tuck — the ABP reads this
	 * as the target of a REPLACE component-space Transform(Modify)Bone on spine_01. Sampled every
	 * frame while tucked in cover idle (the "tucked" torso), then HELD while reloading so the reload
	 * montage's arms animate on top of the idle torso orientation. Component space is mesh-relative,
	 * so this is facing-independent. Driven at CoverReloadSpineAlpha so it eases in/out.
	 */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Companion|Animation|Cover")
	FRotator CoverReloadSpineRefRotation = FRotator::ZeroRotator;

	/**
	 * Eased 0..1 alpha for the cover-reload spine Replace modify bone. Interpolates to 1 while
	 * bInCover && bIsReloading, else to 0. The ABP feeds this into the modify bone's Alpha so the
	 * captured torso orientation blends in when the reload starts and out when it ends.
	 */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Companion|Animation|Cover")
	float CoverReloadSpineAlpha = 0.f;

	/** Called by the character's OnWeaponFiredCallback to add one shot's impulse to the spring. */
	void AddRecoilImpulse();

	// --- Cover Pose Interface ---

	/** Enter cover idle pose on the given side. For Stand height, plays no montage (companion stays in locomotion idle). */
	UFUNCTION(BlueprintCallable, Category = "Cover")
	void EnterCoverPose(EPeekSide DefaultSide, ECoverHeight Height = ECoverHeight::Crouch, bool bPlayEnterMontage = true);

	/** Stop active cover/peek montages with a short blend out. */
	UFUNCTION(BlueprintCallable, Category = "Cover")
	void ExitCoverPose();

	/** Play the matching peek montage; returns it so caller can bind OnMontageEnded. */
	UFUNCTION(BlueprintCallable, Category = "Cover")
	UAnimMontage* PlayPeekFire(EPeekSide Side, float PlayRate = 1.f);

	/** Play the over-top peek (crouch cover, no usable side gap): stops the idles, picks the
	 *  side-matched entry variant, returns the montage (nullptr when unset — caller falls back to
	 *  a plain montage-less stand-up). */
	UAnimMontage* PlayOverTopPeek(EPeekSide FromSide, float PlayRate = 1.f);

	UFUNCTION(BlueprintPure, Category = "Cover")
	bool IsInCover() const { return bInCover; }

	/** Returns true if the cover-idle montage for the given side is currently playing. */
	UFUNCTION(BlueprintPure, Category = "Companion|Cover")
	bool IsCoverIdleMontagePlaying(EPeekSide Side) const;

	UFUNCTION(BlueprintPure, Category = "Cover")
	EPeekSide GetActivePeekSide() const { return ActivePeekSide; }

	/** Called by the cover BT each tick during a lateral strafe; overrides locomotion Speed/Direction so the blend space animates. */
	UFUNCTION(BlueprintCallable, Category = "Cover")
	void SetCoverStrafeVelocity(const FVector& Velocity);

	UFUNCTION(BlueprintCallable, Category = "Cover")
	void ClearCoverStrafeVelocity();

protected:
	// --- Cached Refs ---

	UPROPERTY(BlueprintReadOnly, Category = "Companion|Animation")
	TObjectPtr<ACompanionCharacter> OwningCompanion;

	UPROPERTY(BlueprintReadOnly, Category = "Companion|Animation")
	TObjectPtr<UCharacterMovementComponent> MovementComponent;

	// --- Locomotion ---

	UPROPERTY(BlueprintReadOnly, Category = "Companion|Animation|Locomotion")
	float Speed = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Companion|Animation|Locomotion")
	float Direction = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Companion|Animation|Locomotion")
	float NormalizedSpeed = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Companion|Animation|Locomotion")
	bool bIsInAir = false;

	UPROPERTY(BlueprintReadOnly, Category = "Companion|Animation|Locomotion")
	bool bIsFalling = false;

	UPROPERTY(BlueprintReadOnly, Category = "Companion|Animation|Locomotion")
	bool bHasVelocity = false;

	UPROPERTY(BlueprintReadOnly, Category = "Companion|Animation|Locomotion")
	bool bIsAccelerating = false;

	UPROPERTY(BlueprintReadOnly, Category = "Companion|Animation|Locomotion")
	bool bIsSprinting = false;

	UPROPERTY(BlueprintReadOnly, Category = "Companion|Animation|Locomotion")
	bool bIsCrouched = false;

	UPROPERTY(BlueprintReadOnly, Category = "Companion|Animation|Locomotion")
	bool bIsAlive = true;

	UPROPERTY(BlueprintReadOnly, Category = "Companion|Animation|DBNO")
	bool bIsDBNO = false;

	// --- Aim Offset ---

	UPROPERTY(BlueprintReadOnly, Category = "Companion|Animation|AimOffset")
	float AimPitch = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Companion|Animation|AimOffset")
	float AimYaw = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Companion|Animation|AimOffset")
	bool bIsAiming = false;

	// --- Combat ---

	UPROPERTY(BlueprintReadOnly, Category = "Companion|Animation|Combat")
	bool bIsFiring = false;

	UPROPERTY(BlueprintReadOnly, Category = "Companion|Animation|Combat")
	bool bIsReloading = false;

	// --- Traversal ---

	UPROPERTY(BlueprintReadOnly, Category = "Animation|State")
	bool bIsVaulting = false;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|State")
	bool bIsClimbing = false;

	UPROPERTY(BlueprintReadOnly, Category = "Animation|State")
	bool bIsMantling = false;

	// --- Montage Assets (designer-assigned on ABP_Companion) ---

	UPROPERTY(EditDefaultsOnly, Category = "Companion|Animation|Montages")
	TObjectPtr<UAnimMontage> FireMontage;

	UPROPERTY(EditDefaultsOnly, Category = "Companion|Animation|Montages")
	TObjectPtr<UAnimMontage> ReloadMontage;

	UPROPERTY(EditDefaultsOnly, Category = "Companion|Animation|Montages")
	TObjectPtr<UAnimMontage> ReloadMontage_Crouch;

	UPROPERTY(EditDefaultsOnly, Category = "Companion|Animation|Montages")
	TObjectPtr<UAnimMontage> HitReactMontage;

	/** Hit react played when companion is firing or aiming. Falls back to HitReactMontage if unset. */
	UPROPERTY(EditDefaultsOnly, Category = "Companion|Animation|Montages")
	TObjectPtr<UAnimMontage> HitReactMontage_Aim;

	UPROPERTY(EditDefaultsOnly, Category = "Companion|Animation|Montages")
	TObjectPtr<UAnimMontage> DeathMontage;

	// --- Traversal Montages (designer-assigned on ABP_Companion) ---

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation|Traversal")
	TObjectPtr<UAnimMontage> VaultMontage;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation|Traversal")
	TObjectPtr<UAnimMontage> ClimbMontage;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation|Traversal")
	TObjectPtr<UAnimMontage> MantleMontage;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation|Traversal")
	TObjectPtr<UAnimMontage> DropDownMontage;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation|Traversal")
	TObjectPtr<UAnimMontage> JumpMontage;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation|Traversal")
	TObjectPtr<UAnimMontage> SprintJumpMontage;

	// --- Cover Montages (designer-assigned on ABP_Companion) ---

	// Crouch-height cover montages (existing)
	UPROPERTY(EditDefaultsOnly, Category = "Cover")
	TObjectPtr<UAnimMontage> CoverIdleLeftMontage;

	UPROPERTY(EditDefaultsOnly, Category = "Cover")
	TObjectPtr<UAnimMontage> CoverIdleRightMontage;

	UPROPERTY(EditDefaultsOnly, Category = "Cover")
	TObjectPtr<UAnimMontage> CoverPeekLeftMontage;

	UPROPERTY(EditDefaultsOnly, Category = "Cover")
	TObjectPtr<UAnimMontage> CoverPeekRightMontage;

	// Stand-height cover montages
	UPROPERTY(EditDefaultsOnly, Category = "Cover")
	TObjectPtr<UAnimMontage> CoverIdleLeftMontage_Stand;

	UPROPERTY(EditDefaultsOnly, Category = "Cover")
	TObjectPtr<UAnimMontage> CoverIdleRightMontage_Stand;

	UPROPERTY(EditDefaultsOnly, Category = "Cover")
	TObjectPtr<UAnimMontage> CoverPeekLeftMontage_Stand;

	UPROPERTY(EditDefaultsOnly, Category = "Cover")
	TObjectPtr<UAnimMontage> CoverPeekRightMontage_Stand;

	/** Over-top peek for crouch cover (stand up, fire over the wall) — enemy LoU parity; the task
	 *  UnCrouches at commit and this montage owns the stand-up-and-aim visual. Entered from the
	 *  RIGHT-side idle by default. */
	UPROPERTY(EditDefaultsOnly, Category = "Cover")
	TObjectPtr<UAnimMontage> CoverPeekOverTopMontage;

	/** Over-top variant entered from the LEFT-side idle. Falls back to CoverPeekOverTopMontage when unset. */
	UPROPERTY(EditDefaultsOnly, Category = "Cover")
	TObjectPtr<UAnimMontage> CoverPeekOverTopLeftMontage;

	/** Eased aim gate speed — scales AimPitch/AimYaw to 0 while tucked in cover idle. */
	UPROPERTY(EditDefaultsOnly, Category = "Cover")
	float CoverAimGateSpeed = 8.f;

	/** Max spine yaw twist (deg) the aim offset may drive while in cover — the peek montage owns
	 *  the body rotation; the offset only fine-aims. Mirrors the enemy's CoverAimYawClampDeg. */
	UPROPERTY(EditDefaultsOnly, Category = "Cover")
	float CoverAimYawClampDeg = 75.f;

	/** Pre-clamp |yaw| (deg) beyond which the whole cover aim offset eases to zero — the target is
	 *  far outside the pose's reach (behind the wall / behind the body), so twisting toward it
	 *  reads as a broken spine. Mirrors the enemy's CoverAimTrackLimitDeg. */
	UPROPERTY(EditDefaultsOnly, Category = "Cover")
	float CoverAimTrackLimitDeg = 80.f;

	// --- Cover-Reload Spine Tuck (dynamic capture: reproduce cover-idle torso during reload) ---

	/** FInterpTo speed for CoverReloadSpineAlpha ease-in/out. */
	UPROPERTY(EditDefaultsOnly, Category = "Companion|Animation|Cover")
	float CoverReloadSpineBlendSpeed = 10.f;

	// --- Cover-Align Config (weapon-socket poses for cover scenario alignment) ---

	UPROPERTY(EditDefaultsOnly, Category = "Companion|Weapon|CoverAlign")
	FName CoverAlignBoneName = TEXT("hand_r");

	UPROPERTY(EditDefaultsOnly, Category = "Companion|Weapon|CoverAlign")
	FTransform CoverAlignIdleTransform = FTransform(FRotator(21.496962, 96.317499, -2.231149), FVector(-20.773425, -0.492783, 8.053094));

	UPROPERTY(EditDefaultsOnly, Category = "Companion|Weapon|CoverAlign")
	FTransform CoverAlignOverTopTransform = FTransform(FRotator(20.988338, 87.989429, -5.253666), FVector(-20.538873, 1.471584, 8.793081));

	UPROPERTY(EditDefaultsOnly, Category = "Companion|Weapon|CoverAlign")
	FTransform CoverAlignPeekLeftTransform = FTransform(FRotator(21.298056, 92.214519, -3.730851), FVector(-20.684166, 1.605956, 8.089899));

	UPROPERTY(EditDefaultsOnly, Category = "Companion|Weapon|CoverAlign")
	FTransform CoverAlignPeekRightTransform = FTransform(FRotator(20.942675, 95.956006, 1.921055), FVector(-19.839732, -0.012652, 6.100975));

	UPROPERTY(EditDefaultsOnly, Category = "Companion|Weapon|CoverAlign")
	FTransform CoverAlignStandIdleLeftTransform = FTransform(FRotator(37.371757, 88.738964, -2.988765), FVector(-19.728897, -2.63442, 9.25047));

	UPROPERTY(EditDefaultsOnly, Category = "Companion|Weapon|CoverAlign")
	FTransform CoverAlignStandIdleRightTransform = FTransform(FRotator(37.371757, 88.738964, -2.988765), FVector(-19.829018, -1.06718, 7.20189));

	UPROPERTY(EditDefaultsOnly, Category = "Companion|Weapon|CoverAlign")
	FTransform CoverAlignStandPeekLeftTransform = FTransform(FRotator(19.891578, 89.147858, -3.965413), FVector(-19.577595, 0.723464, 7.839758));

	UPROPERTY(EditDefaultsOnly, Category = "Companion|Weapon|CoverAlign")
	FTransform CoverAlignStandPeekRightTransform = FTransform(FRotator(19.430915, 82.007391, -5.116891), FVector(-19.577595, 0.723464, 7.839758));

	UPROPERTY(EditDefaultsOnly, Category = "Companion|Weapon|CoverAlign")
	float CoverAlignBlendSpeed = 8.f;

	// --- Fire-Align Config (dormant until designer sets FireAlignSocketName on ABP defaults) ---

	/**
	 * Socket on the companion skeleton to blend the weapon toward while the fire montage plays.
	 * Leave NAME_None (default) to keep fire-align dormant for this ABP.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Companion|Weapon|FireAlign")
	FName FireAlignSocketName = NAME_None;

	/** Interpolation speed (1/s) for the fire-align offset blend. */
	UPROPERTY(EditDefaultsOnly, Category = "Companion|Weapon|FireAlign")
	float FireAlignBlendSpeed = 12.f;

	// --- Idle-Carry (Patrol-Align) Config ---

	/** Interpolation speed for easing the weapon between idle (relaxed) and ADS transforms.
	 *  Driven by bIsAiming: not aiming = relaxed carry (alpha 1), aiming = ADS (alpha 0). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Companion|Weapon|IdleCarry")
	float PatrolAlignBlendSpeed = 8.f;

	/** Integrate the recoil spring and push outputs to RecoilSpineRotation + RecoilSpineOffset. */
	void UpdateRecoilSolver(float DeltaSeconds);

	// --- Cover strafe override ---

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Companion|Animation|Cover")
	bool bInCover = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Companion|Animation|Cover")
	ECoverHeight CoverHeight = ECoverHeight::Stand;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Companion|Animation|Cover")
	ECoverLean CoverLeanDirection = ECoverLean::None;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Companion|Animation|Cover")
	bool bCoverBlindFiring = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Companion|Animation|Cover")
	bool bCoverPeeking = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Companion|Animation|Cover")
	bool bCoverStrafeActive = false;

	FVector CoverStrafeVelocity = FVector::ZeroVector;
	float CoverStrafeStaleTimer = 0.f;

	// --- Posture mirror (read each tick in NativeUpdateAnimation) ---

	UPROPERTY(BlueprintReadOnly, Category = "Companion")
	ECompanionPosture CurrentPosture = ECompanionPosture::Exploration;

	// --- Takedown (read each tick in NativeUpdateAnimation) ---

	/** True while the companion is crouched-sneaking toward a knife takedown anchor. */
	UPROPERTY(BlueprintReadOnly, Category = "Companion|Animation|Takedown")
	bool bTakedownCrouchApproach = false;

	/** True while a takedown montage is actively playing. */
	UPROPERTY(BlueprintReadOnly, Category = "Companion|Animation|Takedown")
	bool bTakedownMontagePlaying = false;

private:
	UFUNCTION()
	void OnReloadMontageBlendingOut(UAnimMontage* Montage, bool bInterrupted);

	// --- Recoil spring state ---
	FEnemyRecoilProfile RecoilProfile;
	bool bHasRecoilProfile = false;

	FRotator RecoilTargetRot = FRotator::ZeroRotator;
	FRotator RecoilCurrentRot = FRotator::ZeroRotator;
	float RecoilTargetKickback = 0.f;
	float RecoilCurrentKickback = 0.f;

	// --- Fire-align state ---
	float FireAlignAlpha = 0.f;
	bool bFireAlignSetup = false;

	// --- Patrol-align (idle-carry) state ---
	float PatrolAlignAlpha = 0.f;
	bool bPatrolAlignSetup = false;

	// --- Cover-align state ---
	bool bCoverAlignSetup = false;

	// --- Cover pose cache (resolved at init, re-resolved if stale) ---
	TWeakObjectPtr<UCoverPoseComponent> CachedCoverPoseComponent;

	// --- Weapon cache ---
	TWeakObjectPtr<AWeaponBase> CachedWeapon;

	// --- Left-Hand IK cache (resolved once on weapon equip) ---
	bool bGripSocketValid = false;
	TWeakObjectPtr<USkeletalMeshComponent> CachedGripMesh;
	FName CachedGripSocketName = NAME_None;

	// --- Death-edge ---
	bool bWasAlive = true;

	EPeekSide ActivePeekSide = EPeekSide::Right;
	bool bPrevIsReloading = false;

	/** Throttle accumulator for the [RELOADTUCK] cover-reload-spine diagnostic line. */
	float CoverReloadTuckLogAccum = 0.f;

	/** Eased gate that scales AimPitch/AimYaw — 0 when in cover idle (not peeking). */
	float CoverAimGate = 1.f;

	/** Unclamped aim yaw (deg) captured before the cover clamp — feeds the track-limit test. */
	float RawAimYawDeg = 0.f;

	/** Eased 0..1 — fades the cover aim offset out while the raw bearing exceeds
	 *  CoverAimTrackLimitDeg, back in otherwise. Reset to 1 outside cover. */
	float CoverAimTrackAlpha = 1.f;

	/** Cover height latched at EnterCoverPose — PlayPeekFire selects from this rather than the
	 *  pose-component mirror, which can be stale right after an ExitCoverPose reset. */
	ECoverHeight LatchedCoverHeight = ECoverHeight::Crouch;

	/** True while any cover peek montage plays (four side peeks + the over-top pair) — companion-side
	 *  peek signal for the aim gate (nothing companion-side sets the pose component's bPeeking).
	 *  BlueprintPure so the ABP's grip/aim-gate EventGraph can read ONE call instead of per-asset checks. */
	UFUNCTION(BlueprintPure, Category = "Cover")
	bool IsAnyCoverPeekMontagePlaying() const;

	/** Capture spine_01 while tucked-idle, hold it while reloading in cover, and ease
	 *  CoverReloadSpineAlpha 0..1 so the Replace modify bone blends in/out. */
	void UpdateCoverReloadSpine(float DeltaSeconds);
};
