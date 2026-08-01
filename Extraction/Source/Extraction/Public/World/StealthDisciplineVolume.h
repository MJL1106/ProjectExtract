// AStealthDisciplineVolume -- placeable pressure volume that monitors player sprinting
// and weapon fire inside a stealth zone. Accumulates pressure via FStealthPressureAccumulator;
// warns on threshold, escalates via the Director punishment pipeline on full breach.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "World/StealthDisciplineTypes.h"
#include "Enemy/EnemyTypes.h"
#include "StealthDisciplineVolume.generated.h"

class UBoxComponent;
class UDirectorConfigData;
class AExtractionPlayer;
class UWeaponComponent;

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
	void BroadcastToast(UWorld* World, const FText& Message);

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
