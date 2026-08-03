// AStealthDisciplineVolume -- placeable pressure volume that monitors player sprinting
// and weapon fire inside a stealth zone. Accumulates pressure via FStealthPressureAccumulator;
// warns on threshold, escalates via the Director punishment pipeline on full breach.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "World/StealthDisciplineTypes.h"
#include "World/LevelObjectiveFlow.h"
#include "Enemy/EnemyTypes.h"
#include "StealthDisciplineVolume.generated.h"

class UBoxComponent;
class UDirectorConfigData;
class AExtractionPlayer;
class UWeaponComponent;
enum class EToastSeverity : uint8;

UCLASS(Blueprintable)
class EXTRACTION_API AStealthDisciplineVolume : public AActor
{
	GENERATED_BODY()

public:
	AStealthDisciplineVolume();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	// ---- Components ----

	UPROPERTY(VisibleAnywhere, Category = "StealthDiscipline")
	TObjectPtr<UBoxComponent> BoxComponent;

	// ---- Config ----

	UPROPERTY(EditAnywhere, Category = "StealthDiscipline|Settings")
	FStealthDisciplineSettings Settings;

	/** Text shown to the player on Warning (via the existing HUD toast channel). */
	UPROPERTY(EditAnywhere, Category = "StealthDiscipline|Settings")
	FText WarningText;

	/** Shown on escalation, when the alarm trips and the Director starts fielding reinforcements. */
	UPROPERTY(EditAnywhere, Category = "StealthDiscipline|Settings")
	FText EscalationText;

	/** Director config activated on escalation. Assign in the placed instance. */
	UPROPERTY(EditAnywhere, Category = "StealthDiscipline|Punishment")
	TObjectPtr<UDirectorConfigData> PunishmentConfig;

	/** Mission phase passed to ActivatePunishmentProfile on escalation. */
	UPROPERTY(EditAnywhere, Category = "StealthDiscipline|Punishment")
	EMissionPhase PunishmentPhase = EMissionPhase::Infiltration;

	// ---- Objective gating ----

	/** The volume stays inert until the objective flow has advanced strictly PAST this step.
	 *  Stops the player being punished for sprinting before the game has told them to go quiet. */
	UPROPERTY(EditAnywhere, Category = "StealthDiscipline|Gating")
	ELevelObjectiveStep ArmAfterStep = ELevelObjectiveStep::SwitchCompanionToStealth;

	/** Clear to arm the volume immediately on BeginPlay, ignoring ArmAfterStep. */
	UPROPERTY(EditAnywhere, Category = "StealthDiscipline|Gating")
	bool bGateOnObjectiveStep = true;

	// ---- Named constants ----

	static constexpr float SamplingIntervalSeconds = 0.25f;

	// ---- Runtime state (authority only) ----

	TWeakObjectPtr<AExtractionPlayer> TrackedPlayer;
	TWeakObjectPtr<UWeaponComponent> BoundWeaponComponent;
	FStealthPressureAccumulator Accumulator;
	int32 PendingNormalShots = 0;
	int32 PendingSuppressedShots = 0;
	bool bWarningSent = false;

	/** True while the tracked player is physically inside the box. When outside,
	 *  sampling continues (decay-only) but new sprint/shot pressure is suppressed. */
	bool bPlayerInsideVolume = false;

	/** Set once defend waves begin -- discipline never re-arms for the rest of the level. */
	bool bPermanentlyDisabled = false;

	/** One-way latch: pressure only accrues once the objective flow is past ArmAfterStep. Never
	 *  cleared, so a step regression or a flow reset can't disarm a volume mid-run. */
	bool bArmed = false;

	/** Resolved once at BeginPlay. Null in levels without a flow actor -- the gate fails open. */
	TWeakObjectPtr<ALevelObjectiveFlow> ObjectiveFlow;

	/** One log line per level load when the player enters an unarmed volume -- a flow stalled on a
	 *  mis-wired reference would otherwise leave this inert for the whole run with no diagnostic. */
	bool bLoggedUnarmedEntry = false;

	FTimerHandle SamplingTimerHandle;

	// ---- Overlap handlers ----

	UFUNCTION()
	void OnBeginOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
		bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void OnEndOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

	void HandleNewPlayerEntry(AExtractionPlayer* Player);

	// ---- Sampling ----

	void StartSampling();
	void StopSampling();
	void SampleTick();
	void SampleTickOutside();
	void SampleTickInside(AExtractionPlayer* Player);
	void HandlePressureTransition(EStealthPressureTransition Result);
	void BroadcastToast(UWorld* World, const FText& Message, EToastSeverity Severity);

	// ---- Objective gate ----

	/** Polled from SampleTick rather than driven by a step-changed delegate -- polling also picks up
	 *  checkpoint fast-forwards, and needs no subscription or teardown. */
	void UpdateArmedState();

	// ---- Shot relay binding ----

	void BindShotRelay(UWeaponComponent* WeaponComp);
	void UnbindShotRelay();

	UFUNCTION()
	void OnPlayerShotFired(bool bStealthExempt);

	// ---- Wave gate ----

	UFUNCTION()
	void OnWaveStarted(FName WaveId);

	void DisableForWave();

	// ---- Stealth music ----

	/** Drives Enter/ExitStealthZone on the music subsystem via a single flag so the
	 *  refcount stays balanced regardless of how many code paths touch it. */
	void SetStealthMusicActive(bool bActive);
	bool bStealthMusicActive = false;

	// ---- Cleanup ----

	void CleanupTrackedPlayer();
};
