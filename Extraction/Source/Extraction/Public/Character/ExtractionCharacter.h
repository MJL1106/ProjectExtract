// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Logging/LogMacros.h"
#include "ExtractionCharacter.generated.h"

class UInputComponent;
class USkeletalMeshComponent;
class UCameraComponent;
class UInputAction;
class UExtractionAnimInstance;
struct FInputActionValue;

/**
 * Base first-person character for Extraction.
 * Handles movement, input binding, sprint, and replication setup.
 */
UCLASS()
class EXTRACTION_API AExtractionCharacter : public ACharacter
{
	GENERATED_BODY()

	/** Pawn mesh: first person view (arms; seen only by self) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USkeletalMeshComponent> FirstPersonMesh;

	/** First person camera */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCameraComponent> FirstPersonCameraComponent;

protected:

	// ---- Input Actions ----

	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<UInputAction> JumpAction;

	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<UInputAction> MoveAction;

	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<UInputAction> LookAction;

	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<UInputAction> MouseLookAction;

	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<UInputAction> SprintAction;

	/** Single action for crouch and slide. Sprint + press = slide. No sprint + press = toggle crouch. */
	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<UInputAction> CrouchSlideAction;

	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<UInputAction> VaultAction;

	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<UInputAction> InteractAction;

	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<UInputAction> ProneAction;

	// ---- Movement Config ----

	/** Walk speed in cm/s */
	UPROPERTY(EditDefaultsOnly, Category = "Movement|Config")
	float WalkSpeed = 600.0f;

	/** Sprint speed in cm/s */
	UPROPERTY(EditDefaultsOnly, Category = "Movement|Config")
	float SprintSpeed = 900.0f;

	/** Half-height of capsule when crouched */
	UPROPERTY(EditDefaultsOnly, Category = "Movement|Config")
	float CrouchedHalfHeight = 44.0f;

	/** Max walk speed while crouched in cm/s */
	UPROPERTY(EditDefaultsOnly, Category = "Movement|Config")
	float MaxWalkSpeedCrouched = 300.0f;

	/** Max walk speed while prone in cm/s (should match prone blendspace max Speed axis) */
	UPROPERTY(EditDefaultsOnly, Category = "Movement|Config")
	float ProneSpeed = 80.0f;

	/** Exponent controlling the sprint-to-prone momentum deceleration curve */
	UPROPERTY(EditDefaultsOnly, Category = "Movement|Prone",
		meta = (ClampMin = "0.5", ClampMax = "5.0",
			ToolTip = "Controls how sprint-to-prone momentum decays.\n1.0 = Linear (constant deceleration)\n2.0 = Holds speed longer, then drops off\n3.0+ = Even more hang time at peak before a sharp decel"))
	float ProneMomentumDecelerationExponent = 2.0f;

	/** How fast the camera interpolates between standing and crouched height (units/s) */
	UPROPERTY(EditDefaultsOnly, Category = "Movement|Config")
	float CrouchCameraInterpSpeed = 12.0f;

	/** Peak speed at the start of the slide in cm/s */
	UPROPERTY(EditDefaultsOnly, Category = "Movement|Slide",
		meta = (ToolTip = "The fastest the character moves during the slide. Reached immediately at slide entry. Higher = faster initial burst."))
	float SlidePeakSpeed = 800.0f;

	/** Speed the slide decelerates to at the end in cm/s (should be near WalkSpeed) */
	UPROPERTY(EditDefaultsOnly, Category = "Movement|Slide",
		meta = (ToolTip = "Speed the slide slows down to before ending. Set near WalkSpeed for a seamless transition back to walking."))
	float SlideEndSpeed = 250.0f;

	/** Total duration of the slide in seconds */
	UPROPERTY(EditDefaultsOnly, Category = "Movement|Slide",
		meta = (ToolTip = "How long the slide lasts in seconds. The character decelerates from PeakSpeed to EndSpeed over this time."))
	float SlideDuration = 1.2f;

	/** Exponent controlling the deceleration curve */
	UPROPERTY(EditDefaultsOnly, Category = "Movement|Slide",
		meta = (ClampMin = "0.5", ClampMax = "5.0",
			ToolTip = "Controls how the speed drops off over the slide duration.\n1.0 = Linear (constant deceleration)\n2.0 = Holds peak speed longer, then drops off quickly at the end\n3.0+ = Even more hang time at peak before a sharp decel\nHigher values make the slide feel faster for longer."))
	float SlideDecelerationExponent = 2.0f;

	/** How fast the slide direction steers toward the player's look direction (degrees/s) */
	UPROPERTY(EditDefaultsOnly, Category = "Movement|Slide",
		meta = (ClampMin = "0.0",
			ToolTip = "How quickly the slide curves toward where the player is looking, in degrees per second.\n0 = Fully locked to entry direction (no steering)\n45 = Subtle drift\n90 = Moderate steering (can curve around corners)\n180 = Very responsive mid-slide control"))
	float SlideSteerRate = 90.0f;

	// ---- Replicated State ----

	/** True while sprint input is held and conditions are met */
	UPROPERTY(ReplicatedUsing = OnRep_IsSprinting, BlueprintReadOnly, Category = "Movement|State")
	bool bIsSprinting;

	/** True while the character is sliding */
	UPROPERTY(ReplicatedUsing = OnRep_IsSliding, BlueprintReadOnly, Category = "Movement|State")
	bool bIsSliding;

	/** True while the character is prone */
	UPROPERTY(ReplicatedUsing = OnRep_IsProne, BlueprintReadOnly, Category = "Movement|State")
	bool bIsProne;

public:

	AExtractionCharacter();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:

	// ---- Input Handlers ----

	void MoveInput(const FInputActionValue& Value);
	void LookInput(const FInputActionValue& Value);

	UFUNCTION(BlueprintCallable, Category = "Input")
	virtual void DoAim(float Yaw, float Pitch);

	UFUNCTION(BlueprintCallable, Category = "Input")
	virtual void DoMove(float Right, float Forward);

	UFUNCTION(BlueprintCallable, Category = "Input")
	virtual void DoJumpStart();

	UFUNCTION(BlueprintCallable, Category = "Input")
	virtual void DoJumpEnd();

	// ---- Sprint ----

	void SprintStart(const FInputActionValue& Value);
	void SprintStop(const FInputActionValue& Value);

	/** Evaluates sprint conditions and updates bIsSprinting */
	void UpdateSprint();

	UFUNCTION()
	void OnRep_IsSprinting();

	/** Applies the correct MaxWalkSpeed based on sprint state */
	void ApplySprintSpeed();

	// ---- Crouch / Slide (unified) ----

	/** Sprint + press = slide. No sprint + press = toggle crouch. Ignores input while prone. */
	void HandleCrouchSlide(const FInputActionValue& Value);

	/** Evaluates whether the slide should continue or end */
	void UpdateSlide(float DeltaTime);

	/** Applies movement and capsule changes for slide entry */
	void EnterSlide();

	/** Reverts movement and capsule changes on slide exit */
	void EndSlide();

	UFUNCTION()
	void OnRep_IsSliding();

	// ---- Prone ----

	void ToggleProne(const FInputActionValue& Value);

	/** Drives sprint-to-prone deceleration each frame (identical pattern to UpdateSlide) */
	void UpdateProneMomentum(float DeltaTime);

	/** Ends the sprint-to-prone momentum phase, hands off to normal prone crawl speed */
	void EndProneMomentum();

	UFUNCTION()
	void OnRep_IsProne();

	// ---- Stub Handlers (future systems) ----
	void VaultStart(const FInputActionValue& Value);
	void InteractStart(const FInputActionValue& Value);

	// ---- Input Binding ----

	virtual void SetupPlayerInputComponent(UInputComponent* InputComponent) override;

public:

	// ---- Getters ----

	USkeletalMeshComponent* GetFirstPersonMesh() const { return FirstPersonMesh; }
	UCameraComponent* GetFirstPersonCameraComponent() const { return FirstPersonCameraComponent; }

	UFUNCTION(BlueprintPure, Category = "Animation")
	UExtractionAnimInstance* GetExtractionAnimInstance() const;

	UFUNCTION(BlueprintPure, Category = "Movement")
	bool GetIsSprinting() const { return bIsSprinting; }

	UFUNCTION(BlueprintPure, Category = "Movement")
	bool GetIsSliding() const { return bIsSliding; }

	UFUNCTION(BlueprintPure, Category = "Movement")
	bool GetIsProne() const { return bIsProne; }

private:

	/** Tracks whether the sprint input is currently held */
	bool bWantsToSprint;

	/** Current camera Z offset driven by crouch interpolation */
	float CrouchCameraCurrentOffset;

	/** Target camera Z offset (0 when standing, negative when crouched) */
	float CrouchCameraTargetOffset;

	/** Standing BaseEyeHeight cached from constructor, used as interp baseline */
	float StandingBaseEyeHeight;

	/** Time elapsed since slide started */
	float SlideElapsed;

	/** Speed captured at slide entry (used as lerp start) */
	float SlideStartSpeed;

	/** Direction the slide was initiated in (locked at entry) */
	FVector SlideDirection;

	/** True during the sprint-to-prone momentum phase (knee slide) */
	bool bIsInProneMomentum;

	/** Forward direction locked at prone momentum entry */
	FVector ProneMomentumDirection;

	/** Time elapsed since prone momentum started */
	float ProneMomentumElapsed;

	/** Speed captured at momentum entry (typically SprintSpeed) */
	float ProneMomentumStartSpeed;

	/** Total momentum duration — captured from montage play length at entry */
	float ProneMomentumDuration;

};
