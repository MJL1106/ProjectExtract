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

	const FEnemySweepResult Sweep = SweepEnemies();

	UpdateTension(DirectorTickInterval, Sweep.EngagedCount);
	UpdateSawtooth(DirectorTickInterval);

	if (ShouldSpawn(Sweep.AliveCount))
	{
		TrySpawn(Sweep.AliveCount);
	}

	TimeSinceLastSpawn += DirectorTickInterval;
}

// ============================================================
// v2: Kill Tracking
// ============================================================

void UEnemyDirectorSubsystem::HandleEnemyKilled(AEnemyCharacter* DeadEnemy, FVector Location, bool bWasOfficer)
{
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

	if (IsValid(Config))
	{
		DecayPerSec = Config->TensionDecayPerSecond;
		PerHealthLost = Config->TensionPerPlayerHealthLost;
		PerKill = Config->TensionPerKill;
		PerEngaged = Config->TensionPerEngagedEnemy;
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

	if (IsValid(Config))
	{
		PeakThreshold = Config->PeakTensionThreshold;
		ReliefEntry = Config->ReliefEntryThreshold;
		ReliefDur = Config->ReliefDuration;
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
	if (AlertLevel != EGlobalAlertLevel::Loud) return false;
	if (DirectorState != EDirectorState::Build) return false;

	const FMissionPhaseConfig& PhaseConfig = GetCurrentPhaseConfig();

	if (TimeSinceLastSpawn < PhaseConfig.SpawnCadenceSeconds) return false;
	if (AliveCount >= PhaseConfig.MaxAlive) return false;
	if (Tension >= PhaseConfig.IntensityCeiling) return false;

	return true;
}

void UEnemyDirectorSubsystem::TrySpawn(int32 AliveCount)
{
	const FMissionPhaseConfig& PhaseConfig = GetCurrentPhaseConfig();

	FSquadComposition Composition;
	if (!PickComposition(PhaseConfig, AliveCount, Composition))
	{
		if (!bLoggedNoComposition)
		{
			UE_LOG(LogEnemyAI, Verbose, TEXT("Director: no valid composition fits (alive %d, max %d, phase %d)"),
				AliveCount, PhaseConfig.MaxAlive, static_cast<int32>(CurrentMissionPhase));
			bLoggedNoComposition = true;
		}
		return;
	}

	UWorld* World = GetWorld();
	if (!IsValid(World)) return;

	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(World, 0);
	if (!IsValid(PlayerPawn)) return;

	APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0);
	if (!IsValid(PC)) return;

	FVector ViewLoc;
	FRotator ViewRot;
	PC->GetPlayerViewPoint(ViewLoc, ViewRot);

	AEnemySpawnZone* Zone = PickSpawnZone(PlayerPawn->GetActorLocation(), ViewLoc, ViewRot);
	if (!IsValid(Zone))
	{
		if (!bLoggedNoZone)
		{
			UE_LOG(LogEnemyAI, Warning, TEXT("Director: no eligible spawn zone (phase %d, %d zones registered)"),
				static_cast<int32>(CurrentMissionPhase), SpawnZones.Num());
			bLoggedNoZone = true;
		}
		return;
	}

	SpawnSquadAtZone(Composition, Zone);
	TimeSinceLastSpawn = 0.f;
	bLoggedNoComposition = false;
	bLoggedNoZone = false;
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
	if (IsValid(Config)) return Config->GetPhaseConfig(CurrentMissionPhase);

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

AEnemySpawnZone* UEnemyDirectorSubsystem::PickSpawnZone(const FVector& PlayerLoc, const FVector& ViewLoc, const FRotator& ViewRot) const
{
	UWorld* World = GetWorld();
	if (!IsValid(World)) return nullptr;

	float DistMin = DefaultSpawnDistMin;
	float DistMax = DefaultSpawnDistMax;
	if (IsValid(Config))
	{
		DistMin = Config->SpawnDistanceMin;
		DistMax = Config->SpawnDistanceMax;
	}

	const float DistMinSq = DistMin * DistMin;
	const float DistMaxSq = DistMax * DistMax;
	const FVector ViewDir = ViewRot.Vector();

	FCollisionQueryParams QueryParams;
	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(World, 0);
	if (IsValid(PlayerPawn)) QueryParams.AddIgnoredActor(PlayerPawn);

	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	const bool bUseScopeVolumes = HasActiveScopeVolumes();

	TArray<AEnemySpawnZone*> Candidates;
	Candidates.Reserve(MaxZoneCandidates);

	for (const TWeakObjectPtr<AEnemySpawnZone>& WeakZone : SpawnZones)
	{
		AEnemySpawnZone* Zone = WeakZone.Get();
		if (!IsValid(Zone)) continue;
		if (!Zone->IsActiveForPhase(CurrentMissionPhase)) continue;

		const FVector ZoneLoc = Zone->GetZoneOrigin();
		if (bUseScopeVolumes && !IsPointInsideAnyScope(ZoneLoc)) continue;

		const float DistSq = FVector::DistSquared(PlayerLoc, ZoneLoc);
		if (DistSq < DistMinSq || DistSq > DistMaxSq) continue;

		if (IsPointInPlayerSightline(ZoneLoc, ViewLoc, ViewDir, QueryParams)) continue;

		static constexpr int32 SightlineSampleCount = 3;
		bool bAnySampleVisible = false;
		for (int32 Si = 0; Si < SightlineSampleCount; ++Si)
		{
			const FVector Pt = Zone->GetSpawnTransform(Si).GetLocation();
			if (IsPointInPlayerSightline(Pt, ViewLoc, ViewDir, QueryParams))
			{
				bAnySampleVisible = true;
				break;
			}
		}
		if (bAnySampleVisible) continue;

		if (IsValid(NavSys))
		{
			FNavLocation NavLoc;
			const FVector Extent(NavProjectExtentXY, NavProjectExtentXY, NavProjectExtentZ);
			if (!NavSys->ProjectPointToNavigation(ZoneLoc, NavLoc, Extent)) continue;
		}

		Candidates.Add(Zone);
		if (Candidates.Num() >= MaxZoneCandidates) break;
	}

	if (Candidates.Num() == 0) return nullptr;
	return Candidates[FMath::RandRange(0, Candidates.Num() - 1)];
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

void UEnemyDirectorSubsystem::SpawnSquadAtZone(const FSquadComposition& Composition, AEnemySpawnZone* Zone)
{
	UWorld* World = GetWorld();
	if (!IsValid(World) || !IsValid(Zone)) return;

	if (!CachedSquadSubsystem.IsValid())
	{
		CachedSquadSubsystem = World->GetSubsystem<UEnemySquadSubsystem>();
	}

	const int32 ExpectedSize = GetCompositionSize(Composition);
	TArray<AEnemyCharacter*> SpawnedMembers;
	SpawnedMembers.Reserve(ExpectedSize);
	int32 SpawnIndex = 0;

	for (const FSquadCompositionEntry& Entry : Composition.Entries)
	{
		if (!Entry.EnemyClass || Entry.Count <= 0) continue;

		for (int32 i = 0; i < Entry.Count; ++i)
		{
			AEnemyCharacter* Spawned = SpawnEntryAtZone(World, Entry.EnemyClass, Zone, SpawnIndex);
			if (IsValid(Spawned)) SpawnedMembers.Add(Spawned);
			++SpawnIndex;
		}
	}

	UEnemySquad* Squad = nullptr;
	if (SpawnedMembers.Num() > 0 && CachedSquadSubsystem.IsValid())
	{
		Squad = CachedSquadSubsystem->CreateSquadForGroup(SpawnedMembers);
	}

	if (IsValid(Squad))
	{
		SeedSquadWithFight(Squad);
	}

	UE_LOG(LogEnemyAI, Log, TEXT("Director spawned squad (%d/%d members) at zone %s"),
		SpawnedMembers.Num(), SpawnIndex, *Zone->GetName());
}

void UEnemyDirectorSubsystem::SeedSquadWithFight(UEnemySquad* Squad) const
{
	UWorld* World = GetWorld();
	if (!IsValid(World) || !IsValid(Squad)) return;

	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(World, 0);
	if (!IsValid(PlayerPawn)) return;

	Squad->ReportSighting(PlayerPawn, PlayerPawn->GetActorLocation());
}
