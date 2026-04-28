// Copyright Epic Games, Inc. All Rights Reserved.

#include "ExtractionCharacter.h"
#include "ExtractionAnimInstance.h"
#include "TraversalComponent.h"
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
#include "WeaponComponent.h"
#include "WeaponBase.h"
#include "WeaponDataAsset.h"
#include "ExtractionDamageType.h"
#include "TimerManager.h"
#include "Engine/DamageEvents.h"
#include "Extraction.h"

namespace ExtractionCharacterConstants
{
	/** Dot product threshold to consider movement as "forward" for sprint */
	static constexpr float SprintForwardDotThreshold = 0.1f;

	/** Minimum 2D speed (cm/s) to use walk-to-prone instead of idle-to-prone */
	static constexpr float ProneWalkVelocityThreshold = 10.0f;

	/** Default ADS movement speed fallback when no weapon data asset is available */
	static constexpr float DefaultADSMovementSpeed = 400.0f;

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
	, bIsDBNO(false)
	, BleedoutTimeRemaining(0.f)
	, ReviveElapsed(0.f)
	, bIsReviving(false)
	, PendingTraversalType(ETraversalType::None)
	, bIsSprintJumping(false)
	, StandingCapsuleHalfHeight(96.0f)
	, BaseFOV(70.0f)
	, PreADSWalkSpeed(600.0f)
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

	// Weapon component
	WeaponComponent = CreateDefaultSubobject<UWeaponComponent>(TEXT("WeaponComponent"));

	// Traversal component
	TraversalComponent = CreateDefaultSubobject<UTraversalComponent>(TEXT("TraversalComponent"));

	// Default bone-to-region map (UE5 mannequin skeleton)
	BoneToHitRegionMap.Reserve(24);
	BoneToHitRegionMap.Add(FName("head"), EHitRegion::Head);
	BoneToHitRegionMap.Add(FName("neck_01"), EHitRegion::Torso);
	BoneToHitRegionMap.Add(FName("neck_02"), EHitRegion::Torso);
	BoneToHitRegionMap.Add(FName("spine_01"), EHitRegion::Torso);
	BoneToHitRegionMap.Add(FName("spine_02"), EHitRegion::Torso);
	BoneToHitRegionMap.Add(FName("spine_03"), EHitRegion::Torso);
	BoneToHitRegionMap.Add(FName("spine_04"), EHitRegion::Torso);
	BoneToHitRegionMap.Add(FName("spine_05"), EHitRegion::Torso);
	BoneToHitRegionMap.Add(FName("pelvis"), EHitRegion::Torso);
	BoneToHitRegionMap.Add(FName("clavicle_l"), EHitRegion::Arms);
	BoneToHitRegionMap.Add(FName("upperarm_l"), EHitRegion::Arms);
	BoneToHitRegionMap.Add(FName("lowerarm_l"), EHitRegion::Arms);
	BoneToHitRegionMap.Add(FName("hand_l"), EHitRegion::Arms);
	BoneToHitRegionMap.Add(FName("clavicle_r"), EHitRegion::Arms);
	BoneToHitRegionMap.Add(FName("upperarm_r"), EHitRegion::Arms);
	BoneToHitRegionMap.Add(FName("lowerarm_r"), EHitRegion::Arms);
	BoneToHitRegionMap.Add(FName("hand_r"), EHitRegion::Arms);
	BoneToHitRegionMap.Add(FName("thigh_l"), EHitRegion::Legs);
	BoneToHitRegionMap.Add(FName("calf_l"), EHitRegion::Legs);
	BoneToHitRegionMap.Add(FName("foot_l"), EHitRegion::Legs);
	BoneToHitRegionMap.Add(FName("thigh_r"), EHitRegion::Legs);
	BoneToHitRegionMap.Add(FName("calf_r"), EHitRegion::Legs);
	BoneToHitRegionMap.Add(FName("foot_r"), EHitRegion::Legs);
	BoneToHitRegionMap.Add(FName("ball_l"), EHitRegion::Legs);
	BoneToHitRegionMap.Add(FName("ball_r"), EHitRegion::Legs);

	StandingBaseEyeHeight = BaseEyeHeight;
}

void AExtractionCharacter::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	if (IsValid(TraversalComponent))
	{
		TraversalComponent->OnTraversalStarted.AddUObject(this, &AExtractionCharacter::HandleTraversalStarted);
		TraversalComponent->OnTraversalEnded.AddUObject(this, &AExtractionCharacter::HandleTraversalEnded);
	}
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

	// Cache anim instances — these don't change after initialization
	CachedAnimInstance = Cast<UExtractionAnimInstance>(GetMesh()->GetAnimInstance());
	if (!IsValid(CachedAnimInstance))
		UE_LOG(LogExtraction, Warning, TEXT("'%s': 3P AnimInstance is not UExtractionAnimInstance — check ABP parent class."), *GetNameSafe(this));

	CachedFPAnimInstance = Cast<UExtractionAnimInstance>(FirstPersonMesh->GetAnimInstance());

	if (IsValid(FirstPersonCameraComponent))
		BaseFOV = FirstPersonCameraComponent->FirstPersonFieldOfView;
}

void AExtractionCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (IsValid(TraversalComponent))
	{
		TraversalComponent->OnTraversalStarted.RemoveAll(this);
		TraversalComponent->OnTraversalEnded.RemoveAll(this);
	}

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
	DOREPLIFETIME(AExtractionCharacter, bIsDBNO);
}

void AExtractionCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Tick down bleedout locally (server uses FTimerHandle for authoritative expiry)
	if (bIsDBNO && BleedoutTimeRemaining > 0.f)
		BleedoutTimeRemaining = FMath::Max(BleedoutTimeRemaining - DeltaTime, 0.f);

	if (IsLocallyControlled())
	{
		if (bIsSliding) UpdateSlide(DeltaTime);

		if (bIsInProneMomentum) UpdateProneMomentum(DeltaTime);

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
			if (IsValid(TraversalComponent))
				TraversalComponent->ExecuteByType(Type, bIsSprinting);
		}

		if (bIsReviving) UpdateRevive(DeltaTime);

		UpdateSprint();

		// Weapon: FOV interpolation and recoil recovery
		UpdateWeaponFOV(DeltaTime);

		if (IsValid(WeaponComponent))
		{
			AWeaponBase* Weapon = WeaponComponent->GetCurrentWeapon();
			if (IsValid(Weapon))
				Weapon->UpdateRecoilRecovery(DeltaTime);
		}

		// Smooth camera height interpolation during crouch transitions (local player only)
		const UCharacterMovementComponent* CameraComp = GetCharacterMovement();
		if (IsValid(CameraComp))
		{
			const float HalfHeightDelta = CameraComp->GetCrouchedHalfHeight() - GetDefaultHalfHeight();
			CrouchCameraTargetOffset = CameraComp->IsCrouching() ? HalfHeightDelta : 0.f;

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

	// Fire
	if (FireAction)
	{
		EnhancedInput->BindAction(FireAction, ETriggerEvent::Started, this, &AExtractionCharacter::FireStart);
		EnhancedInput->BindAction(FireAction, ETriggerEvent::Completed, this, &AExtractionCharacter::FireStop);
	}

	// Reload
	if (ReloadAction)
		EnhancedInput->BindAction(ReloadAction, ETriggerEvent::Started, this, &AExtractionCharacter::ReloadStart);

	// ADS
	if (ADSAction)
	{
		EnhancedInput->BindAction(ADSAction, ETriggerEvent::Started, this, &AExtractionCharacter::ADSStart);
		EnhancedInput->BindAction(ADSAction, ETriggerEvent::Completed, this, &AExtractionCharacter::ADSStop);
	}

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

	// Cancel recoil recovery when player moves mouse
	if (IsValid(WeaponComponent))
	{
		AWeaponBase* Weapon = WeaponComponent->GetCurrentWeapon();
		if (IsValid(Weapon))
			Weapon->CancelRecoilRecovery();
	}

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
	if (IsValid(MoveComp) && MoveComp->IsMovingOnGround() && !bIsInProneMomentum && IsValid(TraversalComponent))
	{
		FVector SnapTarget;
		ETraversalType DetectedType;
		if (TraversalComponent->DetectTraversalAhead(SnapTarget, DetectedType))
		{
			if (MoveComp->IsCrouching())
			{
				UnCrouch();
				PendingTraversalType = DetectedType;
				return;
			}
			TraversalComponent->ExecuteByType(DetectedType, bIsSprinting);
			return;
		}
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
	else if (bIsSprinting)
	{
		MoveComp->MaxWalkSpeed = SprintSpeed;
	}
	else if (IsValid(WeaponComponent) && WeaponComponent->IsAiming())
	{
		const AWeaponBase* Weapon = WeaponComponent->GetCurrentWeapon();
		const UWeaponDataAsset* Data = IsValid(Weapon) ? Weapon->GetWeaponData() : nullptr;
		MoveComp->MaxWalkSpeed = IsValid(Data) ? Data->ADSMovementSpeed : WalkSpeed;
	}
	else
	{
		MoveComp->MaxWalkSpeed = WalkSpeed;
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
		if (IsValid(CachedFPAnimInstance)) CachedFPAnimInstance->PlayProneExitMontage();
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
			if (IsValid(CachedFPAnimInstance)) CachedFPAnimInstance->PlayProneTransitionMontage(TransitionType);

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
			if (IsValid(CachedFPAnimInstance)) CachedFPAnimInstance->PlayProneTransitionMontage(TransitionType);
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
	if (!IsValid(TraversalComponent)) return;

	const UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	if (!IsValid(MoveComp)) return;
	if (MoveComp->IsFalling()) return;

	FVector SnapTarget;
	ETraversalType DetectedType;
	if (TraversalComponent->DetectTraversalAhead(SnapTarget, DetectedType))
	{
		if (MoveComp->IsCrouching())
		{
			UnCrouch();
			PendingTraversalType = DetectedType;
			return;
		}
		TraversalComponent->ExecuteByType(DetectedType, bIsSprinting);
	}
}

void AExtractionCharacter::HandleTraversalStarted(ETraversalType Type, float PlayRate)
{
	bIsSprinting = false;
	bWantsToSprint = false;

	UExtractionAnimInstance* AnimInst = GetExtractionAnimInstance();
	if (IsValid(AnimInst))
	{
		switch (Type)
		{
		case ETraversalType::Vault:  AnimInst->PlayVaultMontage(PlayRate);  break;
		case ETraversalType::Climb:  AnimInst->PlayClimbMontage(PlayRate);  break;
		case ETraversalType::Mantle: AnimInst->PlayMantleMontage(PlayRate); break;
		default: break;
		}

		FOnMontageEnded EndDelegate;
		EndDelegate.BindUObject(this, &AExtractionCharacter::OnTraversalMontageEnded);
		AnimInst->Montage_SetEndDelegate(EndDelegate, AnimInst->GetCurrentActiveMontage());
	}

	if (IsValid(CachedFPAnimInstance))
	{
		switch (Type)
		{
		case ETraversalType::Vault:  CachedFPAnimInstance->PlayVaultMontage(PlayRate);  break;
		case ETraversalType::Climb:  CachedFPAnimInstance->PlayClimbMontage(PlayRate);  break;
		case ETraversalType::Mantle: CachedFPAnimInstance->PlayMantleMontage(PlayRate); break;
		default: break;
		}
	}
}

void AExtractionCharacter::OnTraversalMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	if (IsValid(TraversalComponent))
		TraversalComponent->EndTraversal();
}

void AExtractionCharacter::HandleTraversalEnded()
{
	PendingTraversalType = ETraversalType::None;
	ApplySprintSpeed();
}

ETraversalType AExtractionCharacter::GetActiveTraversalType() const
{
	if (!IsValid(TraversalComponent)) return ETraversalType::None;
	return TraversalComponent->GetActiveType();
}

bool AExtractionCharacter::IsInTraversal() const
{
	if (!IsValid(TraversalComponent)) return false;
	return TraversalComponent->IsInTraversal();
}

bool AExtractionCharacter::GetIsVaulting() const
{
	return GetActiveTraversalType() == ETraversalType::Vault;
}

FVector AExtractionCharacter::GetVaultTargetLocation() const
{
	if (!IsValid(TraversalComponent)) return FVector::ZeroVector;
	return TraversalComponent->GetVaultTargetLocation();
}

float AExtractionCharacter::GetVaultSurfaceHeight() const
{
	if (!IsValid(TraversalComponent)) return 0.f;
	return TraversalComponent->GetVaultSurfaceHeight();
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

	if (IsValid(CachedFPAnimInstance))
		CachedFPAnimInstance->PlaySprintJumpMontage(SprintJumpPlayRate);

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

float AExtractionCharacter::GetHitboxDamageMultiplier(const FDamageEvent& DamageEvent) const
{
	if (!DamageEvent.IsOfType(FPointDamageEvent::ClassID))
		return 1.0f;

	const FPointDamageEvent& PointDamage = static_cast<const FPointDamageEvent&>(DamageEvent);

	const EHitRegion* Region = BoneToHitRegionMap.Find(PointDamage.HitInfo.BoneName);
	if (!Region)
		return 1.0f;

	if (!PointDamage.DamageTypeClass)
		return 1.0f;

	const UExtractionDamageType* DmgType = Cast<UExtractionDamageType>(
		PointDamage.DamageTypeClass->GetDefaultObject());
	if (!IsValid(DmgType))
		return 1.0f;

	return DmgType->GetMultiplierForRegion(*Region);
}

float AExtractionCharacter::TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	const float ActualDamage = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	const float FinalDamage = ActualDamage * GetHitboxDamageMultiplier(DamageEvent);

	if (IsValid(HealthComponent))
		HealthComponent->TakeDamage(FinalDamage);

	return FinalDamage;
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
	if (IsValid(TraversalComponent) && TraversalComponent->IsInTraversal())
		TraversalComponent->CancelTraversal();
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

	// Initialize local countdown so client can tick it down without replication
	if (bIsDBNO)
		BleedoutTimeRemaining = BleedoutDuration;

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

// ---- Weapon Input ----

void AExtractionCharacter::FireStart(const FInputActionValue& Value)
{
	if (bIsDBNO) return;
	if (IsInTraversal()) return;
	if (!IsValid(WeaponComponent)) return;

	WeaponComponent->StartFire();
}

void AExtractionCharacter::FireStop(const FInputActionValue& Value)
{
	if (!IsValid(WeaponComponent)) return;
	WeaponComponent->StopFire();
}

void AExtractionCharacter::ReloadStart(const FInputActionValue& Value)
{
	if (bIsDBNO) return;
	if (IsInTraversal()) return;
	if (!IsValid(WeaponComponent)) return;

	WeaponComponent->StartReload();
}

void AExtractionCharacter::ADSStart(const FInputActionValue& Value)
{
	if (bIsDBNO) return;
	if (IsInTraversal()) return;
	if (!IsValid(WeaponComponent)) return;

	// Cancel sprint when entering ADS
	if (bIsSprinting)
	{
		bWantsToSprint = false;
		UpdateSprint();
	}

	WeaponComponent->SetAiming(true);
	UpdateADSMovementSpeed();
}

void AExtractionCharacter::ADSStop(const FInputActionValue& Value)
{
	if (!IsValid(WeaponComponent)) return;

	WeaponComponent->SetAiming(false);
	UpdateADSMovementSpeed();
}

void AExtractionCharacter::UpdateWeaponFOV(float DeltaTime)
{
	if (!IsValid(FirstPersonCameraComponent)) return;
	if (!IsValid(WeaponComponent)) return;

	const AWeaponBase* Weapon = WeaponComponent->GetCurrentWeapon();
	const UWeaponDataAsset* Data = IsValid(Weapon) ? Weapon->GetWeaponData() : nullptr;

	const float TargetFOV = (WeaponComponent->IsAiming() && IsValid(Data))
		? Data->ADSFOV
		: BaseFOV;

	const float InterpSpeed = IsValid(Data) && Data->ADSTransitionTime > 0.f
		? 1.0f / Data->ADSTransitionTime
		: 10.0f;

	const float CurrentFOV = FirstPersonCameraComponent->FirstPersonFieldOfView;
	if (!FMath::IsNearlyEqual(CurrentFOV, TargetFOV, 0.1f))
	{
		FirstPersonCameraComponent->FirstPersonFieldOfView = FMath::FInterpTo(
			CurrentFOV, TargetFOV, DeltaTime, InterpSpeed);
	}
	else
	{
		FirstPersonCameraComponent->FirstPersonFieldOfView = TargetFOV;
	}
}

void AExtractionCharacter::UpdateADSMovementSpeed()
{
	UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	if (!IsValid(MoveComp)) return;
	if (!IsValid(WeaponComponent)) return;

	if (WeaponComponent->IsAiming())
	{
		PreADSWalkSpeed = MoveComp->MaxWalkSpeed;

		const AWeaponBase* Weapon = WeaponComponent->GetCurrentWeapon();
		const UWeaponDataAsset* Data = IsValid(Weapon) ? Weapon->GetWeaponData() : nullptr;
		const float ADSSpeed = IsValid(Data) ? Data->ADSMovementSpeed : ExtractionCharacterConstants::DefaultADSMovementSpeed;

		MoveComp->MaxWalkSpeed = ADSSpeed;
	}
	else
	{
		MoveComp->MaxWalkSpeed = bIsSprinting ? SprintSpeed : WalkSpeed;
	}
}

// ---- Getters ----

UExtractionAnimInstance* AExtractionCharacter::GetExtractionAnimInstance() const
{
	return CachedAnimInstance;
}
