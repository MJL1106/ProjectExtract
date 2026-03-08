// Copyright Epic Games, Inc. All Rights Reserved.

#include "ExtractionCharacter.h"
#include "ExtractionAnimInstance.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "EnhancedInputComponent.h"
#include "InputActionValue.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"
#include "HealthComponent.h"
#include "TimerManager.h"
#include "Extraction.h"

namespace ExtractionCharacterConstants
{
	/** Dot product threshold to consider movement as "forward" for sprint */
	static constexpr float SprintForwardDotThreshold = 0.1f;

	/** Minimum 2D speed (cm/s) to use walk-to-prone instead of idle-to-prone */
	static constexpr float ProneWalkVelocityThreshold = 10.0f;

	/** Small forward offset past the wall face for the downward surface trace (cm) */
	static constexpr float VaultLedgeTraceForwardOffset = 10.0f;

	/** Buffer above the tallest traversal height for the downward trace start position (cm) */
	static constexpr float TraversalHeightTraceBuffer = 50.0f;

	/** Min dot product between wall normal and -ForwardVector to accept wall as face-on (~60 degrees) */
	static constexpr float VaultWallFacingDotThreshold = 0.5f;

}

AExtractionCharacter::AExtractionCharacter()
	: bIsSprinting(false)
	, bIsSliding(false)
	, bIsProne(false)
	, ActiveTraversalType(ETraversalType::None)
	, bWantsToSprint(false)
	, CrouchCameraCurrentOffset(0.f)
	, CrouchCameraTargetOffset(0.f)
	, StandingBaseEyeHeight(0.f)
	, SlideElapsed(0.f)
	, SlideStartSpeed(0.f)
	, SlideDirection(FVector::ZeroVector)
	, bIsInProneMomentum(false)
	, ProneMomentumDirection(FVector::ZeroVector)
	, ProneMomentumElapsed(0.f)
	, ProneMomentumStartSpeed(0.f)
	, ProneMomentumDuration(0.f)
	, LastCrouchSlideTime(0.0)
	, bWasSprintingOnLastCrouchPress(false)
	, bIsDBNO(false)
	, BleedoutTimeRemaining(0.f)
	, ReviveElapsed(0.f)
	, bIsReviving(false)
	, PendingTraversalType(ETraversalType::None)
	, VaultTargetLocation(FVector::ZeroVector)
	, VaultSurfaceLocation(FVector::ZeroVector)
	, VaultWallNormal(FVector::ZeroVector)
	, VaultWallImpactPoint(FVector::ZeroVector)
	, VaultSurfaceHeight(0.f)
	, bWasSprintingAtTraversalEntry(false)
	, VaultSnapTarget(FVector::ZeroVector)
	, bIsSnappingToVault(false)
	, VaultSnapTimeRemaining(0.f)
	, bIsSprintJumping(false)
	, StandingCapsuleHalfHeight(96.0f)
{
	// Replication
	bReplicates = true;

	// Capsule
	GetCapsuleComponent()->SetCapsuleSize(34.0f, 96.0f);

	// First-person arms mesh (owner only)
	FirstPersonMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("FirstPersonMesh"));
	FirstPersonMesh->SetupAttachment(GetMesh());
	FirstPersonMesh->SetOnlyOwnerSee(true);
	FirstPersonMesh->FirstPersonPrimitiveType = EFirstPersonPrimitiveType::FirstPerson;
	FirstPersonMesh->SetCollisionProfileName(FName("NoCollision"));

	// Camera attached to FP mesh head socket
	FirstPersonCameraComponent = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCamera"));
	FirstPersonCameraComponent->SetupAttachment(FirstPersonMesh, FName("head"));
	FirstPersonCameraComponent->SetRelativeLocationAndRotation(
		FVector(-2.8f, 5.89f, 0.0f),
		FRotator(0.0f, 90.0f, -90.0f)
	);
	FirstPersonCameraComponent->bUsePawnControlRotation = true;
	FirstPersonCameraComponent->bEnableFirstPersonFieldOfView = true;
	FirstPersonCameraComponent->bEnableFirstPersonScale = true;
	FirstPersonCameraComponent->FirstPersonFieldOfView = 70.0f;
	FirstPersonCameraComponent->FirstPersonScale = 0.6f;

	// Third-person mesh config
	GetMesh()->SetOwnerNoSee(true);
	GetMesh()->FirstPersonPrimitiveType = EFirstPersonPrimitiveType::WorldSpaceRepresentation;

	// Character movement
	UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	if (IsValid(MoveComp))
	{
		MoveComp->MaxWalkSpeed = WalkSpeed;
		MoveComp->MaxAcceleration = 2048.0f;
		MoveComp->BrakingDecelerationWalking = 2048.0f;
		MoveComp->BrakingDecelerationFalling = 1500.0f;
		MoveComp->JumpZVelocity = 500.0f;
		MoveComp->AirControl = 0.2f;
		MoveComp->NavAgentProps.bCanCrouch = true;
		MoveComp->CrouchedHalfHeight = CrouchedHalfHeight;
		MoveComp->MaxWalkSpeedCrouched = MaxWalkSpeedCrouched;
	}

	// Health component
	HealthComponent = CreateDefaultSubobject<UHealthComponent>(TEXT("HealthComponent"));

	StandingBaseEyeHeight = BaseEyeHeight;
}

void AExtractionCharacter::BeginPlay()
{
	Super::BeginPlay();

	// Re-apply movement speeds with BP-overridden values
	// (constructor uses C++ defaults which may differ from BP instance values)
	UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	if (IsValid(MoveComp))
	{
		MoveComp->MaxWalkSpeed = WalkSpeed;
		MoveComp->MaxWalkSpeedCrouched = MaxWalkSpeedCrouched;
		MoveComp->CrouchedHalfHeight = CrouchedHalfHeight;
	}

	const UCapsuleComponent* Capsule = GetCapsuleComponent();
	if (IsValid(Capsule))
		StandingCapsuleHalfHeight = Capsule->GetUnscaledCapsuleHalfHeight();

	if (IsValid(HealthComponent))
		HealthComponent->OnDeath.AddDynamic(this, &AExtractionCharacter::HandleDeath);
}

void AExtractionCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (const UWorld* World = GetWorld())
		World->GetTimerManager().ClearTimer(BleedoutTimerHandle);

	Super::EndPlay(EndPlayReason);
}

void AExtractionCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION(AExtractionCharacter, bIsSprinting, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(AExtractionCharacter, bIsSliding, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(AExtractionCharacter, bIsProne, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(AExtractionCharacter, ActiveTraversalType, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(AExtractionCharacter, bWasSprintingAtTraversalEntry, COND_SkipOwner);
	DOREPLIFETIME(AExtractionCharacter, bIsDBNO);
	DOREPLIFETIME(AExtractionCharacter, BleedoutTimeRemaining);
}

void AExtractionCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Server: tick down bleedout timer for UI replication
	if (HasAuthority() && bIsDBNO && BleedoutTimeRemaining > 0.f)
		BleedoutTimeRemaining = FMath::Max(BleedoutTimeRemaining - DeltaTime, 0.f);

	if (IsLocallyControlled())
	{
		if (bIsSliding) UpdateSlide(DeltaTime);

		if (bIsInProneMomentum) UpdateProneMomentum(DeltaTime);

		if (IsInTraversal()) UpdateTraversal(DeltaTime);

		// Sprint jump safety net: end if montage was interrupted without delegate firing
		if (bIsSprintJumping)
		{
			UExtractionAnimInstance* AnimInst = GetExtractionAnimInstance();
			if (!IsValid(AnimInst) || !AnimInst->IsPlayingSprintJumpMontage())
				EndSprintJump();
		}

		// Deferred traversal: execute as soon as uncrouch camera interp finishes
		if (PendingTraversalType != ETraversalType::None && FMath::IsNearlyEqual(CrouchCameraCurrentOffset, 0.f, 1.f))
		{
			const ETraversalType Type = PendingTraversalType;
			PendingTraversalType = ETraversalType::None;
			ExecuteTraversalByType(Type);
		}

		if (bIsReviving) UpdateRevive(DeltaTime);

		UpdateSprint();

	}

	// Smooth camera height interpolation during crouch transitions
	const UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	if (IsValid(MoveComp))
	{
		const float HalfHeightDelta = MoveComp->GetCrouchedHalfHeight() - GetDefaultHalfHeight();
		CrouchCameraTargetOffset = MoveComp->IsCrouching() ? HalfHeightDelta : 0.f;

		if (!FMath::IsNearlyEqual(CrouchCameraCurrentOffset, CrouchCameraTargetOffset, 0.1f))
		{
			CrouchCameraCurrentOffset = FMath::FInterpTo(
				CrouchCameraCurrentOffset, CrouchCameraTargetOffset,
				DeltaTime, CrouchCameraInterpSpeed);

			BaseEyeHeight = StandingBaseEyeHeight + CrouchCameraCurrentOffset;
		}
		else
		{
			CrouchCameraCurrentOffset = CrouchCameraTargetOffset;
			BaseEyeHeight = StandingBaseEyeHeight + CrouchCameraCurrentOffset;
		}
	}
}

// ---- Input Binding ----

void AExtractionCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!IsValid(EnhancedInput))
	{
		UE_LOG(LogExtraction, Error,
			TEXT("'%s' Failed to find an Enhanced Input Component!"),
			*GetNameSafe(this));
		return;
	}

	// Movement
	EnhancedInput->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AExtractionCharacter::MoveInput);

	// Looking
	EnhancedInput->BindAction(LookAction, ETriggerEvent::Triggered, this, &AExtractionCharacter::LookInput);
	EnhancedInput->BindAction(MouseLookAction, ETriggerEvent::Triggered, this, &AExtractionCharacter::LookInput);

	// Jump
	EnhancedInput->BindAction(JumpAction, ETriggerEvent::Started, this, &AExtractionCharacter::DoJumpStart);
	EnhancedInput->BindAction(JumpAction, ETriggerEvent::Completed, this, &AExtractionCharacter::DoJumpEnd);

	// Sprint
	EnhancedInput->BindAction(SprintAction, ETriggerEvent::Started, this, &AExtractionCharacter::SprintStart);
	EnhancedInput->BindAction(SprintAction, ETriggerEvent::Completed, this, &AExtractionCharacter::SprintStop);

	// Crouch / Slide (unified — sprint+press = slide, no sprint+press = toggle crouch)
	EnhancedInput->BindAction(CrouchSlideAction, ETriggerEvent::Started, this, &AExtractionCharacter::HandleCrouchSlide);

	// Prone
	EnhancedInput->BindAction(ProneAction, ETriggerEvent::Started, this, &AExtractionCharacter::ToggleProne);

	// Vault
	EnhancedInput->BindAction(VaultAction, ETriggerEvent::Started, this, &AExtractionCharacter::VaultStart);

	// Interact (hold for revive)
	EnhancedInput->BindAction(InteractAction, ETriggerEvent::Started, this, &AExtractionCharacter::InteractStart);
	EnhancedInput->BindAction(InteractAction, ETriggerEvent::Completed, this, &AExtractionCharacter::InteractStop);

	// Temp debug: H key applies 25 damage
	PlayerInputComponent->BindKey(EKeys::H, IE_Pressed, this, &AExtractionCharacter::DebugApplyDamage);
}

// ---- Core Input Handlers ----

void AExtractionCharacter::MoveInput(const FInputActionValue& Value)
{
	const FVector2D MovementVector = Value.Get<FVector2D>();
	DoMove(MovementVector.X, MovementVector.Y);
}

void AExtractionCharacter::LookInput(const FInputActionValue& Value)
{
	const FVector2D LookAxisVector = Value.Get<FVector2D>();
	DoAim(LookAxisVector.X, LookAxisVector.Y);
}

void AExtractionCharacter::DoAim(float Yaw, float Pitch)
{
	if (IsValid(GetController()))
	{
		AddControllerYawInput(Yaw);
		AddControllerPitchInput(Pitch);
	}
}

void AExtractionCharacter::DoMove(float Right, float Forward)
{
	if (bIsDBNO) return;
	if (IsInTraversal()) return;

	if (IsValid(GetController()))
	{
		AddMovementInput(GetActorRightVector(), Right);
		AddMovementInput(GetActorForwardVector(), Forward);
	}
}

void AExtractionCharacter::DoJumpStart()
{
	if (bIsDBNO) return;
	if (bIsProne) return;
	if (IsInTraversal()) return;
	if (bIsSliding) return;
	if (bIsSprintJumping) return;

	// Traversal check — grounded and not in a blocking state
	const UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	const ETraversalType DetectedType =
		(IsValid(MoveComp) && MoveComp->IsMovingOnGround() && !bIsInProneMomentum)
		? PerformTraversalDetection()
		: ETraversalType::None;

	if (DetectedType != ETraversalType::None)
	{
		if (IsValid(MoveComp) && MoveComp->IsCrouching())
		{
			UnCrouch();
			PendingTraversalType = DetectedType;
			return;
		}
		ExecuteTraversalByType(DetectedType);
		return;
	}

	// Sprint jump: normal physics jump + visual montage overlay
	if (bIsSprinting && IsValid(MoveComp) && MoveComp->IsMovingOnGround())
	{
		StartSprintJump();
		return;
	}

	Jump();
}

void AExtractionCharacter::DoJumpEnd()
{
	StopJumping();
}

// ---- Sprint ----

void AExtractionCharacter::SprintStart(const FInputActionValue& Value)
{
	if (bIsDBNO) return;
	if (IsInTraversal() || bIsSprintJumping) return;

	bWantsToSprint = true;

	// If crouching, stand up so UpdateSprint can engage sprint
	const UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	if (IsValid(MoveComp) && MoveComp->IsCrouching() && !bIsProne && !bIsSliding) UnCrouch();
}

void AExtractionCharacter::SprintStop(const FInputActionValue& Value)
{
	bWantsToSprint = false;
}

void AExtractionCharacter::UpdateSprint()
{
	const UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	if (!IsValid(MoveComp)) return;

	// Sprint requires: input held, moving forward, on ground, not crouching
	const bool bHasVelocity = MoveComp->Velocity.SizeSquared2D() > KINDA_SMALL_NUMBER;
	const bool bOnGround = MoveComp->IsMovingOnGround();
	const bool bNotCrouching = !MoveComp->IsCrouching();

	// Check if moving roughly forward (dot product > 0 means forward hemisphere)
	bool bMovingForward = false;
	if (bHasVelocity)
	{
		const FVector VelocityDir = MoveComp->Velocity.GetSafeNormal2D();
		const FVector ForwardDir = GetActorForwardVector().GetSafeNormal2D();
		bMovingForward = FVector::DotProduct(VelocityDir, ForwardDir) > ExtractionCharacterConstants::SprintForwardDotThreshold;
	}

	const bool bShouldSprint = bWantsToSprint && bHasVelocity && bOnGround && bNotCrouching && !bIsSliding && !bIsProne && !IsInTraversal() && bMovingForward;

	if (bIsSprinting != bShouldSprint)
	{
		bIsSprinting = bShouldSprint;
		ApplySprintSpeed();
	}
}

void AExtractionCharacter::OnRep_IsSprinting()
{
	ApplySprintSpeed();
}

void AExtractionCharacter::ApplySprintSpeed()
{
	UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	if (!IsValid(MoveComp)) return;

	// During prone momentum, MaxWalkSpeed is managed by the momentum system
	if (bIsInProneMomentum) return;

	if (bIsProne)
	{
		MoveComp->MaxWalkSpeed = ProneSpeed;
	}
	else
	{
		MoveComp->MaxWalkSpeed = bIsSprinting ? SprintSpeed : WalkSpeed;
	}
}

// ---- Crouch / Slide (unified) ----

void AExtractionCharacter::HandleCrouchSlide(const FInputActionValue& Value)
{
	if (bIsDBNO) return;
	if (bIsSliding) return;
	if (IsInTraversal() || bIsSprintJumping) return;
	PendingTraversalType = ETraversalType::None;

	// Prone -> Crouch: exit prone and enter crouch (ABP handles blend via inertialization)
	if (bIsProne)
	{
		UExtractionAnimInstance* AnimInst = GetExtractionAnimInstance();
		if (!IsValid(AnimInst)) return;

		// Block during any active prone transition
		if (AnimInst->bIsTransitioningToProne || AnimInst->bIsTransitioningFromProne) return;

		// Expand capsule from prone to crouch height — clearance check included
		if (!SetCapsuleHalfHeightWithFloorAdjust(CrouchedHalfHeight))
		{
			UE_LOG(LogExtraction, Verbose, TEXT("Cannot transition prone->crouch — clearance blocked"));
			return;
		}

		// Sync CMC's crouch height so it doesn't fight the new capsule size
		UCharacterMovementComponent* MoveComp = GetCharacterMovement();
		if (IsValid(MoveComp))
			MoveComp->CrouchedHalfHeight = CrouchedHalfHeight;

		bIsProne = false;
		ApplySprintSpeed();
		return;
	}

	const UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	if (!IsValid(MoveComp)) return;

	// Double-tap crouch while sprinting = slide
	const double CurrentTime = GetWorld()->GetTimeSeconds();
	const bool bIsDoubleTap = (CurrentTime - LastCrouchSlideTime) < SlideDoubleTapWindow;
	LastCrouchSlideTime = CurrentTime;

	// While sprinting: first press records intent, second press triggers slide
	if (bIsSprinting && MoveComp->IsMovingOnGround())
	{
		if (bIsDoubleTap && bWasSprintingOnLastCrouchPress)
		{
			bWasSprintingOnLastCrouchPress = false;
			EnterSlide();
			return;
		}

		bWasSprintingOnLastCrouchPress = true;
		return;
	}

	bWasSprintingOnLastCrouchPress = false;

	// Toggle crouch
	if (MoveComp->IsCrouching())
	{
		UnCrouch();
	}
	else
	{
		Crouch();

		// Cancel sprint when entering crouch
		bWantsToSprint = false;
	}
}

void AExtractionCharacter::EnterSlide()
{
	UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	if (!IsValid(MoveComp)) return;

	bIsSliding = true;
	SlideElapsed = 0.f;

	// Lock slide direction to current forward
	SlideDirection = GetActorForwardVector().GetSafeNormal2D();

	// Capture entry speed — ramp up to peak from here, not an instant teleport
	SlideStartSpeed = FMath::Max(MoveComp->Velocity.Size2D(), SprintSpeed);

	// Cancel sprint state (but preserve bWantsToSprint so sprint resumes after slide if still held)
	bIsSprinting = false;
	ApplySprintSpeed();

	// Crouch the capsule for the slide
	Crouch();

	// Allow sliding off ledges (crouched characters are blocked by default)
	MoveComp->bCanWalkOffLedgesWhenCrouching = true;

	// Set initial velocity to slide direction at entry speed (no instant impulse)
	MoveComp->Velocity = SlideDirection * SlideStartSpeed + FVector(0.f, 0.f, MoveComp->Velocity.Z);
}

void AExtractionCharacter::UpdateSlide(float DeltaTime)
{
	UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	if (!IsValid(MoveComp))
	{
		EndSlide();
		return;
	}

	SlideElapsed += DeltaTime;

	if (SlideElapsed >= SlideDuration)
	{
		EndSlide();
		return;
	}

	// Normalized time [0..1]
	const float Alpha = FMath::Clamp(SlideElapsed / SlideDuration, 0.f, 1.f);

	// Power curve: holds speed longer at the start, then drops off toward the end
	// Exponent 1 = linear, 2 = quadratic ease-out, 3 = even more hang time at peak
	const float CurvedAlpha = FMath::Pow(Alpha, SlideDecelerationExponent);

	// Lerp from peak speed down to end speed along the curve
	const float DesiredSpeed = FMath::Lerp(SlidePeakSpeed, SlideEndSpeed, CurvedAlpha);

	// Apply velocity along the (potentially steered) slide direction
	MoveComp->Velocity = SlideDirection * DesiredSpeed + FVector(0.f, 0.f, MoveComp->Velocity.Z);
}

void AExtractionCharacter::EndSlide()
{
	if (!bIsSliding) return;

	bIsSliding = false;
	SlideElapsed = 0.f;

	UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	if (IsValid(MoveComp))
	{
		// Restore ledge blocking for normal crouching
		MoveComp->bCanWalkOffLedgesWhenCrouching = false;

		// Set exit velocity to the appropriate walk/sprint speed for a smooth handoff
		const float ExitSpeed = bWantsToSprint ? SprintSpeed : WalkSpeed;
		MoveComp->Velocity = SlideDirection * ExitSpeed + FVector(0.f, 0.f, MoveComp->Velocity.Z);
	}

	// Stand back up — snap camera offset so there's no interp jolt
	CrouchCameraCurrentOffset = 0.f;
	CrouchCameraTargetOffset = 0.f;
	BaseEyeHeight = StandingBaseEyeHeight;

	UnCrouch();
}

void AExtractionCharacter::OnRep_IsSliding()
{
	// Proxies: sync the crouched visual state
	if (bIsSliding)
	{
		Crouch();
	}
	else
	{
		UnCrouch();
	}
}

// ---- Prone ----

/** Selects the correct entry montage type based on the character's current movement state. */
static EProneTransitionType DetermineProneEntryType(const AExtractionCharacter& Char)
{
	const UCharacterMovementComponent* MoveComp = Char.GetCharacterMovement();
	if (!IsValid(MoveComp)) return EProneTransitionType::FromIdle;

	if (MoveComp->IsCrouching())  return EProneTransitionType::FromCrouch;
	if (Char.GetIsSprinting())    return EProneTransitionType::FromSprint;
	if (MoveComp->Velocity.SizeSquared2D() >
		FMath::Square(ExtractionCharacterConstants::ProneWalkVelocityThreshold))
		return EProneTransitionType::FromWalk;
	return EProneTransitionType::FromIdle;
}

void AExtractionCharacter::ToggleProne(const FInputActionValue& Value)
{
	if (bIsDBNO) return;
	if (IsInTraversal() || bIsSprintJumping) return;
	PendingTraversalType = ETraversalType::None;

	// 3P mesh AnimInstance — authoritative for state guards
	UExtractionAnimInstance* AnimInst = GetExtractionAnimInstance();
	if (!IsValid(AnimInst)) return;

	// Block re-entry during any active prone transition
	if (AnimInst->bIsTransitioningToProne || AnimInst->bIsTransitioningFromProne) return;

	// FP mesh AnimInstance — mirrors montage playback so the camera-driving head bone animates
	UExtractionAnimInstance* FPAnimInst = Cast<UExtractionAnimInstance>(
		GetFirstPersonMesh()->GetAnimInstance());

	if (bIsProne)
	{
		// --- Exit prone ---
		// Clearance check: can we expand from prone to standing height?
		if (!SetCapsuleHalfHeightWithFloorAdjust(StandingCapsuleHalfHeight))
		{
			UE_LOG(LogExtraction, Verbose, TEXT("Cannot exit prone — clearance blocked"));
			return;
		}

		// Restore real crouch height and uncrouch — CMC now matches our capsule
		UCharacterMovementComponent* MoveComp = GetCharacterMovement();
		if (IsValid(MoveComp))
			MoveComp->CrouchedHalfHeight = CrouchedHalfHeight;
		UnCrouch();

		bIsProne = false;
		ApplySprintSpeed();
		AnimInst->PlayProneExitMontage();
		if (IsValid(FPAnimInst)) FPAnimInst->PlayProneExitMontage();
	}
	else
	{
		// --- Enter prone ---
		// Query state BEFORE cancelling sprint (GetIsSprinting must still be valid)
		const EProneTransitionType TransitionType = DetermineProneEntryType(*this);

		bWantsToSprint = false;
		bIsSprinting = false;
		bIsProne = true;

		// Shrink capsule via the CMC crouch system so it enforces prone height
		UCharacterMovementComponent* MoveComp = GetCharacterMovement();
		if (IsValid(MoveComp))
			MoveComp->CrouchedHalfHeight = ProneHalfHeight;

		if (TransitionType == EProneTransitionType::FromSprint)
		{
			Crouch();

			// Momentum carry: decelerate from sprint speed to prone speed
			// over the montage duration using a power curve (like the slide system).
			bIsInProneMomentum = true;
			ProneMomentumElapsed = 0.f;
			ProneMomentumStartSpeed = SprintSpeed;
			ProneMomentumDirection = GetActorForwardVector().GetSafeNormal2D();

			// Capture montage duration so the decel curve matches the animation length
			const float MontageDuration = AnimInst->PlayProneTransitionMontage(TransitionType);
			ProneMomentumDuration = FMath::Max(MontageDuration, 0.1f);
			if (IsValid(FPAnimInst)) FPAnimInst->PlayProneTransitionMontage(TransitionType);

			if (IsValid(MoveComp))
			{
				// Keep MaxWalkSpeed high so CMC doesn't clamp our velocity writes
				MoveComp->MaxWalkSpeed = SprintSpeed;
				// Set initial velocity in the locked direction at sprint speed
				MoveComp->Velocity = ProneMomentumDirection * ProneMomentumStartSpeed
					+ FVector(0.f, 0.f, MoveComp->Velocity.Z);
			}
		}
		else if (TransitionType == EProneTransitionType::FromCrouch)
		{
			ApplySprintSpeed();
			// Already crouched — shrink capsule directly from crouch to prone height
			// (always succeeds since we're going smaller). CMC stays in crouch state
			// with CrouchedHalfHeight already set to ProneHalfHeight above.
			SetCapsuleHalfHeightWithFloorAdjust(ProneHalfHeight);
		}
		else
		{
			ApplySprintSpeed();
			Crouch();
			AnimInst->PlayProneTransitionMontage(TransitionType);
			if (IsValid(FPAnimInst)) FPAnimInst->PlayProneTransitionMontage(TransitionType);
		}
	}
}

void AExtractionCharacter::UpdateProneMomentum(float DeltaTime)
{
	UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	if (!IsValid(MoveComp))
	{
		EndProneMomentum();
		return;
	}

	ProneMomentumElapsed += DeltaTime;

	// End when decel duration reached
	if (ProneMomentumElapsed >= ProneMomentumDuration)
	{
		EndProneMomentum();
		return;
	}

	// Safety net: montage was interrupted (e.g. player jumped or got hit)
	UExtractionAnimInstance* AnimInst = GetExtractionAnimInstance();
	if (!IsValid(AnimInst) || !AnimInst->IsPlayingProneEntryMontage())
	{
		EndProneMomentum();
		return;
	}

	// Power curve deceleration: SprintSpeed -> ProneSpeed (same pattern as UpdateSlide)
	const float Alpha = FMath::Clamp(ProneMomentumElapsed / ProneMomentumDuration, 0.f, 1.f);
	const float CurvedAlpha = FMath::Pow(Alpha, ProneMomentumDecelerationExponent);
	const float DesiredSpeed = FMath::Lerp(ProneMomentumStartSpeed, ProneSpeed, CurvedAlpha);

	// Ramp MaxWalkSpeed down with desired speed so CMC doesn't fight our velocity
	MoveComp->MaxWalkSpeed = DesiredSpeed;

	// Write velocity directly along the locked direction
	MoveComp->Velocity = ProneMomentumDirection * DesiredSpeed
		+ FVector(0.f, 0.f, MoveComp->Velocity.Z);
}

void AExtractionCharacter::EndProneMomentum()
{
	bIsInProneMomentum = false;
	ProneMomentumElapsed = 0.f;

	UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	if (IsValid(MoveComp))
	{
		MoveComp->MaxWalkSpeed = ProneSpeed;

		// Set velocity to prone speed in the locked direction for clean blendspace handoff
		MoveComp->Velocity = ProneMomentumDirection * ProneSpeed
			+ FVector(0.f, 0.f, MoveComp->Velocity.Z);
	}
}

void AExtractionCharacter::OnRep_IsProne()
{
	UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	if (!IsValid(MoveComp)) return;

	if (bIsProne)
	{
		MoveComp->CrouchedHalfHeight = ProneHalfHeight;
		Crouch();
	}
	else
	{
		MoveComp->CrouchedHalfHeight = CrouchedHalfHeight;
		UnCrouch();
	}
}

// ---- Traversal (Vault / Climb / Mantle) ----

void AExtractionCharacter::VaultStart(const FInputActionValue& Value)
{
	if (bIsDBNO) return;
	if (IsInTraversal() || bIsSprintJumping) return;
	if (bIsSliding) return;
	if (bIsProne) return;
	if (bIsInProneMomentum) return;

	const UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	if (!IsValid(MoveComp)) return;
	if (MoveComp->IsFalling()) return;

	const ETraversalType DetectedType = PerformTraversalDetection();
	if (DetectedType != ETraversalType::None)
	{
		if (MoveComp->IsCrouching())
		{
			UnCrouch();
			PendingTraversalType = DetectedType;
			return;
		}
		ExecuteTraversalByType(DetectedType);
	}
}

ETraversalType AExtractionCharacter::PerformTraversalDetection()
{
	// Step 1: Forward sweep to find a wall
	FHitResult WallHit;
	if (!TraceForwardForWall(WallHit))
	{
		UE_LOG(LogExtraction, Verbose, TEXT("Traversal: No wall hit"));
		return ETraversalType::None;
	}

	// Reject hits on other characters/pawns
	if (IsValid(WallHit.GetActor()) && WallHit.GetActor()->IsA(APawn::StaticClass())) return ETraversalType::None;

	// Reject walls that aren't roughly facing us
	const FVector Forward = GetActorForwardVector();
	const float FacingDot = FVector::DotProduct(WallHit.ImpactNormal, -Forward);
	if (FacingDot < ExtractionCharacterConstants::VaultWallFacingDotThreshold)
	{
		UE_LOG(LogExtraction, Verbose, TEXT("Traversal: Wall not facing us (dot=%.2f)"), FacingDot);
		return ETraversalType::None;
	}

	// Step 2: Trace down from above the wall to find the top surface
	FHitResult SurfaceHit;
	if (!TraceDownForSurface(WallHit, SurfaceHit))
	{
		UE_LOG(LogExtraction, Verbose, TEXT("Traversal: No surface found above wall"));
		return ETraversalType::None;
	}

	// Step 3: Store shared detection results
	const UCapsuleComponent* Capsule = GetCapsuleComponent();
	if (!IsValid(Capsule)) return ETraversalType::None;
	const float CapsuleRadius = Capsule->GetScaledCapsuleRadius();
	const float CapsuleHalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	const float FeetZ = GetActorLocation().Z - CapsuleHalfHeight;

	VaultWallNormal = WallHit.ImpactNormal;
	VaultWallImpactPoint = WallHit.ImpactPoint;
	VaultSurfaceLocation = SurfaceHit.ImpactPoint;
	VaultSurfaceHeight = SurfaceHit.ImpactPoint.Z - FeetZ;

	// Step 4: Classify by height and clearance
	// VaultTargetLocation is set per-type: vault lands past the obstacle, climb/mantle land on top.
	static constexpr float ClearanceBufferOffset = 5.f;
	const float SurfOnTopOffset = CapsuleRadius + ClearanceBufferOffset;

	// Helper: set VaultTargetLocation at a given forward offset from the surface
	auto SetTargetLocation = [&](float Offset)
	{
		VaultTargetLocation = SurfaceHit.ImpactPoint + Forward * Offset;
		VaultTargetLocation.Z = SurfaceHit.ImpactPoint.Z + CapsuleHalfHeight;
	};

	// Vault range: try thin-obstacle vault first, then climb fallback for thick surfaces
	if (VaultSurfaceHeight >= VaultMinHeight && VaultSurfaceHeight <= VaultMaxHeight)
	{
		const bool bVaultClear = CheckClearance(SurfaceHit.ImpactPoint, VaultLandingForwardOffset);
		if (bVaultClear)
		{
			// In the overlap zone (vault + climb both valid), check obstacle thickness.
			// Trace down at the vault landing position — if the ground is still at surface
			// height, the obstacle is thick (e.g. a ledge) so prefer climb over vault.
			bool bPreferClimb = false;
			if (VaultSurfaceHeight >= ClimbMinHeight)
			{
				static constexpr float DropThreshold = 30.f;

				FCollisionQueryParams DropParams;
				DropParams.AddIgnoredActor(this);

				const FVector DropStart(
					SurfaceHit.ImpactPoint.X + Forward.X * VaultLandingForwardOffset,
					SurfaceHit.ImpactPoint.Y + Forward.Y * VaultLandingForwardOffset,
					SurfaceHit.ImpactPoint.Z + 10.f);
				const FVector DropEnd(DropStart.X, DropStart.Y, FeetZ - 10.f);

				FHitResult DropHit;
				const bool bHitGround = GetWorld()->LineTraceSingleByChannel(
					DropHit, DropStart, DropEnd, ECC_Visibility, DropParams);

				bPreferClimb = bHitGround &&
					(DropHit.ImpactPoint.Z > SurfaceHit.ImpactPoint.Z - DropThreshold);
			}

			if (!bPreferClimb)
			{
				SetTargetLocation(VaultLandingForwardOffset);
				return ETraversalType::Vault;
			}
		}

		// Thick surface or vault clearance failed — try climb
		if (VaultSurfaceHeight >= ClimbMinHeight)
		{
			const bool bClimbClear = CheckClearance(SurfaceHit.ImpactPoint, SurfOnTopOffset);
			if (bClimbClear)
			{
				SetTargetLocation(SurfOnTopOffset);
				return ETraversalType::Climb;
			}
		}
	}

	// Climb-only range (above vault max, up to climb max)
	if (VaultSurfaceHeight > VaultMaxHeight && VaultSurfaceHeight <= ClimbMaxHeight)
	{
		const bool bClimbClear = CheckClearance(SurfaceHit.ImpactPoint, SurfOnTopOffset);
		if (bClimbClear)
		{
			SetTargetLocation(SurfOnTopOffset);
			return ETraversalType::Climb;
		}
	}

	// Mantle range (above climb max, up to mantle max)
	if (VaultSurfaceHeight > ClimbMaxHeight && VaultSurfaceHeight <= MantleMaxHeight)
	{
		const bool bMantleClear = CheckClearance(SurfaceHit.ImpactPoint, SurfOnTopOffset);
		if (bMantleClear)
		{
			SetTargetLocation(SurfOnTopOffset);
			return ETraversalType::Mantle;
		}
	}

	return ETraversalType::None;
}

bool AExtractionCharacter::TraceForwardForWall(FHitResult& OutHit) const
{
	const UCapsuleComponent* Capsule = GetCapsuleComponent();
	if (!IsValid(Capsule)) return false;

	const float CapsuleRadius = Capsule->GetScaledCapsuleRadius();
	const float CapsuleHalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	const FVector Forward = GetActorForwardVector();

	const float FeetZ = GetActorLocation().Z - CapsuleHalfHeight;
	const float TraceHeight = FeetZ + VaultForwardTraceHeight;

	FVector Start = GetActorLocation() + Forward * CapsuleRadius;
	Start.Z = TraceHeight;
	const FVector End = Start + Forward * VaultForwardTraceDistance;

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);

	const FCollisionShape SweepShape = FCollisionShape::MakeSphere(VaultForwardTraceRadius);

	return GetWorld()->SweepSingleByChannel(
		OutHit, Start, End, FQuat::Identity,
		ECC_Visibility, SweepShape, QueryParams);
}

bool AExtractionCharacter::TraceDownForSurface(
	const FHitResult& WallHit, FHitResult& OutSurfaceHit) const
{
	const UCapsuleComponent* Capsule = GetCapsuleComponent();
	if (!IsValid(Capsule)) return false;
	const float CapsuleHalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	const FVector Forward = GetActorForwardVector();
	const float FeetZ = GetActorLocation().Z - CapsuleHalfHeight;

	// Push past the wall face to trace down onto the ledge top
	const FVector LedgeOrigin = WallHit.ImpactPoint
		+ Forward * ExtractionCharacterConstants::VaultLedgeTraceForwardOffset;

	// Scan from above the tallest possible traversal (mantle) down to feet
	const float MaxTraversalHeight = FMath::Max3(VaultMaxHeight, ClimbMaxHeight, MantleMaxHeight);
	const FVector TraceStart(LedgeOrigin.X, LedgeOrigin.Y,
		FeetZ + MaxTraversalHeight + ExtractionCharacterConstants::TraversalHeightTraceBuffer);
	const FVector TraceEnd(LedgeOrigin.X, LedgeOrigin.Y, FeetZ);

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);

	if (!GetWorld()->LineTraceSingleByChannel(
			OutSurfaceHit, TraceStart, TraceEnd, ECC_Visibility, QueryParams))
		return false;

	// Validate the surface height is within the broadest traversal range
	const float SurfaceHeight = OutSurfaceHit.ImpactPoint.Z - FeetZ;
	return SurfaceHeight >= VaultMinHeight && SurfaceHeight <= MaxTraversalHeight;
}

bool AExtractionCharacter::CheckClearance(const FVector& SurfaceLocation, float ForwardOffset) const
{
	const UCapsuleComponent* Capsule = GetCapsuleComponent();
	if (!IsValid(Capsule)) return false;

	const float CapsuleRadius = Capsule->GetScaledCapsuleRadius();
	const float CapsuleHalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	const FVector Forward = GetActorForwardVector();

	// Small upward offset prevents the capsule bottom from overlapping the surface itself.
	// Without this, climb/mantle checks fail because the test capsule sits exactly on the ledge.
	static constexpr float SurfaceSkinOffset = 2.f;

	// Test at the offset position (vault=past obstacle, climb/mantle=on top)
	const FVector TestLocation(
		SurfaceLocation.X + Forward.X * ForwardOffset,
		SurfaceLocation.Y + Forward.Y * ForwardOffset,
		SurfaceLocation.Z + CapsuleHalfHeight + SurfaceSkinOffset);

	const FCollisionShape TestShape = FCollisionShape::MakeCapsule(
		CapsuleRadius, CapsuleHalfHeight);

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);

	const bool bBlocked = GetWorld()->OverlapAnyTestByChannel(
		TestLocation, FQuat::Identity,
		ECC_WorldStatic, TestShape, QueryParams);

	return !bBlocked;
}

void AExtractionCharacter::StartTraversal(ETraversalType Type)
{
	bWasSprintingAtTraversalEntry = bIsSprinting;

	VaultLockedRotation = GetActorRotation();
	bUseControllerRotationYaw = false;

	ActiveTraversalType = Type;
	bIsSprinting = false;
	bWantsToSprint = false;

	UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	if (IsValid(MoveComp))
	{
		MoveComp->SetMovementMode(MOVE_Flying);
		MoveComp->Velocity = FVector::ZeroVector;
	}

	UCapsuleComponent* Capsule = GetCapsuleComponent();
	if (IsValid(Capsule))
		Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	VaultSnapTarget = VaultWallImpactPoint + VaultWallNormal * VaultSnapDistance;
	VaultSnapTarget.Z = GetActorLocation().Z;

	// Climb/mantle: boost character Z during snap so root motion lands on the surface.
	// The animation covers ReferenceHeight of vertical movement — any extra is pre-applied.
	if (Type == ETraversalType::Climb || Type == ETraversalType::Mantle)
	{
		const float RefHeight = (Type == ETraversalType::Climb)
			? ClimbAnimReferenceHeight
			: MantleAnimReferenceHeight;
		const float HeightBoost = FMath::Max(0.f, VaultSurfaceHeight - RefHeight);
		VaultSnapTarget.Z += HeightBoost;
	}

	bIsSnappingToVault = true;
	VaultSnapTimeRemaining = 0.15f;
}

void AExtractionCharacter::ExecuteVault()
{
	StartTraversal(ETraversalType::Vault);

	const float PlayRate = bWasSprintingAtTraversalEntry ? VaultSprintPlayRate : VaultWalkPlayRate;

	UExtractionAnimInstance* AnimInst = GetExtractionAnimInstance();
	if (IsValid(AnimInst))
		AnimInst->PlayVaultMontage(PlayRate);

	UExtractionAnimInstance* FPAnimInst = Cast<UExtractionAnimInstance>(
		GetFirstPersonMesh()->GetAnimInstance());
	if (IsValid(FPAnimInst))
		FPAnimInst->PlayVaultMontage(PlayRate);
}

void AExtractionCharacter::ExecuteClimb()
{
	StartTraversal(ETraversalType::Climb);

	const float PlayRate = bWasSprintingAtTraversalEntry ? ClimbSprintPlayRate : ClimbWalkPlayRate;

	UExtractionAnimInstance* AnimInst = GetExtractionAnimInstance();
	if (IsValid(AnimInst))
		AnimInst->PlayClimbMontage(PlayRate);

	UExtractionAnimInstance* FPAnimInst = Cast<UExtractionAnimInstance>(
		GetFirstPersonMesh()->GetAnimInstance());
	if (IsValid(FPAnimInst))
		FPAnimInst->PlayClimbMontage(PlayRate);
}

void AExtractionCharacter::ExecuteMantle()
{
	StartTraversal(ETraversalType::Mantle);

	const float PlayRate = bWasSprintingAtTraversalEntry ? MantleSprintPlayRate : MantleWalkPlayRate;

	UExtractionAnimInstance* AnimInst = GetExtractionAnimInstance();
	if (IsValid(AnimInst))
		AnimInst->PlayMantleMontage(PlayRate);

	UExtractionAnimInstance* FPAnimInst = Cast<UExtractionAnimInstance>(
		GetFirstPersonMesh()->GetAnimInstance());
	if (IsValid(FPAnimInst))
		FPAnimInst->PlayMantleMontage(PlayRate);
}

void AExtractionCharacter::ExecuteTraversalByType(ETraversalType Type)
{
	switch (Type)
	{
	case ETraversalType::Vault:  ExecuteVault();  break;
	case ETraversalType::Climb:  ExecuteClimb();  break;
	case ETraversalType::Mantle: ExecuteMantle(); break;
	default: break;
	}
}

void AExtractionCharacter::UpdateTraversal(float DeltaTime)
{
	SetActorRotation(VaultLockedRotation);

	if (bIsSnappingToVault)
	{
		VaultSnapTimeRemaining -= DeltaTime;
		if (VaultSnapTimeRemaining <= 0.f)
		{
			bIsSnappingToVault = false;
		}
		else
		{
			const FVector Current = GetActorLocation();
			const FVector NewLoc = FMath::VInterpTo(Current, VaultSnapTarget, DeltaTime, VaultSnapInterpSpeed);
			SetActorLocation(NewLoc);

			if (FVector::DistSquared(NewLoc, VaultSnapTarget) < 1.f)
				bIsSnappingToVault = false;
		}
	}

	UExtractionAnimInstance* AnimInst = GetExtractionAnimInstance();
	if (!IsValid(AnimInst) || !AnimInst->IsPlayingAnyTraversalMontage())
		EndTraversal();
}

void AExtractionCharacter::EndTraversal()
{
	if (ActiveTraversalType == ETraversalType::None) return;

	ActiveTraversalType = ETraversalType::None;
	PendingTraversalType = ETraversalType::None;
	bIsSnappingToVault = false;

	bUseControllerRotationYaw = true;

	// Sweep-in-place to resolve any overlap after root motion movement
	SetActorLocation(GetActorLocation(), true);

	UCapsuleComponent* Capsule = GetCapsuleComponent();
	if (IsValid(Capsule))
		Capsule->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);

	UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	if (IsValid(MoveComp))
		MoveComp->SetMovementMode(MOVE_Walking);

	ApplySprintSpeed();
}

void AExtractionCharacter::OnRep_TraversalType()
{
	if (ActiveTraversalType == ETraversalType::None) return;

	const float VaultRate = bWasSprintingAtTraversalEntry ? VaultSprintPlayRate : VaultWalkPlayRate;
	const float ClimbRate = bWasSprintingAtTraversalEntry ? ClimbSprintPlayRate : ClimbWalkPlayRate;
	const float MantleRate = bWasSprintingAtTraversalEntry ? MantleSprintPlayRate : MantleWalkPlayRate;

	UExtractionAnimInstance* AnimInst = GetExtractionAnimInstance();
	if (!IsValid(AnimInst)) return;

	switch (ActiveTraversalType)
	{
	case ETraversalType::Vault:  AnimInst->PlayVaultMontage(VaultRate);   break;
	case ETraversalType::Climb:  AnimInst->PlayClimbMontage(ClimbRate);   break;
	case ETraversalType::Mantle: AnimInst->PlayMantleMontage(MantleRate); break;
	default: break;
	}
}


// ---- Sprint Jump ----

void AExtractionCharacter::StartSprintJump()
{
	bIsSprintJumping = true;

	UExtractionAnimInstance* AnimInst = GetExtractionAnimInstance();
	if (IsValid(AnimInst))
	{
		AnimInst->PlaySprintJumpMontage(SprintJumpPlayRate);

		FOnMontageEnded EndDelegate;
		EndDelegate.BindUObject(this, &AExtractionCharacter::OnSprintJumpMontageEnded);
		AnimInst->Montage_SetEndDelegate(EndDelegate, AnimInst->GetCurrentActiveMontage());
	}

	UExtractionAnimInstance* FPAnimInst = Cast<UExtractionAnimInstance>(
		GetFirstPersonMesh()->GetAnimInstance());
	if (IsValid(FPAnimInst))
		FPAnimInst->PlaySprintJumpMontage(SprintJumpPlayRate);

	// Normal jump — CMC handles gravity, velocity, landing
	Jump();

	// Boost forward velocity and disable air braking so momentum carries through the arc
	UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	if (IsValid(MoveComp))
	{
		const FVector ForwardDir = GetActorForwardVector().GetSafeNormal2D();
		MoveComp->Velocity += ForwardDir * SprintJumpForwardBoost;

		CachedBrakingDecelerationFalling = MoveComp->BrakingDecelerationFalling;
		MoveComp->BrakingDecelerationFalling = 0.f;
	}
}

void AExtractionCharacter::EndSprintJump()
{
	if (!bIsSprintJumping) return;

	bIsSprintJumping = false;

	UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	if (IsValid(MoveComp))
		MoveComp->BrakingDecelerationFalling = CachedBrakingDecelerationFalling;
}

void AExtractionCharacter::OnSprintJumpMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	EndSprintJump();
}

// ---- Capsule Resize ----

bool AExtractionCharacter::SetCapsuleHalfHeightWithFloorAdjust(float NewHalfHeight)
{
	UCapsuleComponent* Capsule = GetCapsuleComponent();
	if (!IsValid(Capsule)) return false;

	const float OldHalfHeight = Capsule->GetUnscaledCapsuleHalfHeight();
	if (FMath::IsNearlyEqual(OldHalfHeight, NewHalfHeight, 0.1f)) return true;

	const float HeightDelta = NewHalfHeight - OldHalfHeight;

	// Clearance check when expanding
	if (NewHalfHeight > OldHalfHeight)
	{
		const FVector TestLocation = GetActorLocation() + FVector(0.f, 0.f, HeightDelta);
		const FCollisionShape TestShape = FCollisionShape::MakeCapsule(
			Capsule->GetUnscaledCapsuleRadius(), NewHalfHeight);

		FCollisionQueryParams Params;
		Params.AddIgnoredActor(this);

		if (GetWorld()->OverlapAnyTestByChannel(
				TestLocation, FQuat::Identity, ECC_Pawn, TestShape, Params))
			return false;
	}

	Capsule->SetCapsuleHalfHeight(NewHalfHeight);
	SetActorLocation(GetActorLocation() + FVector(0.f, 0.f, HeightDelta));
	return true;
}

// ---- Health / DBNO ----

float AExtractionCharacter::TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	const float ActualDamage = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);

	if (IsValid(HealthComponent))
		HealthComponent->TakeDamage(ActualDamage);

	return ActualDamage;
}

void AExtractionCharacter::HandleDeath()
{
	EnterDBNO();
}

void AExtractionCharacter::EnterDBNO()
{
	if (bIsDBNO) return;
	bIsDBNO = true;

	// Cancel active revive (C2: downed player can't finish reviving someone)
	if (bIsReviving) CancelRevive();

	// Cancel active movement states
	if (IsInTraversal()) EndTraversal();
	if (bIsSliding) EndSlide();
	if (bIsSprintJumping) EndSprintJump();

	UCharacterMovementComponent* MoveComp = GetCharacterMovement();

	// Restore real crouch height before UnCrouch so CMC doesn't try to expand from wrong value
	if (bIsProne || bIsCrouched)
	{
		if (IsValid(MoveComp))
			MoveComp->CrouchedHalfHeight = CrouchedHalfHeight;
		if (bIsProne) bIsProne = false;
		UnCrouch();
	}
	bWantsToSprint = false;
	bIsSprinting = false;

	// Shrink capsule to prone height via CMC crouch system
	if (IsValid(MoveComp))
		MoveComp->CrouchedHalfHeight = ProneHalfHeight;
	Crouch();

	// Stop movement but do NOT call DisableInput (camera look must work)
	if (IsValid(MoveComp))
	{
		MoveComp->StopMovementImmediately();
		MoveComp->DisableMovement();
	}

	// Start bleedout timer (server only)
	if (HasAuthority())
	{
		BleedoutTimeRemaining = BleedoutDuration;

		UWorld* World = GetWorld();
		if (IsValid(World))
		{
			World->GetTimerManager().SetTimer(
				BleedoutTimerHandle, this,
				&AExtractionCharacter::OnBleedoutExpired,
				BleedoutDuration, false);
		}
	}

	OnDBNOStateChanged.Broadcast(true, BleedoutDuration);
	UE_LOG(LogExtraction, Log, TEXT("'%s' entered DBNO (%.0fs bleedout)"),
		*GetNameSafe(this), BleedoutDuration);
}

void AExtractionCharacter::ExitDBNO()
{
	if (!bIsDBNO) return;
	bIsDBNO = false;

	if (const UWorld* World = GetWorld())
		World->GetTimerManager().ClearTimer(BleedoutTimerHandle);

	BleedoutTimeRemaining = 0.f;

	if (IsValid(HealthComponent))
		HealthComponent->Revive(ReviveHealthPercent);

	// Restore standing capsule via CMC uncrouch
	UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	if (IsValid(MoveComp))
	{
		MoveComp->CrouchedHalfHeight = CrouchedHalfHeight;
		MoveComp->SetMovementMode(MOVE_Walking);
	}
	UnCrouch();

	OnDBNOStateChanged.Broadcast(false, 0.f);
	UE_LOG(LogExtraction, Log, TEXT("'%s' revived at %.0f%% health"),
		*GetNameSafe(this), ReviveHealthPercent * 100.f);
}

void AExtractionCharacter::OnBleedoutExpired()
{
	if (!bIsDBNO) return;

	UE_LOG(LogExtraction, Log, TEXT("'%s' bleedout expired — full death"), *GetNameSafe(this));
	FullDeath();
}

void AExtractionCharacter::FullDeath()
{
	bIsDBNO = false;
	BleedoutTimeRemaining = 0.f;

	if (const UWorld* World = GetWorld())
		World->GetTimerManager().ClearTimer(BleedoutTimerHandle);

	// TODO: Ragdoll, drop loot, spectate camera
	UE_LOG(LogExtraction, Log, TEXT("'%s' is fully dead"), *GetNameSafe(this));
}

void AExtractionCharacter::OnRep_IsDBNO()
{
	UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	if (IsValid(MoveComp))
	{
		if (bIsDBNO)
		{
			MoveComp->CrouchedHalfHeight = ProneHalfHeight;
			Crouch();
			MoveComp->StopMovementImmediately();
			MoveComp->DisableMovement();
		}
		else
		{
			MoveComp->CrouchedHalfHeight = CrouchedHalfHeight;
			MoveComp->SetMovementMode(MOVE_Walking);
			UnCrouch();
		}
	}

	OnDBNOStateChanged.Broadcast(bIsDBNO, bIsDBNO ? BleedoutDuration : 0.f);
}

void AExtractionCharacter::DebugApplyDamage()
{
	if (!HasAuthority()) return;
	if (!IsValid(HealthComponent)) return;

	HealthComponent->TakeDamage(25.f);
	UE_LOG(LogExtraction, Log, TEXT("Debug: Applied 25 damage. Health=%.0f/%.0f Shield=%.0f/%.0f"),
		HealthComponent->GetCurrentHealth(), HealthComponent->GetMaxHealth(),
		HealthComponent->GetCurrentShield(), HealthComponent->GetMaxShield());
}

// ---- Interaction / Revive ----

void AExtractionCharacter::InteractStart(const FInputActionValue& Value)
{
	if (bIsDBNO) return;

	AExtractionCharacter* Target = FindReviveTarget();
	if (!IsValid(Target)) return;

	ReviveTarget = Target;
	ReviveElapsed = 0.f;
	bIsReviving = true;

	UE_LOG(LogExtraction, Verbose, TEXT("'%s' began reviving '%s'"),
		*GetNameSafe(this), *GetNameSafe(Target));
}

void AExtractionCharacter::InteractStop(const FInputActionValue& Value)
{
	if (bIsReviving) CancelRevive();
}

AExtractionCharacter* AExtractionCharacter::FindReviveTarget() const
{
	const UWorld* World = GetWorld();
	if (!IsValid(World)) return nullptr;

	const UCameraComponent* Camera = GetFirstPersonCameraComponent();
	if (!IsValid(Camera)) return nullptr;

	const FVector TraceStart = Camera->GetComponentLocation();
	const FVector TraceEnd = TraceStart + Camera->GetForwardVector() * ReviveTraceDistance;

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);

	FHitResult HitResult;
	const FCollisionShape SweepShape = FCollisionShape::MakeSphere(ReviveTraceSphereRadius);

	const bool bHit = World->SweepSingleByChannel(
		HitResult, TraceStart, TraceEnd, FQuat::Identity,
		ECC_Pawn, SweepShape, QueryParams);

	if (!bHit) return nullptr;

	AExtractionCharacter* HitCharacter = Cast<AExtractionCharacter>(HitResult.GetActor());
	if (!IsValid(HitCharacter)) return nullptr;
	if (!HitCharacter->GetIsDBNO()) return nullptr;

	// TODO: Validate team membership when team system exists
	return HitCharacter;
}

void AExtractionCharacter::UpdateRevive(float DeltaTime)
{
	if (!IsValid(ReviveTarget) || !ReviveTarget->GetIsDBNO())
	{
		CancelRevive();
		return;
	}

	const float DistSq = FVector::DistSquared(GetActorLocation(), ReviveTarget->GetActorLocation());
	if (DistSq > FMath::Square(ReviveProximityRadius))
	{
		CancelRevive();
		return;
	}

	ReviveElapsed += DeltaTime;
	if (ReviveElapsed >= ReviveDuration) CompleteRevive();
}

void AExtractionCharacter::CancelRevive()
{
	if (!bIsReviving) return;

	UE_LOG(LogExtraction, Verbose, TEXT("'%s' cancelled revive on '%s'"),
		*GetNameSafe(this), *GetNameSafe(ReviveTarget));

	bIsReviving = false;
	ReviveElapsed = 0.f;
	ReviveTarget = nullptr;
}

void AExtractionCharacter::CompleteRevive()
{
	if (!IsValid(ReviveTarget))
	{
		CancelRevive();
		return;
	}

	UE_LOG(LogExtraction, Log, TEXT("'%s' completed revive on '%s'"),
		*GetNameSafe(this), *GetNameSafe(ReviveTarget));

	Server_CompleteRevive(ReviveTarget);

	bIsReviving = false;
	ReviveElapsed = 0.f;
	ReviveTarget = nullptr;
}

void AExtractionCharacter::Server_CompleteRevive_Implementation(AExtractionCharacter* Target)
{
	if (!IsValid(Target)) return;
	if (!Target->GetIsDBNO()) return;
	if (bIsDBNO) return;

	// Server-side proximity validation
	const float DistSq = FVector::DistSquared(GetActorLocation(), Target->GetActorLocation());
	if (DistSq > FMath::Square(ReviveProximityRadius))
	{
		UE_LOG(LogExtraction, Warning, TEXT("Server_CompleteRevive: '%s' too far from '%s'"),
			*GetNameSafe(this), *GetNameSafe(Target));
		return;
	}

	// TODO: Validate team membership when team system exists
	Target->ExitDBNO();
}

// ---- Getters ----

UExtractionAnimInstance* AExtractionCharacter::GetExtractionAnimInstance() const
{
	UAnimInstance* AnimInst = GetMesh()->GetAnimInstance();
	if (!IsValid(AnimInst)) return nullptr;

	UExtractionAnimInstance* TypedInst = Cast<UExtractionAnimInstance>(AnimInst);
	if (!IsValid(TypedInst))
	{
		UE_LOG(LogExtraction, Warning,
			TEXT("AnimInstance on '%s' is not UExtractionAnimInstance. "
				"Ensure the ABP parent class is set correctly."),
			*GetNameSafe(this));
		return nullptr;
	}

	return TypedInst;
}
