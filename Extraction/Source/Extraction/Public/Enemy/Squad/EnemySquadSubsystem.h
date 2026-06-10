// UEnemySquadSubsystem -- world subsystem that owns UEnemySquad instances and routes death events.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "EnemySquadSubsystem.generated.h"

class AEnemyCharacter;
class UEnemySquad;

DECLARE_LOG_CATEGORY_EXTERN(LogEnemySquad, Log, All);

UCLASS()
class EXTRACTION_API UEnemySquadSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Returns existing or creates a new squad for the given id. */
	UEnemySquad* GetOrCreateSquad(FName SquadId);

	/** Returns the squad the character belongs to, or nullptr if squadless. */
	UFUNCTION(BlueprintPure, Category = "Enemy|Squad")
	UEnemySquad* GetSquadFor(const AEnemyCharacter* Enemy) const;

	/** Registers a member into a squad. NAME_None = squadless, no-op. Called from EnemyCharacter BeginPlay (slice C). */
	void RegisterMember(AEnemyCharacter* Enemy, FName SquadId);

	/** Unregisters a member from its squad. Called from EnemyCharacter EndPlay (slice C). */
	void UnregisterMember(AEnemyCharacter* Enemy);

	/** Creates a squad for a pre-formed group and returns it. Generated id. Used by the P6 director. */
	UEnemySquad* CreateSquadForGroup(const TArray<AEnemyCharacter*>& Group);

private:

	UPROPERTY()
	TMap<FName, TObjectPtr<UEnemySquad>> Squads;

	/** Reverse lookup: enemy -> squad id (fast GetSquadFor). */
	TMap<TWeakObjectPtr<AEnemyCharacter>, FName> EnemyToSquadId;

	/** Monotonic counter for director-created squad ids. */
	int32 GeneratedSquadCounter = 0;

	UFUNCTION()
	void HandleEnemyDied(AEnemyCharacter* DeadEnemy, FVector Location, bool bWasOfficer);
};
