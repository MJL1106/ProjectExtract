// UEnemyAwarenessComponent — manages the enemy awareness state ladder and writes Blackboard keys.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Perception/AIPerceptionComponent.h"
#include "EnemyTypes.h"
#include "EnemyAwarenessComponent.generated.h"

class AEnemyCharacter;
class ACompanionCharacter;
class UBlackboardComponent;
class UEnemyArchetypeData;
class UEnemyDirectorSubsystem;
class UEnemySquadSubsystem;
class UEnemySquad;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnAwarenessStateChanged, EEnemyAwarenessState, OldState, EEnemyAwarenessState, NewState);

/** A hostile the enemy currently factors into cover decisions: live position when sighted,
 *  frozen last-stimulus position while memory of it is still fresh (honest knowledge). */
struct FEnemyKnownThreat
{
	AActor* Actor = nullptr;
	FVector Location = FVector::ZeroVector;
	bool bSighted = false;
};

UCLASS(ClassGroup = (Enemy), meta = (BlueprintSpawnableComponent))
class EXTRACTION_API UEnemyAwarenessComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UEnemyAwarenessComponent();

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Called by the controller after possession to wire up perception and start the timer. */
	void Initialize(UBlackboardComponent* InBB, const UEnemyArchetypeData* InDA);

	/** Handles UAIPerceptionComponent::OnTargetPerceptionUpdated (sight + hearing). */
	UFUNCTION()
	void OnTargetPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus);

	/** Called by AEnemyCharacter::TakeDamage to force Combat state toward the instigator's pawn. */
	void NotifyDamaged(AController* Instigator);

	/** Debug: force-set Combat state with the given actor as target, bypassing perception, suspicion,
	 *  barks, and squad broadcast. Re-assert every awareness tick to prevent decay. */
	void DebugForceEngage(AActor* Target);

	/** Force full Combat entry toward Target through the normal EnterCombat path (barks, BB writes,
	 *  Director tension report). No confirmed visual — LOS establishes via the enemy's own perception.
	 *  Used by UEnemySquad::ForceEngage to seed Director wave squads into the fight on spawn. */
	void ForceEngage(AActor* Target);

	/** Called by AWeaponBase::ReportNearMisses when a near-miss bullet passes close to this enemy.
	 *  ShotOrigin is the bullet trace start (eye/muzzle of the shooter) — sent as the investigate
	 *  point when LOS is blocked so the enemy advances toward the shot corner, not through walls.
	 *  LOS-gated: if eye→ShotOrigin is clear, enters Combat immediately; else transitions to Searching.
	 *  Rate-limited per instigator (~0.4s via FSuspicionTrack::LastShotAtTime). */
	void NotifyShotAt(AActor* InstigatorPawn, const FVector& ShotOrigin);

	/** Called when the controlled pawn dies; stops the update timer and suppresses callbacks. */
	void HandlePawnDeath();

	/** Squad sighting ingress — called by UEnemySquad::ReportSighting on every other living member.
	 *  If below Searching, transitions to Searching at the reported location (reuses hearing/investigate path).
	 *  Never forces Combat — members confirm Combat through their own perception.
	 *  If already in Combat with the same target, refreshes LastKnownLocation. */
	void ReportSquadSighting(AActor* Target, const FVector& LastKnown);

	UPROPERTY(BlueprintAssignable, Category = "Enemy|Awareness")
	FOnAwarenessStateChanged OnAwarenessStateChanged;

	UFUNCTION(BlueprintPure, Category = "Enemy|Awareness")
	EEnemyAwarenessState GetAwarenessState() const { return CurrentState; }

	/** World seconds of the most recent transition INTO Combat. Per-enemy decaying event stamp —
	 *  covers isolated-encounter enemies that never report to the director. */
	float GetLastCombatEnterTime() const { return LastCombatEnterTime; }

	/** Highest current per-target suspicion (0-100). Debug/UI use. */
	UFUNCTION(BlueprintPure, Category = "Enemy|Awareness")
	float GetHighestSuspicion() const;

	/** Normalised 0-1 fill value for the awareness meter widget.
	 *  Returns 1 in Combat (suspicion resets to 0 on combat entry, so raw suspicion is useless there).
	 *  Otherwise clamps GetHighestSuspicion() / SuspicionMax. */
	UFUNCTION(BlueprintPure, Category = "Enemy|Awareness")
	float GetAwarenessMeter01() const;

	/** True if any hostile actor currently has a sighted suspicion track (state-independent perception check). */
	UFUNCTION(BlueprintPure, Category = "Enemy|Awareness")
	bool IsAnyHostileSighted() const;

	/** Current combat target, or nullptr outside Combat. */
	UFUNCTION(BlueprintPure, Category = "Enemy|Awareness")
	AActor* GetCombatTarget() const { return CombatTarget.Get(); }

	/** Last position the combat target was confirmed at — live while sighted, frozen once LOS is lost. */
	UFUNCTION(BlueprintPure, Category = "Enemy|Awareness")
	FVector GetLastKnownLocation() const { return LastKnownLocation; }

	/** True while the enemy currently has line of sight to its combat target. */
	UFUNCTION(BlueprintPure, Category = "Enemy|Awareness")
	bool HasLOSToTarget() const { return bHadLOS; }

	/** Seconds since the given pawn last damaged this enemy, or a large value if it never has (recently). */
	UFUNCTION(BlueprintPure, Category = "Enemy|Awareness")
	float GetTimeSinceDamagedBy(const AActor* Pawn) const;

	/** Living hostiles beyond ExcludeTarget this enemy either sees now (live location) or has
	 *  perceived within MemorySeconds (frozen last-stimulus location — honest knowledge), closest
	 *  first, capped at MaxCount. MemorySeconds <= 0 = currently-sighted only. Cloaked companions
	 *  are excluded (their track is stale by definition). */
	void GetExtraKnownThreats(const AActor* ExcludeTarget, int32 MaxCount, float MemorySeconds,
		TArray<FEnemyKnownThreat>& OutThreats) const;

	/** The local player's pawn if it is currently DBNO, else nullptr. Shared by the posture
	 *  standoff override and the search-converge clamp. */
	static APawn* FindDownedPlayerPawn(const UObject* WorldContext);

private:

	/** Per-stimulus-source suspicion bookkeeping. */
	struct FSuspicionTrack
	{
		float Suspicion = 0.f;
		bool bSighted = false;
		FVector LastStimulusLocation = FVector::ZeroVector;
		/** World time LastStimulusLocation was last written (multi-threat memory window). */
		float LastStimulusTime = -1e9f;
		/** World time of last NotifyShotAt processing for this instigator (rate-limit). */
		float LastShotAtTime = -1e9f;
	};

	/** Writes Track's stimulus location + timestamp together — keep every write on this path. */
	void StampTrack(FSuspicionTrack& Track, const FVector& Location) const;

	/** Acoustic-occlusion cache entry: one computed multiplier per quantized stimulus cell. */
	struct FAcousticCacheEntry
	{
		FIntVector Cell = FIntVector::ZeroValue;
		float Multiplier = 1.f;
		float ExpiryTime = -1.f;
	};

	/** Occlusion-aware audibility (0..1) of a noise at StimLoc for the owning pawn — 1 through a
	 *  clear line or open door, muffled through a closed openable door, 0 through walls/floors/
	 *  locked doors (see AIAcoustics). Cached per AcousticCellSize stimulus cell for
	 *  AcousticCacheTTL so automatic fire doesn't re-trace every shot. */
	float GetCachedAcousticMultiplier(const FVector& StimLoc, const AActor* Instigator);

	void SetState(EEnemyAwarenessState NewState);
	void SetCombatTarget(AActor* NewTarget);
	void UpdateAwareness();
	void UpdateCombat();
	void UpdateSuspicion();
	void ApplySuspicionState(float MaxSuspicion, const FVector& StimulusLocation);
	void EnterCombat(AActor* Target, bool bConfirmedVisual);
	void TransitionToSearching(bool bContactLost);
	void SetInvestigateLocation(const FVector& Location);
	void WriteBBVectors();

	/** Suspicion gained per second for a sighted target — distance, view angle, speed, and stance modifiers. */
	float ComputeSightFillRate(const APawn* MyPawn, const AActor* Target) const;

	void HandleSightStimulus(AActor* Actor, const FAIStimulus& Stimulus);
	void HandleHearingStimulus(AActor* Actor, const FAIStimulus& Stimulus);
	bool TryApplyBreachSearchRoomStartle(AActor* Actor, const FAIStimulus& Stimulus,
		float NormalGain, FSuspicionTrack& Track);
	/** Ally coordination: a mate's heard gunfire raises suspicion toward what he is shooting at. */
	void HandleAllyGunfireHeard(class AEnemyCharacter* Shooter, const FAIStimulus& Stimulus);
	void HandleBodySighted(class AEnemyCharacter* Body);
	void Bark(EBarkType Type) const;

	/** Threat-scored target selection (design §10). Runs on the existing timer cadence during Combat.
	 *  Returns the highest-scoring perceived hostile, or nullptr if none qualify. */
	AActor* ScoreAndSelectTarget() const;

	bool IsCompanionActor(const AActor* Actor) const;
	bool ShouldIgnoreCompanionStimulus(const AActor* Actor) const;
	bool CanSelectCompanionTarget(const AActor* Candidate, const FSuspicionTrack& Track, float WorldTime) const;

	/** Mode-dependent companion sight cloak.
	 *  Combat mode: never cloaked (guns blazing — fully perceivable).
	 *  Stealth (unbroken): always cloaked, even to in-Combat enemies.
	 *  Normal (and broken stealth): cloaked until the fight is on — this enemy in Combat, or the
	 *  global alert has gone Loud (player spotted / firing). Isolated encounters ignore the alert. */
	bool IsCompanionSightCloaked(const AActor* Actor) const;

	/** Companion gunfire audibility. Stealth: audible unless the weapon is suppressed (the
	 *  suppressor buys the silence). Normal: silent while sight-cloaked. Combat: always audible. */
	bool IsCompanionFireInaudible(const AActor* Actor) const;

	/** Seeds/refreshes bSighted tracks for currently-perceived, currently-uncloaked companions.
	 *  Called on cloak-lift transitions (combat entry, global alert Loud) — sight events swallowed
	 *  while cloaked left no track, and perception only fires on edges. */
	void SeedCompanionSightTracks();

	/** Player-DBNO combat handoff fallback: the live, hostile, uncloaked companion within
	 *  SightRadius, with a suspicion track stamped at its current location — or nullptr. Only
	 *  called at the moment a DBNO player drops out of Combat and normal selection found nothing,
	 *  so the position grant reads as mid-fight squad awareness, not a wallhack. */
	AActor* FindDBNOHandoffCompanion();

	/** Companion-DBNO combat handoff fallback — mirror of FindDBNOHandoffCompanion: the live
	 *  player within SightRadius, with a suspicion track stamped at their current location — or
	 *  nullptr. Only called at the moment a DBNO companion drops out of Combat and normal
	 *  selection found nothing. */
	AActor* FindDBNOHandoffPlayer();

	void RefreshSearchRoomExposure();
	void ApplySilentSearchRoomStartle(ACompanionCharacter* Companion, FSuspicionTrack& Track);

	/** Egress: report our current combat target + last-known to the squad (rate-limited by the squad). */
	void BroadcastSightingToSquad();

	UFUNCTION()
	void HandleGlobalAlertChanged(EGlobalAlertLevel OldLevel, EGlobalAlertLevel NewLevel);

	/** True when the controlled pawn has bIsolatedEncounter set (sight-only, no global alert). */
	bool IsOwnerIsolatedEncounter() const;

	/** True when the controlled pawn is inside a takedown volume — awareness is "muffled": gunfire,
	 *  walking and reload noise is dropped so taking one enemy down doesn't cascade to its pocket
	 *  neighbours. A sprint footstep and the global Loud alert still wake it (both bypass the muffle). */
	bool IsOwnerTakedownMuffled() const;

	bool IsHostile(AActor* Actor) const;
	static bool IsActorAlive(const AActor* Actor);

	UPROPERTY()
	TWeakObjectPtr<UBlackboardComponent> BlackboardComp;

	UPROPERTY()
	TObjectPtr<const UEnemyArchetypeData> ArchetypeData;

	TWeakObjectPtr<UEnemyDirectorSubsystem> Director;

	EEnemyAwarenessState CurrentState = EEnemyAwarenessState::Unaware;
	float LastCombatEnterTime = -1e9f;

	TWeakObjectPtr<AActor> CombatTarget;
	FVector LastKnownLocation = FVector::ZeroVector;

	TMap<TWeakObjectPtr<AActor>, FSuspicionTrack> SuspicionTracks;

	TWeakObjectPtr<ACompanionCharacter> CachedPerceivedCompanion;
	uint32 LastSeededSearchRoomExposureGeneration = 0;
	uint32 LastStartledSearchRoomExposureGeneration = 0;

	/** Bodies this enemy has already reacted to (one search trigger per body per enemy). */
	TSet<TWeakObjectPtr<const AActor>> DiscoveredBodies;

	/** The corpse this enemy is currently investigating (set in HandleBodySighted, cleared on timeout/combat/removal). */
	TWeakObjectPtr<AEnemyCharacter> CurrentInvestigateBody;

	/** Distance (cm) within which the investigating enemy triggers corpse removal. */
	UPROPERTY(EditDefaultsOnly, Category = "Enemy|Awareness")
	float CorpseReachRadius = 150.f;

	/** Clears the investigate-body reference and the target flag on the corpse. */
	void ClearInvestigateBody();

	TArray<FAcousticCacheEntry> AcousticCache;

	static constexpr float AcousticCacheTTL = 0.35f;
	static constexpr float AcousticCellSize = 128.f;

	static constexpr float UpdateInterval = 0.15f;
	/** Minimum seconds between NotifyShotAt processing for the same instigator (below Combat). */
	static constexpr float ShotAtRateLimit = 0.4f;
	static constexpr float SuspicionMax = 100.f;
	/** Noise alone caps just below confirmation — hearing never instantly reveals (design §4). */
	static constexpr float NoiseSuspicionCap = 99.f;
	/** Below this speed (cm/s) a target counts as still for fill purposes. */
	static constexpr float StillSpeedThreshold = 25.f;

	// Set to true by HandlePawnDeath to suppress perception/damage callbacks after death
	bool bStopped = false;

	// LOS tracking for Combat→Searching transition
	bool bHadLOS = false;
	float TimeSinceLOSLost = 0.f;

	// Searching timeout
	float TimeSpentSearching = 0.f;

	// DBNO overwatch: search timeout holds while the downed player that was our combat target
	// stays DBNO — cleared the tick the player revives or dies
	bool bSearchHoldForDBNOPlayer = false;

	// Threat-scored targeting: track the actor that last damaged us and when
	TWeakObjectPtr<AActor> RecentDamageInstigatorPawn;
	float RecentDamageWorldTime = -1e9f;

	// Per-attacker damage timestamps for GetTimeSinceDamagedBy — the single slot above is
	// last-writer-wins, so a companion hit would otherwise erase the player's recent damage
	// and make an actively-firing player read as passive to the posture system. Bounded by
	// the hostile count (player + companion).
	TMap<TWeakObjectPtr<const AActor>, float> DamageTimesByAttacker;

	// Guard against squad sighting relay feedback loops: ReportSquadSighting must NOT re-broadcast
	bool bInSquadSightingRelay = false;

	// Debug auto-engage: suppresses Director report in SetState while true
	bool bDebugForcedCombat = false;

	// Edge detection for debug auto-engage flag-off: true while the flag was active last tick
	bool bWasDebugEngaged = false;

	// Bark hysteresis: suppress re-acquire Contact bark shortly after leaving Combat
	float LastCombatExitWorldTime = -1e9f;

	// Cached squad subsystem reference (set in Initialize)
	TWeakObjectPtr<UEnemySquadSubsystem> SquadSubsystem;

	/** Seconds within which damage from a target counts for the RecentDamage threat weight. */
	static constexpr float RecentDamageWindow = 4.f;

	FTimerHandle UpdateTimerHandle;

	// Throttle accumulator for the [SIGHTDIAG] log (enemy.SightDiag cvar)
	float SightDiagAccum = 0.f;
};
