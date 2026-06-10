// USquadAuraComponent — officer passive aura that reduces aim spread for nearby enemy allies.
// Scans once per second (staggered); tracked with a TSet so entering/leaving is clean.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SquadAuraComponent.generated.h"

class UEnemyArchetypeData;
class AEnemyCharacter;

UCLASS(ClassGroup = "Enemy", meta = (BlueprintSpawnableComponent))
class EXTRACTION_API USquadAuraComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USquadAuraComponent();

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Initialise radius and spread multiplier from the archetype data asset. */
	void InitFromArchetype(const UEnemyArchetypeData* Data);

	/**
	 * Removes the spread buff from all buffed members and stops the scan timer.
	 * Called by HandleDeath so a persisted officer corpse stops buffing the squad.
	 */
	void DeactivateAura();

private:
	float AuraRadius = 1500.f;
	float AuraSpreadMultiplier = 0.75f;

	/** Members currently inside the aura radius with the spread buff applied. */
	TSet<TWeakObjectPtr<AEnemyCharacter>> BuffedMembers;

	FTimerHandle ScanTimerHandle;

	void Scan();
	void RemoveBuffFromAll();
};
