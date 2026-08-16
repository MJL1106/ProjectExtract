// UAIOverlaySubsystem -- enemy registry, 10Hz snapshot pass, squad banner queue.

#include "AIOverlaySubsystem.h"

#include "BehaviorTree/BlackboardComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

#include "AI/BlackboardKeyType_Cover.h"
#include "CoverSystemPublicData.h"

#include "CompanionCharacter.h"
#include "CoverPoseComponent.h"
#include "EnemyAIController.h"
#include "EnemyArchetypeData.h"
#include "EnemyAwarenessComponent.h"
#include "EnemyCharacter.h"
#include "EnemyMoraleComponent.h"
#include "EnemySquad.h"
#include "HealthComponent.h"
#include "SuppressionComponent.h"

DEFINE_LOG_CATEGORY(LogAIOverlay);

namespace
{
	const TCHAR* OverlayLevelCVarName = TEXT("ai.Overlay");
	const TCHAR* OverlayScaleCVarName = TEXT("ai.Overlay.Scale");
	const TCHAR* SquadEventMaxAgeCVarName = TEXT("ai.Overlay.SquadEventMaxAge");

	/** AICS cover blackboard key — matches BTTask_EnemyCombatFire's CoverTargetKey default.
	 *  Deliberately NOT BB_CoverSlot, which is always null since the AICS migration. */
	const FName CoverTargetKeyName(TEXT("CoverTarget"));

	/** Skeleton socket the card anchors to. Matches AEnemyCharacter's awareness widget attach. */
	const FName OverlayHeadSocket(TEXT("head"));

	/** Head socket location, or capsule top when the skeleton has no head socket. */
	FVector ResolveHeadAnchor(const AEnemyCharacter* Enemy)
	{
		const USkeletalMeshComponent* Mesh = Enemy->GetMesh();
		if (IsValid(Mesh) && Mesh->DoesSocketExist(OverlayHeadSocket))
			return Mesh->GetSocketLocation(OverlayHeadSocket);

		const UCapsuleComponent* Capsule = Enemy->GetCapsuleComponent();
		const float HalfHeight = IsValid(Capsule) ? Capsule->GetScaledCapsuleHalfHeight() : 0.f;
		return Enemy->GetActorLocation() + FVector(0.f, 0.f, HalfHeight);
	}

	EOverlayTargetKind ClassifyTarget(const AActor* Target)
	{
		if (!IsValid(Target)) return EOverlayTargetKind::None;
		if (Target->IsA<ACompanionCharacter>()) return EOverlayTargetKind::Companion;

		const APawn* Pawn = Cast<APawn>(Target);
		if (Pawn && Pawn->IsPlayerControlled()) return EOverlayTargetKind::Player;

		return EOverlayTargetKind::Other;
	}

	/** ECoverLean → signed lean: -1 left, 0 none, +1 right, +2 over-the-top. */
	int32 LeanToSigned(ECoverLean Lean)
	{
		switch (Lean)
		{
		case ECoverLean::Left:  return -1;
		case ECoverLean::Right: return 1;
		case ECoverLean::Front: return 2;
		default:                return 0;
		}
	}
}

// The cvars themselves are registered once at module load. Every READ resolves by name with a
// null guard (see GetOverlayLevel) — a cached IConsoleVariable* can be left dangling by a Live
// Coding patch, which is what crashed ACompanionRoute::BeginPlay on 2026-06-26.
static FAutoConsoleVariable CVarAIOverlay(
	TEXT("ai.Overlay"), 0,
	TEXT("AI overlay: 0 = off, 1 = enemy cards + squad banner, 2 = also the cover layer."),
	ECVF_Default);

static FAutoConsoleVariable CVarAIOverlayScale(
	TEXT("ai.Overlay.Scale"), 0.7f,
	TEXT("Uniform scale applied to the AI overlay widgets. The card WBP is authored large enough "
	     "to read in screenshots; 0.7 is the in-game size, on top of per-card distance shrink."),
	ECVF_Default);

static FAutoConsoleVariable CVarAIOverlaySquadEventMaxAge(
	TEXT("ai.Overlay.SquadEventMaxAge"), 3.f,
	TEXT("Seconds after which a queued squad banner event is dropped unplayed. The banner holds "
	     "each event for seconds, so a full queue would otherwise show an officer death ten "
	     "seconds after it happened."),
	ECVF_Default);

// ---------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------

void UAIOverlaySubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	LastSnapshotWorldTime = InWorld.GetTimeSeconds();

	InWorld.GetTimerManager().SetTimer(SnapshotTimerHandle, this,
		&UAIOverlaySubsystem::TickSnapshots, SnapshotInterval, /*bLoop=*/true);
}

void UAIOverlaySubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
		World->GetTimerManager().ClearTimer(SnapshotTimerHandle);
	SnapshotTimerHandle.Invalidate();

	for (const TPair<TWeakObjectPtr<UEnemySquad>, FDelegateHandle>& Pair : SquadBindings)
	{
		UEnemySquad* Squad = Pair.Key.Get();
		if (IsValid(Squad)) Squad->OnSquadOverlayEvent.Remove(Pair.Value);
	}
	SquadBindings.Empty();

	Entries.Empty();
	Snapshots.Empty();
	SquadEventQueue.Empty();

	Super::Deinitialize();
}

UAIOverlaySubsystem* UAIOverlaySubsystem::Get(const UObject* WorldContextObject)
{
	if (!IsValid(WorldContextObject) || !GEngine) return nullptr;

	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	return IsValid(World) ? World->GetSubsystem<UAIOverlaySubsystem>() : nullptr;
}

// ---------------------------------------------------------------------
// Registry
// ---------------------------------------------------------------------

void UAIOverlaySubsystem::RegisterEnemy(AEnemyCharacter* Enemy)
{
	if (!IsValid(Enemy)) return;

	const bool bAlreadyTracked = Entries.ContainsByPredicate(
		[Enemy](const FOverlayEnemyEntry& Entry) { return Entry.Enemy.Get() == Enemy; });
	if (bAlreadyTracked) return;

	FOverlayEnemyEntry& Entry = Entries.AddDefaulted_GetRef();
	Entry.Enemy = Enemy;
}

void UAIOverlaySubsystem::UnregisterEnemy(AEnemyCharacter* Enemy)
{
	// Raw null test rather than IsValid: HandleDeath and EndPlay can both run while the actor is
	// already pending-kill, and that is precisely the case whose card must come down. Matching on
	// index+serial rather than Get() is what makes that true — Get() returns null for a garbage
	// object, so a pointer compare would silently no-op in exactly the case that matters.
	if (Enemy == nullptr) return;

	const TWeakObjectPtr<AEnemyCharacter> Target(Enemy);

	Entries.RemoveAll([&Target](const FOverlayEnemyEntry& Entry)
		{ return Entry.Enemy.HasSameIndexAndSerialNumber(Target); });
	Snapshots.RemoveAll([&Target](const FEnemyOverlaySnapshot& Snap)
		{ return Snap.Enemy.HasSameIndexAndSerialNumber(Target); });
}

void UAIOverlaySubsystem::PruneRegistry()
{
	Entries.RemoveAll([](const FOverlayEnemyEntry& Entry)
	{
		const AEnemyCharacter* Enemy = Entry.Enemy.Get();
		if (!IsValid(Enemy)) return true;

		const UHealthComponent* Health = Enemy->GetHealthComponent();
		return IsValid(Health) && Health->IsDead();
	});
}

// ---------------------------------------------------------------------
// Overlay state
// ---------------------------------------------------------------------

int32 UAIOverlaySubsystem::GetOverlayLevel() const
{
	IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(OverlayLevelCVarName);
	if (!CVar) return 0;
	return CVar->GetInt();
}

bool UAIOverlaySubsystem::IsOverlayEnabled() const
{
	return GetOverlayLevel() > 0;
}

float UAIOverlaySubsystem::GetOverlayScale() const
{
	IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(OverlayScaleCVarName);
	if (!CVar) return 1.f;
	return CVar->GetFloat();
}

float UAIOverlaySubsystem::GetMaxSquadEventAgeSeconds() const
{
	IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(SquadEventMaxAgeCVarName);
	if (!CVar) return DefaultMaxSquadEventAgeSeconds;
	return CVar->GetFloat();
}

// ---------------------------------------------------------------------
// Squad banner queue
// ---------------------------------------------------------------------

void UAIOverlaySubsystem::RefreshSquadBindings()
{
	for (auto It = SquadBindings.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid()) It.RemoveCurrent();
	}

	for (const FOverlayEnemyEntry& Entry : Entries)
	{
		AEnemyCharacter* Enemy = Entry.Enemy.Get();
		if (!IsValid(Enemy)) continue;

		UEnemySquad* Squad = Enemy->GetSquad();
		if (!IsValid(Squad) || SquadBindings.Contains(Squad)) continue;

		SquadBindings.Add(Squad,
			Squad->OnSquadOverlayEvent.AddUObject(this, &UAIOverlaySubsystem::HandleSquadOverlayEvent));
	}
}

void UAIOverlaySubsystem::HandleSquadOverlayEvent(const FSquadOverlayEvent& Event)
{
	if (!IsOverlayEnabled()) return;

	// Newest always lands; the oldest is what falls off a full banner queue.
	while (SquadEventQueue.Num() >= MaxQueuedSquadEvents) SquadEventQueue.RemoveAt(0, 1, EAllowShrinking::No);

	FSquadOverlayEvent& Queued = SquadEventQueue.Add_GetRef(Event);

	const UWorld* World = GetWorld();
	Queued.TimeStamp = IsValid(World) ? World->GetTimeSeconds() : 0.f;
}

void UAIOverlaySubsystem::PruneStaleSquadEvents()
{
	const UWorld* World = GetWorld();
	if (!IsValid(World)) return;

	const float WorldTime = World->GetTimeSeconds();
	const float MaxAge = GetMaxSquadEventAgeSeconds();

	// Front of the queue is the oldest, so this stops at the first event still worth showing.
	while (SquadEventQueue.Num() > 0 && (WorldTime - SquadEventQueue[0].TimeStamp) > MaxAge)
		SquadEventQueue.RemoveAt(0, 1, EAllowShrinking::No);
}

bool UAIOverlaySubsystem::DequeueSquadEvent(FSquadOverlayEvent& OutEvent)
{
	// The banner only drains while idle, so a backed-up queue would replay an officer death long
	// after the officer fell. Late is worse than never here — drop rather than show it stale.
	PruneStaleSquadEvents();

	if (SquadEventQueue.Num() == 0) return false;

	OutEvent = SquadEventQueue[0];
	SquadEventQueue.RemoveAt(0, 1, EAllowShrinking::No);
	return true;
}

// ---------------------------------------------------------------------
// Snapshot pass
// ---------------------------------------------------------------------

void UAIOverlaySubsystem::TickSnapshots()
{
	UWorld* World = GetWorld();
	if (!IsValid(World)) return;

	const float WorldTime = World->GetTimeSeconds();
	const float DeltaTime = FMath::Max(WorldTime - LastSnapshotWorldTime, KINDA_SMALL_NUMBER);
	LastSnapshotWorldTime = WorldTime;

	PruneRegistry();

	if (!IsOverlayEnabled())
	{
		Snapshots.Reset();
		SquadEventQueue.Reset();
		return;
	}

	RefreshSquadBindings();

	// Keep NumPendingSquadEvents/PeekSquadEvents honest for a banner that gates on them.
	PruneStaleSquadEvents();

	Snapshots.Reset();
	Snapshots.Reserve(Entries.Num());

	for (FOverlayEnemyEntry& Entry : Entries)
	{
		AEnemyCharacter* Enemy = Entry.Enemy.Get();
		if (!IsValid(Enemy)) continue;

		FEnemyOverlaySnapshot& Snap = Snapshots.AddDefaulted_GetRef();
		BuildSnapshot(Entry, Enemy, WorldTime, DeltaTime, Snap);
	}
}

void UAIOverlaySubsystem::BuildSnapshot(FOverlayEnemyEntry& Entry, AEnemyCharacter* Enemy,
	float WorldTime, float DeltaTime, FEnemyOverlaySnapshot& Out)
{
	Out.Enemy = Enemy;
	Out.WorldAnchor = ResolveHeadAnchor(Enemy);

	// --- Archetype identity ---

	const UEnemyArchetypeData* DA = Enemy->GetArchetypeData();
	if (Entry.ArchetypeLabel.IsEmpty() && IsValid(DA))
	{
		// DA_Enemy_Grunt ships with an empty DisplayName and grunts are most of the roster —
		// without the enum fallback most cards render blank.
		Entry.ArchetypeLabel = DA->DisplayName.IsEmpty()
			? UEnum::GetDisplayValueAsText(DA->Archetype)
			: DA->DisplayName;
	}
	Out.ArchetypeLabel = Entry.ArchetypeLabel;
	Out.bIsOfficer = IsValid(DA) && DA->Archetype == EEnemyArchetype::Officer;
	Out.bFearless  = IsValid(DA) && DA->bFearless;

	// A force-engaged enemy reads Combat with no visible cause — the overlay must be able to say so.
	Out.bForcedEngage = Enemy->bDebugStandAndShoot || Enemy->bDebugAutoEngagePlayer;

	// --- Awareness ---

	AEnemyAIController* AIC = Cast<AEnemyAIController>(Enemy->GetController());
	const UEnemyAwarenessComponent* Awareness = IsValid(AIC) ? AIC->GetAwarenessComponent() : nullptr;
	if (IsValid(Awareness))
	{
		Out.AwarenessState  = static_cast<uint8>(Awareness->GetAwarenessState());
		Out.SuspicionFill01 = Awareness->GetAwarenessMeter01();
		Out.TargetKind      = ClassifyTarget(Awareness->GetCombatTarget());
	}

	// Differentiated here, not AI-side: the meter itself carries no rate. EMA-smoothed so a
	// 10Hz difference reads as a steady arrow rather than a jitter.
	if (Entry.bHasSuspicionSample)
	{
		const float RawRate = (Out.SuspicionFill01 - Entry.LastSuspicion01) / DeltaTime;
		Entry.SmoothedSuspicionRate = FMath::Lerp(Entry.SmoothedSuspicionRate, RawRate, SuspicionRateEMAAlpha);
	}
	else
	{
		Entry.bHasSuspicionSample = true;
	}
	Entry.LastSuspicion01 = Out.SuspicionFill01;
	Out.SuspicionRate = Entry.SmoothedSuspicionRate;

	// --- Cover pose (pure state, no tick) ---

	if (const UCoverPoseComponent* Pose = Enemy->GetCoverPoseComponent())
	{
		Out.bInCover      = Pose->bInCover;
		Out.bPeeking      = Pose->bPeeking;
		Out.bBlindFiring  = Pose->bBlindFiring;
		Out.bCoverMoving  = Pose->bCoverMoving;
		Out.CoverHeight   = static_cast<uint8>(Pose->CoverHeight);
		Out.LeanDirection = LeanToSigned(Pose->LeanDirection);
	}

	// --- Cover slot (AICS CoverTarget key + BB_HasCover) ---

	if (UBlackboardComponent* BB = IsValid(AIC) ? AIC->GetBlackboardComponent() : nullptr)
	{
		const FBlackboard::FKey KeyID = BB->GetKeyID(CoverTargetKeyName);
		if (KeyID != FBlackboard::InvalidKey && BB->GetValueAsBool(AEnemyAIController::BB_HasCover))
		{
			const FCover Cover = BB->GetValue<UBlackboardKeyType_Cover>(KeyID);
			Out.bHasCoverSlot = Cover.IsValid();
			if (Out.bHasCoverSlot) Out.CoverSlotLocation = Cover.Data.Location;
		}
	}

	// --- Morale (fearless archetypes suppress it entirely) ---

	const UEnemyMoraleComponent* Morale = Enemy->GetMoraleComponent();
	if (!Out.bFearless && IsValid(Morale))
	{
		Out.MoraleState = static_cast<uint8>(Morale->GetMoraleState());
		Out.Morale01    = Morale->GetMorale01();
	}

	// --- Suppression ---

	if (const USuppressionComponent* Suppression = Enemy->GetSuppressionComponent())
	{
		Out.Suppression01 = Suppression->GetSuppression01();
		Out.bSuppressed   = Suppression->IsSuppressed();
	}

	// --- Action label ---

	Out.ActionLabel = Entry.Narrator.Update(Enemy, WorldTime);
}
