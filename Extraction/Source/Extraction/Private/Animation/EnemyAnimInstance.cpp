// Anim instance for enemy characters — locomotion, aim offset, combat montages, and delegate-driven reactions.

#include "EnemyAnimInstance.h"
#include "EnemyCharacter.h"
#include "WeaponBase.h"
#include "HealthComponent.h"
#include "Animation/AnimMontage.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "KismetAnimationLibrary.h"

void UEnemyAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();

	bPrevIsFiring = false;
	bPrevIsReloading = false;

	APawn* PawnOwner = TryGetPawnOwner();
	if (!IsValid(PawnOwner)) return;

	OwningEnemy = Cast<AEnemyCharacter>(PawnOwner);
	if (!IsValid(OwningEnemy)) return;

	MovementComponent = OwningEnemy->GetCharacterMovement();
	HealthComponent = OwningEnemy->GetHealthComponent();

	OwningEnemy->OnHitReact.RemoveDynamic(this, &UEnemyAnimInstance::HandleHitReact);
	OwningEnemy->OnMeleePerformed.RemoveDynamic(this, &UEnemyAnimInstance::HandleMeleePerformed);
	OwningEnemy->OnTakedownExecuted.RemoveDynamic(this, &UEnemyAnimInstance::HandleTakedown);

	OwningEnemy->OnHitReact.AddDynamic(this, &UEnemyAnimInstance::HandleHitReact);
	OwningEnemy->OnMeleePerformed.AddDynamic(this, &UEnemyAnimInstance::HandleMeleePerformed);
	OwningEnemy->OnTakedownExecuted.AddDynamic(this, &UEnemyAnimInstance::HandleTakedown);
}

void UEnemyAnimInstance::NativeUninitializeAnimation()
{
	if (IsValid(OwningEnemy))
	{
		OwningEnemy->OnHitReact.RemoveDynamic(this, &UEnemyAnimInstance::HandleHitReact);
		OwningEnemy->OnMeleePerformed.RemoveDynamic(this, &UEnemyAnimInstance::HandleMeleePerformed);
		OwningEnemy->OnTakedownExecuted.RemoveDynamic(this, &UEnemyAnimInstance::HandleTakedown);
	}

	Super::NativeUninitializeAnimation();
}

void UEnemyAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	if (!IsValid(OwningEnemy) || !IsValid(MovementComponent)) return;

	const FRotator ActorRot = OwningEnemy->GetActorRotation();

	// --- Locomotion ---

	const FVector Velocity = MovementComponent->Velocity;
	Speed = Velocity.Size2D();
	bHasVelocity = Speed > 1.f;
	Direction = UKismetAnimationLibrary::CalculateDirection(Velocity, ActorRot);

	const float MaxSpeed = MovementComponent->MaxWalkSpeed;
	NormalizedSpeed = MaxSpeed > 0.f ? Speed / MaxSpeed : 0.f;

	bIsInAir = MovementComponent->IsFalling();
	bIsFalling = bIsInAir;
	bIsAccelerating = MovementComponent->GetCurrentAcceleration().SizeSquared() > 0.f;
	bIsCrouched = OwningEnemy->bIsCrouched;

	// --- Health ---

	bIsAlive = IsValid(HealthComponent) ? HealthComponent->IsAlive() : true;

	// --- Aim Offset ---

	FVector AimLocation;
	if (OwningEnemy->GetAIAimLocation(AimLocation))
	{
		UpdateAimOffset(AimLocation - OwningEnemy->GetActorLocation(), ActorRot);
	}
	else if (AActor* AimTarget = OwningEnemy->GetAIAimTarget())
	{
		UpdateAimOffset(AimTarget->GetActorLocation() - OwningEnemy->GetActorLocation(), ActorRot);
	}
	else
	{
		AimPitch = 0.f;
		AimYaw = 0.f;
		bIsAiming = false;
	}

	// --- Combat ---

	AWeaponBase* Weapon = OwningEnemy->GetCurrentWeapon();
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

	// --- Auto-trigger: fire montage on weapon state transitions ---

	if (bIsAlive)
	{
		if (bIsFiring && !bPrevIsFiring)
			PlayFireMontage();
		else if (!bIsFiring && bPrevIsFiring)
			StopFireMontage();
		bPrevIsFiring = bIsFiring;

		if (bIsReloading && !bPrevIsReloading)
			PlayReloadMontage();
		bPrevIsReloading = bIsReloading;
	}
	else
	{
		bPrevIsFiring = false;
		bPrevIsReloading = false;
	}
}

// --- Aim Offset Helper ---

void UEnemyAnimInstance::UpdateAimOffset(const FVector& ToTarget, const FRotator& ActorRot)
{
	if (ToTarget.SizeSquared() <= KINDA_SMALL_NUMBER)
	{
		AimPitch = 0.f;
		AimYaw = 0.f;
		bIsAiming = false;
		return;
	}

	const FRotator Delta = (ToTarget.Rotation() - ActorRot).GetNormalized();
	AimPitch = Delta.Pitch;
	AimYaw = Delta.Yaw;
	bIsAiming = true;
}

// --- Montage Helpers ---

void UEnemyAnimInstance::PlayFireMontage(float PlayRate)
{
	if (!IsValid(FireMontage)) return;
	if (Montage_IsPlaying(FireMontage)) return;
	Montage_Play(FireMontage, PlayRate);

	if (FireMontage->GetSectionIndex(FireMontageLoopSection) != INDEX_NONE)
		Montage_SetNextSection(FireMontageLoopSection, FireMontageLoopSection, FireMontage);
	else
		UE_LOG(LogTemp, Warning, TEXT("EnemyAnimInstance: FireMontage loop section '%s' not found — montage won't loop"), *FireMontageLoopSection.ToString());
}

void UEnemyAnimInstance::StopFireMontage(float BlendOutTime)
{
	if (IsValid(FireMontage))
		Montage_Stop(BlendOutTime, FireMontage);
}

void UEnemyAnimInstance::PlayReloadMontage(float PlayRate)
{
	if (!IsValid(ReloadMontage)) return;
	if (Montage_IsPlaying(ReloadMontage)) return;
	Montage_Play(ReloadMontage, PlayRate);
}

void UEnemyAnimInstance::PlayHitReactMontage(float PlayRate)
{
	if (!IsValid(HitReactMontage)) return;
	Montage_Play(HitReactMontage, PlayRate);
}

void UEnemyAnimInstance::PlayDeathMontage(float PlayRate)
{
	if (!IsValid(DeathMontage)) return;
	Montage_Play(DeathMontage, PlayRate);
}

void UEnemyAnimInstance::PlayMeleeMontage(float PlayRate)
{
	if (!IsValid(MeleeMontage)) return;
	Montage_Play(MeleeMontage, PlayRate);
}

// --- Delegate Handlers ---

void UEnemyAnimInstance::HandleHitReact(EHitRegion Region)
{
	PlayHitReactMontage();
}

void UEnemyAnimInstance::HandleMeleePerformed()
{
	PlayMeleeMontage();
}

void UEnemyAnimInstance::HandleTakedown(AActor* Instigator)
{
	if (IsValid(TakedownReactionMontage))
		Montage_Play(TakedownReactionMontage);
	else
		PlayDeathMontage();
}
