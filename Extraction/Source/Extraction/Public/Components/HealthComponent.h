// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Net/UnrealNetwork.h"
#include "HealthComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnHealthChanged, float, CurrentHealth, float, MaxHealth);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnShieldChanged, float, CurrentShield, float, MaxShield);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnDeath);

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class EXTRACTION_API UHealthComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UHealthComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintCallable, Category = "Health")
	void TakeDamage(float Damage);

	UFUNCTION(BlueprintCallable, Category = "Health")
	void Heal(float Amount);

	UFUNCTION(BlueprintCallable, Category = "Health")
	void Die();

	UFUNCTION(BlueprintPure, Category = "Health")
	bool IsAlive() const { return CurrentHealth > 0.f; }

	UFUNCTION(BlueprintPure, Category = "Health")
	float GetCurrentHealth() const { return CurrentHealth; }

	UFUNCTION(BlueprintPure, Category = "Health")
	float GetMaxHealth() const { return MaxHealth; }

	UFUNCTION(BlueprintPure, Category = "Health")
	float GetCurrentShield() const { return CurrentShield; }

	UFUNCTION(BlueprintPure, Category = "Health")
	float GetMaxShield() const { return MaxShield; }

	UFUNCTION(BlueprintPure, Category = "Health")
	float GetHealthPercent() const { return MaxHealth > 0.f ? CurrentHealth / MaxHealth : 0.f; }

	UFUNCTION(BlueprintPure, Category = "Health")
	float GetShieldPercent() const { return MaxShield > 0.f ? CurrentShield / MaxShield : 0.f; }

	UPROPERTY(BlueprintAssignable, Category = "Health|Events")
	FOnHealthChanged OnHealthChanged;

	UPROPERTY(BlueprintAssignable, Category = "Health|Events")
	FOnShieldChanged OnShieldChanged;

	UPROPERTY(BlueprintAssignable, Category = "Health|Events")
	FOnDeath OnDeath;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Health", meta = (ClampMin = "0.0"))
	float MaxHealth = 100.f;

	UPROPERTY(ReplicatedUsing = OnRep_CurrentHealth, BlueprintReadOnly, Category = "Health")
	float CurrentHealth;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Shield", meta = (ClampMin = "0.0"))
	float MaxShield = 50.f;

	UPROPERTY(ReplicatedUsing = OnRep_CurrentShield, BlueprintReadOnly, Category = "Shield")
	float CurrentShield;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Shield", meta = (ClampMin = "0.0"))
	float ShieldRegenRate = 10.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Shield", meta = (ClampMin = "0.0"))
	float ShieldRegenDelay = 3.f;

private:
	UFUNCTION()
	void OnRep_CurrentHealth();

	UFUNCTION()
	void OnRep_CurrentShield();

	void StartShieldRegen();
	void RegenShield();

	FTimerHandle ShieldRegenDelayHandle;
	FTimerHandle ShieldRegenTickHandle;

	bool bIsDead = false;
};
