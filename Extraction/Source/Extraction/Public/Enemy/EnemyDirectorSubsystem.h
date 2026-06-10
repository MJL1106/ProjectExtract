// UEnemyDirectorSubsystem — level-wide alert state (Calm → Searching → Loud). Grows into the full tension director in Phase 6.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "EnemyTypes.h"
#include "EnemyDirectorSubsystem.generated.h"

class AEnemyCharacter;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnGlobalAlertChanged, EGlobalAlertLevel, OldLevel, EGlobalAlertLevel, NewLevel);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnEnemyDied, AEnemyCharacter*, DeadEnemy, FVector, Location, bool, bWasOfficer);

UCLASS()
class EXTRACTION_API UEnemyDirectorSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "Enemy|Director")
	EGlobalAlertLevel GetAlertLevel() const { return AlertLevel; }

	/** An enemy reached Searching (unconfirmed stimulus). Raises the level to at least Searching. */
	void ReportEnemySearching();

	/** An enemy confirmed combat contact. Goes Loud — the stealth phase is over. */
	void ReportEnemyCombat();

	/** A corpse was discovered. First body → Searching; a body found while already alerted (or a second body) → Loud. */
	void ReportBodyDiscovered();

	/** Level-scripted alarm/objective trip. Goes Loud immediately. */
	UFUNCTION(BlueprintCallable, Category = "Enemy|Director")
	void TripAlarm();

	/** Keeps a dead enemy around as a discoverable body; recycles the oldest beyond the cap. */
	void RegisterCorpse(AEnemyCharacter* Corpse);

	UPROPERTY(BlueprintAssignable, Category = "Enemy|Director")
	FOnGlobalAlertChanged OnGlobalAlertChanged;

	/** Broadcast when an enemy dies and is registered as a corpse. Morale component subscribes for ally-died / officer-died events. */
	UPROPERTY(BlueprintAssignable, Category = "Enemy|Director")
	FOnEnemyDied OnEnemyDied;

private:
	/** Raises the alert level (never lowers it). Broadcasts on change. */
	void Escalate(EGlobalAlertLevel NewLevel);

	/** Persistent-corpse cap — oldest bodies are destroyed beyond this. */
	static constexpr int32 MaxCorpses = 10;

	EGlobalAlertLevel AlertLevel = EGlobalAlertLevel::Calm;
	int32 BodiesDiscovered = 0;

	TArray<TWeakObjectPtr<AEnemyCharacter>> Corpses;
};
