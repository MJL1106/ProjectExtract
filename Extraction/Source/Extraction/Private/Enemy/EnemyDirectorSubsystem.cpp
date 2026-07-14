// UEnemyDirectorSubsystem — v1 alert ladder + v2 tension director + spawn pipeline.

#include "EnemyDirectorSubsystem.h"
#include "EnemyAIController.h"
#include "EnemyCharacter.h"
#include "EnemyArchetypeData.h"
#include "EnemyAwarenessComponent.h"
#include "DirectorConfigData.h"
#include "EnemyDirectorScopeVolume.h"
#include "EnemySpawnZone.h"
#include "HealthComponent.h"
#include "EnemySquadSubsystem.h"
#include "Squad/EnemySquad.h"
#include "Companion/CompanionCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Kismet/GameplayStatics.h"
#include "NavigationSystem.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "EngineUtils.h"

// ============================================================
// Lifecycle
// ============================================================

void UEnemyDirectorSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	if (!OnEnemyDied.IsAlreadyBound(this, &UEnemyDirectorSubsystem::HandleEnemyKilled))
	{
		OnEnemyDied.AddDynamic(this, &UEnemyDirectorSubsystem::HandleEnemyKilled);
	}
}

void UEnemyDirectorSubsystem::Deinitialize()
{
	UWorld* World = GetWorld();
	if (IsValid(World))
	{
		World->GetTimerManager().ClearTimer(DirectorTimerHandle);
	}

	OnEnemyDied.RemoveDynamic(this, &UEnemyDirectorSubsystem::HandleEnemyKilled);

	SpawnZones.Empty();
	ScopeVolumes.Empty();
	Corpses.Empty();
	WaveMembers.Empty();
	PunishmentSource.Reset();
	PunishmentConfig = nullptr;

	Super::Deinitialize();
}

// ============================================================
// v1 API (preserved)
// ============================================================

void UEnemyDirectorSubsystem::ReportEnemySearching()
{
	Escalate(EGlobalAlertLevel::Searching);
}

void UEnemyDirectorSubsystem::ReportEnemyCombat()
{
	// Stamped before Escalate — Escalate early-outs once already Loud, but the event time must
	// keep refreshing for every enemy that enters Combat (companion stealth-break reads it).
	if (const UWorld* World = GetWorld())
		LastCombatReportTime = World->GetTimeSeconds();
	Escalate(EGlobalAlertLevel::Loud);
}

void UEnemyDirectorSubsystem::ReportBodyDiscovered()
{
	++BodiesDiscovered;

	const bool bEscalateToLoud = BodiesDiscovered >= 2 || AlertLevel >= EGlobalAlertLevel::Searching;
	Escalate(bEscalateToLoud ? EGlobalAlertLevel::Loud : EGlobalAlertLevel::Searching);
}

void UEnemyDirectorSubsystem::TripAlarm()
{
	Escalate(EGlobalAlertLevel::Loud);
}

void UEnemyDirectorSubsystem::RegisterCorpse(AEnemyCharacter* Corpse)
{
	if (!IsValid(Corpse)) return;

	const bool bOfficer = IsValid(Corpse->GetArchetypeData()) && Corpse->GetArchetypeData()->bHasCommandAura;
	OnEnemyDied.Broadcast(Corpse, Corpse->GetCorpseLocation(), bOfficer);

	Corpses.RemoveAll([](const TWeakObjectPtr<AEnemyCharacter>& Entry) { return !Entry.IsValid(); });
	Corpses.Add(Corpse);

	while (Corpses.Num() > MaxCorpses)
	{
		// Prefer evicting the oldest corpse that is NOT currently an investigate target.
		int32 EvictIndex = INDEX_NONE;
		for (int32 i = 0; i < Corpses.Num(); ++i)
		{
			AEnemyCharacter* Candidate = Corpses[i].Get();
			if (!IsValid(Candidate)) { EvictIndex = i; break; }
			if (!Candidate->IsBeingInvestigated()) { EvictIndex = i; break; }
		}
		// All corpses targeted — evict the oldest anyway to stay bounded.
		if (EvictIndex == INDEX_NONE) EvictIndex = 0;

		if (AEnemyCharacter* Evicted = Corpses[EvictIndex].Get())
			Evicted->Destroy();
		Corpses.RemoveAt(EvictIndex);
	}
}

void UEnemyDirectorSubsystem::Escalate(EGlobalAlertLevel NewLevel)
{
	if (NewLevel <= AlertLevel) return;

	const EGlobalAlertLevel OldLevel = AlertLevel;
	AlertLevel = NewLevel;

	UE_LOG(LogEnemyAI, Log, TEXT("Global alert: %d -> %d"), static_cast<int32>(OldLevel), static_cast<int32>(NewLevel));
	OnGlobalAlertChanged.Broadcast(OldLevel, NewLevel);

	if (NewLevel != EGlobalAlertLevel::Loud || DirectorTimerHandle.IsValid()) return;

	UWorld* World = GetWorld();
	if (!IsValid(World)) return;

	RecentKills = 0;

	const float RandomOffset = FMath::FRandRange(0.f, 0.5f);
	World->GetTimerManager().SetTimer(
		DirectorTimerHandle,
		this,
		&UEnemyDirectorSubsystem::DirectorTick,
		DirectorTickInterval,
		true,
		RandomOffset);

	UE_LOG(LogEnemyAI, Log, TEXT("Director woke — starting tension timer (offset %.2f)"), RandomOffset);
}

// ============================================================
// v2: Mission Phase
// ============================================================

void UEnemyDirectorSubsystem::SetMissionPhase(EMissionPhase NewPhase)
{
	if (NewPhase == CurrentMissionPhase) return;

	const EMissionPhase OldPhase = CurrentMissionPhase;
	CurrentMissionPhase = NewPhase;
	TimeSinceLastSpawn = 0.f;

	if (NewPhase > OldPhase)
	{
		DirectorState = EDirectorState::Build;
		ReliefTimer = 0.f;
	}

	UE_LOG(LogEnemyAI, Log, TEXT("Mission phase: %d -> %d"), static_cast<int32>(OldPhase), static_cast<int32>(NewPhase));
	OnMissionPhaseChanged.Broadcast(OldPhase, NewPhase);
}

// ============================================================
// v2: Config
// ============================================================

void UEnemyDirectorSubsystem::SetDirectorConfig(UDirectorConfigData* InConfig)
{
	if (!IsValid(InConfig)) return;

	if (IsValid(Config) && Config != InConfig && !bConfigFromZone)
	{
		UE_LOG(LogEnemyAI, Warning, TEXT("Director config already set to %s — rejecting %s"),
			*Config->GetName(), *InConfig->GetName());
		return;
	}

	if (IsValid(Config) && Config != InConfig && bConfigFromZone)
	{
		UE_LOG(LogEnemyAI, Log, TEXT("Director config override: zone-adopted %s replaced by explicit %s"),
			*Config->GetName(), *InConfig->GetName());
	}

	Config = InConfig;
	bConfigFromZone = false;

	FString ValidationError;
	if (!Config->Validate(ValidationError))
	{
		UE_LOG(LogEnemyAI, Warning, TEXT("Director config %s validation: %s"), *Config->GetName(), *ValidationError);
	}

	UE_LOG(LogEnemyAI, Log, TEXT("Director config set: %s"), *InConfig->GetName());
}

// ============================================================
// v2: Spawn Zone Registration
// ============================================================

void UEnemyDirectorSubsystem::RegisterSpawnZone(AEnemySpawnZone* Zone)
{
	if (!IsValid(Zone)) return;

	SpawnZones.AddUnique(Zone);
	UE_LOG(LogEnemyAI, Verbose, TEXT("Spawn zone registered: %s"), *Zone->GetName());

	if (!IsValid(Config))
	{
		UDirectorConfigData* ZoneConfig = Zone->GetDirectorConfig();
		if (IsValid(ZoneConfig))
		{
			Config = ZoneConfig;
			bConfigFromZone = true;

			FString ValidationError;
			if (!Config->Validate(ValidationError))
			{
				UE_LOG(LogEnemyAI, Warning, TEXT("Director config %s (from zone %s) validation: %s"),
					*Config->GetName(), *Zone->GetName(), *ValidationError);
			}

			UE_LOG(LogEnemyAI, Log, TEXT("Director config adopted from zone %s: %s"),
				*Zone->GetName(), *ZoneConfig->GetName());
		}
	}
}

void UEnemyDirectorSubsystem::UnregisterSpawnZone(AEnemySpawnZone* Zone)
{
	SpawnZones.RemoveAll([&Zone](const TWeakObjectPtr<AEnemySpawnZone>& Entry)
	{
		return !Entry.IsValid() || Entry.Get() == Zone;
	});

	UE_LOG(LogEnemyAI, Verbose, TEXT("Spawn zone unregistered: %s"), IsValid(Zone) ? *Zone->GetName() : TEXT("null"));
}

void UEnemyDirectorSubsystem::PruneStaleZones()
{
	SpawnZones.RemoveAll([](const TWeakObjectPtr<AEnemySpawnZone>& Entry) { return !Entry.IsValid(); });
}

void UEnemyDirectorSubsystem::RegisterScopeVolume(AEnemyDirectorScopeVolume* ScopeVolume)
{
	if (!IsValid(ScopeVolume)) return;

	ScopeVolumes.AddUnique(ScopeVolume);
	UE_LOG(LogEnemyAI, Verbose, TEXT("Director scope registered: %s"), *ScopeVolume->GetName());
}

void UEnemyDirectorSubsystem::UnregisterScopeVolume(AEnemyDirectorScopeVolume* ScopeVolume)
{
	ScopeVolumes.RemoveAll([&ScopeVolume](const TWeakObjectPtr<AEnemyDirectorScopeVolume>& Entry)
	{
		return !Entry.IsValid() || Entry.Get() == ScopeVolume;
	});

	UE_LOG(LogEnemyAI, Verbose, TEXT("Director scope unregistered: %s"), IsValid(ScopeVolume) ? *ScopeVolume->GetName() : TEXT("null"));
}

void UEnemyDirectorSubsystem::PruneStaleScopeVolumes()
{
	ScopeVolumes.RemoveAll([](const TWeakObjectPtr<AEnemyDirectorScopeVolume>& Entry) { return !Entry.IsValid(); });
}

bool UEnemyDirectorSubsystem::HasActiveScopeVolumes() const
{
	for (const TWeakObjectPtr<AEnemyDirectorScopeVolume>& WeakScope : ScopeVolumes)
	{
		const AEnemyDirectorScopeVolume* Scope = WeakScope.Get();
		if (IsValid(Scope) && Scope->IsScopeEnabled())
		{
			return true;
		}
	}

	return false;
}

bool UEnemyDirectorSubsystem::IsActorInsideAnyScope(const AActor* Actor) const
{
	return IsValid(Actor) && IsPointInsideAnyScope(Actor->GetActorLocation());
}

bool UEnemyDirectorSubsystem::IsPointInsideAnyScope(const FVector& Point) const
{
	for (const TWeakObjectPtr<AEnemyDirectorScopeVolume>& WeakScope : ScopeVolumes)
	{
		const AEnemyDirectorScopeVolume* Scope = WeakScope.Get();
		if (IsValid(Scope) && Scope->ContainsPoint(Point))
		{
			return true;
		}
	}

	return false;
}

// ============================================================
// v2: Director Tick (1s cadence)
// ============================================================

void UEnemyDirectorSubsystem::DirectorTick()
{
	if (AlertLevel != EGlobalAlertLevel::Loud) return;

	PruneStaleZones();
	PruneStaleScopeVolumes();
	PruneStaleWaveMembers();
	ReassertWaveMemberEngagement();

	const FEnemySweepResult Sweep = SweepEnemies();

	UpdateTension(DirectorTickInterval, Sweep.EngagedCount);
	UpdateSawtooth(DirectorTickInterval);

	if (WaveProgress.IsActive() && WaveProgress.IsComplete())
	{
		CompleteWave();
	}
	else
	{
		const bool bWaveBlocksSpawn = WaveProgress.IsActive() && !WaveProgress.CanSpawnMore();

		if (!bWaveBlocksSpawn && ShouldSpawn(Sweep.AliveCount))
		{
			TArray<AEnemyCharacter*> Spawned;
			const bool bSpawned = TrySpawn(Sweep.AliveCount, &Spawned);

			if (WaveProgress.IsActive())
			{
				if (bSpawned)
				{
					WaveProgress.RecordSuccessfulSquad(Spawned.Num());
					for (AEnemyCharacter* Member : Spawned)
					{
						if (IsValid(Member)) WaveMembers.Add(Member);
					}
					WaveBlockedTime = 0.f;
					OnDirectorWaveProgress.Broadcast(ActiveWaveRequest.WaveId,
						WaveProgress.SpawnedSquads, WaveProgress.RemainingMembers);
				}
				else
				{
					AccrueWaveBlockedTime();
				}
			}
		}
	}

	TimeSinceLastSpawn += DirectorTickInterval;
}

// ============================================================
// v2: Kill Tracking
// ============================================================

void UEnemyDirectorSubsystem::HandleEnemyKilled(AEnemyCharacter* DeadEnemy, FVector Location, bool bWasOfficer)
{
	if (WaveProgress.IsActive() && IsValid(DeadEnemy))
	{
		const TWeakObjectPtr<AEnemyCharacter> WeakEnemy(DeadEnemy);
		if (WaveMembers.Remove(WeakEnemy) > 0)
		{
			WaveProgress.RecordMemberEnded();
		}
	}

	const bool bUseScopeVolumes = HasActiveScopeVolumes();
	if (bUseScopeVolumes && !IsPointInsideAnyScope(Location) && !IsActorInsideAnyScope(DeadEnemy)) return;

	++RecentKills;
}

// ============================================================
// v2: Merged Enemy Sweep (alive + engaged in one pass)
// ============================================================

UEnemyDirectorSubsystem::FEnemySweepResult UEnemyDirectorSubsystem::SweepEnemies() const
{
	FEnemySweepResult Result;

	UWorld* World = GetWorld();
	if (!IsValid(World)) return Result;

	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(World, 0);
	const FVector PlayerLoc = IsValid(PlayerPawn) ? PlayerPawn->GetActorLocation() : FVector::ZeroVector;
	const float EngageRadiusSq = DefaultEngageRadius * DefaultEngageRadius;
	const bool bUseScopeVolumes = HasActiveScopeVolumes();

	for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
	{
		AEnemyCharacter* Enemy = *It;
		if (!IsValid(Enemy)) continue;
		if (bUseScopeVolumes && !IsActorInsideAnyScope(Enemy)) continue;

		UHealthComponent* HC = Enemy->GetHealthComponent();
		if (!IsValid(HC) || HC->IsDead()) continue;

		++Result.AliveCount;

		if (!IsValid(PlayerPawn)) continue;
		if (FVector::DistSquared(PlayerLoc, Enemy->GetActorLocation()) > EngageRadiusSq) continue;

		AEnemyAIController* AIC = Cast<AEnemyAIController>(Enemy->GetController());
		if (!IsValid(AIC)) continue;

		UEnemyAwarenessComponent* Awareness = AIC->GetAwarenessComponent();
		if (IsValid(Awareness) && Awareness->GetAwarenessState() == EEnemyAwarenessState::Combat)
		{
			Result.EngagedCount += 1.f;
		}
	}

	return Result;
}

// ============================================================
// v2: Tension Estimate
// ============================================================

float UEnemyDirectorSubsystem::PollPlayerHealthLost()
{
	if (CachedPlayerHealth.IsValid())
	{
		const float CurrentHP = CachedPlayerHealth->GetCurrentHealth();
		const float Lost = FMath::Max(0.f, CachedPlayerHealthLastTick - CurrentHP);
		CachedPlayerHealthLastTick = CurrentHP;
		return Lost;
	}

	UWorld* World = GetWorld();
	if (!IsValid(World)) return 0.f;

	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(World, 0);
	if (!IsValid(PlayerPawn)) return 0.f;

	CachedPlayerHealth = PlayerPawn->FindComponentByClass<UHealthComponent>();
	if (CachedPlayerHealth.IsValid())
	{
		CachedPlayerHealthLastTick = CachedPlayerHealth->GetCurrentHealth();
	}

	return 0.f;
}

void UEnemyDirectorSubsystem::UpdateTension(float DeltaSeconds, float EngagedCount)
{
	float DecayPerSec = DefaultTensionDecay;
	float PerHealthLost = DefaultTensionPerHealthLost;
	float PerKill = DefaultTensionPerKill;
	float PerEngaged = DefaultTensionPerEngaged;

	const UDirectorConfigData* Effective = GetEffectiveConfig();
	if (IsValid(Effective))
	{
		DecayPerSec = Effective->TensionDecayPerSecond;
		PerHealthLost = Effective->TensionPerPlayerHealthLost;
		PerKill = Effective->TensionPerKill;
		PerEngaged = Effective->TensionPerEngagedEnemy;
	}

	float TensionDelta = 0.f;
	TensionDelta += PollPlayerHealthLost() * PerHealthLost;
	TensionDelta += RecentKills * PerKill;
	RecentKills = 0;
	TensionDelta += EngagedCount * PerEngaged * DeltaSeconds;
	TensionDelta -= DecayPerSec * DeltaSeconds;

	Tension = FMath::Clamp(Tension + TensionDelta, 0.f, 100.f);
}

// ============================================================
// v2: Sawtooth State Machine
// ============================================================

void UEnemyDirectorSubsystem::UpdateSawtooth(float DeltaSeconds)
{
	float PeakThreshold = DefaultPeakThreshold;
	float ReliefEntry = DefaultReliefEntry;
	float ReliefDur = DefaultReliefDuration;

	const UDirectorConfigData* Effective = GetEffectiveConfig();
	if (IsValid(Effective))
	{
		PeakThreshold = Effective->PeakTensionThreshold;
		ReliefEntry = Effective->ReliefEntryThreshold;
		ReliefDur = Effective->ReliefDuration;
	}

	switch (DirectorState)
	{
	case EDirectorState::Build:
		if (Tension >= PeakThreshold)
		{
			DirectorState = EDirectorState::Peak;
			UE_LOG(LogEnemyAI, Log, TEXT("Director: Build -> Peak (tension %.1f >= %.1f)"), Tension, PeakThreshold);
		}
		break;

	case EDirectorState::Peak:
		if (Tension < ReliefEntry)
		{
			DirectorState = EDirectorState::Relief;
			ReliefTimer = 0.f;
			UE_LOG(LogEnemyAI, Log, TEXT("Director: Peak -> Relief (tension %.1f < %.1f)"), Tension, ReliefEntry);
		}
		break;

	case EDirectorState::Relief:
		ReliefTimer += DeltaSeconds;
		if (ReliefTimer >= ReliefDur)
		{
			DirectorState = EDirectorState::Build;
			ReliefTimer = 0.f;
			UE_LOG(LogEnemyAI, Log, TEXT("Director: Relief -> Build (%.1fs elapsed)"), ReliefDur);
		}
		break;
	}
}

// ============================================================
// v2: Spawn Pipeline
// ============================================================

bool UEnemyDirectorSubsystem::ShouldSpawn(int32 AliveCount) const
{
	const FMissionPhaseConfig& PhaseConfig = GetCurrentPhaseConfig();

	// A finite scripted wave OWNS the director while it lives. Two rules:
	// - While it can still field squads, bypass the ambient pacing gates: StartWave forces Build
	//   once, but a mid-wave Peak transition (tension over threshold — guaranteed during an
	//   endgame assault) froze the remaining squads until Relief ("the wave never spawns").
	//   Waves pace on cadence + MaxAlive only.
	// - Once all squads are fielded, the director goes SILENT until the wave resolves — the
	//   ambient/punishment trickle resuming underneath the defence both diluted the scripted
	//   pacing and used zones the wave deliberately avoids (the player-visible lifts box).
	const bool bWaveActive = WaveProgress.IsActive();
	const bool bWaveCanSpawn = WaveProgress.CanSpawnMore();
	if (bWaveActive && !bWaveCanSpawn) return false;
	if (!bWaveActive)
	{
		if (AlertLevel != EGlobalAlertLevel::Loud) return false;
		if (DirectorState != EDirectorState::Build) return false;
		if (Tension >= PhaseConfig.IntensityCeiling) return false;
	}

	const float Cadence = (bWaveCanSpawn && ActiveWaveRequest.SpawnCadenceOverride > 0.f)
		? ActiveWaveRequest.SpawnCadenceOverride
		: PhaseConfig.SpawnCadenceSeconds;
	if (TimeSinceLastSpawn < Cadence) return false;
	if (AliveCount >= PhaseConfig.MaxAlive) return false;

	return true;
}

bool UEnemyDirectorSubsystem::TrySpawn(int32 AliveCount, TArray<AEnemyCharacter*>* OutSpawned)
{
	const FMissionPhaseConfig& PhaseConfig = GetCurrentPhaseConfig();

	FSquadComposition Composition;
	if (!PickComposition(PhaseConfig, AliveCount, Composition))
	{
		if (!bLoggedNoComposition)
		{
			UE_LOG(LogEnemyAI, Verbose, TEXT("Director: no valid composition fits (alive %d, max %d, phase %d)"),
				AliveCount, PhaseConfig.MaxAlive, static_cast<int32>(GetEffectivePhase()));
			bLoggedNoComposition = true;
		}
		return false;
	}

	UWorld* World = GetWorld();
	if (!IsValid(World)) return false;

	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(World, 0);
	if (!IsValid(PlayerPawn)) return false;

	APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0);
	if (!IsValid(PC)) return false;

	FVector ViewLoc;
	FRotator ViewRot;
	PC->GetPlayerViewPoint(ViewLoc, ViewRot);

	AEnemySpawnZone* Zone = PickSpawnZone(PlayerPawn->GetActorLocation(), ViewLoc, ViewRot, GetCompositionSize(Composition));
	if (!IsValid(Zone))
	{
		if (!bLoggedNoZone)
		{
			UE_LOG(LogEnemyAI, Warning, TEXT("Director: no eligible spawn zone (phase %d, %d zones registered)"),
				static_cast<int32>(GetEffectivePhase()), SpawnZones.Num());
			bLoggedNoZone = true;
		}
		return false;
	}

	TArray<AEnemyCharacter*> Spawned;
	SpawnSquadAtZone(Composition, Zone, Spawned);

	if (Spawned.Num() == 0) return false;

	TimeSinceLastSpawn = 0.f;
	bLoggedNoComposition = false;
	bLoggedNoZone = false;

	if (OutSpawned) *OutSpawned = MoveTemp(Spawned);

	return true;
}

int32 UEnemyDirectorSubsystem::GetCompositionSize(const FSquadComposition& Comp) const
{
	int32 Total = 0;
	for (const FSquadCompositionEntry& Entry : Comp.Entries)
	{
		if (Entry.EnemyClass && Entry.Count > 0) Total += Entry.Count;
	}
	return Total;
}

const FMissionPhaseConfig& UEnemyDirectorSubsystem::GetCurrentPhaseConfig() const
{
	const UDirectorConfigData* Effective = GetEffectiveConfig();
	if (IsValid(Effective)) return Effective->GetPhaseConfig(GetEffectivePhase());

	static const FMissionPhaseConfig DefaultConfig;
	return DefaultConfig;
}

bool UEnemyDirectorSubsystem::PickComposition(const FMissionPhaseConfig& PhaseConfig, int32 AliveCount, FSquadComposition& OutComposition) const
{
	const TArray<FSquadComposition>& Compositions = PhaseConfig.Compositions;
	if (Compositions.Num() == 0) return false;

	float TotalWeight = 0.f;
	for (const FSquadComposition& Comp : Compositions)
	{
		const int32 Size = GetCompositionSize(Comp);
		if (Size <= 0) continue;
		if (AliveCount + Size > PhaseConfig.MaxAlive) continue;
		TotalWeight += FMath::Max(0.f, Comp.Weight);
	}

	if (TotalWeight <= 0.f) return false;

	float Roll = FMath::FRandRange(0.f, TotalWeight);
	for (const FSquadComposition& Comp : Compositions)
	{
		const int32 Size = GetCompositionSize(Comp);
		if (Size <= 0) continue;
		if (AliveCount + Size > PhaseConfig.MaxAlive) continue;

		Roll -= FMath::Max(0.f, Comp.Weight);
		if (Roll <= 0.f)
		{
			OutComposition = Comp;
			return true;
		}
	}

	return false;
}

AEnemySpawnZone* UEnemyDirectorSubsystem::PickSpawnZone(const FVector& PlayerLoc, const FVector& ViewLoc, const FRotator& ViewRot, int32 SquadSize) const
{
	UWorld* World = GetWorld();
	if (!IsValid(World)) return nullptr;

	float DistMin = DefaultSpawnDistMin;
	float DistMax = DefaultSpawnDistMax;
	const UDirectorConfigData* Effective = GetEffectiveConfig();
	if (IsValid(Effective))
	{
		DistMin = Effective->SpawnDistanceMin;
		DistMax = Effective->SpawnDistanceMax;
	}

	const FVector ViewDir = ViewRot.Vector();

	FCollisionQueryParams QueryParams;
	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(World, 0);
	if (IsValid(PlayerPawn)) QueryParams.AddIgnoredActor(PlayerPawn);

	const ACompanionCharacter* Companion = FindCompanion();
	const bool bHasCompanion = IsValid(Companion);
	FVector CompanionLoc = FVector::ZeroVector;
	if (bHasCompanion)
	{
		CompanionLoc = Companion->GetActorLocation();
		// The companion's body must not count as an occluder for "is this zone hidden".
		QueryParams.AddIgnoredActor(Companion);
	}

	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	const bool bUseScopeVolumes = HasActiveScopeVolumes();

	AEnemySpawnZone* BestZone = nullptr;
	float BestScore = -FLT_MAX;

	// Wave-eligibility: scripted waves must never use zones the player can watch from the fight
	// room (the hidden-check trace reads glass as an occluder, so "hidden" lies there) — the
	// designer flags those zones off for waves. Ambient spawns keep the full zone set. Keyed on
	// IsActive: while a wave lives, every spawn the director makes IS a wave spawn (ShouldSpawn
	// silences the ambient trickle for the wave's whole lifetime).
	const bool bWaveActive = WaveProgress.IsActive();

	for (const TWeakObjectPtr<AEnemySpawnZone>& WeakZone : SpawnZones)
	{
		AEnemySpawnZone* Zone = WeakZone.Get();
		if (!IsValid(Zone)) continue;
		if (!Zone->IsActiveForPhase(GetEffectivePhase())) continue;
		if (bWaveActive && !Zone->bWaveEligible) continue;

		const FVector ZoneLoc = Zone->GetZoneOrigin();
		if (bUseScopeVolumes && !IsPointInsideAnyScope(ZoneLoc)) continue;

		// Min distance against the closest point of the box (a wide zone can put spawn
		// points far nearer than its origin); max distance against the origin so large
		// zones don't starve availability.
		if (FVector::DistSquared(PlayerLoc, Zone->GetClosestPointInZone(PlayerLoc)) < DistMin * DistMin) continue;
		if (FVector::DistSquared(PlayerLoc, ZoneLoc) > DistMax * DistMax) continue;

		if (!IsZoneHiddenFromPlayer(Zone, FMath::Max(MinSightlineSamples, SquadSize), ViewLoc, ViewDir, QueryParams, NavSys)) continue;

		if (IsValid(NavSys))
		{
			FNavLocation NavLoc;
			const FVector Extent(NavProjectExtentXY, NavProjectExtentXY, NavProjectExtentZ);
			if (!NavSys->ProjectPointToNavigation(ZoneLoc, NavLoc, Extent)) continue;
		}

		const float Score = ScoreZone(Zone, PlayerLoc, CompanionLoc, bHasCompanion, DistMin, DistMax);
		if (Score > BestScore)
		{
			BestScore = Score;
			BestZone = Zone;
		}
	}

	return BestZone;
}

bool UEnemyDirectorSubsystem::IsZoneHiddenFromPlayer(const AEnemySpawnZone* Zone, int32 SampleCount, const FVector& ViewLoc, const FVector& ViewDir, const FCollisionQueryParams& QueryParams, UNavigationSystemV1* NavSys) const
{
	// Sample where spawned enemies will actually stand: floor-projected points raised to
	// head height, one per squad member. Tracing to the raw box base (at/below the floor)
	// hits the floor and reports "occluded" for zones sitting in plain view.
	const FVector Extent(NavProjectExtentXY, NavProjectExtentXY, NavProjectExtentZ);

	// SpawnEntryAtZone falls back to the projected zone origin when a point fails to
	// project — so that's the location the visibility check must cover too.
	FNavLocation OriginNav;
	bool bOriginProjected = false;
	if (IsValid(NavSys))
		bOriginProjected = NavSys->ProjectPointToNavigation(Zone->GetZoneOrigin(), OriginNav, Extent);

	for (int32 Si = 0; Si < SampleCount; ++Si)
	{
		FVector Pt = Zone->GetSpawnTransform(Si).GetLocation();

		if (IsValid(NavSys))
		{
			FNavLocation NavLoc;
			if (NavSys->ProjectPointToNavigation(Pt, NavLoc, Extent))
				Pt = NavLoc.Location;
			else if (bOriginProjected)
				Pt = OriginNav.Location;
			else
				return false; // can't establish where this member would stand — fail closed
		}

		Pt.Z += SpawnEyeHeightOffset;
		if (IsPointInPlayerSightline(Pt, ViewLoc, ViewDir, QueryParams)) return false;
	}

	return true;
}

float UEnemyDirectorSubsystem::ScoreZone(const AEnemySpawnZone* Zone, const FVector& PlayerLoc, const FVector& CompanionLoc, bool bHasCompanion, float DistMin, float DistMax) const
{
	// Jitter keeps repeated spawns from always electing the same room when scores tie.
	float Score = FMath::FRandRange(0.f, ZoneScoreJitter);

	// Prefer zones a little past min distance — keeps pressure on without pop-in range.
	const float Band = FMath::Max(DistMax - DistMin, 1.f);
	const float IdealDist = DistMin + Band * IdealDistanceFrac;
	const float Dist = FVector::Dist(PlayerLoc, Zone->GetZoneOrigin());
	Score += DistanceBandBonus * (1.f - FMath::Min(FMath::Abs(Dist - IdealDist) / Band, 1.f));

	// Soft penalty near the companion: don't materialise a squad on the second teammate,
	// but don't hard-starve small interiors either.
	if (bHasCompanion)
	{
		const float CompanionDist = FVector::Dist(CompanionLoc, Zone->GetClosestPointInZone(CompanionLoc));
		if (CompanionDist < DistMin)
			Score -= CompanionProximityPenalty * (1.f - CompanionDist / DistMin);
	}

	return Score;
}

const ACompanionCharacter* UEnemyDirectorSubsystem::FindCompanion() const
{
	UWorld* World = GetWorld();
	if (!IsValid(World)) return nullptr;

	for (TActorIterator<ACompanionCharacter> It(World); It; ++It)
	{
		const ACompanionCharacter* Companion = *It;
		if (!IsValid(Companion)) continue;

		const UHealthComponent* HC = Companion->FindComponentByClass<UHealthComponent>();
		if (IsValid(HC) && HC->IsDead()) continue;

		return Companion;
	}

	return nullptr;
}

bool UEnemyDirectorSubsystem::IsPointInPlayerSightline(const FVector& Point, const FVector& ViewLoc, const FVector& ViewDir, const FCollisionQueryParams& QueryParams) const
{
	const FVector Direction = (Point - ViewLoc).GetSafeNormal();
	if (FVector::DotProduct(ViewDir, Direction) < 0.f) return false;

	UWorld* World = GetWorld();
	if (!IsValid(World)) return true;

	FHitResult Hit;
	const bool bBlocked = World->LineTraceSingleByChannel(Hit, ViewLoc, Point, ECC_Visibility, QueryParams);
	return !bBlocked;
}

AEnemyCharacter* UEnemyDirectorSubsystem::SpawnEntryAtZone(UWorld* World, TSubclassOf<AEnemyCharacter> EnemyClass, AEnemySpawnZone* Zone, int32 Index)
{
	FTransform SpawnTransform = Zone->GetSpawnTransform(Index);
	FVector SpawnLoc = SpawnTransform.GetLocation();

	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (IsValid(NavSys))
	{
		FNavLocation NavLoc;
		const FVector Extent(NavProjectExtentXY, NavProjectExtentXY, NavProjectExtentZ);
		if (NavSys->ProjectPointToNavigation(SpawnLoc, NavLoc, Extent))
		{
			SpawnLoc = NavLoc.Location;
		}
		else
		{
			FNavLocation FallbackLoc;
			const FVector ZoneOrigin = Zone->GetZoneOrigin();
			if (NavSys->ProjectPointToNavigation(ZoneOrigin, FallbackLoc, Extent))
			{
				SpawnLoc = FallbackLoc.Location;
			}
			else
			{
				UE_LOG(LogEnemyAI, Warning, TEXT("Director: nav-project failed for spawn point AND zone origin at %s (index %d) — skipping"),
					*Zone->GetName(), Index);
				return nullptr;
			}
		}
	}

	// Nav-projected locations are on the floor; SpawnActor places the capsule CENTRE
	// there, embedding the character waist-deep. Raise by the class's capsule half-height.
	float CapsuleHalfHeight = 88.f;
	if (const AEnemyCharacter* ClassDefaults = EnemyClass->GetDefaultObject<AEnemyCharacter>())
	{
		if (const UCapsuleComponent* Capsule = ClassDefaults->GetCapsuleComponent())
			CapsuleHalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	}
	SpawnLoc.Z += CapsuleHalfHeight + SpawnGroundClearance;

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	AEnemyCharacter* Spawned = World->SpawnActor<AEnemyCharacter>(
		EnemyClass,
		SpawnLoc,
		SpawnTransform.GetRotation().Rotator(),
		SpawnParams);

	if (IsValid(Spawned))
	{
		UE_LOG(LogEnemyAI, Verbose, TEXT("Director spawned %s at zone %s (index %d)"),
			*Spawned->GetName(), *Zone->GetName(), Index);
	}
	else
	{
		UE_LOG(LogEnemyAI, Warning, TEXT("Director spawn failed for %s at zone %s (index %d)"),
			*EnemyClass->GetName(), *Zone->GetName(), Index);
	}

	return Spawned;
}

void UEnemyDirectorSubsystem::SpawnSquadAtZone(const FSquadComposition& Composition, AEnemySpawnZone* Zone, TArray<AEnemyCharacter*>& OutSpawned)
{
	UWorld* World = GetWorld();
	if (!IsValid(World) || !IsValid(Zone)) return;

	if (!CachedSquadSubsystem.IsValid())
	{
		CachedSquadSubsystem = World->GetSubsystem<UEnemySquadSubsystem>();
	}

	const int32 ExpectedSize = GetCompositionSize(Composition);
	OutSpawned.Reserve(ExpectedSize);
	int32 SpawnIndex = 0;

	for (const FSquadCompositionEntry& Entry : Composition.Entries)
	{
		if (!Entry.EnemyClass || Entry.Count <= 0) continue;

		for (int32 i = 0; i < Entry.Count; ++i)
		{
			AEnemyCharacter* Spawned = SpawnEntryAtZone(World, Entry.EnemyClass, Zone, SpawnIndex);
			if (IsValid(Spawned)) OutSpawned.Add(Spawned);
			++SpawnIndex;
		}
	}

	UEnemySquad* Squad = nullptr;
	if (OutSpawned.Num() > 0 && CachedSquadSubsystem.IsValid())
	{
		Squad = CachedSquadSubsystem->CreateSquadForGroup(OutSpawned);
	}

	if (IsValid(Squad))
	{
		SeedSquadWithFight(Squad);
	}

	UE_LOG(LogEnemyAI, Log, TEXT("Director spawned squad (%d/%d members) at zone %s"),
		OutSpawned.Num(), SpawnIndex, *Zone->GetName());
}

void UEnemyDirectorSubsystem::SeedSquadWithFight(UEnemySquad* Squad) const
{
	UWorld* World = GetWorld();
	if (!IsValid(World) || !IsValid(Squad)) return;

	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(World, 0);
	if (!IsValid(PlayerPawn)) return;

	if (WaveProgress.IsActive() && ActiveWaveRequest.bAutoEngage)
	{
		Squad->ForceEngage(PlayerPawn, PlayerPawn->GetActorLocation());
		return;
	}

	Squad->ReportSighting(PlayerPawn, PlayerPawn->GetActorLocation());
}

// ============================================================
// v2: Effective Config / Phase Selection
// ============================================================

const UDirectorConfigData* UEnemyDirectorSubsystem::GetEffectiveConfig() const
{
	return FDirectorProfileSelector::SelectConfig(
		WaveProgress.IsActive(), ActiveWaveRequest.ConfigOverride,
		PunishmentSource.IsValid(), PunishmentConfig, Config);
}

EMissionPhase UEnemyDirectorSubsystem::GetEffectivePhase() const
{
	return FDirectorProfileSelector::SelectPhase(
		WaveProgress.IsActive(), ActiveWaveRequest.MissionPhase,
		PunishmentSource.IsValid(), PunishmentPhase, CurrentMissionPhase);
}

// ============================================================
// v2: Finite Wave Lifecycle
// ============================================================

bool UEnemyDirectorSubsystem::StartWave(const FDirectorWaveRequest& Request)
{
	if (GetWorld()->GetNetMode() == NM_Client) return false;
	if (WaveProgress.IsActive()) return false;
	if (Request.TargetSquads < 1) return false;

	ActiveWaveRequest = Request;
	WaveProgress.Begin(Request.TargetSquads);
	WaveBlockedTime = 0.f;
	bWaveBlockedBroadcast = false;

	TripAlarm();

	DirectorState = EDirectorState::Build;
	ReliefTimer = 0.f;
	TimeSinceLastSpawn = 0.f;

	OnDirectorWaveStarted.Broadcast(Request.WaveId);

	UE_LOG(LogEnemyAI, Log, TEXT("Director wave started: %s (target %d squads)"),
		*Request.WaveId.ToString(), Request.TargetSquads);

	return true;
}

void UEnemyDirectorSubsystem::CancelWave(FName WaveId)
{
	if (GetWorld()->GetNetMode() == NM_Client) return;
	if (!WaveProgress.IsActive()) return;
	if (ActiveWaveRequest.WaveId != WaveId) return;

	UE_LOG(LogEnemyAI, Log, TEXT("Director wave cancelled: %s"), *WaveId.ToString());
	ClearWaveState();
}

void UEnemyDirectorSubsystem::CompleteWave()
{
	const FName WaveId = ActiveWaveRequest.WaveId;
	const int32 Squads = WaveProgress.SpawnedSquads;
	const int32 Remaining = WaveProgress.RemainingMembers;

	ClearWaveState();

	UE_LOG(LogEnemyAI, Log, TEXT("Director wave completed: %s (%d squads)"),
		*WaveId.ToString(), Squads);

	OnDirectorWaveProgress.Broadcast(WaveId, Squads, Remaining);
	OnDirectorWaveCompleted.Broadcast(WaveId);
}

void UEnemyDirectorSubsystem::ClearWaveState()
{
	ActiveWaveRequest = FDirectorWaveRequest();
	WaveProgress = FDirectorWaveProgress();
	WaveMembers.Empty();
	WaveBlockedTime = 0.f;
	bWaveBlockedBroadcast = false;
}

void UEnemyDirectorSubsystem::PruneStaleWaveMembers()
{
	if (!WaveProgress.IsActive()) return;

	for (auto It = WaveMembers.CreateIterator(); It; ++It)
	{
		if (!It->IsValid())
		{
			It.RemoveCurrent();
			WaveProgress.RecordMemberEnded();
		}
	}
}

void UEnemyDirectorSubsystem::ReassertWaveMemberEngagement()
{
	if (!WaveProgress.IsActive() || !ActiveWaveRequest.bAutoEngage) return;

	UWorld* World = GetWorld();
	APawn* PlayerPawn = IsValid(World) ? UGameplayStatics::GetPlayerPawn(World, 0) : nullptr;
	if (!IsValid(PlayerPawn)) return;

	for (const TWeakObjectPtr<AEnemyCharacter>& WeakMember : WaveMembers)
	{
		AEnemyCharacter* Member = WeakMember.Get();
		if (!IsValid(Member)) continue;
		const UHealthComponent* HP = Member->GetHealthComponent();
		if (HP && HP->IsDead()) continue;

		AEnemyAIController* AIC = Cast<AEnemyAIController>(Member->GetController());
		UEnemyAwarenessComponent* Awareness = AIC ? AIC->GetAwarenessComponent() : nullptr;
		if (!Awareness) continue;

		// Searching still hunts. Only a member that fully gave up (Unaware — decayed out of the
		// fight and walked back to a guard post, e.g. parked behind a door) gets re-seeded; the
		// kill-all wave can never complete around a passive holdout the player can't find.
		if (Awareness->GetAwarenessState() != EEnemyAwarenessState::Unaware) continue;
		Awareness->ForceEngage(PlayerPawn);
		UE_LOG(LogEnemyAI, Log, TEXT("Director wave %s: re-engaging idle member %s"),
			*ActiveWaveRequest.WaveId.ToString(), *Member->GetName());
	}
}

void UEnemyDirectorSubsystem::AccrueWaveBlockedTime()
{
	if (!WaveProgress.CanSpawnMore()) return;

	WaveBlockedTime += DirectorTickInterval;

	if (bWaveBlockedBroadcast) return;
	if (WaveBlockedTime < ActiveWaveRequest.BlockedWarningSeconds) return;

	bWaveBlockedBroadcast = true;
	OnDirectorWaveBlocked.Broadcast(ActiveWaveRequest.WaveId,
		FText::FromString(TEXT("Wave spawn blocked: no valid zone or composition")));

	UE_LOG(LogEnemyAI, Warning, TEXT("Director wave %s blocked for %.0fs"),
		*ActiveWaveRequest.WaveId.ToString(), WaveBlockedTime);
}

// ============================================================
// v2: Punishment Profile
// ============================================================

bool UEnemyDirectorSubsystem::ActivatePunishmentProfile(AActor* Source, UDirectorConfigData* Profile, EMissionPhase Phase)
{
	if (GetWorld()->GetNetMode() == NM_Client) return false;
	if (!IsValid(Source) || !IsValid(Profile)) return false;
	if (PunishmentSource.IsValid() && PunishmentSource.Get() != Source) return false;

	PunishmentSource = Source;
	PunishmentConfig = Profile;
	PunishmentPhase = Phase;

	UE_LOG(LogEnemyAI, Log, TEXT("Punishment profile activated: %s from %s (phase %d)"),
		*Profile->GetName(), *Source->GetName(), static_cast<int32>(Phase));

	return true;
}

void UEnemyDirectorSubsystem::DeactivatePunishmentProfile(AActor* Source)
{
	if (GetWorld()->GetNetMode() == NM_Client) return;
	if (PunishmentSource.IsValid() && PunishmentSource.Get() != Source) return;

	UE_LOG(LogEnemyAI, Log, TEXT("Punishment profile deactivated by %s"),
		IsValid(Source) ? *Source->GetName() : TEXT("stale source"));

	PunishmentSource.Reset();
	PunishmentConfig = nullptr;
	PunishmentPhase = EMissionPhase::Infiltration;
}
