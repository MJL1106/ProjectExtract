// UAIOverlaySubsystem -- registry of live enemies, 10Hz snapshot producer, and squad event queue
// behind the ai.Overlay cvar. The overlay widget layer reads from here and never touches the AI.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "AIActionNarrator.h"
#include "AIOverlayTypes.h"
#include "AIOverlaySubsystem.generated.h"

class AEnemyCharacter;
class UEnemySquad;

UCLASS()
class EXTRACTION_API UAIOverlaySubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:

	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	/** Resolves the overlay subsystem from any world context. Null-safe. */
	static UAIOverlaySubsystem* Get(const UObject* WorldContextObject);

	// --- Registry ---

	/** Called from AEnemyCharacter::BeginPlay. Idempotent. */
	void RegisterEnemy(AEnemyCharacter* Enemy);

	/** Called from AEnemyCharacter::HandleDeath AND EndPlay. Drops the enemy's live snapshot in the
	 *  same call, so a card never survives a frame over a ragdoll. */
	void UnregisterEnemy(AEnemyCharacter* Enemy);

	// --- Overlay state (ai.Overlay / ai.Overlay.Scale) ---

	/** True when ai.Overlay is non-zero. */
	bool IsOverlayEnabled() const;

	/** 0 off, 1 cards + banner, 2 adds the cover layer. */
	int32 GetOverlayLevel() const;

	/** ai.Overlay.Scale — uniform scale the widget layer applies. */
	float GetOverlayScale() const;

	/** ai.Overlay.SquadEventMaxAge — a queued banner event older than this is dropped unplayed. */
	float GetMaxSquadEventAgeSeconds() const;

	// --- Snapshots (refreshed at 10Hz; empty while the overlay is off) ---

	const TArray<FEnemyOverlaySnapshot>& GetSnapshots() const { return Snapshots; }

	// --- Squad banner events (queue depth 3, oldest dropped on overflow) ---

	/** Pops the oldest queued event, discarding any that have gone stale first (see
	 *  GetMaxSquadEventAgeSeconds). Returns false when nothing fresh is queued. */
	bool DequeueSquadEvent(FSquadOverlayEvent& OutEvent);

	/** Read-only view of the queue without draining it. */
	const TArray<FSquadOverlayEvent>& PeekSquadEvents() const { return SquadEventQueue; }

	int32 NumPendingSquadEvents() const { return SquadEventQueue.Num(); }

private:

	/** Per-enemy state that must survive between snapshot passes. */
	struct FOverlayEnemyEntry
	{
		TWeakObjectPtr<AEnemyCharacter> Enemy;

		/** Owns the minimum-hold state for this enemy's action label. */
		FAIActionNarrator Narrator;

		/** Resolved once from the archetype DA (DisplayName, else the enum's display text). */
		FText ArchetypeLabel;

		float LastSuspicion01 = 0.f;
		float SmoothedSuspicionRate = 0.f;
		bool  bHasSuspicionSample = false;
	};

	void TickSnapshots();
	void BuildSnapshot(FOverlayEnemyEntry& Entry, AEnemyCharacter* Enemy, float WorldTime,
		float DeltaTime, FEnemyOverlaySnapshot& Out);

	/** Drops entries whose pawn is gone or dead — backstop for a missed HandleDeath unregister. */
	void PruneRegistry();

	/** Binds newly-seen squads, unbinds stale ones. Cheap: runs over the registry at 10Hz. */
	void RefreshSquadBindings();

	void HandleSquadOverlayEvent(const FSquadOverlayEvent& Event);

	/** Drops queued events older than GetMaxSquadEventAgeSeconds from the front of the queue. */
	void PruneStaleSquadEvents();

	TArray<FOverlayEnemyEntry> Entries;

	TArray<FEnemyOverlaySnapshot> Snapshots;

	TArray<FSquadOverlayEvent> SquadEventQueue;

	/** Weak keys so a torn-down squad cannot keep the subsystem holding a stale delegate. */
	TMap<TWeakObjectPtr<UEnemySquad>, FDelegateHandle> SquadBindings;

	FTimerHandle SnapshotTimerHandle;

	float LastSnapshotWorldTime = 0.f;

	/** 10Hz — the whole point is that this is not a per-frame pass. */
	static constexpr float SnapshotInterval = 0.1f;

	/** EMA weight on the newest differentiated suspicion sample. */
	static constexpr float SuspicionRateEMAAlpha = 0.35f;

	/** Banner queue depth. */
	static constexpr int32 MaxQueuedSquadEvents = 3;

	/** Fallback used only when the ai.Overlay.SquadEventMaxAge cvar cannot be resolved. */
	static constexpr float DefaultMaxSquadEventAgeSeconds = 3.f;
};
