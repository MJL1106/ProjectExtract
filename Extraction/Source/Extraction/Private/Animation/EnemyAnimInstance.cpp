// Anim instance for enemy characters — locomotion, aim offset, combat montages, and delegate-driven reactions.

#include "EnemyAnimInstance.h"
#include "EnemyCharacter.h"
#include "EnemyArchetypeData.h"
#include "EnemyAIController.h"
#include "EnemyAwarenessComponent.h"
#include "EnemyTypes.h"
#include "WeaponBase.h"
#include "WeaponDataAsset.h"
#include "SuppressionComponent.h"
#include "HealthComponent.h"
#include "AI/Cover/CoverPoseComponent.h"
#include "AlphaBlend.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "KismetAnimationLibrary.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "EnemyDebug.h"

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
	bWasAlive = true;
	bHasRecoilProfile = false;
	RecoilTargetRot = FRotator::ZeroRotator;
	RecoilCurrentRot = FRotator::ZeroRotator;
	RecoilTargetKickback = 0.f;
	RecoilCurrentKickback = 0.f;
	RecoilSpineRotation = FRotator::ZeroRotator;
	RecoilSpineOffset = FVector::ZeroVector;
	FireAlignAlpha = 0.f;
	bFireAlignSetup = false;
	MeleeMontageWeight = 0.f;
	bMeleeAlignSetup = false;
	PatrolAlignAlpha = 0.f;
	bPatrolAlignSetup = false;
	HandSwapAlpha = 0.f;
	bWantPatrolHand = false;
	bHandSwapEnabled = false;
	bGripSocketValid = false;
	CachedGripMesh.Reset();
	CachedGripSocketName = NAME_None;
	bRepertoireRolled = false;
	RolledRepertoire.Empty();
	ActivePatrolIdleMontage = nullptr;

	// Cover animation state
	ActiveCoverMontage = nullptr;
	ActiveCoverBlindMontage = nullptr;
	bPrevCoverPeeking = false;
	bPrevCoverBlindFiring = false;
	bPrevCoverAnimActive = false;
	PrevCoverHeight = ECoverHeight::Stand;
	LastCoverSide = DefaultCoverSide;
	CoverSettleAccum = 0.f;
	CoverAimGate = 1.f;
	GripPoseAlpha = 1.f;

	APawn* PawnOwner = TryGetPawnOwner();
	if (!IsValid(PawnOwner)) return;

	OwningEnemy = Cast<AEnemyCharacter>(PawnOwner);
	if (!IsValid(OwningEnemy)) return;

	MovementComponent = OwningEnemy->GetCharacterMovement();
	HealthComponent = OwningEnemy->GetHealthComponent();

	// Cache the awareness component once — the controller is set by now at init.
	CachedAwarenessComponent = ResolveAwarenessComponent(OwningEnemy.Get());

	// Cache the cover pose component once — default subobject, always present.
	CachedCoverPoseComponent = OwningEnemy->GetCoverPoseComponent();

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
	CachedCoverPoseComponent.Reset();
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

	// On the alive→dead transition, zero the spring solver and reset the weapon offset
	// so no frozen kick pose bleeds into the ragdoll. Must happen before the early-return below.
	if (!bIsAlive && bWasAlive)
	{
		RecoilTargetRot = FRotator::ZeroRotator;
		RecoilCurrentRot = FRotator::ZeroRotator;
		RecoilTargetKickback = 0.f;
		RecoilCurrentKickback = 0.f;
		RecoilSpineRotation = FRotator::ZeroRotator;
		RecoilSpineOffset = FVector::ZeroVector;
	}
	bWasAlive = bIsAlive;

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
		bInCover = false;
		CoverHeight = ECoverHeight::Stand;
		CoverLeanDirection = ECoverLean::None;
		bCoverBlindFiring = false;
		bCoverPeeking = false;
		bHasLeftHandIK = false;
		LeftHandIKTarget = FTransform::Identity;
		bPrevIsFiring = false;
		bPrevIsReloading = false;
		FireAlignAlpha = 0.f;
		PatrolAlignAlpha = 0.f;
		HandSwapAlpha = 0.f;
		StopPatrolIdle();
		StopCoverMontages(CoverBlendOut);
		ActiveCoverMontage = nullptr;
		ActiveCoverBlindMontage = nullptr;
		bPrevCoverAnimActive = false;
		CoverSettleAccum = 0.f;
		bPrevCoverPeeking = false;
		bPrevCoverBlindFiring = false;
		CoverAimGate = 1.f;
		GripPoseAlpha = 1.f;
		return;
	}

	// --- Cover Pose (mirror from UCoverPoseComponent — trivial copies, no traces) ---
	// Runs BEFORE the aim gate below so the gate reads current-frame cover flags.

	if (!CachedCoverPoseComponent.IsValid())
		CachedCoverPoseComponent = OwningEnemy->GetCoverPoseComponent();

	if (CachedCoverPoseComponent.IsValid())
	{
		bInCover = CachedCoverPoseComponent->bInCover;
		CoverHeight = CachedCoverPoseComponent->CoverHeight;
		CoverLeanDirection = CachedCoverPoseComponent->LeanDirection;
		bCoverBlindFiring = CachedCoverPoseComponent->bBlindFiring;
		bCoverPeeking = CachedCoverPoseComponent->bPeeking;
	}
	else
	{
		bInCover = false;
		CoverHeight = ECoverHeight::Stand;
		CoverLeanDirection = ECoverLean::None;
		bCoverBlindFiring = false;
		bCoverPeeking = false;
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

	// --- Cover aim gate: ease AimPitch/AimYaw to zero when tucked or blind-firing ---
	{
		const bool bGateOff = (bInCover && !bCoverPeeking) || bCoverBlindFiring;
		const float GateTarget = bGateOff ? 0.f : 1.f;
		CoverAimGate = FMath::FInterpTo(CoverAimGate, GateTarget, DeltaSeconds, CoverAimGateSpeed);
		AimPitch *= CoverAimGate;
		AimYaw *= CoverAimGate;

		// In cover the peek montage owns the body rotation — the aim offset only fine-aims.
		// Exception: over-top stand peek (Front lean, standing, peeking) tracks the player freely.
		if (bInCover)
		{
			const bool bOverTopPeek = bCoverPeeking
				&& CoverLeanDirection == ECoverLean::Front
				&& CoverHeight == ECoverHeight::Stand;
			const float YawLimit = bOverTopPeek ? AimYawClampDeg : CoverAimYawClampDeg;
			AimYaw = FMath::Clamp(AimYaw, -YawLimit, YawLimit);
		}
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

		// Reset repertoire so the next patrol stop re-rolls from the correct pool for the new weapon.
		bRepertoireRolled = false;
		RolledRepertoire.Empty();

		// Resolve IK socket validity once on equip — avoids per-frame DoesSocketExist.
		bGripSocketValid = false;
		CachedGripMesh.Reset();
		CachedGripSocketName = NAME_None;

		if (IsValid(Weapon))
		{
			if (const UWeaponDataAsset* DA = Weapon->GetWeaponData())
			{
				WeaponAnimType = DA->EnemyWeaponAnimType;

				// BoundFireWeapon is valid here — roll the repertoire immediately so
				// the first patrol stop draws from the correct pool (pistol vs general).
				RollRepertoireIfNeeded();

				// Copy the inline profile and zero the spring state for the new weapon.
				RecoilProfile = DA->EnemyRecoilProfile;
				bHasRecoilProfile = true;
				RecoilTargetRot = FRotator::ZeroRotator;
				RecoilCurrentRot = FRotator::ZeroRotator;
				RecoilTargetKickback = 0.f;
				RecoilCurrentKickback = 0.f;
				RecoilSpineRotation = FRotator::ZeroRotator;
				RecoilSpineOffset = FVector::ZeroVector;

				// Cache hand-swap enablement from the DA. Validate configured sockets
				// exist on the enemy mesh at equip time — if the patrol socket is absent,
				// disable the feature entirely rather than retrying + logging every frame.
				bHandSwapEnabled = false;
				if (!DA->EnemyPatrolHandSocket.IsNone())
				{
					USkeletalMeshComponent* EnemyMesh = OwningEnemy->GetMesh();
					if (IsValid(EnemyMesh) && EnemyMesh->DoesSocketExist(DA->EnemyPatrolHandSocket))
					{
						const FName CombatSock = DA->EnemyCombatHandSocket.IsNone()
							? OwningEnemy->WeaponSocket : DA->EnemyCombatHandSocket;
						if (EnemyMesh->DoesSocketExist(CombatSock))
							bHandSwapEnabled = true;
					}
				}
				if (!bHandSwapEnabled)
				{
					HandSwapAlpha = 0.f;
					bWantPatrolHand = false;
				}

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
		else
		{
			bHasRecoilProfile = false;
			RecoilTargetRot = FRotator::ZeroRotator;
			RecoilCurrentRot = FRotator::ZeroRotator;
			RecoilTargetKickback = 0.f;
			RecoilCurrentKickback = 0.f;
			RecoilSpineRotation = FRotator::ZeroRotator;
			RecoilSpineOffset = FVector::ZeroVector;
			bHandSwapEnabled = false;
			HandSwapAlpha = 0.f;
			bWantPatrolHand = false;
		}

		// Fire-align setup: compute rest→fire offset once on equip so per-frame cost is just a lerp.
		bFireAlignSetup = false;
		FireAlignAlpha = 0.f;
		if (IsValid(Weapon) && !FireAlignSocketName.IsNone())
		{
			Weapon->SetupFireAlign(GetOwningComponent(), FireAlignSocketName);
			bFireAlignSetup = true;
		}

		// Melee-align setup: compute rest→melee offset once on equip, mirroring fire-align.
		bMeleeAlignSetup = false;
		MeleeMontageWeight = 0.f;
		if (IsValid(Weapon) && !MeleeAlignSocketName.IsNone())
		{
			Weapon->SetupMeleeAlign(GetOwningComponent(), MeleeAlignSocketName);
			// Reflect actual readiness — when the socket isn't authored yet the per-frame block stays dormant.
			bMeleeAlignSetup = Weapon->IsMeleeAlignReady();
		}

		// Patrol-align setup: cache the DA-driven offset once on equip, mirroring fire/melee-align.
		bPatrolAlignSetup = false;
		PatrolAlignAlpha = 0.f;
		if (IsValid(Weapon))
		{
			Weapon->SetupPatrolAlign();
			bPatrolAlignSetup = Weapon->IsPatrolAlignReady();
		}

		// Cover-align setup: compose the per-scenario ABP-tuned socket poses once on equip.
		bCoverAlignSetup = false;
		if (IsValid(Weapon))
		{
			Weapon->SetupCoverAlign(GetOwningComponent(), CoverAlignBoneName, CoverAlignIdleTransform,
				CoverAlignOverTopTransform, CoverAlignPeekLeftTransform, CoverAlignPeekRightTransform);
			bCoverAlignSetup = Weapon->IsCoverAlignReady();
		}
	}

	// --- Awareness (cached — no per-frame controller cast) ---

	if (!CachedAwarenessComponent.IsValid())
		CachedAwarenessComponent = ResolveAwarenessComponent(OwningEnemy.Get());

	bInCombat = CachedAwarenessComponent.IsValid()
		&& (CachedAwarenessComponent->GetAwarenessState() == EEnemyAwarenessState::Combat);

	bIsPatrolling = !bInCombat && !bIsAiming;

	// Stop any playing patrol-idle the moment combat starts or the enemy moves beyond jitter
	// threshold, independent of the BT abort (covers the 1-frame perception/BT seam).
	// PatrolIdleMotionAbortSpeed guards against stationary physics jitter cutting idles short.
	static constexpr float PatrolIdleMotionAbortSpeed = 20.f;
	if (ActivePatrolIdleMontage && IsPlayingPatrolIdle() && (!bIsPatrolling || Speed > PatrolIdleMotionAbortSpeed))
		StopPatrolIdle();

	// --- Suppression ---

	if (USuppressionComponent* Suppression = OwningEnemy->GetSuppressionComponent())
		bIsSuppressed = Suppression->IsSuppressed();
	else
		bIsSuppressed = false;

	// --- Cover Animation (edge-detected montage playback) ---
	UpdateCoverAnimation(DeltaSeconds);

	// --- Left-Hand IK (socket transform — cheap once validity is cached) ---
	// Suppressed only while a blind-fire montage actually plays — v1 may ship with null blind
	// montages (graceful skip), and the grip arms must stay glued in that case.

	const bool bBlindMontagePlaying = IsValid(ActiveCoverBlindMontage) && Montage_IsPlaying(ActiveCoverBlindMontage);
	if (bGripSocketValid && CachedGripMesh.IsValid() && !bBlindMontagePlaying)
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

	// In cover the peek montage IS the fire pose (recoil spring + weapon-mesh bolt anim carry
	// the shot feedback) — the full-body fire loop sits downstream of the Cover slot and would
	// visually stomp the peek, dropping the body back behind the wall mid-burst.
	if (bIsFiring && !bPrevIsFiring && !bCoverBlindFiring && !bInCover)
		PlayFireMontage();
	else if (!bIsFiring && bPrevIsFiring)
		StopFireMontage();
	// Natural decay (RecoverySpeed) settles the recoil spring — no explicit stop needed.
	bPrevIsFiring = bIsFiring;

	if (bIsReloading && !bPrevIsReloading)
	{
		if (IsReloadDebugEnabled())
		{
			const FString EnemyName = IsValid(OwningEnemy) ? OwningEnemy->GetName() : TEXT("?");
			UE_LOG(LogTemp, Warning, TEXT("[RELOADDBG] %s reload detected -> PlayReloadMontage"), *EnemyName);
			if (GEngine)
				GEngine->AddOnScreenDebugMessage(
					static_cast<uint64>(GetTypeHash(FString::Printf(TEXT("ReloadTrig_%s"), *EnemyName))), 4.f, FColor::Cyan,
					FString::Printf(TEXT("[RELOADDBG] %s reload detected"), *EnemyName));
		}
		PlayReloadMontage();
	}
	bPrevIsReloading = bIsReloading;

	// --- Patrol-align: smoothly blend weapon to relaxed carry while patrolling ---
	// Runs BEFORE fire-align and melee-align so combat overrides take priority on transition frames
	// (all three are last-writer-wins on WeaponMesh->SetRelativeTransform; patrol eases to 0 as
	// fire/melee ease to 1, but on a single frame where both are non-zero the combat writer wins).
	if (bPatrolAlignSetup && IsValid(Weapon))
	{
		const float AlphaTarget = bIsPatrolling ? 1.f : 0.f;
		const float PrevAlpha = PatrolAlignAlpha;
		PatrolAlignAlpha = FMath::FInterpTo(PatrolAlignAlpha, AlphaTarget, DeltaSeconds, PatrolAlignBlendSpeed);

		const bool bAlphaSettled = FMath::IsNearlyEqual(PatrolAlignAlpha, PrevAlpha, KINDA_SMALL_NUMBER);
		if (!bAlphaSettled || PatrolAlignAlpha > KINDA_SMALL_NUMBER)
			Weapon->SetPatrolAlignAlpha(PatrolAlignAlpha);
	}

	// --- Hand-swap: ease the weapon between patrol and combat hand sockets ---
	// Runs AFTER patrol-align and BEFORE fire/melee align. The settle writes
	// WeaponMesh->SetRelativeTransform to ease from the KeepWorldTransform residual to identity
	// (seated on the new socket). Fire/melee/recoil align are dormant while patrolling, and
	// during the combat transition they overwrite the settle once they activate. The settle
	// defers to any active fire/melee/recoil align by stopping when it detects those are writing.
	// NativeUpdateAnimation runs on server/listen-host, which is the authority for this
	// single-player project. SetWeaponHandSocket early-returns off-authority.
	if (bHandSwapEnabled && IsValid(Weapon) && IsValid(OwningEnemy))
	{
		// Hard override: if firing has started, force the weapon to the combat hand immediately.
		// The gun must never fire from the patrol hand — snap it back before the first shot's
		// fire-align and hitscan run. Bypass the eased hysteresis entirely.
		if (bIsFiring && bWantPatrolHand)
		{
			bWantPatrolHand = false;
			HandSwapAlpha = 0.f;
			OwningEnemy->SetWeaponHandSocket(false, /*bImmediate=*/true);
		}

		const float SwapTarget = bIsPatrolling ? 1.f : 0.f;
		HandSwapAlpha = FMath::FInterpTo(HandSwapAlpha, SwapTarget, DeltaSeconds, HandSwapBlendSpeed);

		// Hysteresis: only commit the socket swap when the alpha crosses a threshold,
		// preventing flicker from the bIsAiming toggle that drives bIsPatrolling.
		const bool bPrevWantPatrol = bWantPatrolHand;
		if (!bWantPatrolHand && HandSwapAlpha > HandSwapRiseThreshold)
			bWantPatrolHand = true;
		else if (bWantPatrolHand && HandSwapAlpha < HandSwapFallThreshold)
			bWantPatrolHand = false;

		if (bWantPatrolHand != bPrevWantPatrol)
			OwningEnemy->SetWeaponHandSocket(bWantPatrolHand);

		// Drive the settle interpolation. Skip when fire/melee/cover align are actively writing
		// (those have priority and would fight the settle).
		const bool bCombatAlignActive = (FireAlignAlpha > KINDA_SMALL_NUMBER) || (MeleeMontageWeight > KINDA_SMALL_NUMBER)
			|| Weapon->IsCoverAlignWriting();
		if (!bCombatAlignActive)
			Weapon->UpdateHandSwapSettle(DeltaSeconds);
	}

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

	// --- Melee montage weight (unconditional) + melee-align (gated on socket setup) ---
	// MeleeMontageWeight eases toward 1 while the melee montage plays and back to 0 when it ends.
	// Exposed BlueprintReadOnly so the ABP can fade the aim-offset out during the swing
	// (AimAlpha = 1 - MeleeMontageWeight). The weapon melee-align write stays gated on a valid
	// align socket — archetypes without the socket still get the weight for the aim gate.
	{
		UAnimMontage* EffMeleeMontage = GetEffectiveMeleeMontage();
		const bool bIsMeleePlaying = IsValid(EffMeleeMontage) && Montage_IsPlaying(EffMeleeMontage);
		MeleeMontageWeight = FMath::FInterpTo(MeleeMontageWeight, bIsMeleePlaying ? 1.f : 0.f, DeltaSeconds, MeleeAlignBlendSpeed);

		if (bMeleeAlignSetup && IsValid(Weapon) && (bIsMeleePlaying || MeleeMontageWeight > KINDA_SMALL_NUMBER))
			Weapon->SetMeleeAlignAlpha(MeleeMontageWeight);
	}

	// --- Cover-align: ease the weapon to the per-scenario cover socket while posed ---
	// Runs AFTER fire/melee align so its write wins the frame while in cover (last-writer-wins);
	// out of cover it eases home then goes dormant (stops writing entirely), handing the weapon
	// back to patrol/fire/melee align. Scenario pick mirrors SelectCoverPeekMontage.
	if (bCoverAlignSetup && IsValid(Weapon))
	{
		ECoverWeaponAlign Scenario = ECoverWeaponAlign::None;
		if (bInCover)
		{
			if (bCoverPeeking)
			{
				if (CoverLeanDirection == ECoverLean::Front)
				{
					Scenario = ECoverWeaponAlign::OverTop;
				}
				else
				{
					ECoverLean Side = CoverLeanDirection;
					if (Side != ECoverLean::Left && Side != ECoverLean::Right) Side = LastCoverSide;
					Scenario = (Side == ECoverLean::Left) ? ECoverWeaponAlign::PeekLeft : ECoverWeaponAlign::PeekRight;
				}
			}
			else
			{
				Scenario = ECoverWeaponAlign::Idle;
			}
		}
		Weapon->UpdateCoverAlign(Scenario, DeltaSeconds, CoverAlignBlendSpeed);
	}

	// --- Recoil spring solver ---
	UpdateRecoilSolver(DeltaSeconds);

	// --- Grip pose alpha: ease to 0 during blind-fire or tucked idle without grip arms ---
	// Blind-fire suppression keys off the montage actually playing (bBlindMontagePlaying), not the
	// flag — with null blind montages the arms would otherwise detach with no spray anim.
	{
		const bool bTuckedIdle = bInCover && !bCoverPeeking && !bCoverBlindFiring;
		// Peek: the montage owns the arms. The grip layer is a mesh-space clavicle-down blend of a
		// STANDING ready idle — at alpha 1 it pins the gun to that fixed orientation, cancelling the
		// montage's aim-over-cover arm raise (gun points low/forward while the torso aims).
		const bool bWantGripOff = bBlindMontagePlaying
			|| (bInCover && bCoverPeeking)
			|| (bTuckedIdle && !bUseGripArmsInCoverIdle);
		const float GripTarget = bWantGripOff ? 0.f : 1.f;
		GripPoseAlpha = FMath::FInterpTo(GripPoseAlpha, GripTarget, DeltaSeconds, GripPoseBlendSpeed);
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

	// Clamp to the aim-offset asset's authored range — unclamped deltas (up to ±180 yaw while
	// the body is cover-aligned rather than target-facing) extrapolate the spine into a wrench.
	const FRotator Delta = (ToTarget.Rotation() - ActorRot).GetNormalized();
	AimPitch = FMath::Clamp(Delta.Pitch, -AimPitchClampDeg, AimPitchClampDeg);
	AimYaw = FMath::Clamp(Delta.Yaw, -AimYawClampDeg, AimYawClampDeg);
	bIsAiming = true;
}

// --- Patrol Idle ---

void UEnemyAnimInstance::RollRepertoireIfNeeded()
{
	if (bRepertoireRolled) return;
	// Defer without committing bRepertoireRolled so the roll retries on the next stop once
	// the weapon is bound and WeaponAnimType has resolved from the DA.
	if (!BoundFireWeapon.IsValid()) return;

	const TArray<TObjectPtr<UAnimSequence>>& SourcePool =
		(WeaponAnimType == EEnemyWeaponAnimType::Pistol) ? PistolPatrolIdlePool : GeneralPatrolIdlePool;

	// Compact nulls into a temp array.
	TArray<TObjectPtr<UAnimSequence>> ValidClips;
	ValidClips.Reserve(SourcePool.Num());
	for (const TObjectPtr<UAnimSequence>& Clip : SourcePool)
	{
		if (IsValid(Clip)) ValidClips.Add(Clip);
	}

	if (ValidClips.Num() <= PatrolIdleRepertoireSize)
	{
		RolledRepertoire = MoveTemp(ValidClips);
	}
	else
	{
		// Fisher-Yates partial shuffle — pick PatrolIdleRepertoireSize entries.
		for (int32 i = 0; i < PatrolIdleRepertoireSize; ++i)
		{
			const int32 j = FMath::RandRange(i, ValidClips.Num() - 1);
			ValidClips.Swap(i, j);
		}
		RolledRepertoire.Reset(PatrolIdleRepertoireSize);
		for (int32 i = 0; i < PatrolIdleRepertoireSize; ++i)
			RolledRepertoire.Add(ValidClips[i]);
	}

	bRepertoireRolled = true;
}

float UEnemyAnimInstance::PlayRandomPatrolIdle()
{
	RollRepertoireIfNeeded();
	if (RolledRepertoire.IsEmpty()) return 0.f;

	const int32 Pick = FMath::RandRange(0, RolledRepertoire.Num() - 1);
	UAnimSequence* Clip = RolledRepertoire[Pick].Get();
	if (!IsValid(Clip)) return 0.f;

	ActivePatrolIdleMontage = PlaySlotAnimationAsDynamicMontage(
		Clip, PatrolIdleSlotName, PatrolIdleBlendIn, PatrolIdleBlendOut, 1.f, 1);

	// Re-trigger the next idle as this clip begins blending out (length - blend-out) rather than at
	// its full length. The next PlayRandomPatrolIdle then starts while this one is still on the slot,
	// so the montage system crossfades pose A -> pose B directly instead of settling onto the base
	// relaxed pose in between (which read as a dwell + snap). Clamped so a very short clip can't
	// produce a zero/negative wait and machine-gun the re-trigger.
	const float ReTriggerTime = FMath::Max(Clip->GetPlayLength() - PatrolIdleBlendOut, 0.15f);
	return IsValid(ActivePatrolIdleMontage) ? ReTriggerTime : 0.f;
}

void UEnemyAnimInstance::StopPatrolIdle(float BlendOutTime)
{
	if (!IsValid(ActivePatrolIdleMontage)) return;
	if (!Montage_IsPlaying(ActivePatrolIdleMontage)) return;
	Montage_Stop(BlendOutTime, ActivePatrolIdleMontage);
}

bool UEnemyAnimInstance::IsPlayingPatrolIdle() const
{
	if (!IsValid(ActivePatrolIdleMontage)) return false;
	return Montage_IsPlaying(ActivePatrolIdleMontage);
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
	// The ABP-level SingleFireMontage is the AK/Companion-tuned fallback — only pose-correct for Rifle
	// weapons. Non-rifle weapons (e.g. the Mk14 sniper) must supply their own via the DA; otherwise the
	// AK montage swings the arms off the weapon-specific grip pose, jolting per shot. Return null instead.
	return (WeaponAnimType == EEnemyWeaponAnimType::Rifle) ? SingleFireMontage.Get() : nullptr;
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
	// bStopAllMontages=false: DefaultGroup fire must not kill the group-Cover pose montage.
	Montage_Play(Montage, PlayRate, EMontagePlayReturnType::MontageLength, 0.f, false);

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

	// bStopAllMontages=false: reload must not kill the group-Cover pose montage.
	// NB: in cover this full-body reload (DefaultSlot, downstream of CoverSlot) visually overrides
	// the tucked pose for its duration — accepted; a body-less reload read worse (gun reloading
	// itself). A cover-authored reload montage is the eventual fix if the twist bothers.
	const float PlayResult = Montage_Play(Montage, EffectiveRate, EMontagePlayReturnType::MontageLength, 0.f, false);

	if (IsReloadDebugEnabled())
	{
		const FName SlotName = (Montage->SlotAnimTracks.Num() > 0)
			? Montage->SlotAnimTracks[0].SlotName : NAME_None;
		const UWeaponDataAsset* DA = BoundFireWeapon.IsValid() ? BoundFireWeapon->GetWeaponData() : nullptr;
		const FString EnemyName = IsValid(OwningEnemy) ? OwningEnemy->GetName() : TEXT("?");
		const FString MontageName = GetNameSafe(Montage);
		const FString WeaponName = GetNameSafe(BoundFireWeapon.Get());
		const FString DAName = GetNameSafe(DA);
		const FString PerWeaponReload = DA ? GetNameSafe(DA->EnemyAnimSet.Reload) : TEXT("null");

		UE_LOG(LogTemp, Warning,
			TEXT("[RELOADDBG] %s PlayReloadMontage: montage=%s slot=%s weapon=%s DA=%s DA.Reload=%s rate=%.2f playResult=%.2f"),
			*EnemyName, *MontageName, *SlotName.ToString(), *WeaponName, *DAName, *PerWeaponReload,
			EffectiveRate, PlayResult);

		if (GEngine)
		{
			const FString Msg = FString::Printf(
				TEXT("[RELOADDBG] %s montage=%s slot=%s DA.Reload=%s play=%.2f"),
				*EnemyName, *MontageName, *SlotName.ToString(), *PerWeaponReload, PlayResult);
			GEngine->AddOnScreenDebugMessage(
				static_cast<uint64>(GetTypeHash(FString::Printf(TEXT("ReloadDbg_%s"), *EnemyName))), 4.f, FColor::Yellow, Msg);
		}
	}

	if (BoundFireWeapon.IsValid())
	{
		BoundFireWeapon->PlayVisualWeaponReload(EffectiveRate);

		// Shell-by-shell weapons: prime the Loop section to self-loop now that both body and gun
		// montages are playing. The per-shell notify breaks out to End on the last shell.
		if (const UWeaponDataAsset* DA = BoundFireWeapon->GetWeaponData())
		{
			if (DA->bShellByShellReload)
				BoundFireWeapon->PrimeShellReloadLoop();
		}
	}
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
	// bStopAllMontages=false: the additive flinch layers over whatever is playing.
	Montage_Play(Montage, PlayRate, EMontagePlayReturnType::MontageLength, 0.f, false);
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
	// Don't restart a swing already in progress — a mid-swing replay would wipe the contact-frame
	// damage notify before it fires. Pace swings via MeleeCooldown (>= montage length). Matches the
	// IsPlaying guard on the fire/reload/grenade montage helpers.
	if (Montage_IsPlaying(Montage)) return;
	Montage_Play(Montage, PlayRate);

	const int32 NumSections = Montage->CompositeSections.Num();
	if (NumSections > 1)
	{
		const int32 Pick = FMath::RandRange(0, NumSections - 1);
		Montage_JumpToSection(Montage->CompositeSections[Pick].SectionName, Montage);
	}
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
	if (IsValid(ActiveGrenadeMontage))
	{
		Montage_Stop(BlendOutTime, ActiveGrenadeMontage.Get());
		ActiveGrenadeMontage = nullptr;
		return;
	}

	// Back-compat: legacy single-slot path (e.g. BP-assigned GrenadeMontage, no DA montages set).
	UAnimMontage* Fallback = GetEffectiveGrenadeMontage();
	if (IsValid(Fallback))
		Montage_Stop(BlendOutTime, Fallback);
}

// --- Delegate Handlers ---

void UEnemyAnimInstance::HandleWeaponFired()
{
	if (!bIsAlive) return;

	AddRecoilImpulse();

	// Blind-fire owns the pose (Kubold arm-over-cover spray) — don't stomp it with the
	// KINEMATION single-fire montage on DefaultSlot. Recoil already self-gates in AddRecoilImpulse.
	if (bCoverBlindFiring) return;

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

UAnimMontage* UEnemyAnimInstance::SelectGrenadeMontage() const
{
	if (!IsValid(OwningEnemy)) return GetEffectiveGrenadeMontage();

	const UEnemyArchetypeData* DA = OwningEnemy->GetArchetypeData();
	if (!IsValid(DA)) return GetEffectiveGrenadeMontage();

	const bool bCrouched = OwningEnemy->bIsCrouched;

	if (bCrouched && IsValid(DA->GrenadeThrowCrouchMontage))
		return DA->GrenadeThrowCrouchMontage.Get();

	// Pick a valid stand montage — start at a random index and walk forward (wrap) to skip null slots.
	const int32 Num = DA->GrenadeThrowStandMontages.Num();
	if (Num > 0)
	{
		const int32 Start = FMath::RandRange(0, Num - 1);
		for (int32 i = 0; i < Num; ++i)
		{
			UAnimMontage* Candidate = DA->GrenadeThrowStandMontages[(Start + i) % Num].Get();
			if (IsValid(Candidate)) return Candidate;
		}
	}

	// Crouched with no crouch montage, or standing with no stand montages — legacy fallback.
	return GetEffectiveGrenadeMontage();
}

void UEnemyAnimInstance::HandleGrenadeThrow(FVector PredictedLanding, float TimeToImpact)
{
	UAnimMontage* Montage = SelectGrenadeMontage();

	// FIX 2: warn once when a grenadier has no throw montage assigned so the gap fails loud.
	if (!IsValid(Montage) && !bGrenadeMontageWarnedMissing)
	{
		bGrenadeMontageWarnedMissing = true;
		UE_LOG(LogTemp, Warning,
			TEXT("UEnemyAnimInstance [%s]: no grenade throw montage found — assign GrenadeThrowCrouchMontage / GrenadeThrowStandMontages on the DA or GrenadeMontage on the ABP."),
			*GetNameSafe(OwningEnemy.Get()));
	}

	if (!IsValid(Montage)) return;
	if (Montage_IsPlaying(Montage)) return;

	Montage_Play(Montage);
	ActiveGrenadeMontage = Montage;
}

// --- Recoil Solver ---

void UEnemyAnimInstance::AddRecoilImpulse()
{
	if (!bHasRecoilProfile) return;
	if (bCoverBlindFiring) return;

	const float AimScale = bIsAiming ? RecoilProfile.AimRecoilScale : 1.f;

	// Pitch: positive Pitch on FRotator = nose down in UE; we negate so PitchKick > 0 = upward kick.
	// Designers set PitchKick as a positive "up" value; the sign is absorbed here.
	RecoilTargetRot.Pitch -= RecoilProfile.PitchKick * AimScale;
	RecoilTargetRot.Pitch = FMath::Clamp(RecoilTargetRot.Pitch, -RecoilProfile.MaxAccumulatedPitch, 0.f);

	// Yaw and roll wander both directions — random sign per shot so bursts don't walk sideways.
	// Guard min<=max so a fat-fingered DA value doesn't crash RandRange.
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
// The weapon SetRelativeTransform call has been removed — RecoilSpineOffset is a plain member write,
// which is safe here and avoids the previous thread-safety concern.
void UEnemyAnimInstance::UpdateRecoilSolver(float DeltaSeconds)
{
	if (!bHasRecoilProfile)
	{
		RecoilSpineRotation = FRotator::ZeroRotator;
		RecoilSpineOffset = FVector::ZeroVector;
		return;
	}

	// Settle-guard: skip integration when the spring is at rest.
	// "Active" = any accumulator is meaningfully non-zero.
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
	// Component-space -Y = backward along the aim axis. (Component -X reads as lateral/side-to-side
	// because the enemy mesh component is yawed -90 deg; if this ever kicks forward, flip the sign.)
	RecoilSpineOffset = FVector(0.f, -RecoilCurrentKickback, 0.f);
}

// ---------------------------------------------------------------------------
// Cover Animation
// ---------------------------------------------------------------------------

UAnimMontage* UEnemyAnimInstance::SelectCoverIdleMontage() const
{
	// Idle hugs the corner they'll peek from — None/Front fall back to the last committed side.
	ECoverLean Side = CoverLeanDirection;
	if (Side != ECoverLean::Left && Side != ECoverLean::Right) Side = LastCoverSide;

	if (CoverHeight == ECoverHeight::Crouch)
		return (Side == ECoverLean::Left) ? CrouchIdleLeft.Get() : CrouchIdleRight.Get();
	return (Side == ECoverLean::Left) ? StandIdleLeft.Get() : StandIdleRight.Get();
}

UAnimMontage* UEnemyAnimInstance::SelectCoverPeekMontage() const
{
	// Front = no side gap → stand-up over-the-top pop-up; always the standing montage regardless of height.
	if (CoverLeanDirection == ECoverLean::Front)
		return PeekOverTop.Get();

	ECoverLean Side = CoverLeanDirection;
	if (Side != ECoverLean::Left && Side != ECoverLean::Right) Side = LastCoverSide;

	if (CoverHeight == ECoverHeight::Crouch)
		return (Side == ECoverLean::Left) ? CrouchPeekLeft.Get() : CrouchPeekRight.Get();
	return (Side == ECoverLean::Left) ? StandPeekLeft.Get() : StandPeekRight.Get();
}

void UEnemyAnimInstance::StopCoverMontages(float BlendOut)
{
	if (IsValid(ActiveCoverMontage) && Montage_IsPlaying(ActiveCoverMontage))
		Montage_Stop(BlendOut, ActiveCoverMontage);
	if (IsValid(ActiveCoverBlindMontage) && Montage_IsPlaying(ActiveCoverBlindMontage))
		Montage_Stop(BlendOut, ActiveCoverBlindMontage);
}

void UEnemyAnimInstance::PlayCoverIdleMontage(float BlendIn, FName PreferredSection)
{
	UAnimMontage* Idle = SelectCoverIdleMontage();
	if (!IsValid(Idle))
	{
		if (GetCoverAnimLogLevel() > 0)
			UE_LOG(LogTemp, Warning, TEXT("[COVERANIM] %s idle slot EMPTY (height=%s)"),
				*GetNameSafe(OwningEnemy.Get()),
				CoverHeight == ECoverHeight::Crouch ? TEXT("Crouch") : TEXT("Stand"));
		return;
	}

	// Already looping this exact idle — bail rather than stacking a second instance on the slot.
	if (ActiveCoverMontage == Idle && Montage_IsPlaying(Idle))
		return;

	// bStopAllMontages=false does NOT auto-stop same-group montages — the previous cover
	// montage must be stopped explicitly, or a looping idle keeps full weight on the
	// CoverSlot and buries whatever plays next. false stays so DefaultGroup fire/reload
	// are never touched by this play.
	if (IsValid(ActiveCoverMontage) && Montage_IsPlaying(ActiveCoverMontage))
		Montage_Stop(BlendIn, ActiveCoverMontage);

	const float PlayLen = Montage_PlayWithBlendIn(Idle, FAlphaBlendArgs(BlendIn), 1.f,
		EMontagePlayReturnType::MontageLength, 0.f, false);
	if (Idle->GetSectionIndex(PreferredSection) != INDEX_NONE)
		Montage_JumpToSection(PreferredSection, Idle);
	else if (Idle->GetSectionIndex(TEXT("Loop")) != INDEX_NONE)
		Montage_JumpToSection(TEXT("Loop"), Idle);
	ActiveCoverMontage = Idle;

	if (GetCoverAnimLogLevel() > 0)
		UE_LOG(LogTemp, Log, TEXT("[COVERANIM] %s idle play %s section=%s result=%.2f"),
			*GetNameSafe(OwningEnemy.Get()), *Idle->GetName(), *PreferredSection.ToString(), PlayLen);
}

void UEnemyAnimInstance::PlayCoverPeekMontage(FName PreferredSection)
{
	UAnimMontage* Peek = SelectCoverPeekMontage();
	if (!IsValid(Peek))
	{
		if (GetCoverAnimLogLevel() > 0)
			UE_LOG(LogTemp, Warning, TEXT("[COVERANIM] %s peek slot EMPTY (height=%s lean=%d)"),
				*GetNameSafe(OwningEnemy.Get()),
				CoverHeight == ECoverHeight::Crouch ? TEXT("Crouch") : TEXT("Stand"),
				static_cast<int32>(CoverLeanDirection));
		return;
	}

	// Stop the tucked idle (or a stale peek) explicitly — bStopAllMontages=false does NOT
	// auto-stop same-group montages, so without this the looping idle keeps full weight on
	// the CoverSlot and the peek plays buried underneath it: the body never visibly stands,
	// while its root motion and the opened aim gate still run (twisted crouch firing into
	// the wall). A same-asset stop also covers peek re-entry — fresh replay from Out.
	if (IsValid(ActiveCoverMontage) && Montage_IsPlaying(ActiveCoverMontage))
		Montage_Stop(0.2f, ActiveCoverMontage);

	const float PlayLen = Montage_PlayWithBlendIn(Peek, FAlphaBlendArgs(0.2f), 1.f,
		EMontagePlayReturnType::MontageLength, 0.f, false);
	if (Peek->GetSectionIndex(PreferredSection) != INDEX_NONE)
		Montage_JumpToSection(PreferredSection, Peek);
	ActiveCoverMontage = Peek;

	// Clear any running full-body fire loop — it plays downstream of the Cover slot and would
	// visually override the peek (see the in-cover gate at the fire auto-trigger).
	StopFireMontage(0.15f);

	if (GetCoverAnimLogLevel() > 0)
		UE_LOG(LogTemp, Log, TEXT("[COVERANIM] %s peek play %s section=%s result=%.2f"),
			*GetNameSafe(OwningEnemy.Get()), *Peek->GetName(), *PreferredSection.ToString(), PlayLen);
}

void UEnemyAnimInstance::UpdateCoverAnimation(float DeltaSeconds)
{
	// Track resolved side for fallback on ECoverLean::None.
	if (CoverLeanDirection == ECoverLean::Left || CoverLeanDirection == ECoverLean::Right)
		LastCoverSide = CoverLeanDirection;

	// --- Velocity gate: cover montages only play while settled at the cover point ---
	// The full-body Cover slot suppresses locomotion, so AI-driven capsule motion under a cover
	// montage reads as floor-sliding (shuffle, relocate, stale-pose transit). Root-motion-driven
	// speed is exempt — the peek montages move the capsule themselves and must keep playing.
	// Gate closes the instant speed exceeds the threshold; re-opens only after the speed has
	// stayed below it for CoverAnimSettleTime so deceleration doesn't flicker the montage.
	// Only the peek montages drive root motion — key the exemption off the playing cover
	// montage, NOT bCoverPeeking: the Return section runs after SetPeeking(false) and its
	// root-motion speed must not close the gate mid-tuck. Idles are in-place (no root
	// motion), so AI corrective moves under an idle still close the gate as intended.
	USkeletalMeshComponent* MeshComp = GetOwningComponent();
	const bool bCoverMontageMoves = IsValid(ActiveCoverMontage)
		&& Montage_IsPlaying(ActiveCoverMontage) && ActiveCoverMontage->HasRootMotion();
	const bool bRootMotionDriven = bCoverMontageMoves && MeshComp && MeshComp->IsPlayingRootMotion();
	if (Speed > CoverAnimMaxSpeed && !bRootMotionDriven)
		CoverSettleAccum = 0.f;
	else
		CoverSettleAccum = FMath::Min(CoverSettleAccum + DeltaSeconds, CoverAnimSettleTime);
	const bool bCoverAnimActive = bInCover && CoverSettleAccum >= CoverAnimSettleTime;

	if (GetCoverAnimLogLevel() > 0 && bCoverAnimActive != bPrevCoverAnimActive)
		UE_LOG(LogTemp, Log, TEXT("[COVERANIM] %s gate %s (bInCover=%d speed=%.0f peek=%d height=%s)"),
			*GetNameSafe(OwningEnemy.Get()), bCoverAnimActive ? TEXT("OPEN") : TEXT("CLOSED"),
			bInCover ? 1 : 0, Speed, bCoverPeeking ? 1 : 0,
			CoverHeight == ECoverHeight::Crouch ? TEXT("Crouch") : TEXT("Stand"));

	// 1Hz twist/slide diagnostic: who owns the yaw (actor vs control vs focus), how much the aim
	// offset contributes, whether root motion is live, and what the Cover slot is playing.
	if (GetCoverAnimLogLevel() > 0 && bInCover)
	{
		CoverStateLogAccum += DeltaSeconds;
		if (CoverStateLogAccum >= 1.f)
		{
			CoverStateLogAccum = 0.f;
			const AController* Ctrl = OwningEnemy->GetController();
			const AAIController* AIC = Cast<AAIController>(Ctrl);
			const AActor* FocusActor = AIC ? AIC->GetFocusActor() : nullptr;
			const FVector Focal = AIC ? AIC->GetFocalPoint() : FVector::ZeroVector;
			const bool bMontagePlaying = IsValid(ActiveCoverMontage) && Montage_IsPlaying(ActiveCoverMontage);
			UE_LOG(LogTemp, Log,
				TEXT("[COVERSTATE] %s yaw=%.0f ctrlYaw=%.0f focus=%s focalValid=%d aimYaw=%.1f aimPitch=%.1f gate=%.2f speed=%.0f rootMotion=%d peek=%d lean=%d montage=%s pos=%.2f"),
				*GetNameSafe(OwningEnemy.Get()),
				OwningEnemy->GetActorRotation().Yaw,
				Ctrl ? Ctrl->GetControlRotation().Yaw : 0.f,
				*GetNameSafe(FocusActor),
				FAISystem::IsValidLocation(Focal) ? 1 : 0,
				AimYaw, AimPitch, CoverAimGate, Speed,
				bRootMotionDriven ? 1 : 0, bCoverPeeking ? 1 : 0,
				static_cast<int32>(CoverLeanDirection),
				bMontagePlaying ? *ActiveCoverMontage->GetName() : TEXT("NONE"),
				bMontagePlaying ? Montage_GetPosition(ActiveCoverMontage) : -1.f);
		}
	}

	// --- Gate fall or death: stop everything ---
	if ((!bCoverAnimActive && bPrevCoverAnimActive) || !bIsAlive)
	{
		StopCoverMontages(CoverBlendOut);
		ActiveCoverMontage = nullptr;
		ActiveCoverBlindMontage = nullptr;
		bPrevCoverAnimActive = bCoverAnimActive;
		bPrevCoverPeeking = bCoverPeeking;
		bPrevCoverBlindFiring = bCoverBlindFiring;
		PrevCoverHeight = CoverHeight;
		return;
	}

	// --- Gate rise: enter the cover pose (peek re-entry replays from Out — its root motion
	// starts from the hug position, so a fresh step-out is the position-correct resume). ---
	if (bCoverAnimActive && !bPrevCoverAnimActive)
	{
		if (bCoverPeeking)
			PlayCoverPeekMontage(TEXT("Out"));
		else
			PlayCoverIdleMontage(CoverBlendIn, TEXT("Loop"));
	}
	// --- Peek rise: play peek montage from Out section ---
	else if (bCoverPeeking && !bPrevCoverPeeking && bCoverAnimActive)
	{
		PlayCoverPeekMontage(TEXT("Out"));
	}
	// --- Peek fall: the montage owns the tuck-back — route it to Return (root motion carries
	// the body home); once it ends, the backstop below re-asserts the idle. ---
	else if (!bCoverPeeking && bPrevCoverPeeking && bCoverAnimActive)
	{
		if (IsValid(ActiveCoverMontage) && Montage_IsPlaying(ActiveCoverMontage)
			&& ActiveCoverMontage->GetSectionIndex(TEXT("Return")) != INDEX_NONE)
			Montage_JumpToSection(TEXT("Return"), ActiveCoverMontage);
		else
			PlayCoverIdleMontage(0.2f, TEXT("Loop"));
	}
	// --- Height change while in cover (no peek/blind state change): crossfade the idle ---
	// Idles are side-agnostic, so lean changes alone don't replay anything.
	else if (bCoverAnimActive && bPrevCoverAnimActive
		&& CoverHeight != PrevCoverHeight
		&& !bCoverPeeking && !bCoverBlindFiring)
	{
		PlayCoverIdleMontage(CoverBlendOut, TEXT("Loop"));
	}
	// --- Backstop: posed and settled but nothing playing on the Cover slot — re-assert. ---
	// Covers finite peeks running out mid-burst (auto Out→Aim→Return→end: pop back out) and
	// anything that stopped the montage externally.
	else if (bCoverAnimActive
		&& !(IsValid(ActiveCoverMontage) && Montage_IsPlaying(ActiveCoverMontage)))
	{
		if (bCoverPeeking)
			PlayCoverPeekMontage(TEXT("Out"));
		else
			PlayCoverIdleMontage(0.2f, TEXT("Loop"));
	}

	// --- Blind-fire rise: play blind montage + stop active fire montage ---
	// Gated like the rest: a blind edge that lands while the gate is closed is skipped for the
	// episode (blind-fire only triggers from the stationary Pause phase, so this is a backstop).
	if (bCoverBlindFiring && !bPrevCoverBlindFiring && bCoverAnimActive)
	{
		UAnimMontage* Blind = CoverBlindFireMontage.Get();
		if (IsValid(Blind))
		{
			// bStopAllMontages=false: the blind additive lives in its own slot group (CoverAdd) and must
			// layer OVER the tucked idle, not replace it — the body stays hunkered under the spray.
			Montage_PlayWithBlendIn(Blind, FAlphaBlendArgs(0.2f), 1.f, EMontagePlayReturnType::MontageLength, 0.f, false);
			ActiveCoverBlindMontage = Blind;
		}
		// Stop KINEMATION fire montage so it doesn't fight the blind-fire pose.
		StopFireMontage(0.15f);
	}
	// --- Blind-fire fall: stop blind montage; backstop the idle if something displaced it ---
	else if (!bCoverBlindFiring && bPrevCoverBlindFiring)
	{
		if (IsValid(ActiveCoverBlindMontage) && Montage_IsPlaying(ActiveCoverBlindMontage))
			Montage_Stop(0.25f, ActiveCoverBlindMontage);
		ActiveCoverBlindMontage = nullptr;

		// The idle normally survives the blind window (separate group, bStopAllMontages=false above);
		// replay only if it was lost anyway — e.g. a blind montage misauthored into the idle's group.
		if (bCoverAnimActive && !bCoverPeeking && !(IsValid(ActiveCoverMontage) && Montage_IsPlaying(ActiveCoverMontage)))
			PlayCoverIdleMontage(0.2f, TEXT("Loop"));
	}

	// Latch previous-frame state for next tick's edge detection.
	bPrevCoverAnimActive = bCoverAnimActive;
	bPrevCoverPeeking = bCoverPeeking;
	bPrevCoverBlindFiring = bCoverBlindFiring;
	PrevCoverHeight = CoverHeight;
}
