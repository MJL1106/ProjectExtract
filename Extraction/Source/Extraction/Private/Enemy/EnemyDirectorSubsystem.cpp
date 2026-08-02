// UEnemyDirectorSubsystem — v1 alert ladder + v2 tension director + spawn pipeline.

#include "EnemyDirectorSubsystem.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
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
#include "EnemyMoraleComponent.h"

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

	UnlatchAllLastMan();
	SpawnZones.Empty();
	ScopeVolumes.Empty();
	Corpses.Empty();
	WaveMembers.Empty();
	PunishmentSource.Reset();
	PunishmentConfig = nullptr;
	bAmbientSpawningEnabled = true;

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

bool UEnemyDirectorSubsystem::GetWaveThreatReference(FVector& OutLocation) const
{
	if (!WaveProgress.IsActive()) return false;

	// Prefer the last-picked zone (the direction the most recent squad came from).
	if (AEnemySpawnZone* Zone = LastPickedZone.Get())
	{
		OutLocation = Zone->GetActorLocation();
		return true;
	}

	// Fallback: any registered wave-eligible zone that is active for the current phase.
	for (const TWeakObjectPtr<AEnemySpawnZone>& ZonePtr : SpawnZones)
	{
		AEnemySpawnZone* Zone = ZonePtr.Get();
		if (!IsValid(Zone)) continue;
		if (!Zone->bWaveEligible) continue;
		OutLocation = Zone->GetActorLocation();
		return true;
	}

	return false;
}

void UEnemyDirectorSubsystem::Escalate(EGlobalAlertLevel NewLevel)
{
	if (NewLevel <= AlertLevel) return;

	const EGlobalAlertLevel OldLevel = AlertLevel;
	AlertLevel = NewLevel;

	UE_LOG(LogEnemyAI, Log, TEXT("Global alert: %d -> %d"), static_cast<int32>(OldLevel), static_cast<int32>(NewLevel));
	OnGlobalAlertChanged.Broadcast(OldLevel, NewLevel);

	// First (and only — the ladder is a ratchet) arrival at Loud: stealth kills before the
	// fight went loud must not pre-charge the tension estimate.
	if (NewLevel != EGlobalAlertLevel::Loud) return;

	RecentKills = 0;
	UE_LOG(LogEnemyAI, Log, TEXT("Director woke — tension estimate armed"));
}

void UEnemyDirectorSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	// The tick runs at every alert level (the awareness sweep feeds music/presentation
	// during stealth); everything Loud-gated early-outs inside DirectorTick.
	const float RandomOffset = FMath::FRandRange(0.f, 0.5f);
	InWorld.GetTimerManager().SetTimer(
		DirectorTimerHandle,
		this,
		&UEnemyDirectorSubsystem::DirectorTick,
		DirectorTickInterval,
		true,
		RandomOffset);
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
// v2: Ambient Spawning Control
// ============================================================

void UEnemyDirectorSubsystem::SetAmbientSpawningEnabled(bool bEnabled)
{
	if (bAmbientSpawningEnabled == bEnabled) return;

	bAmbientSpawningEnabled = bEnabled;
	UE_LOG(LogEnemyAI, Log, TEXT("Director ambient spawning %s"), bEnabled ? TEXT("enabled") : TEXT("disabled"));
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
	TRACE_CPUPROFILER_EVENT_SCOPE(Extraction_Director_Tick);

	// Sweep runs at every alert level — the searching/combat counts feed presentation
	// (music) during stealth, before the Loud-only spawn pipeline below is live.
	const FEnemySweepResult Sweep = SweepEnemies();
	LastSweepSearchingCount = Sweep.SearchingCount;
	LastSweepCombatCount = Sweep.CombatCount;
	LastSweepAliveCount = Sweep.AliveCount;

	if (AlertLevel != EGlobalAlertLevel::Loud) return;

	PruneStaleZones();
	PruneStaleScopeVolumes();
	PruneStaleWaveMembers();
	ReassertWaveMemberEngagement();
	RefreshPunishmentSquadTargets();

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
			const bool bIsWave = WaveProgress.IsActive();
			const FMissionPhaseConfig& PhaseConfig = GetCurrentPhaseConfig();
			const int32 ZoneCount = (!bIsWave && PhaseConfig.ConcurrentSpawnZones > 1)
				? PhaseConfig.ConcurrentSpawnZones : 1;

			TSet<AEnemySpawnZone*> UsedZones;
			int32 RunningAlive = Sweep.AliveCount;

			for (int32 ZoneIdx = 0; ZoneIdx < ZoneCount; ++ZoneIdx)
			{
				// MaxAlive gate for additional squads only — iteration 0's cap is ShouldSpawn's
				// job and already waives the cap for guaranteed wave squads (see FindGuaranteedSquadForNext).
				if (ZoneIdx > 0 && RunningAlive >= PhaseConfig.MaxAlive) break;

				TArray<AEnemyCharacter*> Spawned;
				AEnemySpawnZone* PickedZone = nullptr;
				const TSet<AEnemySpawnZone*>* Exclusion = UsedZones.Num() > 0 ? &UsedZones : nullptr;
				const bool bSpawned = TrySpawn(RunningAlive, &Spawned, Exclusion, &PickedZone);

				if (bSpawned)
				{
					RunningAlive += Spawned.Num();

					// Track the zone just used so the next iteration picks a different one.
					if (IsValid(PickedZone))
						UsedZones.Add(PickedZone);
				}

				if (bIsWave)
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
					// Waves always field one squad per beat — break after the first attempt.
					break;
				}

				// Ambient: if this iteration failed to find a zone, no point retrying.
				if (!bSpawned) break;
			}
		}
	}

	// Last-man refresh runs AFTER wave completion: latched survivors outlive the wave and keep
	// getting their contact refresh until they die or a new wave clears the set.
	RefreshLastManLatched();

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
	TRACE_CPUPROFILER_EVENT_SCOPE(Extraction_Director_SweepEnemies);

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

		AEnemyAIController* AIC = Cast<AEnemyAIController>(Enemy->GetController());
		if (!IsValid(AIC)) continue;

		UEnemyAwarenessComponent* Awareness = AIC->GetAwarenessComponent();
		if (!IsValid(Awareness)) continue;

		const EEnemyAwarenessState AwareState = Awareness->GetAwarenessState();
		if (AwareState == EEnemyAwarenessState::Searching) ++Result.SearchingCount;
		if (AwareState == EEnemyAwarenessState::Combat) ++Result.CombatCount;

		// EngagedCount stays distance-gated — it feeds the tension estimate, which only
		// cares about enemies pressing the player, not a distant unresolved fight.
		if (!IsValid(PlayerPawn)) continue;
		if (FVector::DistSquared(PlayerLoc, Enemy->GetActorLocation()) > EngageRadiusSq) continue;

		if (AwareState == EEnemyAwarenessState::Combat)
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
	TRACE_CPUPROFILER_EVENT_SCOPE(Extraction_Director_UpdateTension);

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
	TRACE_CPUPROFILER_EVENT_SCOPE(Extraction_Director_UpdateSawtooth);

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
	TRACE_CPUPROFILER_EVENT_SCOPE(Extraction_Director_ShouldSpawn);

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
		if (!bAmbientSpawningEnabled) return false;
		if (AlertLevel != EGlobalAlertLevel::Loud) return false;
		// Sustained-pressure phases bypass the sawtooth: no Peak/Relief pauses, no tension ceiling.
		// Cadence and MaxAlive still gate below.
		if (!PhaseConfig.bSustainedPressure)
		{
			if (DirectorState != EDirectorState::Build) return false;
			if (Tension >= PhaseConfig.IntensityCeiling) return false;
		}
	}

	// The wave's FIRST squad gets its own delay. StartWave zeroes TimeSinceLastSpawn, so the cadence
	// gate below applied to squad one too and the wave opened with a full cadence of silence — the
	// player triggers the defence and nothing happens for 25s.
	const bool bFirstWaveSquad = bWaveCanSpawn && WaveProgress.SpawnedSquads == 0;
	const float Cadence = bFirstWaveSquad
		? ActiveWaveRequest.FirstSquadDelaySeconds
		: ((bWaveCanSpawn && ActiveWaveRequest.SpawnCadenceOverride > 0.f)
			? ActiveWaveRequest.SpawnCadenceOverride
			: PhaseConfig.SpawnCadenceSeconds);
	if (TimeSinceLastSpawn < Cadence) return false;

	// A guaranteed squad waives the alive cap — otherwise the set-piece it exists to deliver is
	// silently dropped exactly when the fight is busiest, which is when it matters most. One squad,
	// once per wave slot; PickComposition applies the same waiver to the fit filter.
	if (AliveCount >= PhaseConfig.MaxAlive && !FindGuaranteedSquadForNext()) return false;

	return true;
}

bool UEnemyDirectorSubsystem::TrySpawn(int32 AliveCount, TArray<AEnemyCharacter*>* OutSpawned,
	const TSet<AEnemySpawnZone*>* ExcludedZones, AEnemySpawnZone** OutUsedZone)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Extraction_Director_TrySpawn);

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

	AEnemySpawnZone* Zone = PickSpawnZone(PlayerPawn->GetActorLocation(), ViewLoc, ViewRot, GetCompositionSize(Composition), ExcludedZones);
	if (!IsValid(Zone))
	{
		// Suppress the warning for secondary picks in a multi-zone beat — running out of
		// distinct zones is a legitimate outcome, not a fault.
		if (!bLoggedNoZone && !ExcludedZones)
		{
			UE_LOG(LogEnemyAI, Warning, TEXT("Director: no eligible spawn zone (phase %d, %d zones registered, wave=%s) — %s"),
				static_cast<int32>(GetEffectivePhase()), SpawnZones.Num(),
				WaveProgress.IsActive() ? *ActiveWaveRequest.WaveId.ToString() : TEXT("none"),
				*LastZoneRejects.ToString());
			bLoggedNoZone = true;
		}
		return false;
	}

	// Beat-to-beat repeat-zone penalty: only update for the primary pick (no exclusion
	// set). Secondary picks differ by construction; letting them touch the counter would
	// reset it to 1 every beat and kill the penalty.
	if (!ExcludedZones)
	{
		ConsecutiveZonePicks = (Zone == LastPickedZone.Get()) ? ConsecutiveZonePicks + 1 : 1;
		LastPickedZone = Zone;
	}

	TArray<AEnemyCharacter*> Spawned;
	SpawnSquadAtZone(Composition, Zone, Spawned);

	if (Spawned.Num() == 0) return false;

	TimeSinceLastSpawn = 0.f;
	bLoggedNoComposition = false;
	bLoggedNoZone = false;

	if (OutSpawned) *OutSpawned = MoveTemp(Spawned);
	if (OutUsedZone) *OutUsedZone = Zone;

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

const FDirectorGuaranteedSquad* UEnemyDirectorSubsystem::FindGuaranteedSquadForNext() const
{
	if (!WaveProgress.CanSpawnMore()) return nullptr;

	const int32 NextSquadNumber = WaveProgress.SpawnedSquads + 1;
	for (const FDirectorGuaranteedSquad& Entry : ActiveWaveRequest.GuaranteedSquads)
	{
		if (Entry.SquadNumber == NextSquadNumber && !Entry.CompositionName.IsNone())
			return &Entry;
	}
	return nullptr;
}

bool UEnemyDirectorSubsystem::PickComposition(const FMissionPhaseConfig& PhaseConfig, int32 AliveCount, FSquadComposition& OutComposition) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Extraction_Director_PickComposition);

	const TArray<FSquadComposition>& Compositions = PhaseConfig.Compositions;
	if (Compositions.Num() == 0) return false;

	// Guaranteed slot: return the pinned composition outright, skipping both the weighted roll and
	// the MaxAlive fit filter. An unresolvable name falls through to the normal roll below with a
	// warning — a typo must not stall the wave, but it must be visible.
	if (const FDirectorGuaranteedSquad* Guaranteed = FindGuaranteedSquadForNext())
	{
		const FSquadComposition* Pinned = Compositions.FindByPredicate(
			[Guaranteed](const FSquadComposition& Comp) { return Comp.Name == Guaranteed->CompositionName; });

		if (Pinned && GetCompositionSize(*Pinned) > 0)
		{
			OutComposition = *Pinned;
			UE_LOG(LogEnemyAI, Log, TEXT("Director wave %s: squad %d is guaranteed '%s' (size %d, alive %d/%d)"),
				*ActiveWaveRequest.WaveId.ToString(), Guaranteed->SquadNumber,
				*Guaranteed->CompositionName.ToString(), GetCompositionSize(*Pinned), AliveCount, PhaseConfig.MaxAlive);
			return true;
		}

		UE_LOG(LogEnemyAI, Warning, TEXT("Director wave %s: guaranteed composition '%s' for squad %d not found (or empty) in the phase config — falling back to the weighted roll"),
			*ActiveWaveRequest.WaveId.ToString(), *Guaranteed->CompositionName.ToString(), Guaranteed->SquadNumber);
	}

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

AEnemySpawnZone* UEnemyDirectorSubsystem::PickSpawnZone(const FVector& PlayerLoc, const FVector& ViewLoc, const FRotator& ViewRot, int32 SquadSize,
	const TSet<AEnemySpawnZone*>* ExcludedZones) const
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

	// Per-filter reject tally. "No eligible zone" is otherwise a dead end for anyone debugging a
	// wave that spawns nothing: six independent filters, all silent, and the one warning that does
	// exist latches after its first fire. The counts name the culprit in one line.
	LastZoneRejects = FZoneRejectCounts();

	// First pass: collect candidates that survive all gates.
	TArray<AEnemySpawnZone*, TInlineAllocator<8>> Candidates;
	for (const TWeakObjectPtr<AEnemySpawnZone>& WeakZone : SpawnZones)
	{
		AEnemySpawnZone* Zone = WeakZone.Get();
		if (!IsValid(Zone)) continue;
		++LastZoneRejects.Considered;
		if (ExcludedZones && ExcludedZones->Contains(Zone)) continue;
		if (!Zone->IsActiveForPhase(GetEffectivePhase())) { ++LastZoneRejects.Phase; continue; }
		if (bWaveActive && !Zone->bWaveEligible) { ++LastZoneRejects.WaveIneligible; continue; }

		const FVector ZoneLoc = Zone->GetZoneOrigin();
		if (bUseScopeVolumes && !IsPointInsideAnyScope(ZoneLoc)) { ++LastZoneRejects.OutsideScope; continue; }

		if (IsZoneTooClose(Zone, PlayerLoc)) { ++LastZoneRejects.TooClose; continue; }
		if (FVector::DistSquared(PlayerLoc, ZoneLoc) > DistMax * DistMax) { ++LastZoneRejects.TooFar; continue; }

		if (!bWaveSightlineWaived && !IsZoneHiddenFromPlayer(Zone, FMath::Max(MinSightlineSamples, SquadSize), SquadSize, ViewLoc, ViewDir, QueryParams, NavSys)) { ++LastZoneRejects.Visible; continue; }

		if (IsValid(NavSys))
		{
			FNavLocation NavLoc;
			const FVector Extent(NavProjectExtentXY, NavProjectExtentXY, NavProjectExtentZ);
			if (!NavSys->ProjectPointToNavigation(ZoneLoc, NavLoc, Extent)) { ++LastZoneRejects.OffNavMesh; continue; }
		}

		Candidates.Add(Zone);
	}

	// Second pass: score candidates, applying a repeat-zone penalty when alternatives exist.
	// The penalty only bites after a zone has taken MaxConsecutiveZonePicks in a row, so a zone
	// whose WaveScoreBias out-scores its distance disadvantage wins every pick until it has spawned
	// twice running, then yields one pick to the runner-up — a ~2:1 majority, never 3+ in a row.
	for (AEnemySpawnZone* Zone : Candidates)
	{
		float Score = ScoreZone(Zone, PlayerLoc, CompanionLoc, bHasCompanion, DistMin, DistMax);
		if (bWaveActive)
			Score += Zone->WaveScoreBias;
		if (Candidates.Num() > 1 && Zone == LastPickedZone.Get() && ConsecutiveZonePicks >= MaxConsecutiveZonePicks)
			Score -= RepeatZonePenalty;
		if (Score > BestScore)
		{
			BestScore = Score;
			BestZone = Zone;
		}
	}

	return BestZone;
}

FString UEnemyDirectorSubsystem::FZoneRejectCounts::ToString() const
{
	return FString::Printf(
		TEXT("considered=%d rejected: phase=%d wave-ineligible=%d outside-scope=%d too-close=%d too-far=%d visible=%d off-navmesh=%d"),
		Considered, Phase, WaveIneligible, OutsideScope, TooClose, TooFar, Visible, OffNavMesh);
}

bool UEnemyDirectorSubsystem::IsZoneTooClose(const AEnemySpawnZone* Zone, const FVector& PlayerLoc) const
{
	float DistMin = DefaultSpawnDistMin;
	float StoreySep = DefaultStoreySeparationHeight;
	float DistMinOtherStorey = DefaultSpawnDistMinDifferentStorey;

	const UDirectorConfigData* Cfg = GetEffectiveConfig();
	if (IsValid(Cfg))
	{
		DistMin = Cfg->SpawnDistanceMin;
		StoreySep = Cfg->StoreySeparationHeight;
		DistMinOtherStorey = Cfg->SpawnDistanceMinDifferentStorey;
	}

	const FVector ClosestPt = Zone->GetClosestPointInZone(PlayerLoc);
	const float VerticalSep = FMath::Abs(ClosestPt.Z - PlayerLoc.Z);

	// The storey relaxation is only defensible while the sightline gate (IsZoneHiddenFromPlayer)
	// is actually running -- it independently rejects zones the player can see through stairwells
	// or over mezzanine rails. When bWaveSightlineWaived is set, that gate is skipped, so the
	// distance gate is the last line of defence against visible pop-in and must stay strict.
	if (!bWaveSightlineWaived && VerticalSep >= StoreySep)
	{
		const float HorizDistSq = FVector::DistSquared2D(ClosestPt, PlayerLoc);
		return HorizDistSq < DistMinOtherStorey * DistMinOtherStorey;
	}

	// Same floor (or sightline waived): full 3D distance against the standard minimum.
	return FVector::DistSquared(PlayerLoc, ClosestPt) < DistMin * DistMin;
}

bool UEnemyDirectorSubsystem::IsZoneHiddenFromPlayer(const AEnemySpawnZone* Zone, int32 SampleCount, int32 SquadSize, const FVector& ViewLoc, const FVector& ViewDir, const FCollisionQueryParams& QueryParams, UNavigationSystemV1* NavSys) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Extraction_Director_ZoneVisibility);

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
		FVector Pt = Zone->GetSpawnTransform(Si, SquadSize).GetLocation();

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
	TRACE_CPUPROFILER_EVENT_SCOPE(Extraction_Director_ScoreZone);

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

	// Spawn-zone scoring anchors on the primary companion when it's alive (deterministic with a
	// second companion in the level); any living companion is an acceptable fallback.
	const ACompanionCharacter* Fallback = nullptr;
	for (TActorIterator<ACompanionCharacter> It(World); It; ++It)
	{
		const ACompanionCharacter* Companion = *It;
		if (!IsValid(Companion)) continue;

		const UHealthComponent* HC = Companion->FindComponentByClass<UHealthComponent>();
		if (IsValid(HC) && HC->IsDead()) continue;

		if (Companion->IsPrimaryCompanion()) return Companion;
		if (!Fallback) Fallback = Companion;
	}

	return Fallback;
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

bool UEnemyDirectorSubsystem::ProjectSpawnPointToNav(UNavigationSystemV1* NavSys, const AEnemySpawnZone* Zone,
	int32 EffectiveIndex, FVector& InOutLocation) const
{
	static constexpr float GoldenAngleDeg = 137.508f;
	const FVector Extent(NavProjectExtentXY, NavProjectExtentXY, NavProjectExtentZ);

	FNavLocation NavLoc;
	if (NavSys->ProjectPointToNavigation(InOutLocation, NavLoc, Extent))
	{
		InOutLocation = NavLoc.Location;
		return true;
	}

	// Jittered fallback: offset from zone origin using this index's spiral angle
	// so failures fan out instead of collapsing onto one point.
	const float Rad = FMath::DegreesToRadians(FMath::Fmod(float(EffectiveIndex) * GoldenAngleDeg, 360.f));
	const float R = NavProjectExtentXY * 0.5f;
	FVector Fb = Zone->GetZoneOrigin() + FVector(FMath::Cos(Rad) * R, FMath::Sin(Rad) * R, 0.f);
	FNavLocation FbLoc;
	if (!NavSys->ProjectPointToNavigation(Fb, FbLoc, Extent)) return false;
	InOutLocation = FbLoc.Location;
	return true;
}

bool UEnemyDirectorSubsystem::FindSeparatedSpawnLocation(UNavigationSystemV1* NavSys, AEnemySpawnZone* Zone,
	int32 Index, int32 SquadSize, TArray<FVector>& UsedPositions, FVector& OutLocation, FRotator& OutRotation) const
{
	static constexpr int32 MaxRetries = 3;
	const float ClampedSep = FMath::Max(Zone->MinSpawnSeparation, 0.f);
	const float MinSepSq = ClampedSep * ClampedSep;

	for (int32 Retry = 0; Retry <= MaxRetries; ++Retry)
	{
		const int32 EffIdx = Index + Retry;
		const FTransform SpawnXform = Zone->GetSpawnTransform(EffIdx, SquadSize);
		FVector Loc = SpawnXform.GetLocation();

		if (IsValid(NavSys) && !ProjectSpawnPointToNav(NavSys, Zone, EffIdx, Loc))
			continue;

		// Reject points within MinSpawnSeparation of an already-used position in this squad.
		bool bTooClose = false;
		if (MinSepSq > 0.f)
			for (const FVector& U : UsedPositions)
				if (FVector::DistSquared2D(Loc, U) < MinSepSq) { bTooClose = true; break; }

		if (bTooClose && Retry < MaxRetries) continue;
		if (bTooClose)
			UE_LOG(LogEnemyAI, Warning, TEXT("Director: separation retries exhausted at zone '%s' (index %d)"), *Zone->GetName(), Index);

		UsedPositions.Add(Loc);
		OutLocation = Loc;
		OutRotation = SpawnXform.GetRotation().Rotator();
		return true;
	}
	return false;
}

AEnemyCharacter* UEnemyDirectorSubsystem::SpawnEntryAtZone(UWorld* World, TSubclassOf<AEnemyCharacter> EnemyClass,
	AEnemySpawnZone* Zone, int32 Index, int32 SquadSize, TArray<FVector>& UsedPositions)
{
	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);

	FVector SpawnLoc;
	FRotator SpawnRot;
	if (!FindSeparatedSpawnLocation(NavSys, Zone, Index, SquadSize, UsedPositions, SpawnLoc, SpawnRot))
	{
		UE_LOG(LogEnemyAI, Warning, TEXT("Director: nav-project failed for all retries at zone '%s' (index %d)"),
			*Zone->GetName(), Index);
		return nullptr;
	}

	// Nav-projected locations are on the floor; raise by the class's capsule half-height.
	float CapsuleHalfHeight = 88.f;
	if (const AEnemyCharacter* CDO = EnemyClass->GetDefaultObject<AEnemyCharacter>())
		if (const UCapsuleComponent* Cap = CDO->GetCapsuleComponent())
			CapsuleHalfHeight = Cap->GetScaledCapsuleHalfHeight();
	SpawnLoc.Z += CapsuleHalfHeight + SpawnGroundClearance;

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	AEnemyCharacter* Spawned = World->SpawnActor<AEnemyCharacter>(
		EnemyClass, SpawnLoc, SpawnRot, SpawnParams);

	if (IsValid(Spawned))
	{
		UE_LOG(LogEnemyAI, Verbose, TEXT("Director spawned %s at zone %s (index %d)"), *Spawned->GetName(), *Zone->GetName(), Index);
	}
	else
	{
		UE_LOG(LogEnemyAI, Warning, TEXT("Director spawn failed for %s at zone %s (index %d)"), *EnemyClass->GetName(), *Zone->GetName(), Index);
	}

	return Spawned;
}

void UEnemyDirectorSubsystem::SpawnSquadAtZone(const FSquadComposition& Composition, AEnemySpawnZone* Zone, TArray<AEnemyCharacter*>& OutSpawned)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Extraction_Director_SpawnSquad);

	UWorld* World = GetWorld();
	if (!IsValid(World) || !IsValid(Zone)) return;

	if (!CachedSquadSubsystem.IsValid())
	{
		CachedSquadSubsystem = World->GetSubsystem<UEnemySquadSubsystem>();
	}

	const int32 ExpectedSize = GetCompositionSize(Composition);
	OutSpawned.Reserve(ExpectedSize);
	TArray<FVector> UsedPositions;
	UsedPositions.Reserve(ExpectedSize);
	int32 SpawnIndex = 0;

	for (const FSquadCompositionEntry& Entry : Composition.Entries)
	{
		if (!Entry.EnemyClass || Entry.Count <= 0) continue;

		for (int32 i = 0; i < Entry.Count; ++i)
		{
			AEnemyCharacter* Spawned = SpawnEntryAtZone(World, Entry.EnemyClass, Zone, SpawnIndex, ExpectedSize, UsedPositions);
			if (IsValid(Spawned)) OutSpawned.Add(Spawned);
			++SpawnIndex;
		}
	}

	FinalizeSpawnedSquad(OutSpawned);

	UE_LOG(LogEnemyAI, Log, TEXT("Director spawned squad (%d/%d members) at zone %s"),
		OutSpawned.Num(), SpawnIndex, *Zone->GetName());
}

void UEnemyDirectorSubsystem::FinalizeSpawnedSquad(const TArray<AEnemyCharacter*>& Spawned)
{
	if (Spawned.Num() == 0 || !CachedSquadSubsystem.IsValid()) return;

	UEnemySquad* Squad = CachedSquadSubsystem->CreateSquadForGroup(Spawned);
	if (!IsValid(Squad)) return;

	SeedSquadWithFight(Squad);

	if (PunishmentSource.IsValid())
		PunishmentSquads.Add(Squad);
}

void UEnemyDirectorSubsystem::SeedSquadWithFight(UEnemySquad* Squad) const
{
	UWorld* World = GetWorld();
	if (!IsValid(World) || !IsValid(Squad)) return;

	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(World, 0);
	if (!IsValid(PlayerPawn)) return;

	const bool bWaveAutoEngage = WaveProgress.IsActive() && ActiveWaveRequest.bAutoEngage;
	// Sustained pressure is an ambient concept; the wave request's own bAutoEngage governs waves.
	const bool bSustainedPressure = !WaveProgress.IsActive() && GetCurrentPhaseConfig().bSustainedPressure;

	if (bWaveAutoEngage || bSustainedPressure)
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

bool UEnemyDirectorSubsystem::IsSustainedPressureActive() const
{
	// Sustained pressure is an ambient concept — waves use bAutoEngage, not this flag.
	if (WaveProgress.IsActive()) return false;
	return GetCurrentPhaseConfig().bSustainedPressure;
}

// ============================================================
// v2: Finite Wave Lifecycle
// ============================================================

bool UEnemyDirectorSubsystem::StartWave(const FDirectorWaveRequest& Request)
{
	if (GetWorld()->GetNetMode() == NM_Client) return false;
	if (WaveProgress.IsActive()) return false;
	if (Request.TargetSquads < 1) return false;

	// A new wave supersedes any surviving last-man latch from the previous wave. Unlatch before
	// overwriting ActiveWaveRequest so SetMoralePinnedConfident(false) fires with the old state.
	UnlatchAllLastMan();

	ActiveWaveRequest = Request;
	WaveProgress.Begin(Request.TargetSquads);
	WaveBlockedTime = 0.f;
	bWaveBlockedBroadcast = false;

	// Clear the once-only diagnostic latches. They only reset after a SUCCESSFUL spawn, so an
	// ambient failure earlier in the level would otherwise silence every failure this wave makes —
	// a wave that spawns nothing for its whole life with not one line in the log.
	bLoggedNoComposition = false;
	bLoggedNoZone = false;
	bLoggedNoScopeVolumes = false;

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
	bWaveSightlineWaived = false;
	LastRallyWorldTime = 0.0;
	LastPickedZone = nullptr;
	ConsecutiveZonePicks = 0;
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

	// Last-man rally: when every squad has spawned and no combat has been reported for a while,
	// ForceEngage + rally morale so stuck survivors push out of hiding toward the player.
	// Throttled by LastRallyWorldTime so it fires at most once per StaleCombatRallySeconds
	// (ForceEngage early-outs on already-Combat enemies without refreshing LastCombatReportTime).
	constexpr float StaleCombatRallySeconds = 12.f;
	const double Now = World->GetTimeSeconds();
	const bool bAllSquadsSpawned = WaveProgress.SpawnedSquads >= WaveProgress.TargetSquads;
	const bool bCombatStale = bAllSquadsSpawned
		&& (Now - LastCombatReportTime) >= StaleCombatRallySeconds
		&& (Now - LastRallyWorldTime) >= StaleCombatRallySeconds;

	// Wave-roster alive count for the membership floor latch below. Roster-based on purpose:
	// TryArmLastManHunt counts and latches in-scope enemies only, so the survivor who has already
	// fled the room reads as 0 alive and never arms, while the stale-combat rally cannot fire as
	// long as he keeps re-entering Combat (every entry re-stamps LastCombatReportTime). The
	// intersection of those two blind spots is exactly the last enemy hiding in the next room.
	int32 AliveMembers = 0;
	for (const TWeakObjectPtr<AEnemyCharacter>& WeakMember : WaveMembers)
	{
		const AEnemyCharacter* Member = WeakMember.Get();
		if (!IsValid(Member)) continue;
		const UHealthComponent* HP = Member->GetHealthComponent();
		if (HP && HP->IsDead()) continue;
		++AliveMembers;
	}
	const int32 HuntThreshold = ActiveWaveRequest.LastManHuntThreshold;
	const bool bMembershipFloor = bAllSquadsSpawned && HuntThreshold > 0
		&& AliveMembers > 0 && AliveMembers <= HuntThreshold;

	for (const TWeakObjectPtr<AEnemyCharacter>& WeakMember : WaveMembers)
	{
		AEnemyCharacter* Member = WeakMember.Get();
		if (!IsValid(Member)) continue;
		const UHealthComponent* HP = Member->GetHealthComponent();
		if (HP && HP->IsDead()) continue;

		AEnemyAIController* AIC = Cast<AEnemyAIController>(Member->GetController());
		UEnemyAwarenessComponent* Awareness = AIC ? AIC->GetAwarenessComponent() : nullptr;
		if (!Awareness) continue;

		// Original logic: re-seed Combat on any member that fully gave up (decayed to Unaware).
		if (Awareness->GetAwarenessState() == EEnemyAwarenessState::Unaware)
		{
			Awareness->ForceEngage(PlayerPawn);
			UE_LOG(LogEnemyAI, Log, TEXT("Director wave %s: re-engaging idle member %s"),
				*ActiveWaveRequest.WaveId.ToString(), *Member->GetName());
		}

		// Last-man rally: refresh position + restore morale so Shaken/Broken survivors push out.
		if (bCombatStale)
		{
			Awareness->ForceEngage(PlayerPawn);
			if (UEnemyMoraleComponent* Morale = Member->GetMoraleComponent())
				Morale->RallyToConfident();

			// ForceEngage on its own only re-points the survivor at a stale LastKnownLocation, so he
			// re-camps cover a room away and the rally re-fires every StaleCombatRallySeconds forever
			// (the holdout who runs upstairs and is never found). Latching the hunt here is deliberately
			// independent of TryArmLastManHunt's alive-count threshold and scope volumes: a fight that
			// has gone silent with every squad spawned IS the holdout case regardless of how many are
			// left, and the runner has usually fled outside the scope volume by then. RefreshLastManLatched
			// then holds live contact at 1 Hz and CombatFire's no-contact test releases him to pursue.
			if (!Awareness->IsLastManHunting())
			{
				Awareness->SetLastManHunting(true);
				LastManLatched.Add(Member);
			}

			UE_LOG(LogEnemyAI, Log, TEXT("Director wave %s: rallying stale member %s (hunting)"),
				*ActiveWaveRequest.WaveId.ToString(), *Member->GetName());
		}

		// Membership floor: latch the moment the wave's own roster is down to the hunt threshold —
		// deterministic, no scope-volume or combat-staleness dependency. RefreshLastManLatched runs
		// later this same tick and immediately stamps live player contact + pins morale Confident.
		if (bMembershipFloor && !Awareness->IsLastManHunting())
		{
			Awareness->SetLastManHunting(true);
			LastManLatched.Add(Member);
			UE_LOG(LogEnemyAI, Log, TEXT("Director wave %s: last-man floor latched %s (%d wave members alive)"),
				*ActiveWaveRequest.WaveId.ToString(), *Member->GetName(), AliveMembers);
		}
	}

	if (bCombatStale) LastRallyWorldTime = Now;

	// Arming only: populate LastManLatched when the wave qualifies. Refresh runs separately in
	// DirectorTick so it survives wave completion.
	TryArmLastManHunt();
}

void UEnemyDirectorSubsystem::TryArmLastManHunt()
{
	if (!WaveProgress.IsActive()) return;
	const bool bAllSquadsSpawned = WaveProgress.SpawnedSquads >= WaveProgress.TargetSquads;
	if (!bAllSquadsSpawned) return;

	const int32 Threshold = ActiveWaveRequest.LastManHuntThreshold;
	if (Threshold <= 0) return;

	if (LastSweepAliveCount <= 0 || LastSweepAliveCount > Threshold) return;

	if (!HasActiveScopeVolumes())
	{
		if (!bLoggedNoScopeVolumes)
		{
			bLoggedNoScopeVolumes = true;
			UE_LOG(LogEnemyAI, Warning,
				TEXT("Director wave %s: last-man hunt would arm (alive=%d, threshold=%d) but no scope volumes are active"),
				*ActiveWaveRequest.WaveId.ToString(), LastSweepAliveCount, Threshold);
		}
		return;
	}

	// Already armed — RefreshLastManLatched handles the ongoing work.
	if (LastManLatched.Num() > 0) return;

	UWorld* World = GetWorld();
	if (!IsValid(World)) return;

	for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
	{
		AEnemyCharacter* Enemy = *It;
		if (!IsValid(Enemy)) continue;
		if (!IsActorInsideAnyScope(Enemy)) continue;

		const UHealthComponent* HC = Enemy->GetHealthComponent();
		if (!IsValid(HC) || HC->IsDead()) continue;

		AEnemyAIController* AIC = Cast<AEnemyAIController>(Enemy->GetController());
		UEnemyAwarenessComponent* Awareness = IsValid(AIC) ? AIC->GetAwarenessComponent() : nullptr;
		if (!IsValid(Awareness)) continue;

		Awareness->SetLastManHunting(true);
		LastManLatched.Add(Enemy);
		UE_LOG(LogEnemyAI, Log, TEXT("Director wave %s: last-man hunt latched on %s (%d alive in scope)"),
			*ActiveWaveRequest.WaveId.ToString(), *Enemy->GetName(), LastSweepAliveCount);
	}
}

void UEnemyDirectorSubsystem::RefreshLastManLatched()
{
	if (LastManLatched.Num() == 0) return;

	UWorld* World = GetWorld();
	APawn* PlayerPawn = IsValid(World) ? UGameplayStatics::GetPlayerPawn(World, 0) : nullptr;

	for (auto It = LastManLatched.CreateIterator(); It; ++It)
	{
		AEnemyCharacter* Enemy = It->Get();
		if (!IsValid(Enemy))
		{
			It.RemoveCurrent();
			continue;
		}

		const UHealthComponent* HC = Enemy->GetHealthComponent();
		if (IsValid(HC) && HC->IsDead())
		{
			// Death path already called SetLastManHunting(false) + DeactivateForDeath.
			It.RemoveCurrent();
			continue;
		}

		if (!IsValid(PlayerPawn)) continue;

		AEnemyAIController* AIC = Cast<AEnemyAIController>(Enemy->GetController());
		UEnemyAwarenessComponent* Awareness = IsValid(AIC) ? AIC->GetAwarenessComponent() : nullptr;
		if (!IsValid(Awareness)) continue;

		Awareness->RefreshLastManContact(PlayerPawn);
		if (UEnemyMoraleComponent* Morale = Enemy->GetMoraleComponent())
			Morale->SetMoralePinnedConfident(true);
	}
}

void UEnemyDirectorSubsystem::UnlatchAllLastMan()
{
	for (const TWeakObjectPtr<AEnemyCharacter>& WeakEnemy : LastManLatched)
	{
		AEnemyCharacter* Enemy = WeakEnemy.Get();
		if (!IsValid(Enemy)) continue;

		AEnemyAIController* AIC = Cast<AEnemyAIController>(Enemy->GetController());
		UEnemyAwarenessComponent* Awareness = IsValid(AIC) ? AIC->GetAwarenessComponent() : nullptr;
		if (IsValid(Awareness)) Awareness->SetLastManHunting(false);

		if (UEnemyMoraleComponent* Morale = Enemy->GetMoraleComponent())
			Morale->SetMoralePinnedConfident(false);
	}
	LastManLatched.Empty();
}

void UEnemyDirectorSubsystem::AccrueWaveBlockedTime()
{
	if (!WaveProgress.CanSpawnMore()) return;

	WaveBlockedTime += DirectorTickInterval;

	if (!bWaveBlockedBroadcast && WaveBlockedTime >= ActiveWaveRequest.BlockedWarningSeconds)
	{
		bWaveBlockedBroadcast = true;
		OnDirectorWaveBlocked.Broadcast(ActiveWaveRequest.WaveId,
			FText::FromString(TEXT("Wave spawn blocked: no valid zone or composition")));
		UE_LOG(LogEnemyAI, Warning, TEXT("Director wave %s blocked for %.0fs"),
			*ActiveWaveRequest.WaveId.ToString(), WaveBlockedTime);
	}

	// Sightline relief: once blocked for twice the warning period, waive the IsZoneHiddenFromPlayer
	// gate so the wave can make progress via a visible spawn zone. All other filters (scope, nav,
	// phase, wave-eligible) remain enforced; the storey-distance relaxation reverts to the strict
	// same-floor 3D minimum so enemies cannot pop in over a mezzanine rail without occlusion cover.
	if (!bWaveSightlineWaived && WaveBlockedTime >= ActiveWaveRequest.BlockedWarningSeconds * 2.f)
	{
		bWaveSightlineWaived = true;
		UE_LOG(LogEnemyAI, Warning, TEXT("Director wave %s: sightline gate waived after %.0fs blocked"),
			*ActiveWaveRequest.WaveId.ToString(), WaveBlockedTime);
	}
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
	PunishmentSquads.Empty();
	LastPunishmentSquadRefreshTime = -1e9;
}

void UEnemyDirectorSubsystem::RefreshPunishmentSquadTargets()
{
	// Prune unconditionally so dead squads don't accumulate behind the config/rate gates.
	PunishmentSquads.RemoveAll([](const TWeakObjectPtr<UEnemySquad>& Entry) { return !Entry.IsValid(); });

	if (AlertLevel != EGlobalAlertLevel::Loud) return;
	if (!PunishmentSource.IsValid()) return;

	const UDirectorConfigData* EffectiveConfig = GetEffectiveConfig();
	if (!EffectiveConfig || !EffectiveConfig->bRefreshSquadSearchTarget) return;

	UWorld* World = GetWorld();
	if (!IsValid(World)) return;

	const double Now = World->GetTimeSeconds();
	if (Now - LastPunishmentSquadRefreshTime < EffectiveConfig->SquadSearchRefreshInterval) return;
	LastPunishmentSquadRefreshTime = Now;

	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(World, 0);
	if (!IsValid(PlayerPawn)) return;

	for (const TWeakObjectPtr<UEnemySquad>& WeakSquad : PunishmentSquads)
	{
		UEnemySquad* Squad = WeakSquad.Get();
		if (!IsValid(Squad)) continue;

		// Feed squads the best PERCEIVED position instead of the player's live location.
		// If no member has perceived the player, the escalation-time snapshot already
		// stored in SquadLastKnown is the correct fallback.
		const FVector LastKnown = FindBestLastKnownFromSquad(Squad, PlayerPawn);
		if (!LastKnown.IsZero())
			Squad->ReportSighting(PlayerPawn, LastKnown);
	}
}

FVector UEnemyDirectorSubsystem::FindBestLastKnownFromSquad(const UEnemySquad* Squad, const APawn* PlayerPawn) const
{
	if (!IsValid(Squad) || !IsValid(PlayerPawn)) return FVector::ZeroVector;

	FVector BestLastKnown = FVector::ZeroVector;

	for (const TWeakObjectPtr<AEnemyCharacter>& WeakMember : Squad->GetMembers())
	{
		AEnemyCharacter* Member = WeakMember.Get();
		if (!IsValid(Member)) continue;

		AEnemyAIController* AIC = Cast<AEnemyAIController>(Member->GetController());
		if (!IsValid(AIC)) continue;

		UEnemyAwarenessComponent* Awareness = AIC->GetAwarenessComponent();
		if (!IsValid(Awareness)) continue;

		// Only accept members whose combat target is the player pawn. Members fighting
		// the companion would relay the companion's position as the player's last-known.
		// GetCombatTarget() returns nullptr outside Combat, so only Combat-state members
		// that are actively targeting the player pass this check.
		if (Awareness->GetCombatTarget() != PlayerPawn) continue;

		const FVector MemberLastKnown = Awareness->GetLastKnownLocation();
		if (MemberLastKnown.IsZero()) continue;

		BestLastKnown = MemberLastKnown;
		if (Awareness->HasLOSToTarget()) break;
	}

	return BestLastKnown;
}
