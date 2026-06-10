// AI companion character — follows player, engages enemies, revives downed teammates.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "GameplayTagAssetInterface.h"
#include "GameplayTagContainer.h"
#include "GenericTeamAgentInterface.h"
#include "Movement/TraversalTypes.h"
#include "Companion/CompanionTypes.h"
#include "AIShooterInterface.h"
#include "CompanionCharacter.generated.h"

class UHealthComponent;
class USuppressionComponent;
class AWeaponBase;
class UCompanionAnimInstance;
class UTraversalComponent;
class UWidgetComponent;
class UUserWidget;

DECLARE_LOG_CATEGORY_EXTERN(LogCompanion, Log, All);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCompanionPostureChanged, ECompanionPosture, NewPosture);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnLowReadyAimChanged, bool, bIsLowReady);

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
	float MaxEngageRange = 2500.0f;

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

private:
	UPROPERTY(ReplicatedUsing = OnRep_LowReadyAim)
	bool bLowReadyAim = false;

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
};
