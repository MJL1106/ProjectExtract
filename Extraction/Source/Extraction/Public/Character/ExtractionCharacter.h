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

	/** Time window for double-tap crouch to trigger a slide while sprinting */
	UPROPERTY(EditDefaultsOnly, Category = "Movement|Slide",
		meta = (ClampMin = "0.1", ClampMax = "1.0",
			ToolTip = "Max time between two crouch presses to trigger a slide while sprinting.\nLower = tighter timing required."))
	float SlideDoubleTapWindow = 0.3f;

	// ---- Vault Config ----

	/** Maximum forward distance from capsule edge to detect a vaultable wall (cm) */
	UPROPERTY(EditDefaultsOnly, Category = "Movement|Vault",
		meta = (ClampMin = "10.0", ClampMax = "200.0",
			ToolTip = "How far ahead of the capsule edge the character checks for vaultable surfaces."))
	float VaultForwardTraceDistance = 80.0f;

	/** Radius of the forward sphere sweep for wall detection (cm) */
	UPROPERTY(EditDefaultsOnly, Category = "Movement|Vault",
		meta = (ClampMin = "1.0", ClampMax = "34.0",
			ToolTip = "Radius of the sphere sweep for forward wall detection.\nLarger = more forgiving alignment."))
	float VaultForwardTraceRadius = 15.0f;

	/** Height above feet where the forward trace fires (cm) */
	UPROPERTY(EditDefaultsOnly, Category = "Movement|Vault",
		meta = (ClampMin = "0.0", ClampMax = "200.0",
			ToolTip = "Height of the forward wall-detection trace above the character's feet.\nLower = catches shorter obstacles. Independent of VaultMinHeight."))
	float VaultForwardTraceHeight = 50.0f;

	/** Minimum obstacle height from feet to be vaultable (cm) */
	UPROPERTY(EditDefaultsOnly, Category = "Movement|Vault",
		meta = (ClampMin = "0.0",
			ToolTip = "Obstacles shorter than this are stepped over, not vaulted.\nTune after reviewing vault montages."))
	float VaultMinHeight = 50.0f;

	/** Maximum obstacle height from feet to be vaultable (cm) */
	UPROPERTY(EditDefaultsOnly, Category = "Movement|Vault",
		meta = (ClampMin = "0.0",
			ToolTip = "Obstacles taller than this cannot be vaulted.\nTune after reviewing vault montages."))
	float VaultMaxHeight = 120.0f;

	/** Forward distance past the ledge edge for the landing target (cm) */
	UPROPERTY(EditDefaultsOnly, Category = "Movement|Vault",
		meta = (ClampMin = "0.0",
			ToolTip = "How far past the ledge edge the vault target is placed.\nShould be >= capsule radius (34) to prevent clipping back off the edge."))
	float VaultLandingForwardOffset = 45.0f;

	/** Montage play rate when vaulting while sprinting */
	UPROPERTY(EditDefaultsOnly, Category = "Movement|Vault",
		meta = (ClampMin = "0.5", ClampMax = "3.0",
			ToolTip = "Playback speed multiplier for sprint vaults.\nHigher = faster vault animation."))
	float VaultSprintPlayRate = 1.3f;

	/** Montage play rate when vaulting while walking */
	UPROPERTY(EditDefaultsOnly, Category = "Movement|Vault",
		meta = (ClampMin = "0.5", ClampMax = "3.0",
			ToolTip = "Playback speed multiplier for walk vaults."))
	float VaultWalkPlayRate = 1.0f;

	/** Distance from the wall face the character snaps to when a vault starts (cm).
	 *  Ensures the montage always begins at a consistent offset regardless of detection range. */
	UPROPERTY(EditDefaultsOnly, Category = "Movement|Vault",
		meta = (ClampMin = "5.0", ClampMax = "100.0",
			ToolTip = "How far from the wall the character is placed at vault start.\nTune to match where the vault animation expects the character to be."))
	float VaultSnapDistance = 50.0f;

	/** How fast the character interpolates to the vault start position */
	UPROPERTY(EditDefaultsOnly, Category = "Movement|Vault",
		meta = (ClampMin = "5.0", ClampMax = "50.0",
			ToolTip = "Interpolation speed for the vault snap.\nHigher = faster/snappier, lower = smoother glide."))
	float VaultSnapInterpSpeed = 18.0f;

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

	/** True while the character is mid-vault */
	UPROPERTY(ReplicatedUsing = OnRep_IsVaulting, BlueprintReadOnly, Category = "Movement|State")
	bool bIsVaulting;

	/** Whether the character was sprinting when the vault started (replicated for proxy play rate) */
	UPROPERTY(Replicated)
	bool bWasSprintingAtVaultEntry;

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

	// ---- Vault ----

	/** Called on IA_Vault press. Runs detection, executes vault if valid. No jump fallback. */
	void VaultStart(const FInputActionValue& Value);

	/** Runs the full multi-trace vault detection sequence. Stores results in member state.
	 *  @return true if a valid vaultable surface was found */
	bool PerformVaultDetection();

	/** Starts the vault: disables collision, switches to MOVE_Flying, snaps to wall, plays montage */
	void ExecuteVault();

	/** Ends the vault: restores MOVE_Walking, resets state */
	void EndVault();

	/** Tick safety net — ends vault if montage was interrupted */
	void UpdateVault(float DeltaTime);

	UFUNCTION()
	void OnRep_IsVaulting();

	// ---- Stub Handlers (future systems) ----
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

	UFUNCTION(BlueprintPure, Category = "Movement")
	bool GetIsVaulting() const { return bIsVaulting; }

	UFUNCTION(BlueprintPure, Category = "Movement")
	bool GetCanVault() const { return bCanVault; }

	UFUNCTION(BlueprintPure, Category = "Movement")
	FVector GetVaultTargetLocation() const { return VaultTargetLocation; }

	UFUNCTION(BlueprintPure, Category = "Movement")
	float GetVaultSurfaceHeight() const { return VaultSurfaceHeight; }

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

	/** Timestamp of last crouch/slide button press (for double-tap slide detection) */
	double LastCrouchSlideTime;

	/** Whether the player was sprinting on the previous crouch press (for double-tap slide detection) */
	bool bWasSprintingOnLastCrouchPress;

	// ---- Vault Detection (helpers) ----

	/** Sphere-sweeps forward from capsule edge to find a wall or obstacle.
	 *  @return true if an obstacle was hit */
	bool TraceForwardForWall(FHitResult& OutHit) const;

	/** Traces downward from above the wall hit to find the top surface.
	 *  @return true if a walkable surface was found within the vaultable height range */
	bool TraceDownForSurface(const FHitResult& WallHit, FHitResult& OutSurfaceHit) const;

	/** Capsule overlap test at the proposed landing position to verify the character fits.
	 *  @return true if there is room for the character */
	bool CheckVaultClearance(const FVector& SurfaceLocation) const;

	// ---- Vault State ----

	/** True when a valid vaultable surface was detected on the last check */
	bool bCanVault;

	/** Final position the character will move to after vaulting (capsule center) */
	FVector VaultTargetLocation;

	/** Impact point on top of the detected vaultable surface */
	FVector VaultSurfaceLocation;

	/** Normal of the wall face (points away from wall, toward character) */
	FVector VaultWallNormal;

	/** Impact point on the wall face from the forward trace */
	FVector VaultWallImpactPoint;

	/** Height of the vault surface above the character's feet (cm) */
	float VaultSurfaceHeight;

	/** Position the character is interpolating toward at vault start */
	FVector VaultSnapTarget;

	/** True while still interpolating to the snap position */
	bool bIsSnappingToVault;

	/** Time remaining for snap interpolation before root motion takes over */
	float VaultSnapTimeRemaining;

};
