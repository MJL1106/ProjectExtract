// Anim instance for enemy characters — locomotion, aim offset, combat montages, and delegate-driven reactions.

#include "EnemyAnimInstance.h"
#include "EnemyCharacter.h"
#include "EnemyAIController.h"
#include "EnemyAwarenessComponent.h"
#include "EnemyTypes.h"
#include "WeaponBase.h"
#include "WeaponDataAsset.h"
#include "SuppressionComponent.h"
#include "HealthComponent.h"
#include "Animation/AnimMontage.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "KismetAnimationLibrary.h"
#include "Components/SkeletalMeshComponent.h"

// ---------------------------------------------------------------------------
// Helpers — cached awareness resolve

static UEnemyAwarenessComponent* ResolveAwarenessComponent(const AEnemyCharacter* Enemy)
{
	if (!IsValid(Enemy)) return nullptr;
	AEnemyAIController* AIC = Cast<AEnemyAIController>(Enemy->GetController());
	if (!IsValid(AIC)) return nullptr;
	return AIC->GetAwarenessComponent();
}

// ---------------------------------------------------------------------------

void UEnemyAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();

	bPrevIsFiring = false;
	bPrevIsReloading = false;
	FireAlignAlpha = 0.f;
	bFireAlignSetup = false;
	bGripSocketValid = false;
	CachedGripMesh.Reset();
	CachedGripSocketName = NAME_None;

	APawn* PawnOwner = TryGetPawnOwner();
	if (!IsValid(PawnOwner)) return;

	OwningEnemy = Cast<AEnemyCharacter>(PawnOwner);
	if (!IsValid(OwningEnemy)) return;

	MovementComponent = OwningEnemy->GetCharacterMovement();
	HealthComponent = OwningEnemy->GetHealthComponent();

	// Cache the awareness component once — the controller is set by now at init.
	CachedAwarenessComponent = ResolveAwarenessComponent(OwningEnemy.Get());

	OwningEnemy->OnHitReact.RemoveDynamic(this, &UEnemyAnimInstance::HandleHitReact);
	OwningEnemy->OnMeleePerformed.RemoveDynamic(this, &UEnemyAnimInstance::HandleMeleePerformed);
	OwningEnemy->OnTakedownExecuted.RemoveDynamic(this, &UEnemyAnimInstance::HandleTakedown);
	OwningEnemy->OnGrenadeThrow.RemoveDynamic(this, &UEnemyAnimInstance::HandleGrenadeThrow);

	OwningEnemy->OnHitReact.AddDynamic(this, &UEnemyAnimInstance::HandleHitReact);
	OwningEnemy->OnMeleePerformed.AddDynamic(this, &UEnemyAnimInstance::HandleMeleePerformed);
	OwningEnemy->OnTakedownExecuted.AddDynamic(this, &UEnemyAnimInstance::HandleTakedown);
	OwningEnemy->OnGrenadeThrow.AddDynamic(this, &UEnemyAnimInstance::HandleGrenadeThrow);
}

void UEnemyAnimInstance::NativeUninitializeAnimation()
{
	if (BoundFireWeapon.IsValid())
	{
		BoundFireWeapon->OnWeaponFired.RemoveDynamic(this, &UEnemyAnimInstance::HandleWeaponFired);
		BoundFireWeapon.Reset();
	}

	if (IsValid(OwningEnemy))
	{
		OwningEnemy->OnHitReact.RemoveDynamic(this, &UEnemyAnimInstance::HandleHitReact);
		OwningEnemy->OnMeleePerformed.RemoveDynamic(this, &UEnemyAnimInstance::HandleMeleePerformed);
		OwningEnemy->OnTakedownExecuted.RemoveDynamic(this, &UEnemyAnimInstance::HandleTakedown);
		OwningEnemy->OnGrenadeThrow.RemoveDynamic(this, &UEnemyAnimInstance::HandleGrenadeThrow);
	}

	CachedAwarenessComponent.Reset();
	CachedGripMesh.Reset();

	Super::NativeUninitializeAnimation();
}

void UEnemyAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	if (!IsValid(OwningEnemy) || !IsValid(MovementComponent)) return;

	const FRotator ActorRot = OwningEnemy->GetActorRotation();

	// --- Locomotion (always updated — drives the ragdoll blend too) ---

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

	// Dead: zero combat/IK signals and skip the rest. Locomotion is left running so the
	// death-montage→ragdoll blend in the ABP has correct speed/direction inputs.
	if (!bIsAlive)
	{
		bIsFiring = false;
		bIsReloading = false;
		bInCombat = false;
		bIsPatrolling = false;
		bIsAiming = false;
		bIsSuppressed = false;
		bHasLeftHandIK = false;
		LeftHandIKTarget = FTransform::Identity;
		bPrevIsFiring = false;
		bPrevIsReloading = false;
		return;
	}

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

	// --- Weapon state ---

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

	// Lazy bind/rebind per-shot delegate and IK socket cache — fires only when the weapon changes.
	if (Weapon != BoundFireWeapon.Get())
	{
		if (BoundFireWeapon.IsValid())
			BoundFireWeapon->OnWeaponFired.RemoveDynamic(this, &UEnemyAnimInstance::HandleWeaponFired);

		if (IsValid(Weapon))
			Weapon->OnWeaponFired.AddDynamic(this, &UEnemyAnimInstance::HandleWeaponFired);

		BoundFireWeapon = Weapon;

		// Resolve weapon animation type once on equip.
		WeaponAnimType = EEnemyWeaponAnimType::Rifle;

		// Resolve IK socket validity once on equip — avoids per-frame DoesSocketExist.
		bGripSocketValid = false;
		CachedGripMesh.Reset();
		CachedGripSocketName = NAME_None;

		if (IsValid(Weapon))
		{
			if (const UWeaponDataAsset* DA = Weapon->GetWeaponData())
			{
				WeaponAnimType = DA->EnemyWeaponAnimType;
				const FName SocketName = DA->LeftHandGripSocket;
				if (!SocketName.IsNone())
				{
					// Query the VISIBLE mesh (ThirdPersonGripMesh), not the hidden frame mesh.
					if (USkeletalMeshComponent* GripMesh = Weapon->GetThirdPersonGripMesh())
					{
						if (GripMesh->DoesSocketExist(SocketName))
						{
							bGripSocketValid = true;
							CachedGripMesh = GripMesh;
							CachedGripSocketName = SocketName;
						}
					}
				}
			}
		}

		// Fire-align setup: compute rest→fire offset once on equip so per-frame cost is just a lerp.
		bFireAlignSetup = false;
		FireAlignAlpha = 0.f;
		if (IsValid(Weapon) && !FireAlignSocketName.IsNone())
		{
			Weapon->SetupFireAlign(GetOwningComponent(), FireAlignSocketName);
			bFireAlignSetup = true;
		}
	}

	// --- Awareness (cached — no per-frame controller cast) ---

	if (!CachedAwarenessComponent.IsValid())
		CachedAwarenessComponent = ResolveAwarenessComponent(OwningEnemy.Get());

	bInCombat = CachedAwarenessComponent.IsValid()
		&& (CachedAwarenessComponent->GetAwarenessState() == EEnemyAwarenessState::Combat);

	bIsPatrolling = !bInCombat && !bIsAiming;

	// --- Suppression ---

	if (USuppressionComponent* Suppression = OwningEnemy->GetSuppressionComponent())
		bIsSuppressed = Suppression->IsSuppressed();
	else
		bIsSuppressed = false;

	// --- Left-Hand IK (socket transform — cheap once validity is cached) ---

	if (bGripSocketValid && CachedGripMesh.IsValid())
	{
		LeftHandIKTarget = CachedGripMesh->GetSocketTransform(CachedGripSocketName, RTS_World);
		bHasLeftHandIK = true;
	}
	else
	{
		LeftHandIKTarget = FTransform::Identity;
		bHasLeftHandIK = false;
	}

	// --- Auto-trigger: fire / reload montages on state transitions ---

	if (bIsFiring && !bPrevIsFiring)
		PlayFireMontage();
	else if (!bIsFiring && bPrevIsFiring)
		StopFireMontage();
	bPrevIsFiring = bIsFiring;

	if (bIsReloading && !bPrevIsReloading)
		PlayReloadMontage();
	bPrevIsReloading = bIsReloading;

	// --- Fire-align: smoothly blend weapon offset while the fire-loop montage plays ---
	// Alpha drives SetFireAlignAlpha per-frame via FInterpTo so it eases in/out with the montage
	// blend rather than popping. Skipped entirely when not set up (FireAlignSocketName is None or
	// weapon lacks the required sockets) and when alpha is already settled at 0 while not firing.

	if (bFireAlignSetup && IsValid(Weapon))
	{
		UAnimMontage* FireLoopMontage = GetEffectiveFireLoopMontage();
		const bool bIsFireMontagePlaying = IsValid(FireLoopMontage) && Montage_IsPlaying(FireLoopMontage);
		const float AlphaTarget = bIsFireMontagePlaying ? 1.f : 0.f;

		const float PrevAlpha = FireAlignAlpha;
		FireAlignAlpha = FMath::FInterpTo(FireAlignAlpha, AlphaTarget, DeltaSeconds, FireAlignBlendSpeed);

		// Only push a new transform when the alpha is moving or non-zero — avoids per-frame
		// SetRelativeTransform calls while the weapon is fully at rest.
		const bool bAlphaSettled = FMath::IsNearlyEqual(FireAlignAlpha, PrevAlpha, KINDA_SMALL_NUMBER);
		if (!bAlphaSettled || FireAlignAlpha > KINDA_SMALL_NUMBER)
			Weapon->SetFireAlignAlpha(FireAlignAlpha);
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

// --- Resolved-set helpers ---
// Per-weapon DA anim set takes priority; ABP-level single field is the fallback.

UAnimMontage* UEnemyAnimInstance::GetEffectiveFireLoopMontage() const
{
	if (BoundFireWeapon.IsValid())
		if (const UWeaponDataAsset* DA = BoundFireWeapon->GetWeaponData())
			if (IsValid(DA->EnemyAnimSet.FireLoop)) return DA->EnemyAnimSet.FireLoop.Get();
	return FireMontage.Get();
}

UAnimMontage* UEnemyAnimInstance::GetEffectiveFireSingleMontage() const
{
	if (BoundFireWeapon.IsValid())
		if (const UWeaponDataAsset* DA = BoundFireWeapon->GetWeaponData())
			if (IsValid(DA->EnemyAnimSet.FireSingle)) return DA->EnemyAnimSet.FireSingle.Get();
	return SingleFireMontage.Get();
}

UAnimMontage* UEnemyAnimInstance::GetEffectiveReloadMontage() const
{
	if (BoundFireWeapon.IsValid())
		if (const UWeaponDataAsset* DA = BoundFireWeapon->GetWeaponData())
			if (IsValid(DA->EnemyAnimSet.Reload)) return DA->EnemyAnimSet.Reload.Get();
	return ReloadMontage.Get();
}

UAnimMontage* UEnemyAnimInstance::GetEffectiveMeleeMontage() const
{
	if (BoundFireWeapon.IsValid())
		if (const UWeaponDataAsset* DA = BoundFireWeapon->GetWeaponData())
			if (IsValid(DA->EnemyAnimSet.Melee)) return DA->EnemyAnimSet.Melee.Get();
	return MeleeMontage.Get();
}

UAnimMontage* UEnemyAnimInstance::GetEffectiveGrenadeMontage() const
{
	if (BoundFireWeapon.IsValid())
		if (const UWeaponDataAsset* DA = BoundFireWeapon->GetWeaponData())
			if (IsValid(DA->EnemyAnimSet.Grenade)) return DA->EnemyAnimSet.Grenade.Get();
	return GrenadeMontage.Get();
}

UAnimMontage* UEnemyAnimInstance::GetEffectiveDeathMontage() const
{
	if (BoundFireWeapon.IsValid())
		if (const UWeaponDataAsset* DA = BoundFireWeapon->GetWeaponData())
			if (IsValid(DA->EnemyAnimSet.Death)) return DA->EnemyAnimSet.Death.Get();
	return DeathMontage.Get();
}

UAnimMontage* UEnemyAnimInstance::GetEffectiveHitReactFlinchMontage() const
{
	if (BoundFireWeapon.IsValid())
		if (const UWeaponDataAsset* DA = BoundFireWeapon->GetWeaponData())
			if (IsValid(DA->EnemyAnimSet.HitReactFlinch)) return DA->EnemyAnimSet.HitReactFlinch.Get();
	return nullptr; // no ABP-level fallback for the flinch slot — it is intentionally new
}

// --- Montage Helpers ---

void UEnemyAnimInstance::PlayFireMontage(float PlayRate)
{
	UAnimMontage* Montage = GetEffectiveFireLoopMontage();
	if (!IsValid(Montage)) return;
	if (Montage_IsPlaying(Montage)) return;
	Montage_Play(Montage, PlayRate);

	if (Montage->GetSectionIndex(FireMontageLoopSection) != INDEX_NONE)
		Montage_SetNextSection(FireMontageLoopSection, FireMontageLoopSection, Montage);
	else
		UE_LOG(LogTemp, Warning, TEXT("EnemyAnimInstance: FireMontage loop section '%s' not found — montage won't loop"), *FireMontageLoopSection.ToString());
}

void UEnemyAnimInstance::StopFireMontage(float BlendOutTime)
{
	UAnimMontage* Montage = GetEffectiveFireLoopMontage();
	if (IsValid(Montage))
		Montage_Stop(BlendOutTime, Montage);
}

void UEnemyAnimInstance::PlayReloadMontage(float PlayRate)
{
	UAnimMontage* Montage = GetEffectiveReloadMontage();
	if (!IsValid(Montage)) return;
	if (Montage_IsPlaying(Montage)) return;

	// Scale playback rate so the montage covers the weapon's reload window.
	// When the DA has a per-weapon set with an authored-duration montage, PlayRate stays near 1.
	// When the fallback AR montage is used on a slow-reload weapon (LMG/Sniper), this stretches
	// it to fill the gap rather than leaving dead time after the montage ends.
	float EffectiveRate = PlayRate;
	if (BoundFireWeapon.IsValid())
	{
		if (const UWeaponDataAsset* DA = BoundFireWeapon->GetWeaponData())
		{
			const float ReloadTime = DA->ReloadTime;
			const float MontageLength = Montage->GetPlayLength();
			if (ReloadTime > 0.f && MontageLength > 0.f)
				EffectiveRate = FMath::Clamp(MontageLength / ReloadTime, 0.5f, 2.0f);
		}
	}

	Montage_Play(Montage, EffectiveRate);

	if (BoundFireWeapon.IsValid())
		BoundFireWeapon->PlayVisualWeaponReload(EffectiveRate);
}

void UEnemyAnimInstance::PlayHitReactMontage(float PlayRate)
{
	// Suppress full-body hit react during active combat so repeated hits don't stomp firing/aiming.
	// The additive flinch path (PlayHitReactFlinch) remains ungated.
	if (bIsFiring || bIsAiming || bInCombat) return;
	if (!IsValid(HitReactMontage)) return;
	Montage_Play(HitReactMontage, PlayRate);
}

void UEnemyAnimInstance::PlayHitReactFlinch(float PlayRate)
{
	UAnimMontage* Montage = GetEffectiveHitReactFlinchMontage();
	if (!IsValid(Montage)) return;
	if (Montage_IsPlaying(Montage)) return;
	Montage_Play(Montage, PlayRate);
}

void UEnemyAnimInstance::PlayDeathMontage(float PlayRate)
{
	UAnimMontage* Montage = GetEffectiveDeathMontage();
	if (!IsValid(Montage)) return;
	Montage_Play(Montage, PlayRate);
}

void UEnemyAnimInstance::PlayMeleeMontage(float PlayRate)
{
	UAnimMontage* Montage = GetEffectiveMeleeMontage();
	if (!IsValid(Montage)) return;
	Montage_Play(Montage, PlayRate);
}

void UEnemyAnimInstance::PlayGrenadeMontage(float PlayRate)
{
	UAnimMontage* Montage = GetEffectiveGrenadeMontage();
	if (!IsValid(Montage)) return;
	if (Montage_IsPlaying(Montage)) return;
	Montage_Play(Montage, PlayRate);
}

void UEnemyAnimInstance::StopGrenadeMontage(float BlendOutTime)
{
	UAnimMontage* Montage = GetEffectiveGrenadeMontage();
	if (IsValid(Montage))
		Montage_Stop(BlendOutTime, Montage);
}

// --- Delegate Handlers ---

void UEnemyAnimInstance::HandleWeaponFired()
{
	if (!bIsAlive) return;

	UAnimMontage* LoopMontage = GetEffectiveFireLoopMontage();

	// Sustained/auto fire already shows the loop montage — don't double up.
	if (IsValid(LoopMontage) && Montage_IsPlaying(LoopMontage)) return;

	UAnimMontage* SingleMontage = GetEffectiveFireSingleMontage();
	if (IsValid(SingleMontage) && !Montage_IsPlaying(SingleMontage))
		Montage_Play(SingleMontage);
}

void UEnemyAnimInstance::HandleHitReact(EHitRegion Region)
{
	// Always attempt the additive flinch — not gated by combat state.
	PlayHitReactFlinch();

	// Full-body react still available for non-combat hits (gate unchanged).
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

void UEnemyAnimInstance::HandleGrenadeThrow(FVector PredictedLanding, float TimeToImpact)
{
	PlayGrenadeMontage();
}
