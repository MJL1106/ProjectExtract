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

	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<UInputAction> CrouchAction;

	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<UInputAction> SlideAction;

	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<UInputAction> VaultAction;

	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<UInputAction> InteractAction;

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

	/** How fast the camera interpolates between standing and crouched height (units/s) */
	UPROPERTY(EditDefaultsOnly, Category = "Movement|Config")
	float CrouchCameraInterpSpeed = 12.0f;

	// ---- Replicated State ----

	/** True while sprint input is held and conditions are met */
	UPROPERTY(ReplicatedUsing = OnRep_IsSprinting, BlueprintReadOnly, Category = "Movement|State")
	bool bIsSprinting;

public:

	AExtractionCharacter();

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

	// ---- Crouch ----

	void ToggleCrouch(const FInputActionValue& Value);

	// ---- Stub Handlers (future systems) ----
	void SlideStart(const FInputActionValue& Value);
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

private:

	/** Tracks whether the sprint input is currently held */
	bool bWantsToSprint;

	/** Current camera Z offset driven by crouch interpolation */
	float CrouchCameraCurrentOffset;

	/** Target camera Z offset (0 when standing, negative when crouched) */
	float CrouchCameraTargetOffset;

	/** Standing BaseEyeHeight cached from constructor, used as interp baseline */
	float StandingBaseEyeHeight;
};
