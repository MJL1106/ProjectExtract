// Anim instance for the AI companion — drives locomotion, aim offset, and combat montages.

#include "CompanionAnimInstance.h"
#include "AI/CompanionDiag.h"
#include "CompanionCharacter.h"
#include "WeaponBase.h"
#include "WeaponDataAsset.h"
#include "HealthComponent.h"
#include "TraversalComponent.h"
#include "AI/Cover/CoverPoseComponent.h"
#include "AlphaBlend.h"
#include "Animation/AnimMontage.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "KismetAnimationLibrary.h"
#include "CompanionAIController.h"

void UCompanionAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();

	// Cover state reset — mirrors the enemy's init block so a re-init never inherits stale
	// cover pose. Placed before the pawn early-outs so it always runs.
	bInCover = false;
	CoverHeight = ECoverHeight::Stand;
	CoverLeanDirection = ECoverLean::None;
	bCoverPeeking = false;
	bCoverBlindFiring = false;
	ActivePeekSide = EPeekSide::Right;
	LatchedCoverHeight = ECoverHeight::Crouch;
	bCoverStrafeActive = false;
	CoverStrafeVelocity = FVector::ZeroVector;
	CoverStrafeStaleTimer = 0.f;
	CoverAimGate = 1.f;

	APawn* PawnOwner = TryGetPawnOwner();
	if (!IsValid(PawnOwner)) return;

	OwningCompanion = Cast<ACompanionCharacter>(PawnOwner);
	if (!IsValid(OwningCompanion)) return;

	MovementComponent = OwningCompanion->GetCharacterMovement();

	// Cache the cover pose component once — default subobject, always present.
	CachedCoverPoseComponent = OwningCompanion->GetCoverPoseComponent();

	OnMontageBlendingOut.AddDynamic(this, &UCompanionAnimInstance::OnReloadMontageBlendingOut);

	// Left-hand IK initial state
	bGripSocketValid = false;
	CachedGripMesh.Reset();
	CachedGripSocketName = NAME_None;
	bHasLeftHandIK = false;
	LeftHandIKTarget = FTransform::Identity;

	// Recoil + fire-align initial state
	bHasRecoilProfile = false;
	RecoilTargetRot = FRotator::ZeroRotator;
	RecoilCurrentRot = FRotator::ZeroRotator;
	RecoilTargetKickback = 0.f;
	RecoilCurrentKickback = 0.f;
	RecoilSpineRotation = FRotator::ZeroRotator;
	RecoilSpineOffset = FVector::ZeroVector;
	FireAlignAlpha = 0.f;
	bFireAlignSetup = false;
	PatrolAlignAlpha = 0.f;
	bPatrolAlignSetup = false;
	CachedWeapon.Reset();
	bWasAlive = true;
}

void UCompanionAnimInstance::NativeUninitializeAnimation()
{
	OnMontageBlendingOut.RemoveDynamic(this, &UCompanionAnimInstance::OnReloadMontageBlendingOut);
	CachedCoverPoseComponent.Reset();

	Super::NativeUninitializeAnimation();
}

void UCompanionAnimInstance::OnReloadMontageBlendingOut(UAnimMontage* Montage, bool bInterrupted)
{
	AWeaponBase* W = IsValid(OwningCompanion) ? OwningCompanion->GetCurrentWeapon() : nullptr;
	const UWeaponDataAsset* DA = IsValid(W) ? W->GetWeaponData() : nullptr;
	const bool bIsPerWeaponReload = DA && Montage == DA->EnemyAnimSet.Reload;
	if (Montage != ReloadMontage && Montage != ReloadMontage_Crouch && !bIsPerWeaponReload) return;

	UE_LOG(LogCompanionDiag, Verbose, TEXT("%s: MONTAGE-RELOAD-END reason=blend-out interrupted=%d"),
		IsValid(OwningCompanion) ? *OwningCompanion->GetName() : TEXT("Unknown"),
		(int32)bInterrupted);

	if (IsValid(W))
		W->StopVisualWeaponReload();
}

void UCompanionAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	if (!IsValid(OwningCompanion) || !IsValid(MovementComponent)) return;

	CurrentPosture = OwningCompanion->GetPosture();
	bTakedownCrouchApproach = OwningCompanion->IsInTakedownApproach();
	bTakedownMontagePlaying = OwningCompanion->IsTakedownMontagePlaying();

	// --- Cover Pose (mirror from UCoverPoseComponent — trivial copies, no traces) ---

	if (!CachedCoverPoseComponent.IsValid())
		CachedCoverPoseComponent = OwningCompanion->GetCoverPoseComponent();

	if (CachedCoverPoseComponent.IsValid())
	{
		CoverHeight = CachedCoverPoseComponent->CoverHeight;
		CoverLeanDirection = CachedCoverPoseComponent->LeanDirection;
		bCoverBlindFiring = CachedCoverPoseComponent->bBlindFiring;
		bCoverPeeking = CachedCoverPoseComponent->bPeeking;
	}
	else
	{
		CoverHeight = ECoverHeight::Stand;
		CoverLeanDirection = ECoverLean::None;
		bCoverBlindFiring = false;
		bCoverPeeking = false;
	}

	const FVector RawVelocity = MovementComponent->Velocity;
	FVector EffectiveVelocity = RawVelocity;

	if (bCoverStrafeActive)
	{
		EffectiveVelocity = CoverStrafeVelocity;

		static float LastApplyLogTime = 0.f;
		if (UWorld* W = GetWorld())
		{
			const float Now = W->GetTimeSeconds();
			if (Now - LastApplyLogTime > 0.25f)
			{
				const float DiagDir = UKismetAnimationLibrary::CalculateDirection(EffectiveVelocity, OwningCompanion->GetActorRotation());
				UE_LOG(LogCompanionAI, Verbose, TEXT("%s: COVERSTRAFE-APPLY effSpeed=%.0f effDir=%.1f rawCmcSpeed=%.0f"),
					IsValid(OwningCompanion) ? *OwningCompanion->GetName() : TEXT("?"),
					EffectiveVelocity.Size2D(),
					DiagDir,
					RawVelocity.Size2D());
				LastApplyLogTime = Now;
			}
		}

		CoverStrafeStaleTimer -= DeltaSeconds;
		if (CoverStrafeStaleTimer <= 0.f)
		{
			bCoverStrafeActive = false;
			EffectiveVelocity = RawVelocity;
		}
	}

	Speed = EffectiveVelocity.Size2D();
	bHasVelocity = Speed > 1.f;
	Direction = UKismetAnimationLibrary::CalculateDirection(EffectiveVelocity, OwningCompanion->GetActorRotation());

	const float MaxSpeed = MovementComponent->MaxWalkSpeed;
	NormalizedSpeed = MaxSpeed > 0.f ? Speed / MaxSpeed : 0.f;

	// Throttled speed diag — only logs while actually moving (>20) so it's not idle spam.
	if (Speed > 20.f)
	{
		static float LastSpeedLogTime = 0.f;
		if (UWorld* W = GetWorld())
		{
			const float Now = W->GetTimeSeconds();
			if (Now - LastSpeedLogTime > 0.25f)
			{
				UE_LOG(LogCompanionDiag, Verbose, TEXT("%s: SPEED speed=%.0f maxWalk=%.0f norm=%.2f dir=%.1f sprint=%d coverStrafe=%d"),
					IsValid(OwningCompanion) ? *OwningCompanion->GetName() : TEXT("?"),
					Speed, MaxSpeed, NormalizedSpeed, Direction,
					(int32)OwningCompanion->IsSprinting(),
					(int32)bCoverStrafeActive);
				LastSpeedLogTime = Now;
			}
		}
	}

	bIsInAir = MovementComponent->IsFalling();
	bIsFalling = bIsInAir;

	bIsAccelerating = bCoverStrafeActive
		? (Speed > 1.f)
		: (MovementComponent->GetCurrentAcceleration().SizeSquared() > 0.f);

	bIsSprinting = OwningCompanion->IsSprinting();
	bIsCrouched = OwningCompanion->bIsCrouched;

	// Health
	UHealthComponent* Health = OwningCompanion->GetHealthComponent();
	bIsAlive = IsValid(Health) ? Health->IsAlive() : true;

	// Aim offset
	AActor* AimTarget = OwningCompanion->GetAimTarget();
	if (IsValid(AimTarget))
	{
		const FVector ToTarget = AimTarget->GetActorLocation() - OwningCompanion->GetActorLocation();
		const FRotator AimRot = ToTarget.Rotation();
		const FRotator ActorRot = OwningCompanion->GetActorRotation();
		const FRotator Delta = (AimRot - ActorRot).GetNormalized();
		AimPitch = Delta.Pitch;
		AimYaw = Delta.Yaw;
		bIsAiming = true;
	}
	else if (OwningCompanion->IsScriptedAiming() ||
		(CurrentPosture == ECompanionPosture::Combat && !OwningCompanion->IsLowReadyAim()))
	{
		// Weapon up with no actor target (route Alert/Crouch legs, cover holds, blocked combat):
		// aim along where the AIController is looking (focal point drives control rotation).
		const FRotator AimRot = OwningCompanion->GetBaseAimRotation();
		const FRotator Delta = (AimRot - OwningCompanion->GetActorRotation()).GetNormalized();
		AimPitch = Delta.Pitch;
		AimYaw = Delta.Yaw;
		bIsAiming = true;
	}
	else
	{
		AimPitch = 0.f;
		AimYaw = 0.f;
		bIsAiming = false;
	}

	// --- Cover aim gate: ease AimPitch/AimYaw to zero when tucked in cover ---
	// Nothing companion-side sets the pose component's bPeeking, so an actively playing peek
	// montage is the peek signal — the gate opens for the finite peek-fire window.
	{
		const bool bGateOff = bInCover && !bCoverPeeking && !IsAnyCoverPeekMontagePlaying();
		const float GateTarget = bGateOff ? 0.f : 1.f;
		CoverAimGate = FMath::FInterpTo(CoverAimGate, GateTarget, DeltaSeconds, CoverAimGateSpeed);
		AimPitch *= CoverAimGate;
		AimYaw *= CoverAimGate;
	}

	// Traversal
	UTraversalComponent* TC = OwningCompanion->GetTraversalComponent();
	if (IsValid(TC))
	{
		const ETraversalType ActiveType = TC->GetActiveType();
		bIsVaulting = (ActiveType == ETraversalType::Vault);
		bIsClimbing = (ActiveType == ETraversalType::Climb);
		bIsMantling = (ActiveType == ETraversalType::Mantle);
	}
	else
	{
		bIsVaulting = false;
		bIsClimbing = false;
		bIsMantling = false;
	}

	// Combat
	AWeaponBase* Weapon = OwningCompanion->GetCurrentWeapon();
	if (IsValid(Weapon))
	{
		bIsFiring = Weapon->IsFiring();
		bIsReloading = Weapon->IsReloading();
	}
	else
	{
		bIsFiring = false;
		bIsReloading = false;
	}

	// --- Death edge: zero recoil + fire-align + patrol-align on the frame we die ---
	if (!bIsAlive && bWasAlive)
	{
		RecoilTargetRot = FRotator::ZeroRotator;
		RecoilCurrentRot = FRotator::ZeroRotator;
		RecoilTargetKickback = 0.f;
		RecoilCurrentKickback = 0.f;
		RecoilSpineRotation = FRotator::ZeroRotator;
		RecoilSpineOffset = FVector::ZeroVector;
		FireAlignAlpha = 0.f;
		PatrolAlignAlpha = 0.f;
		bHasLeftHandIK = false;
		LeftHandIKTarget = FTransform::Identity;

		// Cover cleanup — symmetric with the enemy dead branch; don't rely on BT teardown alone.
		if (bInCover)
			ExitCoverPose();
		CoverAimGate = 1.f;
	}
	bWasAlive = bIsAlive;

	if (bIsAlive)
	{
		// --- Equip block: rebind when the wielded weapon changes ---
		if (Weapon != CachedWeapon.Get())
		{
			CachedWeapon = Weapon;
			bFireAlignSetup = false;
			FireAlignAlpha = 0.f;
			bPatrolAlignSetup = false;
			PatrolAlignAlpha = 0.f;

			// Reset left-hand IK cache for the new weapon.
			bGripSocketValid = false;
			CachedGripMesh.Reset();
			CachedGripSocketName = NAME_None;

			if (IsValid(Weapon))
			{
				if (const UWeaponDataAsset* DA = Weapon->GetWeaponData())
				{
					RecoilProfile = DA->EnemyRecoilProfile;
					bHasRecoilProfile = true;

					// Cache left-hand IK socket — resolved once on equip, not per frame.
					const FName GripSocket = DA->LeftHandGripSocket;
					if (!GripSocket.IsNone())
					{
						if (USkeletalMeshComponent* GripMesh = Weapon->GetThirdPersonGripMesh())
						{
							if (GripMesh->DoesSocketExist(GripSocket))
							{
								bGripSocketValid = true;
								CachedGripMesh = GripMesh;
								CachedGripSocketName = GripSocket;
							}
						}
					}
				}
				else
				{
					bHasRecoilProfile = false;
				}
				RecoilTargetRot = FRotator::ZeroRotator;
				RecoilCurrentRot = FRotator::ZeroRotator;
				RecoilTargetKickback = 0.f;
				RecoilCurrentKickback = 0.f;
				RecoilSpineRotation = FRotator::ZeroRotator;
				RecoilSpineOffset = FVector::ZeroVector;

				// Patrol-align (idle-carry) setup: cache DA-driven offset once on equip.
				Weapon->SetupPatrolAlign();
				bPatrolAlignSetup = Weapon->IsPatrolAlignReady();
			}
			else
			{
				bHasRecoilProfile = false;
				RecoilTargetRot = FRotator::ZeroRotator;
				RecoilCurrentRot = FRotator::ZeroRotator;
				RecoilTargetKickback = 0.f;
				RecoilCurrentKickback = 0.f;
				RecoilSpineRotation = FRotator::ZeroRotator;
				RecoilSpineOffset = FVector::ZeroVector;
			}
		}

		// Fire-align setup: run once after an equip when the socket name is configured.
		if (IsValid(Weapon) && !FireAlignSocketName.IsNone() && !bFireAlignSetup)
		{
			Weapon->SetupFireAlign(GetOwningComponent(), FireAlignSocketName);
			bFireAlignSetup = true;
		}

		// --- Fire-align per-frame: ease the weapon toward its fire socket while the montage plays ---
		if (bFireAlignSetup && IsValid(Weapon))
		{
			const bool bFirePlaying = IsValid(FireMontage) && Montage_IsPlaying(FireMontage);
			const float AlphaTarget = bFirePlaying ? 1.f : 0.f;
			FireAlignAlpha = FMath::FInterpTo(FireAlignAlpha, AlphaTarget, DeltaSeconds, FireAlignBlendSpeed);

			// Skip the call when fully settled at rest to avoid fighting the rest transform each frame.
			if (FireAlignAlpha > KINDA_SMALL_NUMBER || bFirePlaying)
				Weapon->SetFireAlignAlpha(FireAlignAlpha);
		}

		// --- Patrol-align (idle-carry): ease weapon between relaxed idle and ADS pose ---
		// Mirrors the enemy patrol-align drive but keyed on aim instead of patrol state.
		if (bPatrolAlignSetup && IsValid(Weapon))
		{
			const float AlphaTarget = bIsAiming ? 0.f : 1.f;
			const float PrevAlpha = PatrolAlignAlpha;
			PatrolAlignAlpha = FMath::FInterpTo(PatrolAlignAlpha, AlphaTarget, DeltaSeconds, PatrolAlignBlendSpeed);

			const bool bAlphaSettled = FMath::IsNearlyEqual(PatrolAlignAlpha, PrevAlpha, KINDA_SMALL_NUMBER);
			if (!bAlphaSettled || PatrolAlignAlpha > KINDA_SMALL_NUMBER)
				Weapon->SetPatrolAlignAlpha(PatrolAlignAlpha);
		}

		// --- Left-Hand IK (socket transform — cheap once validity is cached) ---
		// Disabled during reloads so the left hand follows the reload montage (mag grab).
		if (bGripSocketValid && CachedGripMesh.IsValid() && !bIsReloading)
		{
			LeftHandIKTarget = CachedGripMesh->GetSocketTransform(CachedGripSocketName, RTS_World);
			bHasLeftHandIK = true;
		}
		else
		{
			LeftHandIKTarget = FTransform::Identity;
			bHasLeftHandIK = false;
		}

		// --- Recoil solver ---
		UpdateRecoilSolver(DeltaSeconds);
	}

	if (bIsReloading != bPrevIsReloading)
	{
		if (UE_LOG_ACTIVE(LogCompanionDiag, Log))
		{
			UE_LOG(LogCompanionDiag, Verbose, TEXT("%s: MONTAGE-RELOAD-TOGGLE isReloading=%d vel=%.1f"),
				IsValid(OwningCompanion) ? *OwningCompanion->GetName() : TEXT("Unknown"),
				(int32)bIsReloading,
				IsValid(MovementComponent) ? MovementComponent->Velocity.Size() : 0.f);
		}
		// Fire the reload montage on the false→true transition.
		if (bIsReloading)
		{
			PlayReloadMontage(1.f);
		}
		bPrevIsReloading = bIsReloading;
	}
}

void UCompanionAnimInstance::PlayFireMontage(float PlayRate)
{
	if (!IsValid(FireMontage)) return;
	// Don't restart if already playing (avoid hitching on every shot for loop montages).
	if (Montage_IsPlaying(FireMontage)) return;
	Montage_Play(FireMontage, PlayRate);
	// Chain the Default section to itself so the montage loops until StopFireMontage is called.
	Montage_SetNextSection(TEXT("Default"), TEXT("Default"), FireMontage);
}

void UCompanionAnimInstance::StopFireMontage(float BlendOutTime)
{
	if (IsValid(FireMontage))
		Montage_Stop(BlendOutTime, FireMontage);
}

void UCompanionAnimInstance::PlayReloadMontage(float PlayRate)
{
	AWeaponBase* Weapon = IsValid(OwningCompanion) ? OwningCompanion->GetCurrentWeapon() : nullptr;
	UAnimMontage* MontageToPlay = (bIsCrouched && IsValid(ReloadMontage_Crouch))
		? ReloadMontage_Crouch.Get()
		: ReloadMontage.Get();

	float EffectiveRate = PlayRate;
	if (IsValid(Weapon))
	{
		if (const UWeaponDataAsset* DA = Weapon->GetWeaponData())
		{
			if (IsValid(DA->EnemyAnimSet.Reload))
				MontageToPlay = DA->EnemyAnimSet.Reload.Get();

			const float ReloadTime = DA->ReloadTime;
			const float MontageLength = IsValid(MontageToPlay) ? MontageToPlay->GetPlayLength() : 0.f;
			if (ReloadTime > 0.f && MontageLength > 0.f)
				EffectiveRate = FMath::Clamp(MontageLength / ReloadTime, 0.5f, 2.0f);
		}
	}

	if (!IsValid(MontageToPlay)) return;
	if (Montage_IsPlaying(MontageToPlay)) return;

	Montage_Play(MontageToPlay, EffectiveRate);
	if (IsValid(Weapon))
		Weapon->PlayVisualWeaponReload(EffectiveRate);

	UE_LOG(LogCompanionDiag, Verbose, TEXT("%s: MONTAGE-RELOAD-START len=%.2f playRate=%.2f"),
		IsValid(OwningCompanion) ? *OwningCompanion->GetName() : TEXT("Unknown"),
		MontageToPlay->GetPlayLength(),
		EffectiveRate);
}

void UCompanionAnimInstance::PlayHitReactMontage(float PlayRate)
{
	// Suppress hit react for the entire engagement — cover-idle gaps between bursts clear
	// bIsFiring/bIsAiming, but posture stays Combat while a BB target is held.
	// Proper upper-body additive blend is future work.
	if (bIsFiring || bIsAiming || CurrentPosture == ECompanionPosture::Combat) return;

	// Out-of-combat path — HitReactMontage_Aim reserved for the future blend step.
	if (IsValid(HitReactMontage))
		Montage_Play(HitReactMontage, PlayRate);
}

void UCompanionAnimInstance::PlayDeathMontage(float PlayRate)
{
	if (IsValid(DeathMontage))
		Montage_Play(DeathMontage, PlayRate);
}

float UCompanionAnimInstance::PlayTraversalMontage(ETraversalType Type, float PlayRate)
{
	UAnimMontage* Selected = nullptr;

	switch (Type)
	{
	case ETraversalType::Vault:      Selected = VaultMontage;      break;
	case ETraversalType::Climb:      Selected = ClimbMontage;      break;
	case ETraversalType::Mantle:     Selected = MantleMontage;     break;
	case ETraversalType::DropDown:   Selected = DropDownMontage;   break;
	case ETraversalType::Jump:       Selected = JumpMontage;       break;
	case ETraversalType::SprintJump: Selected = SprintJumpMontage; break;
	default: return 0.f;
	}

	if (!IsValid(Selected)) return 0.f;

	return Montage_Play(Selected, PlayRate);
}

bool UCompanionAnimInstance::HasMontageForType(ETraversalType Type) const
{
	switch (Type)
	{
	case ETraversalType::Vault:      return IsValid(VaultMontage);
	case ETraversalType::Climb:      return IsValid(ClimbMontage);
	case ETraversalType::Mantle:     return IsValid(MantleMontage);
	case ETraversalType::DropDown:   return IsValid(DropDownMontage);
	case ETraversalType::Jump:       return IsValid(JumpMontage);
	case ETraversalType::SprintJump: return IsValid(SprintJumpMontage);
	default: return false;
	}
}

UAnimMontage* UCompanionAnimInstance::GetMontageForType(ETraversalType Type) const
{
	switch (Type)
	{
	case ETraversalType::Vault:      return VaultMontage;
	case ETraversalType::Climb:      return ClimbMontage;
	case ETraversalType::Mantle:     return MantleMontage;
	case ETraversalType::DropDown:   return DropDownMontage;
	case ETraversalType::Jump:       return JumpMontage;
	case ETraversalType::SprintJump: return SprintJumpMontage;
	default:                         return nullptr;
	}
}

void UCompanionAnimInstance::SetCoverStrafeVelocity(const FVector& Velocity)
{
	static float LastSetLogTime = 0.f;
	if (UWorld* W = GetWorld())
	{
		const float Now = W->GetTimeSeconds();
		if (Now - LastSetLogTime > 0.25f)
		{
			UE_LOG(LogCompanionAI, Verbose, TEXT("%s: COVERSTRAFE-SET vel=%s speed=%.0f"),
				IsValid(OwningCompanion) ? *OwningCompanion->GetName() : TEXT("?"),
				*Velocity.ToString(), Velocity.Size2D());
			LastSetLogTime = Now;
		}
	}
	// Stop cover-idle montage so locomotion blendspace animates the strafe.
	// The BT re-calls EnterCoverPose on arrival — do NOT auto-restart here.
	if (!bCoverStrafeActive)
	{
		if (IsValid(CoverIdleLeftMontage) && Montage_IsPlaying(CoverIdleLeftMontage))
			Montage_Stop(0.2f, CoverIdleLeftMontage);
		if (IsValid(CoverIdleRightMontage) && Montage_IsPlaying(CoverIdleRightMontage))
			Montage_Stop(0.2f, CoverIdleRightMontage);
		if (IsValid(CoverIdleLeftMontage_Stand) && Montage_IsPlaying(CoverIdleLeftMontage_Stand))
			Montage_Stop(0.2f, CoverIdleLeftMontage_Stand);
		if (IsValid(CoverIdleRightMontage_Stand) && Montage_IsPlaying(CoverIdleRightMontage_Stand))
			Montage_Stop(0.2f, CoverIdleRightMontage_Stand);
	}

	CoverStrafeVelocity = Velocity;
	bCoverStrafeActive = true;
	CoverStrafeStaleTimer = 0.1f;
}

void UCompanionAnimInstance::ClearCoverStrafeVelocity()
{
	UE_LOG(LogCompanionAI, Log, TEXT("%s: COVERSTRAFE-CLEAR"),
		IsValid(OwningCompanion) ? *OwningCompanion->GetName() : TEXT("?"));
	CoverStrafeVelocity = FVector::ZeroVector;
	bCoverStrafeActive = false;
	CoverStrafeStaleTimer = 0.f;
}

namespace
{
	constexpr float CoverBlendOutTime = 0.15f;
}

void UCompanionAnimInstance::EnterCoverPose(EPeekSide DefaultSide, ECoverHeight Height, bool bPlayEnterMontage)
{
	// Select the idle by height and side.
	UAnimMontage* IdleMontage = (Height == ECoverHeight::Stand)
		? ((DefaultSide == EPeekSide::Left) ? CoverIdleLeftMontage_Stand.Get() : CoverIdleRightMontage_Stand.Get())
		: ((DefaultSide == EPeekSide::Left) ? CoverIdleLeftMontage.Get() : CoverIdleRightMontage.Get());

	// bPlayEnterMontage=false means "don't re-bob an already-posed idle" — keep a matching idle
	// running instead of restarting it. Post-strafe arrivals have none (the strafe stopped it),
	// so the idle still starts below and the tucked pose survives every shuffle.
	const bool bKeepRunningIdle = !bPlayEnterMontage && IsValid(IdleMontage) && Montage_IsPlaying(IdleMontage);
	UAnimMontage* const KeptIdle = bKeepRunningIdle ? IdleMontage : nullptr;

	// Stop any active cover/peek montage before switching pose (sparing a kept idle).
	if (IsValid(CoverIdleLeftMontage) && CoverIdleLeftMontage != KeptIdle && Montage_IsPlaying(CoverIdleLeftMontage))
		Montage_Stop(CoverBlendOutTime, CoverIdleLeftMontage);
	if (IsValid(CoverIdleRightMontage) && CoverIdleRightMontage != KeptIdle && Montage_IsPlaying(CoverIdleRightMontage))
		Montage_Stop(CoverBlendOutTime, CoverIdleRightMontage);
	if (IsValid(CoverPeekLeftMontage) && Montage_IsPlaying(CoverPeekLeftMontage))
		Montage_Stop(CoverBlendOutTime, CoverPeekLeftMontage);
	if (IsValid(CoverPeekRightMontage) && Montage_IsPlaying(CoverPeekRightMontage))
		Montage_Stop(CoverBlendOutTime, CoverPeekRightMontage);
	if (IsValid(CoverIdleLeftMontage_Stand) && CoverIdleLeftMontage_Stand != KeptIdle && Montage_IsPlaying(CoverIdleLeftMontage_Stand))
		Montage_Stop(CoverBlendOutTime, CoverIdleLeftMontage_Stand);
	if (IsValid(CoverIdleRightMontage_Stand) && CoverIdleRightMontage_Stand != KeptIdle && Montage_IsPlaying(CoverIdleRightMontage_Stand))
		Montage_Stop(CoverBlendOutTime, CoverIdleRightMontage_Stand);
	if (IsValid(CoverPeekLeftMontage_Stand) && Montage_IsPlaying(CoverPeekLeftMontage_Stand))
		Montage_Stop(CoverBlendOutTime, CoverPeekLeftMontage_Stand);
	if (IsValid(CoverPeekRightMontage_Stand) && Montage_IsPlaying(CoverPeekRightMontage_Stand))
		Montage_Stop(CoverBlendOutTime, CoverPeekRightMontage_Stand);

	bInCover = true;
	ActivePeekSide = DefaultSide;
	LatchedCoverHeight = Height;

	// Keep the pose component in sync — it is the source of truth for the mirror fields
	// (CoverHeight/CoverLeanDirection/etc.) read back in NativeUpdateAnimation.
	if (UCoverPoseComponent* Pose = IsValid(OwningCompanion) ? OwningCompanion->GetCoverPoseComponent() : nullptr)
	{
		Pose->SetInCover(true, Height);
		Pose->SetLean(DefaultSide == EPeekSide::Left ? ECoverLean::Left : ECoverLean::Right);
	}

	if (!bKeepRunningIdle && IsValid(IdleMontage))
	{
		Montage_PlayWithBlendIn(IdleMontage, FAlphaBlendArgs(0.4f), 1.f);
		if (IdleMontage->GetSectionIndex(TEXT("Loop")) != INDEX_NONE)
			Montage_JumpToSection(TEXT("Loop"), IdleMontage);
	}
}

void UCompanionAnimInstance::ExitCoverPose()
{
	if (IsValid(CoverIdleLeftMontage) && Montage_IsPlaying(CoverIdleLeftMontage))
		Montage_Stop(CoverBlendOutTime, CoverIdleLeftMontage);
	if (IsValid(CoverIdleRightMontage) && Montage_IsPlaying(CoverIdleRightMontage))
		Montage_Stop(CoverBlendOutTime, CoverIdleRightMontage);
	if (IsValid(CoverPeekLeftMontage) && Montage_IsPlaying(CoverPeekLeftMontage))
		Montage_Stop(CoverBlendOutTime, CoverPeekLeftMontage);
	if (IsValid(CoverPeekRightMontage) && Montage_IsPlaying(CoverPeekRightMontage))
		Montage_Stop(CoverBlendOutTime, CoverPeekRightMontage);
	if (IsValid(CoverIdleLeftMontage_Stand) && Montage_IsPlaying(CoverIdleLeftMontage_Stand))
		Montage_Stop(CoverBlendOutTime, CoverIdleLeftMontage_Stand);
	if (IsValid(CoverIdleRightMontage_Stand) && Montage_IsPlaying(CoverIdleRightMontage_Stand))
		Montage_Stop(CoverBlendOutTime, CoverIdleRightMontage_Stand);
	if (IsValid(CoverPeekLeftMontage_Stand) && Montage_IsPlaying(CoverPeekLeftMontage_Stand))
		Montage_Stop(CoverBlendOutTime, CoverPeekLeftMontage_Stand);
	if (IsValid(CoverPeekRightMontage_Stand) && Montage_IsPlaying(CoverPeekRightMontage_Stand))
		Montage_Stop(CoverBlendOutTime, CoverPeekRightMontage_Stand);

	// CoverAimGate is left to ease back — its target returns to 1 once bInCover clears.
	bInCover = false;

	if (UCoverPoseComponent* Pose = IsValid(OwningCompanion) ? OwningCompanion->GetCoverPoseComponent() : nullptr)
		Pose->ResetCoverPose();

	UE_LOG(LogCompanionAI, Log, TEXT("Cover EXIT"));
}

bool UCompanionAnimInstance::IsCoverIdleMontagePlaying(EPeekSide Side) const
{
	// Check both heights for the given side.
	if (Side == EPeekSide::Left)
	{
		if (IsValid(CoverIdleLeftMontage) && Montage_IsPlaying(CoverIdleLeftMontage)) return true;
		if (IsValid(CoverIdleLeftMontage_Stand) && Montage_IsPlaying(CoverIdleLeftMontage_Stand)) return true;
	}
	else
	{
		if (IsValid(CoverIdleRightMontage) && Montage_IsPlaying(CoverIdleRightMontage)) return true;
		if (IsValid(CoverIdleRightMontage_Stand) && Montage_IsPlaying(CoverIdleRightMontage_Stand)) return true;
	}
	return false;
}

UAnimMontage* UCompanionAnimInstance::PlayPeekFire(EPeekSide Side, float PlayRate)
{
	// Stop all cover-idle montages so the peek montage owns the body slot.
	if (IsValid(CoverIdleLeftMontage) && Montage_IsPlaying(CoverIdleLeftMontage))
		Montage_Stop(CoverBlendOutTime, CoverIdleLeftMontage);
	if (IsValid(CoverIdleRightMontage) && Montage_IsPlaying(CoverIdleRightMontage))
		Montage_Stop(CoverBlendOutTime, CoverIdleRightMontage);
	if (IsValid(CoverIdleLeftMontage_Stand) && Montage_IsPlaying(CoverIdleLeftMontage_Stand))
		Montage_Stop(CoverBlendOutTime, CoverIdleLeftMontage_Stand);
	if (IsValid(CoverIdleRightMontage_Stand) && Montage_IsPlaying(CoverIdleRightMontage_Stand))
		Montage_Stop(CoverBlendOutTime, CoverIdleRightMontage_Stand);

	// Select by height (latched at EnterCoverPose — the pose-component mirror can be stale) then side.
	UAnimMontage* PeekMontage = nullptr;
	if (LatchedCoverHeight == ECoverHeight::Stand)
		PeekMontage = (Side == EPeekSide::Left) ? CoverPeekLeftMontage_Stand.Get() : CoverPeekRightMontage_Stand.Get();
	else
		PeekMontage = (Side == EPeekSide::Left) ? CoverPeekLeftMontage.Get() : CoverPeekRightMontage.Get();

	ActivePeekSide = Side;

	if (!IsValid(PeekMontage)) return nullptr;

	Montage_Play(PeekMontage, PlayRate);
	return PeekMontage;
}

bool UCompanionAnimInstance::IsAnyCoverPeekMontagePlaying() const
{
	if (IsValid(CoverPeekLeftMontage) && Montage_IsPlaying(CoverPeekLeftMontage)) return true;
	if (IsValid(CoverPeekRightMontage) && Montage_IsPlaying(CoverPeekRightMontage)) return true;
	if (IsValid(CoverPeekLeftMontage_Stand) && Montage_IsPlaying(CoverPeekLeftMontage_Stand)) return true;
	if (IsValid(CoverPeekRightMontage_Stand) && Montage_IsPlaying(CoverPeekRightMontage_Stand)) return true;
	return false;
}

// --- Recoil Solver ---

void UCompanionAnimInstance::AddRecoilImpulse()
{
	if (!bHasRecoilProfile) return;

	const float AimScale = bIsAiming ? RecoilProfile.AimRecoilScale : 1.f;

	// Pitch: positive Pitch on FRotator = nose down in UE; we negate so PitchKick > 0 = upward kick.
	RecoilTargetRot.Pitch -= RecoilProfile.PitchKick * AimScale;
	RecoilTargetRot.Pitch = FMath::Clamp(RecoilTargetRot.Pitch, -RecoilProfile.MaxAccumulatedPitch, 0.f);

	// Yaw and roll wander both directions — random sign per shot so bursts don't walk sideways.
	const float YawSign = FMath::RandBool() ? 1.f : -1.f;
	const float RollSign = FMath::RandBool() ? 1.f : -1.f;
	const float YawLo = FMath::Min(RecoilProfile.YawKickMin, RecoilProfile.YawKickMax);
	const float YawHi = FMath::Max(RecoilProfile.YawKickMin, RecoilProfile.YawKickMax);
	RecoilTargetRot.Yaw += FMath::RandRange(YawLo, YawHi) * YawSign * AimScale;
	RecoilTargetRot.Roll += RecoilProfile.RollKick * RollSign * AimScale;

	// Clamp yaw and roll so a long LMG belt can't drift the gun off-screen.
	const float YawMax = 2.f * RecoilProfile.YawKickMax;
	const float RollMax = 2.f * RecoilProfile.RollKick;
	RecoilTargetRot.Yaw = FMath::Clamp(RecoilTargetRot.Yaw, -YawMax, YawMax);
	RecoilTargetRot.Roll = FMath::Clamp(RecoilTargetRot.Roll, -RollMax, RollMax);

	// Kickback: accumulate and clamp to 3x single-shot to prevent extreme offsets on LMG bursts.
	RecoilTargetKickback += RecoilProfile.WeaponKickback * AimScale;
	RecoilTargetKickback = FMath::Min(RecoilTargetKickback, RecoilProfile.WeaponKickback * 3.f);
}

// NOTE: runs on the game thread (called from NativeUpdateAnimation).
void UCompanionAnimInstance::UpdateRecoilSolver(float DeltaSeconds)
{
	if (!bHasRecoilProfile)
	{
		RecoilSpineRotation = FRotator::ZeroRotator;
		RecoilSpineOffset = FVector::ZeroVector;
		return;
	}

	// Settle-guard: skip integration when the spring is at rest.
	const bool bRecoilActive =
		!RecoilCurrentRot.IsNearlyZero(KINDA_SMALL_NUMBER) ||
		!RecoilTargetRot.IsNearlyZero(KINDA_SMALL_NUMBER) ||
		!FMath::IsNearlyZero(RecoilCurrentKickback, KINDA_SMALL_NUMBER) ||
		!FMath::IsNearlyZero(RecoilTargetKickback, KINDA_SMALL_NUMBER);

	if (!bRecoilActive)
	{
		RecoilSpineRotation = FRotator::ZeroRotator;
		RecoilSpineOffset = FVector::ZeroVector;
		return;
	}

	// Ease current → target (attack transient).
	RecoilCurrentRot = FMath::RInterpTo(RecoilCurrentRot, RecoilTargetRot, DeltaSeconds, RecoilProfile.Sharpness);
	RecoilCurrentKickback = FMath::FInterpTo(RecoilCurrentKickback, RecoilTargetKickback, DeltaSeconds, RecoilProfile.Sharpness);

	// Decay target → zero (recovery between shots / on cease-fire).
	RecoilTargetRot = FMath::RInterpTo(RecoilTargetRot, FRotator::ZeroRotator, DeltaSeconds, RecoilProfile.RecoverySpeed);
	RecoilTargetKickback = FMath::FInterpTo(RecoilTargetKickback, 0.f, DeltaSeconds, RecoilProfile.RecoverySpeed);

	// Spine rotation output: fraction of the rotation routed to the body additive.
	RecoilSpineRotation = RecoilCurrentRot * RecoilProfile.SpineKickScale;

	// Forward/back piston: route the eased kickback (cm) to a backward spine translation.
	// Component-space -Y = backward along the aim axis (companion mesh also yawed -90 deg).
	RecoilSpineOffset = FVector(0.f, -RecoilCurrentKickback, 0.f);
}
