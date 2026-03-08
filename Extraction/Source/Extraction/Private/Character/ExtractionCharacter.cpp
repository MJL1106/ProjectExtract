// Copyright Epic Games, Inc. All Rights Reserved.

#include "ExtractionCharacter.h"
#include "ExtractionAnimInstance.h"
#include "Animation/AnimInstance.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "EnhancedInputComponent.h"
#include "InputActionValue.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"
#include "Extraction.h"

namespace ExtractionCharacterConstants
{
	/** Dot product threshold to consider movement as "forward" for sprint */
	static constexpr float SprintForwardDotThreshold = 0.1f;

	/** Minimum 2D speed (cm/s) to use walk-to-prone instead of idle-to-prone */
	static constexpr float ProneWalkVelocityThreshold = 10.0f;

}

AExtractionCharacter::AExtractionCharacter()
	: bIsSprinting(false)
	, bIsSliding(false)
	, bIsProne(false)
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
}

void AExtractionCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION(AExtractionCharacter, bIsSprinting, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(AExtractionCharacter, bIsSliding, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(AExtractionCharacter, bIsProne, COND_SkipOwner);
}

void AExtractionCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (IsLocallyControlled())
	{
		if (bIsSliding) UpdateSlide(DeltaTime);

		if (bIsInProneMomentum) UpdateProneMomentum(DeltaTime);

		UpdateSprint();

		// Deferred UnCrouch: when entering prone from crouch, the UnCrouch is delayed
		// until the crouch-to-prone montage finishes. Uses IsPlayingProneEntryMontage()
		// (real-time Montage_IsPlaying check) instead of bIsTransitioningToProne
		// because NativeUpdateAnimation runs AFTER Tick — the cached flag would be
		// stale on the frame the montage starts, allowing UnCrouch to fire too early.
		if (bIsProne && GetCharacterMovement()->IsCrouching())
		{
			UExtractionAnimInstance* AnimInst = GetExtractionAnimInstance();
			const bool bMontPlaying = IsValid(AnimInst) && AnimInst->IsPlayingProneEntryMontage();
			if (!IsValid(AnimInst) || !bMontPlaying) UnCrouch();
		}
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

	// Interact
	EnhancedInput->BindAction(InteractAction, ETriggerEvent::Started, this, &AExtractionCharacter::InteractStart);
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
	if (IsValid(GetController()))
	{
		AddMovementInput(GetActorRightVector(), Right);
		AddMovementInput(GetActorForwardVector(), Forward);
	}
}

void AExtractionCharacter::DoJumpStart()
{
	if (bIsProne) return;

	if (bIsSliding) EndSlide();

	Jump();
}

void AExtractionCharacter::DoJumpEnd()
{
	StopJumping();
}

// ---- Sprint ----

void AExtractionCharacter::SprintStart(const FInputActionValue& Value)
{
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

	const bool bShouldSprint = bWantsToSprint && bHasVelocity && bOnGround && bNotCrouching && !bIsSliding && !bIsProne && bMovingForward;

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
	if (bIsSliding) return;

	// Prone -> Crouch: exit prone and enter crouch (ABP handles blend via inertialization)
	if (bIsProne)
	{
		UExtractionAnimInstance* AnimInst = GetExtractionAnimInstance();
		if (!IsValid(AnimInst)) return;

		// Block during any active prone transition
		if (AnimInst->bIsTransitioningToProne || AnimInst->bIsTransitioningFromProne) return;

		bIsProne = false;
		ApplySprintSpeed();
		Crouch();
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

		if (TransitionType == EProneTransitionType::FromSprint)
		{
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

			UCharacterMovementComponent* MoveComp = GetCharacterMovement();
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
			// No montage — just UnCrouch and let the ABP state machine
			// transition Crouch→Prone via inertialization.
			UnCrouch();
		}
		else
		{
			ApplySprintSpeed();
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
	// Proxies: AnimInstance reads bIsProne via GetIsProne() each frame
}

// ---- Stub Handlers (future systems) ----

void AExtractionCharacter::VaultStart(const FInputActionValue& Value)
{
	// TODO: Implement vault system
}

void AExtractionCharacter::InteractStart(const FInputActionValue& Value)
{
	// TODO: Implement interaction system
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
