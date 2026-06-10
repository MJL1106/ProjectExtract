// UEnemySniperTelegraphComponent

#include "EnemySniperTelegraphComponent.h"
#include "EnemyArchetypeData.h"
#include "TimerManager.h"
#include "Engine/World.h"

UEnemySniperTelegraphComponent::UEnemySniperTelegraphComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UEnemySniperTelegraphComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(TelegraphTimerHandle);
		World->GetTimerManager().ClearTimer(ShotCooldownTimerHandle);
	}

	Super::EndPlay(EndPlayReason);
}

void UEnemySniperTelegraphComponent::InitFromArchetype(const UEnemyArchetypeData* Data)
{
	if (!IsValid(Data)) return;

	SniperAimTelegraphTime = Data->SniperAimTelegraphTime;
	SniperShotCooldown = Data->SniperShotCooldown;
}

void UEnemySniperTelegraphComponent::BeginTelegraph(AActor* Target)
{
	if (!CanBeginTelegraph()) return;

	TelegraphTarget = Target;
	bTelegraphing = true;
	bReadyToFire = false;

	OnLaserChanged.Broadcast(true, Target);

	if (UWorld* World = GetWorld(); IsValid(World))
	{
		World->GetTimerManager().SetTimer(
			TelegraphTimerHandle,
			this,
			&UEnemySniperTelegraphComponent::OnTelegraphElapsed,
			SniperAimTelegraphTime,
			false);
	}
}

void UEnemySniperTelegraphComponent::CancelTelegraph()
{
	if (!bTelegraphing) return;

	bTelegraphing = false;
	bReadyToFire = false;

	if (UWorld* World = GetWorld())
		World->GetTimerManager().ClearTimer(TelegraphTimerHandle);

	OnLaserChanged.Broadcast(false, TelegraphTarget.Get());
	TelegraphTarget = nullptr;
}

void UEnemySniperTelegraphComponent::NotifyShotFired()
{
	bTelegraphing = false;
	bReadyToFire = false;
	bShotCooldownActive = true;
	ShotsSinceRelocate++;

	OnLaserChanged.Broadcast(false, nullptr);
	TelegraphTarget = nullptr;

	if (UWorld* World = GetWorld(); IsValid(World))
	{
		World->GetTimerManager().SetTimer(
			ShotCooldownTimerHandle,
			this,
			&UEnemySniperTelegraphComponent::OnShotCooldownElapsed,
			SniperShotCooldown,
			false);
	}
}

void UEnemySniperTelegraphComponent::ResetRelocateCounter()
{
	ShotsSinceRelocate = 0;
}

void UEnemySniperTelegraphComponent::OnTelegraphElapsed()
{
	bReadyToFire = true;
}

void UEnemySniperTelegraphComponent::OnShotCooldownElapsed()
{
	bShotCooldownActive = false;
}
