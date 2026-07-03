// AI companion character — follows player, engages enemies, revives downed teammates.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "GameplayTagAssetInterface.h"
#include "GameplayTagContainer.h"
#include "GenericTeamAgentInterface.h"
#include "Movement/TraversalTypes.h"
#include "Companion/CompanionTypes.h"
#include "Companion/CompanionCommandTypes.h"
#include "AIShooterInterface.h"
#include "CompanionCharacter.generated.h"

class UHealthComponent;
class USuppressionComponent;
class UCoverPoseComponent;
class AWeaponBase;
class UCompanionAnimInstance;
class UTraversalComponent;
class UWidgetComponent;
class UUserWidget;
class AExtractionPlayer;

DECLARE_LOG_CATEGORY_EXTERN(LogCompanion, Log, All);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCompanionPostureChanged, ECompanionPosture, NewPosture);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnLowReadyAimChanged, bool, bIsLowReady);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnCommandedTakedownFinished);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnCommandedTakedownStarted);

UCLASS(Blueprintable)
class EXTRACTION_API ACompanionCharacter : public ACharacter, public IGameplayTagAssetInterface, public IAIShooterInterface, public IGenericTeamAgentInterface
{
	GENERATED_BODY()

public:
	ACompanionCharacter();

	virtual void Tick(float DeltaTime) override;
	virtual float TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;
	virtual void OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;

	// --- IGameplayTagAssetInterface ---
	virtual void GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const override;

	// --- IAIShooterInterface ---
	virtual AActor* GetAIAimTarget() const override { return CurrentAimTarget.Get(); }
	virtual float GetAIAimSpreadDegrees() const override { return GetCurrentInaccuracy(); }
	virtual FVector GetAimPointForTarget(const AActor* Target) const override;

	// --- IGenericTeamAgentInterface ---
	virtual FGenericTeamId GetGenericTeamId() const override { return FGenericTeamId(0); }

	// --- Weapon Interface ---

	UFUNCTION(BlueprintCallable, Category = "Companion|Combat")
	void StartWeaponFire();

	UFUNCTION(BlueprintCallable, Category = "Companion|Combat")
	void StopWeaponFire();

	UFUNCTION(BlueprintCallable, Category = "Companion|Combat")
	void ReloadWeapon();

	UFUNCTION(BlueprintPure, Category = "Companion|Combat")
	bool CanFire() const;

	UFUNCTION(BlueprintPure, Category = "Companion|Combat")
	bool NeedsReload() const;

	UFUNCTION(BlueprintPure, Category = "Companion|Combat")
	bool IsReloading() const;

	/** True if the equipped weapon can currently be reloaded. */
	UFUNCTION(BlueprintPure, Category = "Companion|Combat")
	bool CanReload() const;

	/** Current ammo in the equipped weapon's magazine. Returns 0 if no weapon. */
	UFUNCTION(BlueprintPure, Category = "Companion|Combat")
	int32 GetCurrentAmmo() const;

	/** Returns the reload time of the equipped weapon. Returns 0 if no weapon or no data. */
	UFUNCTION(BlueprintPure, Category = "Companion|Combat")
	float GetWeaponReloadTime() const;

	// --- Aim Inaccuracy ---

	void SetAimTarget(AActor* NewTarget);

	UFUNCTION(BlueprintPure, Category = "Companion|Combat")
	float GetCurrentInaccuracy() const;

	// --- Getters ---

	UFUNCTION(BlueprintPure, Category = "Companion")
	UHealthComponent* GetHealthComponent() const { return HealthComponent; }

	UFUNCTION(BlueprintPure, Category = "Companion")
	USuppressionComponent* GetSuppressionComponent() const { return SuppressionComponent; }

	UFUNCTION(BlueprintPure, Category = "Companion")
	UCoverPoseComponent* GetCoverPoseComponent() const { return CoverPoseComponent; }

	UFUNCTION(BlueprintPure, Category = "Companion")
	AWeaponBase* GetCurrentWeapon() const { return CurrentWeapon; }

	UFUNCTION(BlueprintPure, Category = "Companion")
	TSubclassOf<AWeaponBase> GetWeaponClass() const { return WeaponClass; }

	/** Target the companion is currently aiming at. Used by WeaponBase to aim along muzzle->target. */
	UFUNCTION(BlueprintPure, Category = "Companion|Combat")
	AActor* GetAimTarget() const { return CurrentAimTarget.Get(); }

	// --- Low Ready Aim ---

	UFUNCTION(BlueprintCallable, Category = "Companion|Combat")
	void SetLowReadyAim(bool bNewLowReady);

	UFUNCTION(BlueprintPure, Category = "Companion|Combat")
	bool IsLowReadyAim() const { return bLowReadyAim; }

	UPROPERTY(BlueprintAssignable, Category = "Companion|Combat")
	FOnLowReadyAimChanged OnLowReadyAimChanged;

	// --- Scripted Aim (route Alert/Crouch legs — weapon up along control rotation, no actor target) ---

	UFUNCTION(BlueprintCallable, Category = "Companion|Combat")
	void SetScriptedAim(bool bNewScriptedAim);

	UFUNCTION(BlueprintPure, Category = "Companion|Combat")
	bool IsScriptedAiming() const { return bScriptedAim; }

	// --- Sprint API ---

	UFUNCTION(BlueprintCallable, Category = "Companion|Movement")
	void SetSprinting(bool bSprint);

	UFUNCTION(BlueprintPure, Category = "Companion|Movement")
	bool IsSprinting() const { return bIsSprinting; }

	// --- Traversal ---

	UFUNCTION(BlueprintPure, Category = "Companion|Movement")
	UTraversalComponent* GetTraversalComponent() const { return TraversalComponent; }

	// --- Suppression / Health ---

	/** True if damage was received within Window seconds. Window <= 0 always returns false. */
	UFUNCTION(BlueprintPure, Category = "Companion|Combat")
	bool IsSuppressed(float Window) const;

	/** Health fraction [0,1]. Returns 1 if HealthComponent missing. */
	UFUNCTION(BlueprintPure, Category = "Companion|Combat")
	float GetHealthFraction() const;

	// --- Posture ---

	UFUNCTION(BlueprintPure, Category = "Companion")
	ECompanionPosture GetPosture() const { return Posture; }

	UFUNCTION(BlueprintCallable, Category = "Companion")
	void SetPosture(ECompanionPosture NewPosture);

	UPROPERTY(BlueprintAssignable, Category = "Companion")
	FOnCompanionPostureChanged OnPostureChanged;

	// --- Commanded Takedown (synced to player commit) ---

	/** Arms the companion for a coordinated takedown. Binds to the player's OnPlayerTakedownCommitted.
	 *  Knife: companion faces victim and waits at its current position.
	 *  Shoot: companion aims at the victim immediately.
	 *  Execution fires when the player commits (or deferred until in-position for knife). */
	UFUNCTION(BlueprintCallable, Category = "Companion|Takedown")
	void ArmCommandedTakedown(AActor* Victim, ETakedownMethod Method);

	/** Disarms without executing. Unbinds delegate, clears state. Safe to call when not armed. */
	UFUNCTION(BlueprintCallable, Category = "Companion|Takedown")
	void DisarmCommandedTakedown();

	/** True while armed and waiting for (or executing) a coordinated takedown. */
	UFUNCTION(BlueprintPure, Category = "Companion|Takedown")
	bool IsCommandedTakedownArmed() const { return bTakedownArmed; }

	/** True while the companion is in a crouched knife approach. Readable by the AnimInstance. */
	UFUNCTION(BlueprintPure, Category = "Companion|Takedown")
	bool IsInTakedownApproach() const { return bTakedownCrouchApproach; }

	/** True while a takedown montage is actively playing. */
	UFUNCTION(BlueprintPure, Category = "Companion|Takedown")
	bool IsTakedownMontagePlaying() const { return bTakedownMontagePlaying; }

	/** True from ExecuteCommandedTakedown entry until FinishCommandedTakedown/Disarm.
	 *  BT task uses this to transition Armed -> Executing and stop the hold timeout. */
	UFUNCTION(BlueprintPure, Category = "Companion|Takedown")
	bool IsCommandedTakedownExecuting() const { return bTakedownExecuting; }

	/** Autonomous (no player sync) execution trigger — used when the companion is
	 *  commanded to solo a lone target (no paired takedown partner). */
	UFUNCTION(BlueprintCallable, Category = "Companion|Takedown")
	void CommitTakedownNow();

	void SetTakedownCrouchApproach(bool bApproach) { bTakedownCrouchApproach = bApproach; }
	void SetTakedownInPosition(bool bInPos);


	/** Broadcast when a KNIFE commanded takedown begins executing — BP shows the knife mesh here. */
	UPROPERTY(BlueprintAssignable, Category = "Companion|Takedown")
	FOnCommandedTakedownStarted OnCommandedTakedownStarted;

	/** Broadcast when the companion's takedown execution completes (kill applied or shot fired). */
	UPROPERTY(BlueprintAssignable, Category = "Companion|Takedown")
	FOnCommandedTakedownFinished OnCommandedTakedownFinished;

protected:

	UPROPERTY(ReplicatedUsing = OnRep_Posture)
	ECompanionPosture Posture = ECompanionPosture::Exploration;

	UFUNCTION()
	void OnRep_Posture();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void PostInitializeComponents() override;

	// --- Components ---

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Companion|Components")
	TObjectPtr<UHealthComponent> HealthComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Companion|Components")
	TObjectPtr<USuppressionComponent> SuppressionComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Companion|Components")
	TObjectPtr<UCoverPoseComponent> CoverPoseComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Companion|Movement")
	TObjectPtr<UTraversalComponent> TraversalComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Companion|UI")
	TObjectPtr<UWidgetComponent> HealthWidgetComponent;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Companion|UI")
	TSubclassOf<UUserWidget> HealthWidgetClass;

	// --- Config ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Companion|Combat")
	TSubclassOf<AWeaponBase> WeaponClass;

	UPROPERTY(EditDefaultsOnly, Category = "Companion|Weapon")
	FName WeaponAttachSocket = TEXT("WeaponSocket");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Companion|Combat", meta = (ClampMin = "0.0"))
	float MaxInaccuracyDegrees = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Companion|Combat", meta = (ClampMin = "0.0"))
	float MinInaccuracyDegrees = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Companion|Combat", meta = (ClampMin = "0.1"))
	float InaccuracySettleTime = 1.5f;

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Companion|Combat", meta = (ClampMin = "0.0"))
	float MaxEngageRange = 3500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Companion|Combat", meta = (ClampMin = "1.0"))
	float RotationInterpSpeed = 8.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Companion|Revive", meta = (ClampMin = "0.5"))
	float ReviveDuration = 4.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Companion|Revive", meta = (ClampMin = "50.0"))
	float ReviveProximityRadius = 200.0f;

protected:

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Companion|Config", meta = (ClampMin = "0.0"))
	float DestroyDelay = 3.0f;

	// --- Movement ---

	UPROPERTY(EditDefaultsOnly, Category = "Companion|Movement")
	float WalkSpeed = 400.f;

	UPROPERTY(EditDefaultsOnly, Category = "Companion|Movement")
	float SprintSpeed = 650.f;

	UPROPERTY(EditDefaultsOnly, Category = "Companion|Movement")
	float CrouchedWalkSpeed = 150.f;

	// --- Takedown ---

	/** Knife mesh shown only during a knife takedown. Designer assigns SKM_Knife to it on BP_Companion.
	 *  Attached to KnifeAttachSocket on the body mesh, hidden + no-collision by default. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Takedown")
	TObjectPtr<USkeletalMeshComponent> TakedownKnifeMesh;

	/** Socket on the companion body mesh the takedown knife attaches to. */
	UPROPERTY(EditDefaultsOnly, Category = "Takedown")
	FName KnifeAttachSocket = TEXT("KnifeSocket");

	/** Knife takedown montage — designer assigns in BP. */
	UPROPERTY(EditDefaultsOnly, Category = "Companion|Takedown")
	TObjectPtr<UAnimMontage> KnifeTakedownMontage;

	/** Victim-relative-to-attacker placement for the knife takedown, in the shared facing frame:
	 *  X = forward gap (the companion stands this far BEHIND the victim), Y = lateral, Z = height.
	 *  Default 90 = the contact spacing the player takedown uses for this same ClavicleStabDown pair
	 *  (root motion is off, so the bodies are placed at the already-closed distance, not the demo's
	 *  wider at-rest spacing). Tunable per finisher on BP_Companion. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Companion|Takedown")
	FVector CommandedTakedownOffset = FVector(90.f, 0.f, 0.f);

	/** Shoot takedown: how many frames to hold aim before firing the lethal shot (legacy, unused). */
	UPROPERTY(EditDefaultsOnly, Category = "Companion|Takedown", meta = (ClampMin = "0.0"))
	float ShootAimSettleDelay = 0.15f;

	/** Hold duration after aiming-in before the first cosmetic shot. */
	UPROPERTY(EditDefaultsOnly, Category = "Companion|Takedown", meta = (ClampMin = "0.0"))
	float ShootAimInDuration = 1.0f;

	/** Number of cosmetic (visual-only) shots fired before the kill. */
	UPROPERTY(EditDefaultsOnly, Category = "Companion|Takedown", meta = (ClampMin = "1"))
	int32 ShootShotCount = 2;

	/** Gap between cosmetic shots. */
	UPROPERTY(EditDefaultsOnly, Category = "Companion|Takedown", meta = (ClampMin = "0.0"))
	float ShootShotInterval = 0.12f;

	/** Delay after the kill before lowering the weapon and releasing facing. */
	UPROPERTY(EditDefaultsOnly, Category = "Companion|Takedown", meta = (ClampMin = "0.0"))
	float ShootLowerDelay = 1.0f;

private:
	UPROPERTY(ReplicatedUsing = OnRep_LowReadyAim)
	bool bLowReadyAim = false;

	/** Scripted weapon-up: aim along control rotation with no actor target (e.g. route Alert/Crouch legs). Not replicated — single-player feature. */
	bool bScriptedAim = false;

	UFUNCTION()
	void OnRep_LowReadyAim();

	UPROPERTY(ReplicatedUsing = OnRep_IsSprinting)
	bool bIsSprinting = false;

	UFUNCTION()
	void OnRep_IsSprinting();

	UFUNCTION()
	void HandleDeath();

	UFUNCTION()
	void HandleRevive();

	UFUNCTION()
	void OnWeaponFiredCallback();

	UFUNCTION()
	void HandleTraversalStarted(ETraversalType Type, float PlayRate, FVector ObstacleLocation, FVector LandingLocation);

	UFUNCTION()
	void HandleTraversalEnded();

	UFUNCTION()
	void OnTraversalMontageEnded(UAnimMontage* Montage, bool bInterrupted);

	void DestroyAfterDeath();

	UPROPERTY(VisibleInstanceOnly, Category = "Companion|Tags")
	FGameplayTagContainer OwnedTags;

	UPROPERTY()
	TObjectPtr<AWeaponBase> CurrentWeapon;

	UPROPERTY()
	TWeakObjectPtr<AActor> CurrentAimTarget;

	float TimeAimingAtCurrentTarget = 0.0f;
	float LastDamageWorldTime = -1e9f;

	FTimerHandle DestroyTimerHandle;

	// --- Commanded takedown state ---

	UFUNCTION()
	void OnPlayerTakedownCommittedHandler();

	UFUNCTION()
	void OnPlayerFiredWeaponHandler();

	void ExecuteCommandedTakedown();
	void FinishCommandedTakedown();

	/** Cosmetic fire: plays the fire montage + weapon muzzle FX with no hitscan/damage/alert. */
	void FireCosmeticShotAt(const FVector& AimEndPoint);

	// Shoot takedown phased helpers (each phase re-arms ShootDelayTimerHandle for the next)
	void HandleTakedownAimedIn();
	void HandleTakedownKill();
	void HandleTakedownLower();

	UFUNCTION()
	void OnTakedownMontageEnded(UAnimMontage* Montage, bool bInterrupted);

	TWeakObjectPtr<AActor> TakedownVictim;
	TWeakObjectPtr<AExtractionPlayer> TakedownPlayerRef;
	ETakedownMethod TakedownActiveMethod = ETakedownMethod::Knife;
	FTimerHandle ShootDelayTimerHandle;
	bool bTakedownArmed = false;
	bool bTakedownPlayerCommitted = false;
	bool bTakedownInPosition = false;
	bool bTakedownExecuting = false;
	bool bTakedownCrouchApproach = false;
	bool bTakedownMontagePlaying = false;

	/** Remaining cosmetic shots in the shoot takedown sequence. Transient runtime counter. */
	int32 TakedownShotsRemaining = 0;
};
