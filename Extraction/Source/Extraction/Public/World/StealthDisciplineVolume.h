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

	/** Text shown to the player on Warning / Escalation (via the existing HUD toast channel). */
	UPROPERTY(EditAnywhere, Category = "StealthDiscipline|Settings")
	FText WarningText;

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
	bool bWarningSent = false;

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

	// ---- Sampling ----

	void StartSampling();
	void StopSampling();
	void SampleTick();

	// ---- Shot relay binding ----

	void BindShotRelay(UWeaponComponent* WeaponComp);
	void UnbindShotRelay();

	UFUNCTION()
	void OnPlayerShotFired(bool bStealthExempt);

	// ---- Wave gate ----

	UFUNCTION()
	void OnWaveStarted(FName WaveId);

	void DisableForWave();

	// ---- Cleanup ----

	void CleanupTrackedPlayer();
};
