// Per-archetype tuning data asset — all gameplay numbers that vary between archetypes live here.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "EnemyTypes.h"
#include "EnemyArchetypeData.generated.h"

class AWeaponBase;
class UBehaviorTree;
class UBarkSetData;

UCLASS(BlueprintType)
class EXTRACTION_API UEnemyArchetypeData : public UDataAsset
{
	GENERATED_BODY()

public:

	// --- Identity ---

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Identity")
	EEnemyArchetype Archetype = EEnemyArchetype::Grunt;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Identity")
	FText DisplayName;

	// --- Stats ---

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Stats", meta = (ClampMin = "1.0"))
	float MaxHealth = 100.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Stats", meta = (ClampMin = "0.0"))
	float MaxShield = 0.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Stats", meta = (ClampMin = "1.0"))
	float PatrolSpeed = 200.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Stats", meta = (ClampMin = "1.0"))
	float CombatSpeed = 400.f;

	// --- Perception ---

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Perception", meta = (ClampMin = "100.0"))
	float SightRadius = 2500.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Perception", meta = (ClampMin = "100.0"))
	float LoseSightRadius = 3000.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Perception", meta = (ClampMin = "10.0", ClampMax = "180.0"))
	float PeripheralVisionDeg = 110.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Perception", meta = (ClampMin = "0.1"))
	float SightMaxAge = 5.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Perception", meta = (ClampMin = "100.0"))
	float HearingRange = 2000.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Perception", meta = (ClampMin = "0.1"))
	float HearingMaxAge = 3.f;

	// --- Combat ---

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Combat", meta = (ClampMin = "0.0"))
	float EngageRangeMin = 600.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Combat", meta = (ClampMin = "100.0"))
	float EngageRangeMax = 1800.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Combat", meta = (ClampMin = "0.05"))
	float BurstDurationMin = 0.4f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Combat", meta = (ClampMin = "0.05"))
	float BurstDurationMax = 1.2f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Combat", meta = (ClampMin = "0.05"))
	float BurstPauseMin = 0.8f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Combat", meta = (ClampMin = "0.05"))
	float BurstPauseMax = 2.0f;

	/** Seconds between acquiring a target and opening fire. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Combat", meta = (ClampMin = "0.0"))
	float ReactionDelay = 0.5f;

	/** Aim spread (degrees) immediately after acquiring a target. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Combat", meta = (ClampMin = "0.0"))
	float SpreadStartDeg = 7.f;

	/** Aim spread (degrees) after settling. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Combat", meta = (ClampMin = "0.0"))
	float SpreadSettledDeg = 1.5f;

	/** Time (seconds) to lerp from SpreadStartDeg to SpreadSettledDeg. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Combat", meta = (ClampMin = "0.1"))
	float SpreadSettleTime = 2.f;

	/** Extra spread (degrees) added when the target's speed exceeds MovingTargetSpeedThreshold. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Combat", meta = (ClampMin = "0.0"))
	float SpreadWidenMovingTarget = 3.f;

	/** Target speed threshold (cm/s) above which SpreadWidenMovingTarget is added. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Combat", meta = (ClampMin = "0.0"))
	float MovingTargetSpeedThreshold = 300.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Combat", meta = (ClampMin = "100.0"))
	float CoverSearchRadius = 1200.f;

	/** How long the enemy will search before returning to Unaware. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Combat", meta = (ClampMin = "1.0"))
	float SearchDuration = 8.f;

	/** Seconds of no LOS before transitioning from Combat to Searching. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Combat", meta = (ClampMin = "0.0"))
	float LostContactGrace = 4.f;

	// --- Suspicion (awareness ladder fill/decay; suspicion runs 0-100, Combat at 100) ---

	/** Suspicion gained per second from a sighted target with all modifiers at 1 (close, centred, walking, standing). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Suspicion", meta = (ClampMin = "1.0"))
	float SuspicionFillRate = 40.f;

	/** Suspicion lost per second while a stimulus is absent. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Suspicion", meta = (ClampMin = "0.1"))
	float SuspicionDecayRate = 15.f;

	/** Suspicion at which the enemy turns to face the stimulus. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Suspicion", meta = (ClampMin = "1.0", ClampMax = "99.0"))
	float SuspiciousThreshold = 30.f;

	/** Suspicion at which the enemy moves to investigate. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Suspicion", meta = (ClampMin = "1.0", ClampMax = "99.0"))
	float SearchingThreshold = 65.f;

	/** Inside this range a clearly-sighted hostile is confirmed instantly regardless of meter. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Suspicion", meta = (ClampMin = "0.0"))
	float AutoCombatRange = 350.f;

	/** Suspicion added per unit of noise-event loudness. Noise alone never confirms Combat. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Suspicion", meta = (ClampMin = "0.0"))
	float NoiseSuspicionGain = 30.f;

	/** Fill multiplier at the edge of the view cone (1 at centre). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Suspicion", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float AngleEdgeFillFactor = 0.35f;

	/** Fill multiplier for a near-stationary target. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Suspicion", meta = (ClampMin = "0.05"))
	float StillFillFactor = 0.5f;

	/** Fill multiplier for a sprinting target. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Suspicion", meta = (ClampMin = "0.1"))
	float SprintFillFactor = 1.6f;

	/** Target speed (cm/s) treated as sprinting for fill purposes. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Suspicion", meta = (ClampMin = "50.0"))
	float SprintSpeedThreshold = 500.f;

	/** Fill multiplier for a crouched target. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Suspicion", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float CrouchFillFactor = 0.5f;

	/** Fill multiplier for a prone target. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Suspicion", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float ProneFillFactor = 0.35f;

	// --- Takedown ---

	/** Max distance for a silent takedown. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Takedown", meta = (ClampMin = "50.0"))
	float TakedownRange = 160.f;

	/** Rear arc (degrees, centred on backward) inside which the instigator counts as "behind". */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Takedown", meta = (ClampMin = "10.0", ClampMax = "180.0"))
	float TakedownRearArcDeg = 120.f;

	// --- Barks ---

	/** Bark lines for this archetype (subtitle feed). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Barks")
	TObjectPtr<UBarkSetData> BarkSet;

	// --- Weapon ---

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Weapon")
	TSubclassOf<AWeaponBase> WeaponClass;

	// --- Behaviour Tree ---

	/** Combat subtree injected via SetDynamicSubtree at possess. BP-assigned. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|BT")
	TObjectPtr<UBehaviorTree> CombatSubtree;

	// --- Lifecycle ---

	/** Seconds after death before the actor is destroyed. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Lifecycle", meta = (ClampMin = "0.0"))
	float DestroyDelay = 3.f;
};
