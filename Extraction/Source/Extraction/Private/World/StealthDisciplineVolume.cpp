// AStealthDisciplineVolume implementation.

#include "World/StealthDisciplineVolume.h"
#include "Character/ExtractionPlayer.h"
#include "Components/BoxComponent.h"
#include "Kismet/GameplayStatics.h"
#include "WeaponComponent.h"
#include "WeaponBase.h"
#include "Audio/MusicSubsystem.h"
#include "Enemy/EnemyDirectorSubsystem.h"
#include "Enemy/Director/DirectorConfigData.h"
#include "Game/MissionInventorySubsystem.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "TimerManager.h"
#include "Extraction.h"

AStealthDisciplineVolume::AStealthDisciplineVolume()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;

	BoxComponent = CreateDefaultSubobject<UBoxComponent>(TEXT("BoxComponent"));
	SetRootComponent(BoxComponent);
	BoxComponent->SetCollisionProfileName(UCollisionProfile::CustomCollisionProfileName);
	BoxComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	BoxComponent->SetCollisionResponseToAllChannels(ECR_Ignore);
	BoxComponent->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	BoxComponent->SetGenerateOverlapEvents(true);
	BoxComponent->SetBoxExtent(FVector(500.f, 500.f, 200.f));
}

void AStealthDisciplineVolume::BeginPlay()
{
	Super::BeginPlay();

	if (!HasAuthority()) return;

	// Defend waves supersede stealth discipline: once a wave is live this volume stays inert.
	if (UEnemyDirectorSubsystem* Director = GetWorld()->GetSubsystem<UEnemyDirectorSubsystem>())
	{
		if (Director->IsWaveActive())
		{
			DisableForWave();
			return;
		}
		Director->OnDirectorWaveStarted.AddDynamic(this, &AStealthDisciplineVolume::OnWaveStarted);
	}

	ObjectiveFlow = Cast<ALevelObjectiveFlow>(
		UGameplayStatics::GetActorOfClass(GetWorld(), ALevelObjectiveFlow::StaticClass()));

	// Fail open: a level with no objective flow (or gating switched off) must behave exactly as it
	// did before the gate existed rather than silently disabling the volume for the whole run.
	if (!bGateOnObjectiveStep || !ObjectiveFlow.IsValid())
	{
		bArmed = true;
		UE_LOG(LogExtraction, Log, TEXT("StealthDisciplineVolume '%s': objective gate inactive (%s) -- armed immediately"),
			*GetNameSafe(this),
			bGateOnObjectiveStep ? TEXT("no ALevelObjectiveFlow in level") : TEXT("bGateOnObjectiveStep is false"));
	}
	else
	{
		// Early-out only -- the SampleTick poll is what actually guarantees arming, since actor
		// BeginPlay order isn't deterministic and the flow may not have activated yet.
		UpdateArmedState();
	}

	BoxComponent->OnComponentBeginOverlap.AddDynamic(this, &AStealthDisciplineVolume::OnBeginOverlap);
	BoxComponent->OnComponentEndOverlap.AddDynamic(this, &AStealthDisciplineVolume::OnEndOverlap);

	// Catch players already inside at spawn (initial overlap fires during Super::BeginPlay,
	// before AddDynamic runs).
	TArray<AActor*> Overlapping;
	BoxComponent->GetOverlappingActors(Overlapping, AExtractionPlayer::StaticClass());
	for (AActor* Actor : Overlapping)
	{
		AExtractionPlayer* Player = Cast<AExtractionPlayer>(Actor);
		if (!IsValid(Player)) continue;
		if (TrackedPlayer.IsValid()) break;

		TrackedPlayer = Player;
		BindShotRelay(Player->GetWeaponComponent());
		Accumulator.Reset();
		PendingNormalShots = 0;
		PendingSuppressedShots = 0;
		bWarningSent = false;
		bPlayerInsideVolume = true;
		SetStealthMusicActive(true);
		StartSampling();
	}

	if (!IsValid(PunishmentConfig))
		UE_LOG(LogExtraction, Warning, TEXT("StealthDisciplineVolume '%s': PunishmentConfig is unassigned -- escalation will be a silent no-op"), *GetNameSafe(this));
}

void AStealthDisciplineVolume::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	CleanupTrackedPlayer();

	if (const UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(SamplingTimerHandle);

		if (UEnemyDirectorSubsystem* Director = World->GetSubsystem<UEnemyDirectorSubsystem>())
		{
			Director->DeactivatePunishmentProfile(this);
			Director->OnDirectorWaveStarted.RemoveDynamic(this, &AStealthDisciplineVolume::OnWaveStarted);
		}
	}

	Super::EndPlay(EndPlayReason);
}

// ---- Overlap ----

void AStealthDisciplineVolume::OnBeginOverlap(
	UPrimitiveComponent* /*OverlappedComp*/, AActor* OtherActor,
	UPrimitiveComponent* /*OtherComp*/, int32 /*OtherBodyIndex*/,
	bool /*bFromSweep*/, const FHitResult& /*SweepResult*/)
{
	if (!HasAuthority()) return;
	if (bPermanentlyDisabled) return;

	AExtractionPlayer* Player = Cast<AExtractionPlayer>(OtherActor);
	if (!IsValid(Player)) return;

	// Re-entry: the player walked back in. Resume full sampling with pressure intact.
	if (TrackedPlayer == Player)
	{
		bPlayerInsideVolume = true;
		SetStealthMusicActive(true);
		PendingNormalShots = 0;
		PendingSuppressedShots = 0;

		// Rebind the shot relay if it was unbound during escalated-outside sampling stop.
		if (!BoundWeaponComponent.IsValid())
			BindShotRelay(Player->GetWeaponComponent());

		if (!GetWorldTimerManager().IsTimerActive(SamplingTimerHandle))
			StartSampling();

		UE_LOG(LogExtraction, Verbose, TEXT("StealthDisciplineVolume '%s': player '%s' re-entered (pressure %.1f)"),
			*GetNameSafe(this), *GetNameSafe(Player), Accumulator.Pressure);
		return;
	}

	HandleNewPlayerEntry(Player);
}

void AStealthDisciplineVolume::HandleNewPlayerEntry(AExtractionPlayer* Player)
{
	if (TrackedPlayer.IsValid()) return;

	TrackedPlayer = Player;
	BindShotRelay(Player->GetWeaponComponent());

	Accumulator.Reset();
	PendingNormalShots = 0;
	PendingSuppressedShots = 0;
	bWarningSent = false;
	bPlayerInsideVolume = true;
	SetStealthMusicActive(true);

	StartSampling();

	UE_LOG(LogExtraction, Verbose, TEXT("StealthDisciplineVolume '%s': player '%s' entered"),
		*GetNameSafe(this), *GetNameSafe(Player));
}

void AStealthDisciplineVolume::OnEndOverlap(
	UPrimitiveComponent* /*OverlappedComp*/, AActor* OtherActor,
	UPrimitiveComponent* /*OtherComp*/, int32 /*OtherBodyIndex*/)
{
	if (!HasAuthority()) return;
	if (OtherActor != TrackedPlayer.Get()) return;

	// Leaving the volume does NOT wipe pressure or stop sampling. Pressure bleeds via
	// decay-only ticks. Sprint/shot accrual is gated on bPlayerInsideVolume in SampleTick.
	bPlayerInsideVolume = false;
	SetStealthMusicActive(false);

	UE_LOG(LogExtraction, Verbose, TEXT("StealthDisciplineVolume '%s': player exited (pressure %.1f, decay-only sampling continues)"),
		*GetNameSafe(this), Accumulator.Pressure);
}

// ---- Sampling ----

void AStealthDisciplineVolume::StartSampling()
{
	UWorld* World = GetWorld();
	if (!World) return;

	World->GetTimerManager().SetTimer(
		SamplingTimerHandle, this, &AStealthDisciplineVolume::SampleTick,
		SamplingIntervalSeconds, true);
}

void AStealthDisciplineVolume::StopSampling()
{
	if (const UWorld* World = GetWorld())
		World->GetTimerManager().ClearTimer(SamplingTimerHandle);
}

void AStealthDisciplineVolume::SampleTick()
{
	UpdateArmedState();

	AExtractionPlayer* Player = TrackedPlayer.Get();
	if (!IsValid(Player))
	{
		// Player destroyed (died, left level, etc.). Deactivate punishment -- death ends the
		// discipline, but walking OUT does not (the profile survives exit).
		if (UWorld* World = GetWorld())
		{
			if (UEnemyDirectorSubsystem* Director = World->GetSubsystem<UEnemyDirectorSubsystem>())
				Director->DeactivatePunishmentProfile(this);
		}
		CleanupTrackedPlayer();
		return;
	}

	if (bPlayerInsideVolume)
		SampleTickInside(Player);
	else
		SampleTickOutside();
}

void AStealthDisciplineVolume::SampleTickOutside()
{
	// Escalated and outside the volume -- nothing left to observe.
	if (Accumulator.bEscalated)
	{
		StopSampling();
		UnbindShotRelay();
		return;
	}

	Accumulator.Advance(SamplingIntervalSeconds, false, 0, 0, Settings);

	PendingNormalShots = 0;
	PendingSuppressedShots = 0;

	if (Accumulator.Pressure <= 0.f)
	{
		StopSampling();
		UE_LOG(LogExtraction, Verbose, TEXT("StealthDisciplineVolume '%s': pressure decayed to zero outside volume -- sampling stopped"),
			*GetNameSafe(this));
	}
}

void AStealthDisciplineVolume::SampleTickInside(AExtractionPlayer* Player)
{
	// Unarmed: overlap tracking, the shot relay and the stealth music all keep running, but nothing
	// is scored. Pending shots are dropped so an unarmed burst can't bank up and land in one lump
	// the instant the gate opens.
	if (!bArmed)
	{
		PendingNormalShots = 0;
		PendingSuppressedShots = 0;

		if (!bLoggedUnarmedEntry)
		{
			bLoggedUnarmedEntry = true;
			const ALevelObjectiveFlow* Flow = ObjectiveFlow.Get();
			UE_LOG(LogExtraction, Warning, TEXT("StealthDisciplineVolume '%s': player inside but NOT armed -- objective flow %s, arms after step %d"),
				*GetNameSafe(this),
				IsValid(Flow)
					? *FString::Printf(TEXT("is at step %d"), static_cast<int32>(Flow->GetCurrentStep()))
					: TEXT("reference is stale/destroyed -- volume can never arm"),
				static_cast<int32>(ArmAfterStep));
		}

		return;
	}

	const UCharacterMovementComponent* MoveComp = Player->GetCharacterMovement();
	const float HorizontalSpeed = IsValid(MoveComp) ? MoveComp->Velocity.Size2D() : 0.f;
	const bool bSprinting = HorizontalSpeed >= Settings.SprintSpeedThreshold;

	const int32 NormalShots = PendingNormalShots;
	const int32 SuppressedShots = PendingSuppressedShots;
	PendingNormalShots = 0;
	PendingSuppressedShots = 0;

	const EStealthPressureTransition Result = Accumulator.Advance(
		SamplingIntervalSeconds, bSprinting, NormalShots, SuppressedShots, Settings);

	if (Result != EStealthPressureTransition::None)
		HandlePressureTransition(Result);
}

// ---- Objective gate ----

void AStealthDisciplineVolume::UpdateArmedState()
{
	if (bArmed) return;

	const ALevelObjectiveFlow* Flow = ObjectiveFlow.Get();
	if (!IsValid(Flow)) return;

	const ELevelObjectiveStep CurrentStep = Flow->GetCurrentStep();
	if (static_cast<uint8>(CurrentStep) <= static_cast<uint8>(ArmAfterStep)) return;

	bArmed = true;

	UE_LOG(LogExtraction, Log, TEXT("StealthDisciplineVolume '%s': armed -- objective flow advanced past step %d (now %d)"),
		*GetNameSafe(this), static_cast<int32>(ArmAfterStep), static_cast<int32>(CurrentStep));
}

void AStealthDisciplineVolume::BroadcastToast(UWorld* World, const FText& Message, EToastSeverity Severity)
{
	if (UMissionInventorySubsystem* Inventory = World->GetSubsystem<UMissionInventorySubsystem>())
		Inventory->OnToastNotify.Broadcast(Message, Severity);
}

void AStealthDisciplineVolume::HandlePressureTransition(EStealthPressureTransition Result)
{
	UWorld* World = GetWorld();
	if (!World) return;

	if (Result == EStealthPressureTransition::Warned)
	{
		if (bWarningSent) return;
		bWarningSent = true;

		BroadcastToast(World,
			WarningText.IsEmpty()
				? NSLOCTEXT("StealthDiscipline", "DefaultWarning", "Discipline slipping")
				: WarningText,
			EToastSeverity::Warning);
		return;
	}

	// Escalated: activate punishment. Fires unconditionally, even if the warning
	// threshold was skipped entirely (e.g. a burst of unsuppressed shots in one tick).
	UEnemyDirectorSubsystem* Director = World->GetSubsystem<UEnemyDirectorSubsystem>();

	if (Director && Director->IsWaveActive())
	{
		DisableForWave();
		return;
	}

	BroadcastToast(World,
		EscalationText.IsEmpty()
			? NSLOCTEXT("StealthDiscipline", "DefaultEscalation", "Alerted nearby enemies")
			: EscalationText,
		EToastSeverity::Alert);

	if (Director)
	{
		const bool bActivated = Director->ActivatePunishmentProfile(this, PunishmentConfig, PunishmentPhase);
		if (!bActivated)
			UE_LOG(LogExtraction, Warning, TEXT("StealthDisciplineVolume '%s': ActivatePunishmentProfile FAILED"), *GetNameSafe(this));
		Director->TripAlarm();
	}

	UE_LOG(LogExtraction, Warning, TEXT("StealthDisciplineVolume '%s': ESCALATED"), *GetNameSafe(this));
}

// ---- Shot relay ----

void AStealthDisciplineVolume::BindShotRelay(UWeaponComponent* WeaponComp)
{
	UnbindShotRelay();

	if (!IsValid(WeaponComp)) return;

	BoundWeaponComponent = WeaponComp;
	WeaponComp->OnPlayerWeaponShot.AddDynamic(this, &AStealthDisciplineVolume::OnPlayerShotFired);
}

void AStealthDisciplineVolume::UnbindShotRelay()
{
	UWeaponComponent* Comp = BoundWeaponComponent.Get();
	if (IsValid(Comp))
		Comp->OnPlayerWeaponShot.RemoveDynamic(this, &AStealthDisciplineVolume::OnPlayerShotFired);

	BoundWeaponComponent.Reset();
}

void AStealthDisciplineVolume::OnPlayerShotFired(bool bStealthExempt)
{
	if (bStealthExempt) return;
	if (!bPlayerInsideVolume) return;

	// Resolve suppression at shot time via the tracked player's current weapon.
	UWeaponComponent* WeaponComp = BoundWeaponComponent.Get();
	AWeaponBase* CurrentWeapon = IsValid(WeaponComp) ? WeaponComp->GetCurrentWeapon() : nullptr;
	const bool bSuppressed = IsValid(CurrentWeapon) && CurrentWeapon->IsSuppressedEffective();

	if (bSuppressed)
	{
		++PendingSuppressedShots;
	}
	else
	{
		++PendingNormalShots;
	}
}

// ---- Wave gate ----

void AStealthDisciplineVolume::OnWaveStarted(FName WaveId)
{
	DisableForWave();

	UE_LOG(LogExtraction, Log, TEXT("StealthDisciplineVolume '%s': wave '%s' started -- discipline disabled for the rest of the level"),
		*GetNameSafe(this), *WaveId.ToString());
}

void AStealthDisciplineVolume::DisableForWave()
{
	if (bPermanentlyDisabled) return;
	bPermanentlyDisabled = true;

	CleanupTrackedPlayer();

	if (UWorld* World = GetWorld())
	{
		if (UEnemyDirectorSubsystem* Director = World->GetSubsystem<UEnemyDirectorSubsystem>())
		{
			Director->DeactivatePunishmentProfile(this);
			Director->OnDirectorWaveStarted.RemoveDynamic(this, &AStealthDisciplineVolume::OnWaveStarted);
		}
	}
}

// ---- Stealth music ----

void AStealthDisciplineVolume::SetStealthMusicActive(bool bActive)
{
	if (bStealthMusicActive == bActive) return;

	const UWorld* World = GetWorld();
	UMusicSubsystem* Music = World ? World->GetSubsystem<UMusicSubsystem>() : nullptr;
	if (!IsValid(Music)) return;

	// Assigned after the resolve so a null subsystem leaves the flag unchanged -- a later call
	// with the same value retries rather than silently no-oping with a desynced count.
	bStealthMusicActive = bActive;

	if (bActive)
		Music->EnterStealthZone();
	else
		Music->ExitStealthZone();
}

// ---- Cleanup ----

void AStealthDisciplineVolume::CleanupTrackedPlayer()
{
	SetStealthMusicActive(false);
	StopSampling();
	UnbindShotRelay();

	TrackedPlayer.Reset();
	Accumulator.Reset();
	PendingNormalShots = 0;
	PendingSuppressedShots = 0;
	bWarningSent = false;
	bPlayerInsideVolume = false;
}
