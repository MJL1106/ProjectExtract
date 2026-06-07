// Anim instance for the AI companion — drives locomotion, aim offset, and combat montages.

#include "CompanionAnimInstance.h"
#include "AI/CompanionDiag.h"
#include "CompanionCharacter.h"
#include "WeaponBase.h"
#include "HealthComponent.h"
#include "TraversalComponent.h"
#include "Animation/AnimMontage.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "KismetAnimationLibrary.h"
#include "CompanionAIController.h"

void UCompanionAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();

	APawn* PawnOwner = TryGetPawnOwner();
	if (!IsValid(PawnOwner)) return;

	OwningCompanion = Cast<ACompanionCharacter>(PawnOwner);
	if (!IsValid(OwningCompanion)) return;

	MovementComponent = OwningCompanion->GetCharacterMovement();

	OnMontageBlendingOut.AddDynamic(this, &UCompanionAnimInstance::OnReloadMontageBlendingOut);
}

void UCompanionAnimInstance::NativeUninitializeAnimation()
{
	OnMontageBlendingOut.RemoveDynamic(this, &UCompanionAnimInstance::OnReloadMontageBlendingOut);

	Super::NativeUninitializeAnimation();
}

void UCompanionAnimInstance::OnReloadMontageBlendingOut(UAnimMontage* Montage, bool bInterrupted)
{
	if (Montage != ReloadMontage && Montage != ReloadMontage_Crouch) return;
	UE_LOG(LogCompanionDiag, Log, TEXT("%s: MONTAGE-RELOAD-END reason=blend-out interrupted=%d"),
		IsValid(OwningCompanion) ? *OwningCompanion->GetName() : TEXT("Unknown"),
		(int32)bInterrupted);
}

void UCompanionAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	if (!IsValid(OwningCompanion) || !IsValid(MovementComponent)) return;

	CurrentPosture = OwningCompanion->GetPosture();

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
				UE_LOG(LogCompanionAI, Log, TEXT("%s: COVERSTRAFE-APPLY effSpeed=%.0f effDir=%.1f rawCmcSpeed=%.0f"),
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
				UE_LOG(LogCompanionDiag, Log, TEXT("%s: SPEED speed=%.0f maxWalk=%.0f norm=%.2f dir=%.1f sprint=%d coverStrafe=%d"),
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
	else
	{
		AimPitch = 0.f;
		AimYaw = 0.f;
		bIsAiming = false;
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

	if (bIsReloading != bPrevIsReloading)
	{
		if (UE_LOG_ACTIVE(LogCompanionDiag, Log))
		{
			UE_LOG(LogCompanionDiag, Log, TEXT("%s: MONTAGE-RELOAD-TOGGLE isReloading=%d vel=%.1f"),
				IsValid(OwningCompanion) ? *OwningCompanion->GetName() : TEXT("Unknown"),
				(int32)bIsReloading,
				IsValid(MovementComponent) ? MovementComponent->Velocity.Size() : 0.f);
		}
		// Fire the reload montage on the false→true transition.
		if (bIsReloading)
			PlayReloadMontage(1.f);
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
	UAnimMontage* MontageToPlay = (bIsCrouched && IsValid(ReloadMontage_Crouch))
		? ReloadMontage_Crouch.Get()
		: ReloadMontage.Get();
	if (!IsValid(MontageToPlay)) return;

	Montage_Play(MontageToPlay, PlayRate);
	UE_LOG(LogCompanionDiag, Log, TEXT("%s: MONTAGE-RELOAD-START len=%.2f playRate=%.2f"),
		IsValid(OwningCompanion) ? *OwningCompanion->GetName() : TEXT("Unknown"),
		MontageToPlay->GetPlayLength(),
		PlayRate);
}

void UCompanionAnimInstance::PlayHitReactMontage(float PlayRate)
{
	// Pick aim variant when in combat, fall back to default if aim variant not assigned.
	UAnimMontage* MontageToPlay = ((bIsFiring || bIsAiming) && IsValid(HitReactMontage_Aim))
		? HitReactMontage_Aim.Get()
		: HitReactMontage.Get();
	if (IsValid(MontageToPlay))
		Montage_Play(MontageToPlay, PlayRate);
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
			UE_LOG(LogCompanionAI, Log, TEXT("%s: COVERSTRAFE-SET vel=%s speed=%.0f"),
				IsValid(OwningCompanion) ? *OwningCompanion->GetName() : TEXT("?"),
				*Velocity.ToString(), Velocity.Size2D());
			LastSetLogTime = Now;
		}
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
	// Stop any active cover/peek montage before switching pose.
	if (IsValid(CoverIdleLeftMontage) && Montage_IsPlaying(CoverIdleLeftMontage))
		Montage_Stop(CoverBlendOutTime, CoverIdleLeftMontage);
	if (IsValid(CoverIdleRightMontage) && Montage_IsPlaying(CoverIdleRightMontage))
		Montage_Stop(CoverBlendOutTime, CoverIdleRightMontage);
	if (IsValid(CoverPeekLeftMontage) && Montage_IsPlaying(CoverPeekLeftMontage))
		Montage_Stop(CoverBlendOutTime, CoverPeekLeftMontage);
	if (IsValid(CoverPeekRightMontage) && Montage_IsPlaying(CoverPeekRightMontage))
		Montage_Stop(CoverBlendOutTime, CoverPeekRightMontage);

	bInCover = true;
	ActivePeekSide = DefaultSide;

	// Cover idle is driven by the AnimBP locomotion state machine via bIsCrouched + bInCover — no montage needed for any height.
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

	bInCover = false;

	UE_LOG(LogCompanionAI, Log, TEXT("Cover EXIT"));
}

bool UCompanionAnimInstance::IsCoverIdleMontagePlaying(EPeekSide Side) const
{
	UAnimMontage* M = (Side == EPeekSide::Left) ? CoverIdleLeftMontage : CoverIdleRightMontage;
	return IsValid(M) && Montage_IsPlaying(M);
}

UAnimMontage* UCompanionAnimInstance::PlayPeekFire(EPeekSide Side, float PlayRate)
{
	// Stop cover-idle montages so the peek montage owns the body slot.
	if (IsValid(CoverIdleLeftMontage) && Montage_IsPlaying(CoverIdleLeftMontage))
		Montage_Stop(CoverBlendOutTime, CoverIdleLeftMontage);
	if (IsValid(CoverIdleRightMontage) && Montage_IsPlaying(CoverIdleRightMontage))
		Montage_Stop(CoverBlendOutTime, CoverIdleRightMontage);

	UAnimMontage* PeekMontage = (Side == EPeekSide::Left) ? CoverPeekLeftMontage : CoverPeekRightMontage;
	ActivePeekSide = Side;

	if (!IsValid(PeekMontage)) return nullptr;

	Montage_Play(PeekMontage, PlayRate);
	return PeekMontage;
}
