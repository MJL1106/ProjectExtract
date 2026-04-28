// Anim instance for the AI companion — drives locomotion, aim offset, and combat montages.

#include "CompanionAnimInstance.h"
#include "CompanionCharacter.h"
#include "WeaponBase.h"
#include "HealthComponent.h"
#include "TraversalComponent.h"
#include "Animation/AnimMontage.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "KismetAnimationLibrary.h"

void UCompanionAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();

	APawn* PawnOwner = TryGetPawnOwner();
	if (!IsValid(PawnOwner)) return;

	OwningCompanion = Cast<ACompanionCharacter>(PawnOwner);
	if (!IsValid(OwningCompanion)) return;

	MovementComponent = OwningCompanion->GetCharacterMovement();
}

void UCompanionAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	if (!IsValid(OwningCompanion) || !IsValid(MovementComponent)) return;

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
	}
	else
	{
		AimPitch = 0.f;
		AimYaw = 0.f;
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
}

void UCompanionAnimInstance::PlayFireMontage(float PlayRate)
{
	if (IsValid(FireMontage))
		Montage_Play(FireMontage, PlayRate);
}

void UCompanionAnimInstance::PlayReloadMontage(float PlayRate)
{
	if (IsValid(ReloadMontage))
		Montage_Play(ReloadMontage, PlayRate);
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
	case ETraversalType::Vault:  Selected = VaultMontage;  break;
	case ETraversalType::Climb:  Selected = ClimbMontage;  break;
	case ETraversalType::Mantle: Selected = MantleMontage; break;
	default: return 0.f;
	}

	if (!IsValid(Selected)) return 0.f;

	return Montage_Play(Selected, PlayRate);
}
