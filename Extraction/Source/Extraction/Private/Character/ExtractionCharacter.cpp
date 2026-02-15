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
}

AExtractionCharacter::AExtractionCharacter()
	: bIsSprinting(false)
	, bWantsToSprint(false)
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
	}
}

void AExtractionCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION(AExtractionCharacter, bIsSprinting, COND_SkipOwner);
}

void AExtractionCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (IsLocallyControlled())
	{
		UpdateSprint();
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

	// Crouch
	EnhancedInput->BindAction(CrouchAction, ETriggerEvent::Started, this, &AExtractionCharacter::CrouchStart);
	EnhancedInput->BindAction(CrouchAction, ETriggerEvent::Completed, this, &AExtractionCharacter::CrouchStop);

	// Slide
	EnhancedInput->BindAction(SlideAction, ETriggerEvent::Started, this, &AExtractionCharacter::SlideStart);

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
}

void AExtractionCharacter::SprintStop(const FInputActionValue& Value)
{
	bWantsToSprint = false;
}

void AExtractionCharacter::UpdateSprint()
{
	const UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	if (!IsValid(MoveComp))
	{
		return;
	}

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

	const bool bShouldSprint = bWantsToSprint && bHasVelocity && bOnGround && bNotCrouching && bMovingForward;

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
	if (!IsValid(MoveComp))
	{
		return;
	}

	MoveComp->MaxWalkSpeed = bIsSprinting ? SprintSpeed : WalkSpeed;
}

// ---- Stub Handlers ----

void AExtractionCharacter::CrouchStart(const FInputActionValue& Value)
{
	Crouch();
}

void AExtractionCharacter::CrouchStop(const FInputActionValue& Value)
{
	UnCrouch();
}

void AExtractionCharacter::SlideStart(const FInputActionValue& Value)
{
	// TODO: Implement slide system
}

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
	if (!IsValid(AnimInst))
	{
		return nullptr;
	}

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
