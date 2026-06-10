// Shared suppression component — near-miss fire raises value, fast decay, hysteresis.

#include "SuppressionComponent.h"
#include "TimerManager.h"

USuppressionComponent::USuppressionComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void USuppressionComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (const UWorld* World = GetWorld())
		World->GetTimerManager().ClearTimer(DecayTimerHandle);

	Super::EndPlay(EndPlayReason);
}

void USuppressionComponent::DeactivateForDeath()
{
	SuppressionValue = 0.f;
	bIsSuppressed = false;

	if (const UWorld* World = GetWorld())
		World->GetTimerManager().ClearTimer(DecayTimerHandle);
}

void USuppressionComponent::RegisterNearMiss(float Weight)
{
	if (Weight <= 0.f) return;

	const float EffectiveResistance = FMath::Max(Resistance, 0.01f);
	const float Delta = (SuppressionPerNearMiss * Weight) / EffectiveResistance;
	SuppressionValue = FMath::Clamp(SuppressionValue + Delta, 0.f, 1.f);

	if (!bIsSuppressed && SuppressionValue >= SuppressedThreshold)
	{
		bIsSuppressed = true;
		OnSuppressedStateChanged.Broadcast(true);
	}

	StartDecayTimer();
}

void USuppressionComponent::ConfigureSuppression(float InResistance)
{
	Resistance = FMath::Max(InResistance, 0.01f);
}

void USuppressionComponent::StartDecayTimer()
{
	if (DecayTimerHandle.IsValid()) return;

	const UWorld* World = GetWorld();
	if (!World) return;

	const float RandomOffset = FMath::FRandRange(0.f, DecayTickInterval * 0.5f);
	World->GetTimerManager().SetTimer(
		DecayTimerHandle,
		this,
		&USuppressionComponent::OnDecayTick,
		DecayTickInterval,
		true,
		RandomOffset
	);
}

void USuppressionComponent::OnDecayTick()
{
	SuppressionValue -= DecayPerSecond * DecayTickInterval;

	if (SuppressionValue <= 0.f)
	{
		SuppressionValue = 0.f;

		if (const UWorld* World = GetWorld())
			World->GetTimerManager().ClearTimer(DecayTimerHandle);

		if (bIsSuppressed)
		{
			bIsSuppressed = false;
			OnSuppressedStateChanged.Broadcast(false);
		}
		return;
	}

	if (bIsSuppressed && SuppressionValue < UnsuppressedThreshold)
	{
		bIsSuppressed = false;
		OnSuppressedStateChanged.Broadcast(false);
	}
}
