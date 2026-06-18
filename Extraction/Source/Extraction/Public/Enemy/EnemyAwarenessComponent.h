// UEnemyAwarenessComponent — manages the enemy awareness state ladder and writes Blackboard keys.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Perception/AIPerceptionComponent.h"
#include "EnemyTypes.h"
#include "EnemyAwarenessComponent.generated.h"

class AEnemyCharacter;
class UBlackboardComponent;
class UEnemyArchetypeData;
class UEnemyDirectorSubsystem;
class UEnemySquadSubsystem;
class UEnemySquad;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnAwarenessStateChanged, EEnemyAwarenessState, OldState, EEnemyAwarenessState, NewState);

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

private:

	/** Per-stimulus-source suspicion bookkeeping. */
	struct FSuspicionTrack
	{
		float Suspicion = 0.f;
		bool bSighted = false;
		FVector LastStimulusLocation = FVector::ZeroVector;
		/** World time of last NotifyShotAt processing for this instigator (rate-limit). */
		float LastShotAtTime = -1e9f;
	};

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
	void HandleBodySighted(class AEnemyCharacter* Body);
	void Bark(EBarkType Type) const;

	/** Threat-scored target selection (design §10). Runs on the existing timer cadence during Combat.
	 *  Returns the highest-scoring perceived hostile, or nullptr if none qualify. */
	AActor* ScoreAndSelectTarget() const;

	/** Egress: report our current combat target + last-known to the squad (rate-limited by the squad). */
	void BroadcastSightingToSquad();

	UFUNCTION()
	void HandleGlobalAlertChanged(EGlobalAlertLevel OldLevel, EGlobalAlertLevel NewLevel);

	/** True when the controlled pawn has bIsolatedEncounter set (sight-only, no global alert). */
	bool IsOwnerIsolatedEncounter() const;

	bool IsHostile(AActor* Actor) const;
	static bool IsActorAlive(const AActor* Actor);

	UPROPERTY()
	TWeakObjectPtr<UBlackboardComponent> BlackboardComp;

	UPROPERTY()
	TObjectPtr<const UEnemyArchetypeData> ArchetypeData;

	TWeakObjectPtr<UEnemyDirectorSubsystem> Director;

	EEnemyAwarenessState CurrentState = EEnemyAwarenessState::Unaware;

	TWeakObjectPtr<AActor> CombatTarget;
	FVector LastKnownLocation = FVector::ZeroVector;

	TMap<TWeakObjectPtr<AActor>, FSuspicionTrack> SuspicionTracks;

	/** Bodies this enemy has already reacted to (one search trigger per body per enemy). */
	TSet<TWeakObjectPtr<const AActor>> DiscoveredBodies;

	/** The corpse this enemy is currently investigating (set in HandleBodySighted, cleared on timeout/combat/removal). */
	TWeakObjectPtr<AEnemyCharacter> CurrentInvestigateBody;

	/** Distance (cm) within which the investigating enemy triggers corpse removal. */
	UPROPERTY(EditDefaultsOnly, Category = "Enemy|Awareness")
	float CorpseReachRadius = 150.f;

	/** Clears the investigate-body reference and the target flag on the corpse. */
	void ClearInvestigateBody();

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

	// Threat-scored targeting: track the actor that last damaged us and when
	TWeakObjectPtr<AActor> RecentDamageInstigatorPawn;
	float RecentDamageWorldTime = -1e9f;

	// Guard against squad sighting relay feedback loops: ReportSquadSighting must NOT re-broadcast
	bool bInSquadSightingRelay = false;

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
