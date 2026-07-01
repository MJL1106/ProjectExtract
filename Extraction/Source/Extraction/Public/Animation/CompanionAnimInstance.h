// Anim instance for the AI companion — drives locomotion, aim offset, and combat montages.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Movement/TraversalTypes.h"
#include "Companion/CompanionTypes.h"
#include "AI/Cover/CoverSlotTypes.h"
#include "Enemy/EnemyTypes.h"
#include "CompanionAnimInstance.generated.h"

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

	UPROPERTY(EditDefaultsOnly, Category = "Cover")
	TObjectPtr<UAnimMontage> CoverIdleLeftMontage;

	UPROPERTY(EditDefaultsOnly, Category = "Cover")
	TObjectPtr<UAnimMontage> CoverIdleRightMontage;

	UPROPERTY(EditDefaultsOnly, Category = "Cover")
	TObjectPtr<UAnimMontage> CoverPeekLeftMontage;

	UPROPERTY(EditDefaultsOnly, Category = "Cover")
	TObjectPtr<UAnimMontage> CoverPeekRightMontage;

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

	UPROPERTY(BlueprintReadOnly, Category = "Companion|Animation|Cover")
	bool bInCover = false;

	UPROPERTY(BlueprintReadOnly, Category = "Companion|Animation|Cover")
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
};
