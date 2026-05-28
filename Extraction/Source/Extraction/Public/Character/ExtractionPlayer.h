// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "ExtractionTypes.h"
#include "Logging/LogMacros.h"
#include "Character/ExtractionPlayerInterface.h"
#include "ExtractionPlayer.generated.h"

class AWeaponBase;
class UInputComponent;
class UInputAction;
class UExtractionAnimInstance;
class UHealthComponent;
class UWeaponComponent;
class UTraversalComponent;
class UAnimMontage;
struct FInputActionValue;

// Distinct name from the legacy AExtractionCharacter declaration to avoid linker conflicts during the migration period.
// Rename to FOnDBNOStateChanged once AExtractionCharacter is retired (Phase 5).
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnPlayerDBNOStateChanged, bool, bNewIsDBNO, float, BleedoutDuration);

/**
 * Minimal C++ base for the kit-reparented player Blueprint.
 * The BP (duplicate of BP_FPCharacter) owns mesh, camera, spring arm, slide,
 * sprint, crouch, jump, and all kit procedural components. This class owns
 * gameplay components (health, weapon, traversal), DBNO/revive state, hit-region
 * damage routing, and the input handlers the kit doesn't provide.
 */
UCLASS()
class EXTRACTION_API AExtractionPlayer : public ACharacter, public IExtractionPlayerInterface
{
	GENERATED_BODY()

public:

	AExtractionPlayer();

	virtual void PostInitializeComponents() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaTime) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual float TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;
	virtual void NotifyControllerChanged() override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	// ---- Delegates ----

	UPROPERTY(BlueprintAssignable, Category = "Health|Events")
	FOnPlayerDBNOStateChanged OnDBNOStateChanged;

	// ---- BlueprintImplementableEvents ----

	/** Fired locally after the owning client receives the equipped weapon (or on server after equip).
	 *  BP implements this to call AC_ProceduralAnimation->NewHandPose. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Weapon|Events")
	void OnWeaponEquipped(AWeaponBase* EquippedWeapon);

	/** Fired locally when ADS state changes (input down = true, input up = false).
	 *  BP implements this to call AC_ProceduralAnimation->NewHandPose with Aim/Base pose. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Weapon|Events")
	void OnADSChanged(bool bIsADS);

	// ---- Input handlers (BlueprintCallable so kit BP can delegate if needed) ----

	UFUNCTION(BlueprintCallable, Category = "Input")
	virtual void DoAim(float Yaw, float Pitch) override;

	UFUNCTION(BlueprintCallable, Category = "Input")
	virtual void DoMove(float Right, float Forward);

	// ---- Revive API ----

	/** Exit DBNO state and restore health/movement. Called server-side. */
	virtual void ExitDBNO() override;

	// ---- IExtractionPlayerInterface ----

	UFUNCTION(BlueprintPure, Category = "Components")
	virtual UHealthComponent* GetHealthComponent() const override { return HealthComponent; }

	UFUNCTION(BlueprintPure, Category = "Components")
	virtual UWeaponComponent* GetWeaponComponent() const override { return WeaponComponent; }

	UFUNCTION(BlueprintPure, Category = "Components")
	virtual UTraversalComponent* GetTraversalComponent() const override { return TraversalComponent; }

	UFUNCTION(BlueprintPure, Category = "Health")
	virtual bool GetIsDBNO() const override { return bIsDBNO; }

	UFUNCTION(BlueprintPure, Category = "Animation")
	virtual UExtractionAnimInstance* GetExtractionAnimInstance() const override { return CachedAnimInstance; }

	virtual ETraversalType GetActiveTraversalType() const override;
	virtual bool IsInTraversal() const override;

	UFUNCTION(BlueprintPure, Category = "Movement")
	virtual bool GetIsVaulting() const override;

	UFUNCTION(BlueprintPure, Category = "Movement")
	virtual FVector GetVaultTargetLocation() const override;

	UFUNCTION(BlueprintPure, Category = "Movement")
	virtual float GetVaultSurfaceHeight() const override;

	/**
	 * Try to start a traversal at the player's current location. Intended to be called
	 * from the kit BP's Jump handler before invoking Jump() — if this returns true,
	 * the jump should be skipped because a traversal is now playing.
	 *
	 * Returns true if a traversal was initiated, false if no obstacle was detected
	 * or the player is in a state that blocks traversal (DBNO, already in traversal,
	 * falling).
	 */
	UFUNCTION(BlueprintCallable, Category = "Movement|Traversal")
	bool TryStartTraversal();

	/** No WeaponSpawn component on this class — kit BP attaches via socket directly. */
	virtual USceneComponent* GetWeaponSpawn() const override { return nullptr; }

	virtual void NotifyWeaponEquipped(AWeaponBase* EquippedWeapon) override { OnWeaponEquipped(EquippedWeapon); }
	virtual void NotifyADSChanged(bool bIsADS) override { OnADSChanged(bIsADS); }

protected:

	// ---- Gameplay Components ----

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UHealthComponent> HealthComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UWeaponComponent> WeaponComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UTraversalComponent> TraversalComponent;

	// ---- Input Actions (assigned in BP child class) ----

	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<UInputAction> MoveAction;

	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<UInputAction> LookAction;

	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<UInputAction> MouseLookAction;

	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<UInputAction> FireAction;

	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<UInputAction> ReloadAction;

	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<UInputAction> ADSAction;

	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<UInputAction> VaultAction;

	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<UInputAction> InteractAction;

	// ---- DBNO / Revive Config ----

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

	UPROPERTY(ReplicatedUsing = OnRep_IsDBNO, BlueprintReadOnly, Category = "Health|State")
	bool bIsDBNO = false;

	UPROPERTY(BlueprintReadOnly, Category = "Health|State")
	float BleedoutTimeRemaining = 0.f;

private:

	// ---- Input Handlers ----

	void MoveInput(const FInputActionValue& Value);
	void LookInput(const FInputActionValue& Value);

	void FireStart(const FInputActionValue& Value);
	void FireStop(const FInputActionValue& Value);
	void ReloadStart(const FInputActionValue& Value);
	void ADSStart(const FInputActionValue& Value);
	void ADSStop(const FInputActionValue& Value);

	void VaultStart(const FInputActionValue& Value);

	void InteractStart(const FInputActionValue& Value);
	void InteractStop(const FInputActionValue& Value);

	// ---- Traversal ----

	void HandleTraversalStarted(ETraversalType Type, float PlayRate, FVector ObstacleLocation, FVector LandingLocation);
	void HandleTraversalEnded();

	UFUNCTION()
	void OnTraversalMontageEnded(UAnimMontage* Montage, bool bInterrupted);

	// ---- Health / DBNO ----

	UFUNCTION()
	void HandleDeath();

	void EnterDBNO();
	void OnBleedoutExpired();
	void FullDeath();

	float GetHitboxDamageMultiplier(const FDamageEvent& DamageEvent) const;

	UFUNCTION()
	void OnRep_IsDBNO();

	/** Temp debug: apply 25 damage to self (bound to H key) */
	void DebugApplyDamage();

	FTimerHandle BleedoutTimerHandle;

	UPROPERTY()
	TObjectPtr<UExtractionAnimInstance> CachedAnimInstance;

	// ---- Revive ----

	void UpdateRevive(float DeltaTime);
	AExtractionPlayer* FindReviveTarget() const;
	void CancelRevive();
	void CompleteRevive();

	UFUNCTION(Server, Reliable)
	void Server_CompleteRevive(AExtractionPlayer* Target);

	UPROPERTY()
	TObjectPtr<AExtractionPlayer> ReviveTarget;

	float ReviveElapsed = 0.f;
	bool bIsReviving = false;
};
