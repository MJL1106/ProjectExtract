// Copyright Epic Games, Inc. All Rights Reserved.

#include "HealthComponent.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

UHealthComponent::UHealthComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UHealthComponent::BeginPlay()
{
	Super::BeginPlay();

	CurrentHealth = MaxHealth;
	CurrentShield = MaxShield;

	OnHealthChanged.Broadcast(CurrentHealth, MaxHealth);
	OnShieldChanged.Broadcast(CurrentShield, MaxShield);
}

void UHealthComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (const UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ShieldRegenDelayHandle);
		World->GetTimerManager().ClearTimer(ShieldRegenTickHandle);
	}

	Super::EndPlay(EndPlayReason);
}

void UHealthComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UHealthComponent, CurrentHealth);
	DOREPLIFETIME(UHealthComponent, CurrentShield);
	DOREPLIFETIME(UHealthComponent, bIsDead);
}

void UHealthComponent::InitializeHealth(float NewMaxHealth, float NewMaxShield)
{
	if (!GetOwner() || !GetOwner()->HasAuthority()) return;

	MaxHealth = FMath::Max(NewMaxHealth, 0.f);
	MaxShield = FMath::Max(NewMaxShield, 0.f);
	CurrentHealth = MaxHealth;
	CurrentShield = MaxShield;

	OnHealthChanged.Broadcast(CurrentHealth, MaxHealth);
	OnShieldChanged.Broadcast(CurrentShield, MaxShield);
}

void UHealthComponent::TakeDamage(float Damage)
{
	if (bIsDead || Damage <= 0.f) return;

	// Stop any active shield regen
	UWorld* World = GetWorld();
	if (!IsValid(World)) return;

	World->GetTimerManager().ClearTimer(ShieldRegenDelayHandle);
	World->GetTimerManager().ClearTimer(ShieldRegenTickHandle);

	LastDamageWorldTime = World->GetTimeSeconds();

	// Deplete shield first, overflow into health
	if (CurrentShield > 0.f)
	{
		const float ShieldDamage = FMath::Min(Damage, CurrentShield);
		CurrentShield = FMath::Max(CurrentShield - ShieldDamage, 0.f);
		Damage -= ShieldDamage;
		OnShieldChanged.Broadcast(CurrentShield, MaxShield);
	}

	if (Damage > 0.f)
	{
		CurrentHealth = FMath::Max(CurrentHealth - Damage, 0.f);
		OnHealthChanged.Broadcast(CurrentHealth, MaxHealth);
	}

	if (CurrentHealth <= 0.f)
	{
		Die();
		return;
	}

	// Restart shield regen delay
	World->GetTimerManager().SetTimer(
		ShieldRegenDelayHandle,
		this,
		&UHealthComponent::StartShieldRegen,
		ShieldRegenDelay,
		false
	);
}

void UHealthComponent::Heal(float Amount)
{
	if (bIsDead || Amount <= 0.f) return;

	CurrentHealth = FMath::Min(CurrentHealth + Amount, MaxHealth);
	OnHealthChanged.Broadcast(CurrentHealth, MaxHealth);
}

void UHealthComponent::Die()
{
	if (bIsDead) return;
	bIsDead = true;

	CurrentHealth = 0.f;

	UWorld* World = GetWorld();
	if (IsValid(World))
	{
		World->GetTimerManager().ClearTimer(ShieldRegenDelayHandle);
		World->GetTimerManager().ClearTimer(ShieldRegenTickHandle);
	}

	OnHealthChanged.Broadcast(CurrentHealth, MaxHealth);
	OnDeath.Broadcast();
}

void UHealthComponent::Revive(float HealthPercent)
{
	if (!bIsDead) return;
	if (HealthPercent <= 0.f) return;

	bIsDead = false;
	CurrentHealth = FMath::Clamp(HealthPercent * MaxHealth, 1.f, MaxHealth);
	CurrentShield = 0.f;

	OnHealthChanged.Broadcast(CurrentHealth, MaxHealth);
	OnShieldChanged.Broadcast(CurrentShield, MaxShield);
	OnRevive.Broadcast();

	// Kick off shield regen after the usual delay, same as the post-damage path.
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ShieldRegenDelayHandle);
		World->GetTimerManager().ClearTimer(ShieldRegenTickHandle);
		World->GetTimerManager().SetTimer(
			ShieldRegenDelayHandle,
			this,
			&UHealthComponent::StartShieldRegen,
			ShieldRegenDelay,
			false
		);
	}
}

void UHealthComponent::StartShieldRegen()
{
	if (bIsDead || CurrentShield >= MaxShield) return;

	UWorld* World = GetWorld();
	if (!IsValid(World)) return;

	World->GetTimerManager().SetTimer(
		ShieldRegenTickHandle,
		this,
		&UHealthComponent::RegenShield,
		0.1f,
		true
	);
}

void UHealthComponent::RegenShield()
{
	if (bIsDead)
	{
		if (const UWorld* World = GetWorld())
			World->GetTimerManager().ClearTimer(ShieldRegenTickHandle);
		return;
	}

	CurrentShield = FMath::Min(CurrentShield + ShieldRegenRate * 0.1f, MaxShield);
	OnShieldChanged.Broadcast(CurrentShield, MaxShield);

	if (CurrentShield >= MaxShield)
	{
		if (const UWorld* World = GetWorld())
			World->GetTimerManager().ClearTimer(ShieldRegenTickHandle);
	}
}

bool UHealthComponent::TryConsumeGatedDamage(float Now, float MinInterval)
{
	if ((Now - LastGatedDamageWorldTime) < MinInterval) return false;
	LastGatedDamageWorldTime = Now;
	return true;
}

void UHealthComponent::OnRep_CurrentHealth()
{
	OnHealthChanged.Broadcast(CurrentHealth, MaxHealth);
}

void UHealthComponent::OnRep_CurrentShield()
{
	OnShieldChanged.Broadcast(CurrentShield, MaxShield);
}
