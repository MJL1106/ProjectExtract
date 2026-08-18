// Anim instance for enemy characters — locomotion, aim offset, combat montages, and delegate-driven reactions.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "ExtractionTypes.h"
#include "EnemyTypes.h"
#include "AI/Cover/CoverPoseTypes.h"
#include "EnemyAnimInstance.generated.h"

class AEnemyCharacter;
class AWeaponBase;
class UCharacterMovementComponent;
class UHealthComponent;
class UAnimMontage;
class UEnemyAwarenessComponent;
class USkeletalMeshComponent;
class UAnimSequence;
class UCoverPoseComponent;

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

	// --- Patrol Idle ---

	/**
	 * Picks a random clip from the per-enemy repertoire and plays it once via a dynamic montage
	 * on the PatrolIdle slot. Returns the clip's play length so the caller can set WaitTarget.
	 * Returns 0.f when the pool is empty or the sequence is invalid.
	 */
	float PlayRandomPatrolIdle();

	/** Stops the active patrol-idle montage with the given blend-out time. No-op when not playing. */
	void StopPatrolIdle(float BlendOutTime = 0.2f);

	/** True while the patrol-idle dynamic montage is playing. */
	bool IsPlayingPatrolIdle() const;

	/** Exposes the bIsPatrolling flag for ABP locomotion path selection. */
	bool IsPatrolling() const { return bIsPatrolling; }

	/** Stable non-combat signal for the head-driven sight cone gate.
	 *  Unlike bIsPatrolling, this is NOT toggled by per-frame bIsAiming, preventing
	 *  edge-of-FOV detection flicker during Searching while the enemy scans. */
	bool IsInCombat() const { return bInCombat; }

	/** Accessor for the last committed cover lean side (task reads this to decide pre-move hold). */
	ECoverLean GetLastCoverSide() const { return LastCoverSide; }

	/** Debug: name of the active cover-pose montage (idle/peek). */
	UAnimMontage* GetActiveCoverMontage() const { return ActiveCoverMontage; }

	/** Debug: name of the active cover-move montage (shuffle walk). */
	UAnimMontage* GetActiveCoverMoveMontage() const { return ActiveCoverMoveMontage; }

	// --- Recoil (C++ spring solver) ---

	/**
	 * Additive spine rotation output — the ABP reads this with a component-space
	 * Transform(Modify)Bone on spine_03 placed as the last node before Output Pose.
	 * Positive Pitch = upward kick (UE FRotator sign: negative Pitch = up, but we
	 * negate when writing so designers see positive values = upward kick in the DA).
	 * Tuning note: if the torso kicks the wrong direction, flip the sign in UpdateRecoilSolver.
	 */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Enemy|Animation|Recoil")
	FRotator RecoilSpineRotation = FRotator::ZeroRotator;

	/**
	 * Additive spine TRANSLATION output (cm, component space) — the ABP reads this with the
	 * same component-space Transform(Modify)Bone on spine_03 that applies RecoilSpineRotation,
	 * via its Translation channel (Add to Existing). Drives the forward/back recoil piston:
	 * the whole upper body + gripped gun jolt backward along the aim axis and return.
	 * Backward is -X in spine_03 component space as a best guess — verify the direction in PIE;
	 * if it jolts sideways/up, flip the axis/sign in UpdateRecoilSolver (one line, Live-Coding patchable).
	 */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Enemy|Animation|Recoil")
	FVector RecoilSpineOffset = FVector::ZeroVector;

	/**
	 * Captured spine_01 component-space rotation for the cover-reload tuck — the ABP reads this
	 * as the target of a REPLACE component-space Transform(Modify)Bone on spine_01. Sampled every
	 * frame while tucked in cover idle (the "tucked" torso), then HELD while reloading so the reload
	 * montage's arms animate on top of the idle torso orientation. Component space is mesh-relative,
	 * so this is facing-independent. Driven at CoverReloadSpineAlpha so it eases in/out.
	 */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Enemy|Animation|Cover")
	FRotator CoverReloadSpineRefRotation = FRotator::ZeroRotator;

	/**
	 * Eased 0..1 alpha for the cover-reload spine Replace modify bone. Interpolates to 1 while
	 * bInCover && bIsReloading, else to 0. The ABP feeds this into the modify bone's Alpha so the
	 * captured torso orientation blends in when the reload starts and out when it ends.
	 */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Enemy|Animation|Cover")
	float CoverReloadSpineAlpha = 0.f;

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

	/** 0-1 weight tracking the melee montage play-state (eased via FInterpTo). Drives the weapon
	 *  melee-align socket AND lets the ABP fade the aim-offset out during the swing
	 *  (AimAlpha = 1 - MeleeMontageWeight) so the arms match the raw montage pose. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Enemy|Animation|Combat")
	float MeleeMontageWeight = 0.f;

	// --- Cover Pose (mirrored from UCoverPoseComponent each tick) ---

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Enemy|Animation|Cover")
	bool bInCover = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Enemy|Animation|Cover")
	ECoverHeight CoverHeight = ECoverHeight::Stand;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Enemy|Animation|Cover")
	ECoverLean CoverLeanDirection = ECoverLean::None;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Enemy|Animation|Cover")
	bool bCoverBlindFiring = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Enemy|Animation|Cover")
	bool bCoverPeeking = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Enemy|Animation|Cover")
	bool bCoverMoving = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Enemy|Animation|Cover")
	ECoverLean CoverMoveDirection = ECoverLean::None;

	// --- Cover Montages (designer-assigned on the ABP) ---
	// Back-to-cover set: side-specific idles hug the chosen corner; the finite peek montages
	// (Out→Aim…→Return) own the step-out motion and rotation via root motion — code never
	// moves the capsule during a peek. Null slots are skipped gracefully.

	UPROPERTY(EditDefaultsOnly, Category = "Enemy|Animation|Cover")
	TObjectPtr<UAnimMontage> CrouchIdleLeft = nullptr;

	UPROPERTY(EditDefaultsOnly, Category = "Enemy|Animation|Cover")
	TObjectPtr<UAnimMontage> CrouchIdleRight = nullptr;

	UPROPERTY(EditDefaultsOnly, Category = "Enemy|Animation|Cover")
	TObjectPtr<UAnimMontage> CrouchPeekLeft = nullptr;

	UPROPERTY(EditDefaultsOnly, Category = "Enemy|Animation|Cover")
	TObjectPtr<UAnimMontage> CrouchPeekRight = nullptr;

	/** Stand-up over-the-top peek for low (crouch) cover when neither corner has a gap (lean=Front).
	 *  Used when entering the over-top from the RIGHT-side idle (default). */
	UPROPERTY(EditDefaultsOnly, Category = "Enemy|Animation|Cover")
	TObjectPtr<UAnimMontage> PeekOverTop = nullptr;

	/** Over-top peek entered from the LEFT-side idle. Falls back to PeekOverTop when unset. */
	UPROPERTY(EditDefaultsOnly, Category = "Enemy|Animation|Cover")
	TObjectPtr<UAnimMontage> PeekOverTopLeft = nullptr;

	UPROPERTY(EditDefaultsOnly, Category = "Enemy|Animation|Cover")
	TObjectPtr<UAnimMontage> StandIdleLeft = nullptr;

	UPROPERTY(EditDefaultsOnly, Category = "Enemy|Animation|Cover")
	TObjectPtr<UAnimMontage> StandIdleRight = nullptr;

	UPROPERTY(EditDefaultsOnly, Category = "Enemy|Animation|Cover")
	TObjectPtr<UAnimMontage> StandPeekLeft = nullptr;

	UPROPERTY(EditDefaultsOnly, Category = "Enemy|Animation|Cover")
	TObjectPtr<UAnimMontage> StandPeekRight = nullptr;

	/** Optional blind-fire additive (group CoverAdd). Deferred feature — leave unset until the
	 *  re-exported additive clips land. */
	UPROPERTY(EditDefaultsOnly, Category = "Enemy|Animation|Cover")
	TObjectPtr<UAnimMontage> CoverBlindFireMontage = nullptr;

	// Move-along-wall montages (CoverSlot group, looping). When a montage is set AND bCoverMoving
	// is true for the matching stance/direction, it plays looping and the velocity gate is exempted.
	// Leave unset to retain normal locomotion during cover shuffles.

	UPROPERTY(EditDefaultsOnly, Category = "Enemy|Animation|Cover")
	TObjectPtr<UAnimMontage> CoverMoveCrouchLeft = nullptr;

	UPROPERTY(EditDefaultsOnly, Category = "Enemy|Animation|Cover")
	TObjectPtr<UAnimMontage> CoverMoveCrouchRight = nullptr;

	UPROPERTY(EditDefaultsOnly, Category = "Enemy|Animation|Cover")
	TObjectPtr<UAnimMontage> CoverMoveStandLeft = nullptr;

	UPROPERTY(EditDefaultsOnly, Category = "Enemy|Animation|Cover")
	TObjectPtr<UAnimMontage> CoverMoveStandRight = nullptr;

	/** Grip layer alpha driven by C++ — ABP reads this as BlendWeights[0] on the grip LayeredBoneBlend.
	 *  Eased to 0 during blind-fire (Kubold arms win), 1 otherwise. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Enemy|Animation|Cover")
	float GripPoseAlpha = 1.f;

	/** Eased 0..1 gate, 0 while tucked in cover or blind-firing — already scales AimPitch/AimYaw in
	 *  C++; the ABP also multiplies it into the aim-offset layer WEIGHT, because the AO's centre
	 *  sample is not identity (companion-rifle ADS delta) and at full weight it bends the tucked
	 *  idle's arms into an ADS hold even with the aim angles at zero. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Enemy|Animation|Cover")
	float CoverAimGate = 1.f;

	/**
	 * Active cover-aim scenario for the ABP's blend-by-int aim-offset selector.
	 * 0 = None (out-of-cover, tucked, or blind-firing).
	 * 1 = CrouchPeekLeft,  2 = CrouchPeekRight,  3 = CrouchOverTop.
	 * 4 = StandPeekLeft,   5 = StandPeekRight.
	 * Computed per-frame from bCoverPeeking + CoverLeanDirection + CoverHeight, mirroring the
	 * UpdateCoverAlign scenario mapping. The ABP feeds this to a Blend Poses by int selecting
	 * the appropriate cover aim-offset asset (in-engine wiring step).
	 */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Enemy|Animation|Cover")
	int32 CoverAimScenario = 0;

	// --- Cover Tunables ---

	UPROPERTY(EditDefaultsOnly, Category = "Enemy|Animation|Cover")
	float GripPoseBlendSpeed = 10.f;

	UPROPERTY(EditDefaultsOnly, Category = "Enemy|Animation|Cover")
	float CoverAimGateSpeed = 8.f;

	// --- Cover-Reload Spine Tuck (dynamic capture: reproduce cover-idle torso during reload) ---

	/** FInterpTo speed for CoverReloadSpineAlpha ease-in/out. */
	UPROPERTY(EditDefaultsOnly, Category = "Enemy|Animation|Cover")
	float CoverReloadSpineBlendSpeed = 10.f;

	// Cover-align: per-scenario weapon-socket poses the gun eases toward while posed in cover.
	// Each transform is the WeaponSocket pose in CoverAlignBoneName (hand_r) bone space — tune by
	// dragging the skeleton's WeaponSocket gizmo on the target pose in Persona and copying the
	// Socket Parameters values here (UI order: Rotation X=Roll, Y=Pitch, Z=Yaw). Identity = scenario
	// disabled (falls back to the rest socket pose).

	UPROPERTY(EditDefaultsOnly, Category = "Enemy|Animation|Cover")
	FName CoverAlignBoneName = TEXT("hand_r");

	UPROPERTY(EditDefaultsOnly, Category = "Enemy|Animation|Cover")
	FTransform CoverAlignIdleTransform = FTransform(FRotator(21.690085, 96.329381, -8.072317), FVector(-19.132527, 0.11764, 8.008986));

	UPROPERTY(EditDefaultsOnly, Category = "Enemy|Animation|Cover")
	FTransform CoverAlignOverTopTransform = FTransform(FRotator(18.644954, 85.498146, -4.51658), FVector(-19.159786, 1.250684, 8.86404));

	UPROPERTY(EditDefaultsOnly, Category = "Enemy|Animation|Cover")
	FTransform CoverAlignPeekLeftTransform = FTransform(FRotator(18.828378, 88.21273, -3.644527), FVector(-19.154787, 0.071277, 9.155164));

	UPROPERTY(EditDefaultsOnly, Category = "Enemy|Animation|Cover")
	FTransform CoverAlignPeekRightTransform = FTransform(FRotator(19.162533, 98.340626, -0.341529), FVector(-19.215839, 0.383116, 6.570972));

	UPROPERTY(EditDefaultsOnly, Category = "Enemy|Animation|Cover")
	FTransform CoverAlignStandIdleLeftTransform = FTransform(FRotator(12.925247, 92.850698, -0.202346), FVector(-20.586518, -1.317184, 9.726404));

	UPROPERTY(EditDefaultsOnly, Category = "Enemy|Animation|Cover")
	FTransform CoverAlignStandIdleRightTransform = FTransform(FRotator(37.360209, 85.948343, -0.422814), FVector(-21.02151, -1.598945, 8.526152));

	UPROPERTY(EditDefaultsOnly, Category = "Enemy|Animation|Cover")
	FTransform CoverAlignStandPeekLeftTransform = FTransform(FRotator(18.911719, 88.531414, -3.837036), FVector(-20.777953, 0.449693, 8.548291));

	UPROPERTY(EditDefaultsOnly, Category = "Enemy|Animation|Cover")
	FTransform CoverAlignStandPeekRightTransform = FTransform(FRotator(18.315925, 82.362116, -4.906405), FVector(-21.107093, 0.784755, 8.944418));

	UPROPERTY(EditDefaultsOnly, Category = "Enemy|Animation|Cover")
	float CoverAlignBlendSpeed = 8.f;

	/** When true, the grip arm layer stays at alpha 1 during cover idle (project weapon grip).
	 *  When false, eases to 0 (Kubold tucked arms). Designer A/B toggle. */
	UPROPERTY(EditDefaultsOnly, Category = "Enemy|Animation|Cover")
	bool bUseGripArmsInCoverIdle = true;

	UPROPERTY(EditDefaultsOnly, Category = "Enemy|Animation|Cover")
	float CoverBlendIn = 0.4f;

	UPROPERTY(EditDefaultsOnly, Category = "Enemy|Animation|Cover")
	float CoverBlendOut = 0.3f;

	/** Side used when entering cover with ECoverLean::None (e.g. first enter before a peek direction is chosen). */
	UPROPERTY(EditDefaultsOnly, Category = "Enemy|Animation|Cover")
	ECoverLean DefaultCoverSide = ECoverLean::Right;

	/** Cover montage is suppressed while capsule speed exceeds this threshold (cm/s). */
	UPROPERTY(EditDefaultsOnly, Category = "Enemy|Animation|Cover")
	float CoverAnimMaxSpeed = 15.f;

	/** Seconds the speed must remain <= CoverAnimMaxSpeed before gate re-opens (hysteresis). */
	UPROPERTY(EditDefaultsOnly, Category = "Enemy|Animation|Cover")
	float CoverAnimSettleTime = 0.15f;

	/** Clamp on AimYaw fed to the aim offset — raw target deltas reach ±180 while the body is
	 *  cover-aligned, extrapolating the spine past the aim-offset asset's authored range. */
	UPROPERTY(EditDefaultsOnly, Category = "Enemy|Animation|AimOffset")
	float AimYawClampDeg = 75.f;

	/** Tighter AimYaw clamp applied while in cover — the peek montage owns the body rotation,
	 *  the aim offset only fine-aims. Raised to 75 to match the ±90 authored Kubold cover AOs.
	 *  Set to 0 to fully isolate the aim offset when debugging. */
	UPROPERTY(EditDefaultsOnly, Category = "Enemy|Animation|AimOffset")
	float CoverAimYawClampDeg = 75.f;

	/** While in cover, if the PRE-CLAMP raw aim yaw magnitude exceeds this limit, a persistent
	 *  eased alpha (CoverAimTrackAlpha) fades toward 0, attenuating AimYaw and AimPitch after the
	 *  clamp. Prevents the spine-twist pop when the target crosses an extreme bearing. Set above
	 *  CoverAimYawClampDeg so normal aim still clamps; only absurd out-of-cone bearings ease out. */
	UPROPERTY(EditDefaultsOnly, Category = "Enemy|Animation|AimOffset")
	float CoverAimTrackLimitDeg = 80.f;

	/** Clamp on AimPitch fed to the aim offset. */
	UPROPERTY(EditDefaultsOnly, Category = "Enemy|Animation|AimOffset")
	float AimPitchClampDeg = 55.f;

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

	/** Pauses TakedownReactionMontage on its last authored frame so the victim holds the downed
	 *  pose until the attacker's montage fires the kill (see HandleTakedown). */
	FTimerHandle TakedownPoseHoldTimerHandle;

	// Single-shot fire montage — plays via OnWeaponFired delegate for weapons whose fire
	// duration is too short for the loop-montage rising-edge to catch (snipers, shotguns).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Animation|Montages")
	TObjectPtr<UAnimMontage> SingleFireMontage;

	/** Grenade throw montage — upper-body slot; plays when OnGrenadeThrow fires. */
	UPROPERTY(EditDefaultsOnly, Category = "Enemy|Animation|Montages")
	TObjectPtr<UAnimMontage> GrenadeMontage;

	UPROPERTY(EditDefaultsOnly, Category = "Enemy|Animation|Montages")
	FName FireMontageLoopSection = TEXT("Default");

	// --- Patrol Weapon Alignment ---

	/** Interpolation speed (1/s) for the patrol-align offset blend. Higher = snappier transition
	 *  between ADS and relaxed carry. Tunable per ABP. */
	UPROPERTY(EditDefaultsOnly, Category = "Weapon|PatrolAlign")
	float PatrolAlignBlendSpeed = 8.f;

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

	/**
	 * Socket on the enemy skeleton to blend the weapon toward while the melee montage plays.
	 * Leave NAME_None to disable melee-alignment for this ABP.
	 * Set to "WeaponSocket_Melee" (or equivalent) on the ABP defaults.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|MeleeAlign")
	FName MeleeAlignSocketName = FName(TEXT("WeaponSocket_Melee"));

	/** Interpolation speed (1/s) for the melee-align offset blend. Higher = snappier. Tunable per ABP. */
	UPROPERTY(EditDefaultsOnly, Category = "Weapon|MeleeAlign")
	float MeleeAlignBlendSpeed = 12.f;

	// --- Patrol Idle Designer Data ---

	/** Full-body idle sequences used for routed patrols / guard post when the weapon is NOT a pistol.
	 *  Designer fills this on the ABP defaults; no /Game/ paths in C++. */
	UPROPERTY(EditDefaultsOnly, Category = "Enemy|Animation|PatrolIdle")
	TArray<TObjectPtr<UAnimSequence>> GeneralPatrolIdlePool;

	/** Full-body idle sequences used when WeaponAnimType == Pistol. */
	UPROPERTY(EditDefaultsOnly, Category = "Enemy|Animation|PatrolIdle")
	TArray<TObjectPtr<UAnimSequence>> PistolPatrolIdlePool;

	/** How many distinct clips to draw from the pool per enemy instance (shuffled once, then repeated). */
	UPROPERTY(EditDefaultsOnly, Category = "Enemy|Animation|PatrolIdle", meta = (ClampMin = "1"))
	int32 PatrolIdleRepertoireSize = 4;

	/** Slot name in the ABP AnimGraph that the dynamic patrol-idle montage plays on. */
	UPROPERTY(EditDefaultsOnly, Category = "Enemy|Animation|PatrolIdle")
	FName PatrolIdleSlotName = TEXT("PatrolIdle");

	UPROPERTY(EditDefaultsOnly, Category = "Enemy|Animation|PatrolIdle")
	float PatrolIdleBlendIn = 0.25f;

	UPROPERTY(EditDefaultsOnly, Category = "Enemy|Animation|PatrolIdle")
	float PatrolIdleBlendOut = 0.25f;

	// --- Aim Offset Helper ---

	void UpdateAimOffset(const FVector& ToTarget, const FRotator& ActorRot);

	// --- Recoil Solver ---

	/** Add one shot's impulse to the recoil target accumulators. No-op when bHasRecoilProfile=false. */
	void AddRecoilImpulse();

	/** Integrate the spring solver and push outputs to RecoilSpineRotation + RecoilSpineOffset. */
	void UpdateRecoilSolver(float DeltaSeconds);

	// --- Resolved-set helpers ---
	// Return the effective montage for each slot: per-weapon DA set first, ABP fallback second.

	UAnimMontage* GetEffectiveFireLoopMontage() const;
	UAnimMontage* GetEffectiveFireSingleMontage() const;
	UAnimMontage* GetEffectiveReloadMontage() const;
	UAnimMontage* GetEffectiveMeleeMontage() const;
	UAnimMontage* GetEffectiveGrenadeMontage() const;
	UAnimMontage* GetEffectiveDeathMontage() const;
	UAnimMontage* GetEffectiveHitReactFlinchMontage() const;

private:
	// --- Patrol Idle private state ---

	/** Subset of the active pool drawn once per weapon-equip (Fisher-Yates, size = PatrolIdleRepertoireSize). */
	TArray<TObjectPtr<UAnimSequence>> RolledRepertoire;

	/** The dynamic montage started by the most recent PlayRandomPatrolIdle call. */
	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> ActivePatrolIdleMontage = nullptr;

	/** Guards the lazy-roll so RollRepertoireIfNeeded is a no-op after the first call per weapon. */
	bool bRepertoireRolled = false;

	/** Builds RolledRepertoire from the weapon-appropriate pool. No-op after first call (bRepertoireRolled). */
	void RollRepertoireIfNeeded();

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

	/** Selects the appropriate grenade throw montage based on crouch state and archetype DA slots. */
	UAnimMontage* SelectGrenadeMontage() const;

	/** The montage actually started by HandleGrenadeThrow — used by StopGrenadeMontage to stop the right asset on cancel. */
	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> ActiveGrenadeMontage = nullptr;

	/** Suppresses repeated missing-montage warnings after the first per instance. */
	bool bGrenadeMontageWarnedMissing = false;

	UPROPERTY(Transient)
	TWeakObjectPtr<AWeaponBase> BoundFireWeapon;

	// --- Perf: cached awareness component — resolved at init + weapon rebind, not per frame ---
	TWeakObjectPtr<UEnemyAwarenessComponent> CachedAwarenessComponent;

	// --- Perf: cached cover pose component — resolved at init, not per frame ---
	TWeakObjectPtr<UCoverPoseComponent> CachedCoverPoseComponent;

	// --- Perf: IK socket validity cached on weapon rebind ---
	// True when the current weapon's grip socket exists on its ThirdPersonGripMesh.
	bool bGripSocketValid = false;
	// Weak ref to the mesh that owns the grip socket (ThirdPersonGripMesh at equip time).
	TWeakObjectPtr<USkeletalMeshComponent> CachedGripMesh;
	// The socket name resolved at equip time (copied from DA to avoid per-frame DA access).
	FName CachedGripSocketName = NAME_None;

	// --- Recoil solver state ---

	/** Profile copied from the weapon DA on equip. */
	FEnemyRecoilProfile RecoilProfile;

	/** True when a valid profile is loaded for the current weapon. */
	bool bHasRecoilProfile = false;

	/** Accumulated target rotation (before ease-in). Decays toward zero at RecoverySpeed. */
	FRotator RecoilTargetRot = FRotator::ZeroRotator;

	/** Smoothed current rotation (chases target at Sharpness). Written to RecoilSpineRotation * SpineKickScale. */
	FRotator RecoilCurrentRot = FRotator::ZeroRotator;

	/** Accumulated target kickback distance (cm). Decays toward zero at RecoverySpeed. */
	float RecoilTargetKickback = 0.f;

	/** Smoothed current kickback (chases target at Sharpness). Written to RecoilSpineOffset.X (-X = backward). */
	float RecoilCurrentKickback = 0.f;

	// bRecoilWroteWeapon removed — weapon offset path retired; kickback now routes to RecoilSpineOffset.

	// --- Cover animation private state ---

	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> ActiveCoverMontage = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> ActiveCoverBlindMontage = nullptr;

	bool bPrevCoverPeeking = false;
	bool bPrevCoverBlindFiring = false;
	ECoverHeight PrevCoverHeight = ECoverHeight::Stand;

	bool bPrevCoverMoving = false;

	/** Unclamped aim yaw (degrees) captured in UpdateAimOffset BEFORE the AimYawClampDeg clamp.
	 *  Used by the CoverAimTrackAlpha gate to detect when the target crosses the track limit. */
	float RawAimYawDeg = 0.f;

	/** Persistent eased 0..1 alpha for the cover aim track-limit. Fades toward 0 while
	 *  the PRE-clamp raw |AimYaw| > CoverAimTrackLimitDeg, back to 1 otherwise.
	 *  Multiplied into AimYaw/AimPitch AFTER the yaw clamp. Reset to 1 outside cover. */
	float CoverAimTrackAlpha = 1.f;

	/** Effective cover-animate flag: true only when in cover AND speed has settled. */
	bool bPrevCoverAnimActive = false;

	/** Seconds the speed has been continuously <= CoverAnimMaxSpeed (velocity-gate settle accumulator). */
	float CoverSettleAccum = 0.f;

	/** Tracks last committed lean side so cover-idle has a valid direction when ECoverLean::None. */
	ECoverLean LastCoverSide = ECoverLean::Right;

	/** Side of the last side-peek that actually PLAYED. Drives the over-top variant pick and the
	 *  post-peek idle side — a right corner peek must recover into the right idle / right-entry
	 *  over-top even if a lean was rewritten in between (side-swap hold, gap re-pick). */
	ECoverLean LastPeekedSide = ECoverLean::Right;

	/** Active cover-move montage instance (set while bCoverMoving is true and a montage matched). */
	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> ActiveCoverMoveMontage = nullptr;

	void UpdateCoverAnimation(float DeltaSeconds);

	/** Capture spine_01 while tucked-idle, hold it while reloading in cover, and ease
	 *  CoverReloadSpineAlpha 0..1 so the Replace modify bone blends in/out. */
	void UpdateCoverReloadSpine(float DeltaSeconds);

	/** Height-keyed idle pick: crouch → CoverIdleCrouch, stand → CoverIdleStand. */
	UAnimMontage* SelectCoverIdleMontage() const;

	/** Crouch cover peeks over the top; stand cover leans by CoverLeanDirection
	 *  (LastCoverSide fallback when None/Front). */
	UAnimMontage* SelectCoverPeekMontage() const;

	void StopCoverMontages(float BlendOut);

	/** Plays the resolved cover-idle montage with the given blend-in; jumps to PreferredSection
	 *  when authored, falling back to Loop. Updates ActiveCoverMontage. */
	void PlayCoverIdleMontage(float BlendIn, FName PreferredSection);

	/** Plays the resolved cover-peek montage, jumping to PreferredSection when authored
	 *  (Out = full step-out lean, Aim = resume already-exposed). Updates ActiveCoverMontage. */
	void PlayCoverPeekMontage(FName PreferredSection);

	// --- Auto-trigger tracking ---

	bool bPrevIsFiring = false;
	bool bPrevIsReloading = false;
	bool bWasAlive = true;

	// --- Fire-align tracking ---

	/** Current interpolated alpha (0=rest, 1=fire pose). Driven per-frame toward the target. */
	float FireAlignAlpha = 0.f;

	/** True once SetupFireAlign has been called successfully for the current weapon. Reset on weapon rebind. */
	bool bFireAlignSetup = false;

	// --- Melee-align tracking ---

	/** True once SetupMeleeAlign has been called successfully for the current weapon. Reset on weapon rebind. */
	bool bMeleeAlignSetup = false;

	// --- Patrol-align tracking ---

	/** Current interpolated alpha (0=ADS/rest, 1=patrol carry). Driven per-frame toward bIsPatrolling. */
	float PatrolAlignAlpha = 0.f;

	/** True once SetupPatrolAlign has been called successfully for the current weapon. Reset on weapon rebind. */
	bool bPatrolAlignSetup = false;

	/** True once SetupCoverAlign resolved at least one scenario socket for the current weapon. Reset on weapon rebind. */
	bool bCoverAlignSetup = false;

	// --- Hand-swap tracking (two-socket weapon carry) ---

	/** Eased alpha for the hand-swap decision. Driven toward 1 (patrol) or 0 (combat) per-frame.
	 *  Hysteresis thresholds prevent flicker from the bIsAiming toggle on bIsPatrolling. */
	float HandSwapAlpha = 0.f;

	/** True when the weapon is logically wanted on the patrol hand (hysteresis-gated). */
	bool bWantPatrolHand = false;

	/** True when the equipped weapon's DA has a valid EnemyPatrolHandSocket (enables hand-swap).
	 *  Cached on weapon rebind to avoid per-frame DA access. */
	bool bHandSwapEnabled = false;

	/** Interpolation speed for the hand-swap decision alpha. Tune to match the ABP arm blend. */
	UPROPERTY(EditDefaultsOnly, Category = "Weapon|HandSwap")
	float HandSwapBlendSpeed = 8.f;

	/** Alpha threshold (rising) at which the weapon moves to the patrol hand. */
	static constexpr float HandSwapRiseThreshold = 0.55f;

	/** Alpha threshold (falling) at which the weapon moves back to the combat hand. */
	static constexpr float HandSwapFallThreshold = 0.45f;
};
