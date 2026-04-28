// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "ExtractionTypes.h"
#include "Logging/LogMacros.h"
#include "ExtractionCharacter.generated.h"

class UInputComponent;
class USkeletalMeshComponent;
class UCameraComponent;
class UInputAction;
class UExtractionAnimInstance;
class UHealthComponent;
class UWeaponComponent;
class UTraversalComponent;
class UAnimMontage;
struct FInputActionValue;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnDBNOStateChanged, bool, bNewIsDBNO, float, BleedoutDuration);

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

	/** Health and shield management */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UHealthComponent> HealthComponent;

	/** Weapon equip, ADS, and fire management */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UWeaponComponent> WeaponComponent;

	/** Traversal (vault/climb/mantle) logic */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UTraversalComponent> TraversalComponent;

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

	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<UInputAction> FireAction;

	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<UInputAction> ReloadAction;

	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<UInputAction> ADSAction;

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

	/** Half-height of capsule when prone (default = capsule radius for a near-sphere) */
	UPROPERTY(EditDefaultsOnly, Category = "Movement|Config", meta = (ClampMin = "20.0", ClampMax = "96.0"))
	float ProneHalfHeight = 34.0f;

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

	// ---- Sprint Jump Config ----

	/** Montage play rate for the sprint jump */
	UPROPERTY(EditDefaultsOnly, Category = "Movement|SprintJump",
		meta = (ClampMin = "0.5", ClampMax = "3.0"))
	float SprintJumpPlayRate = 1.0f;

	/** Extra forward velocity (cm/s) added on top of sprint speed when sprint jumping */
	UPROPERTY(EditDefaultsOnly, Category = "Movement|SprintJump",
		meta = (ClampMin = "0.0", ClampMax = "1500.0"))
	float SprintJumpForwardBoost = 300.0f;

	// ---- DBNO Config ----

	UPROPERTY(EditDefaultsOnly, Category = "Health|DBNO", meta = (ClampMin = "1.0"))
	float BleedoutDuration = 30.f;

	UPROPERTY(EditDefaultsOnly, Category = "Health|DBNO", meta = (ClampMin = "0.5"))
	float ReviveDuration = 4.f;

	UPROPERTY(EditDefaultsOnly, Category = "Health|DBNO", meta = (ClampMin = "0.01", ClampMax = "1.0"))
	float ReviveHealthPercent = 0.3f;

	UPROPERTY(EditDefaultsOnly, Category = "Health|DBNO", meta = (ClampMin = "50.0"))
	float ReviveProximityRadius = 200.f;

	UPROPERTY(EditDefaultsOnly, Category = "Health|DBNO", meta = (ClampMin = "50.0"))
	float ReviveTraceDistance = 300.f;

	UPROPERTY(EditDefaultsOnly, Category = "Health|DBNO", meta = (ClampMin = "5.0"))
	float ReviveTraceSphereRadius = 30.f;

	// ---- Hitbox Config ----

	/** Maps skeleton bone names to hit regions for damage multiplier lookup.
	 *  Defaults to UE5 mannequin bones. Override in Blueprint for custom skeletons. */
	UPROPERTY(EditDefaultsOnly, Category = "Damage|Hitbox")
	TMap<FName, EHitRegion> BoneToHitRegionMap;

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

	/** True while the character is in Down But Not Out state */
	UPROPERTY(ReplicatedUsing = OnRep_IsDBNO, BlueprintReadOnly, Category = "Health|State")
	bool bIsDBNO;

	/** Seconds remaining before bleedout death (set on DBNO entry, clients use for UI) */
	UPROPERTY(BlueprintReadOnly, Category = "Health|State")
	float BleedoutTimeRemaining = 0.f;

public:

	AExtractionCharacter();

	virtual void PostInitializeComponents() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaTime) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual float TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

	UPROPERTY(BlueprintAssignable, Category = "Health|Events")
	FOnDBNOStateChanged OnDBNOStateChanged;

public:

	UFUNCTION(BlueprintCallable, Category = "Input")
	virtual void DoAim(float Yaw, float Pitch);

protected:

	// ---- Input Handlers ----

	void MoveInput(const FInputActionValue& Value);
	void LookInput(const FInputActionValue& Value);

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

	// ---- Traversal (Vault / Climb / Mantle) ----

	/** Called on IA_Vault press. Delegates detection+execution to TraversalComponent. */
	void VaultStart(const FInputActionValue& Value);

	/** Delegate handler: plays traversal montage on both 3P and 1P anim instances */
	void HandleTraversalStarted(ETraversalType Type, float PlayRate);

	/** Delegate handler: post-traversal cleanup (sprint speed restore) */
	void HandleTraversalEnded();

	/** Montage end callback bound after a traversal montage starts; ends traversal on completion or interrupt. */
	UFUNCTION()
	void OnTraversalMontageEnded(UAnimMontage* Montage, bool bInterrupted);

	// ---- Interaction / Revive ----

	void InteractStart(const FInputActionValue& Value);
	void InteractStop(const FInputActionValue& Value);

	// ---- Input Binding ----

	virtual void SetupPlayerInputComponent(UInputComponent* InputComponent) override;

public:

	// ---- Getters ----

	USkeletalMeshComponent* GetFirstPersonMesh() const { return FirstPersonMesh; }
	UCameraComponent* GetFirstPersonCameraComponent() const { return FirstPersonCameraComponent; }
	UHealthComponent* GetHealthComponent() const { return HealthComponent; }
	UWeaponComponent* GetWeaponComponent() const { return WeaponComponent; }

	UFUNCTION(BlueprintPure, Category = "Health")
	bool GetIsDBNO() const { return bIsDBNO; }

	/** Exit DBNO state and restore health/movement.
	 *  Called by server authority (revive system, companion AI). */
	void ExitDBNO();

	UFUNCTION(BlueprintPure, Category = "Animation")
	UExtractionAnimInstance* GetExtractionAnimInstance() const;

	UFUNCTION(BlueprintPure, Category = "Movement")
	bool GetIsSprinting() const { return bIsSprinting; }

	UFUNCTION(BlueprintPure, Category = "Movement")
	bool GetIsSliding() const { return bIsSliding; }

	UFUNCTION(BlueprintPure, Category = "Movement")
	bool GetIsProne() const { return bIsProne; }

	UFUNCTION(BlueprintPure, Category = "Movement")
	UTraversalComponent* GetTraversalComponent() const { return TraversalComponent; }

	ETraversalType GetActiveTraversalType() const;
	bool IsInTraversal() const;

	UFUNCTION(BlueprintPure, Category = "Movement")
	bool GetIsSprintJumping() const { return bIsSprintJumping; }

	UFUNCTION(BlueprintPure, Category = "Movement")
	bool GetIsVaulting() const;

	UFUNCTION(BlueprintPure, Category = "Movement")
	FVector GetVaultTargetLocation() const;

	UFUNCTION(BlueprintPure, Category = "Movement")
	float GetVaultSurfaceHeight() const;

private:

	// ---- Health / DBNO ----

	UFUNCTION()
	void HandleDeath();

	void EnterDBNO();
	void OnBleedoutExpired();
	void FullDeath();

	/** Resolves hitbox multiplier from the damage event's bone + damage type.
	 *  Returns 1.0 for non-point damage, unknown bones, or non-Extraction damage types. */
	float GetHitboxDamageMultiplier(const FDamageEvent& DamageEvent) const;

	UFUNCTION()
	void OnRep_IsDBNO();

	/** Temp debug: apply 25 damage to self (bound to H key) */
	void DebugApplyDamage();

	FTimerHandle BleedoutTimerHandle;

	/** Cached 3P anim instance (populated in BeginPlay — avoids Cast per call) */
	UPROPERTY()
	TObjectPtr<UExtractionAnimInstance> CachedAnimInstance;

	/** Cached 1P anim instance (populated in BeginPlay — avoids Cast per call) */
	UPROPERTY()
	TObjectPtr<UExtractionAnimInstance> CachedFPAnimInstance;

	// ---- Revive ----

	void UpdateRevive(float DeltaTime);
	AExtractionCharacter* FindReviveTarget() const;
	void CancelRevive();
	void CompleteRevive();

	UFUNCTION(Server, Reliable)
	void Server_CompleteRevive(AExtractionCharacter* Target);

	UPROPERTY()
	TObjectPtr<AExtractionCharacter> ReviveTarget;

	float ReviveElapsed = 0.f;
	bool bIsReviving = false;

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

	// ---- Sprint Jump ----

	/** True while a root-motion sprint jump montage is playing */
	bool bIsSprintJumping;

	/** Cached BrakingDecelerationFalling to restore after sprint jump */
	float CachedBrakingDecelerationFalling;

	/** Begin a root-motion sprint jump (MOVE_Flying + montage) */
	void StartSprintJump();

	/** End sprint jump — restore movement mode */
	void EndSprintJump();

	/** Delegate callback fired when the sprint jump montage ends or is interrupted */
	UFUNCTION()
	void OnSprintJumpMontageEnded(UAnimMontage* Montage, bool bInterrupted);

	// ---- Capsule Resize ----

	/** Resize capsule half-height and adjust actor Z to keep feet on the ground.
	 *  When expanding, performs an overlap clearance check first.
	 *  @return true if resize succeeded (always true when shrinking; may fail when expanding if blocked) */
	bool SetCapsuleHalfHeightWithFloorAdjust(float NewHalfHeight);

	/** Standing capsule half-height cached from constructor, used as restore target */
	float StandingCapsuleHalfHeight;

	// ---- Weapon Input ----

	void FireStart(const FInputActionValue& Value);
	void FireStop(const FInputActionValue& Value);
	void ReloadStart(const FInputActionValue& Value);
	void ADSStart(const FInputActionValue& Value);
	void ADSStop(const FInputActionValue& Value);

	/** Interpolates camera FOV for ADS transitions */
	void UpdateWeaponFOV(float DeltaTime);

	/** Interpolates movement speed for ADS transitions */
	void UpdateADSMovementSpeed();

	/** Base field of view (non-ADS) */
	float BaseFOV;

	/** Cached walk speed to restore when exiting ADS */
	float PreADSWalkSpeed;

	// ---- Traversal State ----

	/** Traversal type waiting for uncrouch to finish before executing (None = no pending) */
	ETraversalType PendingTraversalType;

};
