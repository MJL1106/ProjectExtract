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
	if (Montage != ReloadMontage) return;
	UE_LOG(LogCompanionDiag, Log, TEXT("%s: MONTAGE-RELOAD-END reason=blend-out interrupted=%d"),
		IsValid(OwningCompanion) ? *OwningCompanion->GetName() : TEXT("Unknown"),
		(int32)bInterrupted);
}

void UCompanionAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	if (!IsValid(OwningCompanion) || !IsValid(MovementComponent)) return;

	CurrentPosture = OwningCompanion->GetPosture();

	const FVector Velocity = MovementComponent->Velocity;
	Speed = Velocity.Size2D();
	bHasVelocity = Speed > 1.f;
	Direction = UKismetAnimationLibrary::CalculateDirection(Velocity, OwningCompanion->GetActorRotation());

	const float MaxSpeed = MovementComponent->MaxWalkSpeed;
	NormalizedSpeed = MaxSpeed > 0.f ? Speed / MaxSpeed : 0.f;

	bIsInAir = MovementComponent->IsFalling();
	bIsFalling = bIsInAir;
	bIsAccelerating = MovementComponent->GetCurrentAcceleration().SizeSquared() > 0.f;
	bIsSprinting = OwningCompanion->IsSprinting();

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
		bPrevIsReloading = bIsReloading;
	}
}

void UCompanionAnimInstance::PlayFireMontage(float PlayRate)
{
	if (IsValid(FireMontage))
		Montage_Play(FireMontage, PlayRate);
}

void UCompanionAnimInstance::PlayReloadMontage(float PlayRate)
{
	if (!IsValid(ReloadMontage)) return;

	Montage_Play(ReloadMontage, PlayRate);
	UE_LOG(LogCompanionDiag, Log, TEXT("%s: MONTAGE-RELOAD-START len=%.2f playRate=%.2f"),
		IsValid(OwningCompanion) ? *OwningCompanion->GetName() : TEXT("Unknown"),
		ReloadMontage->GetPlayLength(),
		PlayRate);
}

void UCompanionAnimInstance::PlayHitReactMontage(float PlayRate)
{
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

namespace
{
	constexpr float CoverBlendOutTime = 0.15f;
}

void UCompanionAnimInstance::EnterCoverPose(EPeekSide DefaultSide, bool bPlayEnterMontage)
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

	if (!bPlayEnterMontage) return;

	UAnimMontage* IdleMontage = (DefaultSide == EPeekSide::Left) ? CoverIdleLeftMontage : CoverIdleRightMontage;
	if (IsValid(IdleMontage))
		Montage_Play(IdleMontage, 1.f);

	UE_LOG(LogCompanionAI, Log, TEXT("Cover ENTER side=%s montage=%s (valid=%d, playing=%d)"),
		DefaultSide == EPeekSide::Left ? TEXT("Left") : TEXT("Right"),
		*GetNameSafe(IdleMontage),
		(int32)IsValid(IdleMontage),
		(int32)Montage_IsPlaying(IdleMontage));
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
