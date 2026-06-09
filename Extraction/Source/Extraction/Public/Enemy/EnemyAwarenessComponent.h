// UEnemyAwarenessComponent — manages the enemy awareness state ladder and writes Blackboard keys.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Perception/AIPerceptionComponent.h"
#include "EnemyTypes.h"
#include "EnemyAwarenessComponent.generated.h"

class UBlackboardComponent;
class UEnemyArchetypeData;
class UEnemyDirectorSubsystem;

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

	/** Called when the controlled pawn dies; stops the update timer and suppresses callbacks. */
	void HandlePawnDeath();

	UPROPERTY(BlueprintAssignable, Category = "Enemy|Awareness")
	FOnAwarenessStateChanged OnAwarenessStateChanged;

	UFUNCTION(BlueprintPure, Category = "Enemy|Awareness")
	EEnemyAwarenessState GetAwarenessState() const { return CurrentState; }

	/** Highest current per-target suspicion (0-100). Debug/UI use. */
	UFUNCTION(BlueprintPure, Category = "Enemy|Awareness")
	float GetHighestSuspicion() const;

private:

	/** Per-stimulus-source suspicion bookkeeping. */
	struct FSuspicionTrack
	{
		float Suspicion = 0.f;
		bool bSighted = false;
		FVector LastStimulusLocation = FVector::ZeroVector;
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

	UFUNCTION()
	void HandleGlobalAlertChanged(EGlobalAlertLevel OldLevel, EGlobalAlertLevel NewLevel);

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

	static constexpr float UpdateInterval = 0.15f;
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

	FTimerHandle UpdateTimerHandle;
};
