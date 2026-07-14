// AStealthDisciplineVolume implementation.

#include "World/StealthDisciplineVolume.h"
#include "Character/ExtractionPlayer.h"
#include "Components/BoxComponent.h"
#include "WeaponComponent.h"
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
		bWarningSent = false;
		StartSampling();
	}

	if (!IsValid(PunishmentConfig))
		UE_LOG(LogExtraction, Warning, TEXT("StealthDisciplineVolume '%s': PunishmentConfig is unassigned -- escalation will be a silent no-op"), *GetNameSafe(this));
}

void AStealthDisciplineVolume::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	CleanupTrackedPlayer();

	if (const UWorld* World = GetWorld())
		World->GetTimerManager().ClearTimer(SamplingTimerHandle);

	Super::EndPlay(EndPlayReason);
}

// ---- Overlap ----

void AStealthDisciplineVolume::OnBeginOverlap(
	UPrimitiveComponent* /*OverlappedComp*/, AActor* OtherActor,
	UPrimitiveComponent* /*OtherComp*/, int32 /*OtherBodyIndex*/,
	bool /*bFromSweep*/, const FHitResult& /*SweepResult*/)
{
	if (!HasAuthority()) return;

	AExtractionPlayer* Player = Cast<AExtractionPlayer>(OtherActor);
	if (!IsValid(Player)) return;

	// Already tracking a player (re-entry or second player in co-op).
	if (TrackedPlayer.IsValid()) return;

	TrackedPlayer = Player;

	UWeaponComponent* WeaponComp = Player->GetWeaponComponent();
	BindShotRelay(WeaponComp);

	Accumulator.Reset();
	PendingNormalShots = 0;
	bWarningSent = false;

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

	CleanupTrackedPlayer();
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
	AExtractionPlayer* Player = TrackedPlayer.Get();
	if (!IsValid(Player))
	{
		// Player destroyed while inside (died, left, etc.).
		CleanupTrackedPlayer();
		return;
	}

	// Determine sprinting from horizontal speed.
	const UCharacterMovementComponent* MoveComp = Player->GetCharacterMovement();
	const float HorizontalSpeed = IsValid(MoveComp)
		? MoveComp->Velocity.Size2D()
		: 0.f;
	const bool bSprinting = HorizontalSpeed >= Settings.SprintSpeedThreshold;

	// Feed accumulated non-exempt shots since last sample.
	const int32 Shots = PendingNormalShots;
	PendingNormalShots = 0;

	const EStealthPressureTransition Result = Accumulator.Advance(
		SamplingIntervalSeconds, bSprinting, Shots, Settings);

	if (Result == EStealthPressureTransition::None) return;

	UWorld* World = GetWorld();
	if (!World) return;

	// Warned: show the warning toast once.
	if (Result == EStealthPressureTransition::Warned)
	{
		if (!bWarningSent)
		{
			bWarningSent = true;
			if (UMissionInventorySubsystem* Inventory = World->GetSubsystem<UMissionInventorySubsystem>())
			{
				Inventory->OnLootNotify.Broadcast(
					WarningText.IsEmpty()
						? NSLOCTEXT("StealthDiscipline", "DefaultWarning", "You are being too loud")
						: WarningText);
			}
		}
		return;
	}

	// Escalated: show warning if it was never sent (single sample crossed both thresholds),
	// then activate punishment.
	if (Result == EStealthPressureTransition::Escalated)
	{
		if (!bWarningSent)
		{
			bWarningSent = true;
			if (UMissionInventorySubsystem* Inventory = World->GetSubsystem<UMissionInventorySubsystem>())
			{
				Inventory->OnLootNotify.Broadcast(
					WarningText.IsEmpty()
						? NSLOCTEXT("StealthDiscipline", "DefaultWarning", "You are being too loud")
						: WarningText);
			}
		}

		UEnemyDirectorSubsystem* Director = World->GetSubsystem<UEnemyDirectorSubsystem>();
		if (Director)
		{
			const bool bActivated = Director->ActivatePunishmentProfile(this, PunishmentConfig, PunishmentPhase);
			if (!bActivated)
				UE_LOG(LogExtraction, Warning, TEXT("StealthDisciplineVolume '%s': ActivatePunishmentProfile FAILED (another source holds the slot, or config is null)"), *GetNameSafe(this));

			Director->TripAlarm();
		}

		UE_LOG(LogExtraction, Warning, TEXT("StealthDisciplineVolume '%s': ESCALATED"), *GetNameSafe(this));
	}
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
	++PendingNormalShots;
}

// ---- Cleanup ----

void AStealthDisciplineVolume::CleanupTrackedPlayer()
{
	StopSampling();
	UnbindShotRelay();

	if (UWorld* World = GetWorld())
	{
		if (UEnemyDirectorSubsystem* Director = World->GetSubsystem<UEnemyDirectorSubsystem>())
			Director->DeactivatePunishmentProfile(this);
	}

	TrackedPlayer.Reset();
	Accumulator.Reset();
	PendingNormalShots = 0;
	bWarningSent = false;
}
