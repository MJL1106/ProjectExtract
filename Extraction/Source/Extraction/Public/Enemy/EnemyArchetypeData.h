// Per-archetype tuning data asset — all gameplay numbers that vary between archetypes live here.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "EnemyTypes.h"
#include "EnemyArchetypeData.generated.h"

class AWeaponBase;
class UBehaviorTree;
class UBarkSetData;
class AEnemyGrenadeProjectile;
class UStaticMesh;

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

	/** Distance (cm) within which the sight fill rate stays at full (1.0) before ramping down. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Perception", meta = (ClampMin = "0.0"))
	float FullFillRange = 1500.f;

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

	/** Within this range the enemy skips cover-seeking and fires in place (selector falls through to the open-ground fire branch). 0 = always seek cover. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Combat", meta = (ClampMin = "0.0"))
	float PointBlankFireRange = 0.f;

	/** How long the enemy will search before returning to Unaware. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Combat", meta = (ClampMin = "1.0"))
	float SearchDuration = 8.f;

	/** Seconds of no LOS before transitioning from Combat to Searching. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Combat", meta = (ClampMin = "0.0"))
	float LostContactGrace = 8.f;

	/** Maximum yaw offset (degrees) between actor forward and the aim target before GetAIAimTarget/GetAIAimLocation
	 *  returns null/false — forces the weapon to fall back to forward-fire while the body rotates.
	 *  0 = unlimited (default for all non-heavy archetypes). Heavy DA sets ~60. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Combat", meta = (ClampMin = "0.0", ClampMax = "180.0"))
	float MaxAimYawDeg = 0.f;

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
	float StillFillFactor = 0.85f;

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

	/** After leaving Combat, suppress the re-acquire Contact bark for this long to avoid bark spam when peek-fighting. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Barks", meta = (ClampMin = "0.0"))
	float RecontactBarkCooldown = 8.f;

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

	// --- Patrol (guard-post scanning for route-less enemies) ---

	/** Total yaw sweep (degrees) for a route-less guard; 0 = no scanning. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Patrol", meta = (ClampMin = "0.0", ClampMax = "360.0"))
	float GuardScanYawRange = 90.f;

	/** Seconds between yaw-sweep steps for a route-less guard. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Patrol", meta = (ClampMin = "0.5"))
	float GuardScanInterval = 2.5f;

	/** Interpolation speed (deg/s scale for RInterpTo) for the guard-post yaw sweep. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Patrol", meta = (ClampMin = "1.0"))
	float GuardScanTurnSpeed = 90.f;

	// --- Armour (Phase 3 — Heavy) ---

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Armour")
	bool bHasArmour = false;

	/** Arc (degrees, centred on forward) inside which armour reduces damage. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Armour", meta = (ClampMin = "0.0", ClampMax = "360.0"))
	float ArmourFrontalArcDeg = 140.f;

	/** Damage multiplier for hits inside the frontal arc. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Armour", meta = (ClampMin = "0.0"))
	float ArmourFrontalMultiplier = 0.3f;

	/** Damage multiplier for hits outside the frontal arc (rear). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Armour", meta = (ClampMin = "0.1"))
	float ArmourRearMultiplier = 1.5f;

	/** Number of frontal armour plates before the arc protection degrades to 1.0. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Armour", meta = (ClampMin = "1"))
	int32 ArmourPlateCount = 3;

	// --- Shield (Phase 3 — Shield archetype) ---

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Shield")
	bool bHasShield = false;

	/** Hit points of the shield before it breaks. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Shield", meta = (ClampMin = "1.0"))
	float ShieldHealth = 400.f;

	/** Arc (degrees, centred on forward) inside which the shield blocks hits. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Shield", meta = (ClampMin = "0.0", ClampMax = "360.0"))
	float ShieldBlockArcDeg = 150.f;

	/** Extra aim spread (degrees) applied to the sidearm peeked around the shield. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Shield", meta = (ClampMin = "0.0"))
	float ShieldSidearmSpreadDeg = 6.f;

	/** Walk speed (cm/s) while advancing behind the shield. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Shield", meta = (ClampMin = "1.0"))
	float ShieldAdvanceSpeed = 220.f;

	/** Static mesh for the shield (soft ref — BP assigns; loaded on demand by the shield component). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Shield")
	TSoftObjectPtr<UStaticMesh> ShieldMesh;

	// --- Grenadier (Phase 3) ---

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Grenadier")
	bool bIsGrenadier = false;

	/** Max number of grenades the enemy carries. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Grenadier", meta = (ClampMin = "1"))
	int32 GrenadeSupply = 3;

	/** Minimum seconds between grenade throws. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Grenadier", meta = (ClampMin = "0.5"))
	float GrenadeCooldown = 12.f;

	/** Seconds until the grenade detonates after spawning. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Grenadier", meta = (ClampMin = "0.5"))
	float GrenadeFuseTime = 2.5f;

	/** Seconds of aim telegraph before spawning the grenade. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Grenadier", meta = (ClampMin = "0.1"))
	float GrenadeTelegraphTime = 1.0f;

	/** Minimum throw distance (cm). Targets closer than this won't be lobbed at. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Grenadier", meta = (ClampMin = "0.0"))
	float GrenadeMinRange = 500.f;

	/** Maximum throw distance (cm). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Grenadier", meta = (ClampMin = "1.0"))
	float GrenadeMaxRange = 2000.f;

	/** Damage dealt at the blast centre. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Grenadier", meta = (ClampMin = "1.0"))
	float GrenadeDamage = 80.f;

	/** Radius (cm) of the radial damage sphere. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Grenadier", meta = (ClampMin = "1.0"))
	float GrenadeDamageRadius = 350.f;

	/** Seconds the target must be LOS-blocked before the grenadier lobs. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Grenadier", meta = (ClampMin = "0.5"))
	float GrenadeLobTriggerLOSBlockedTime = 4.f;

	/** Projectile class spawned on throw. Blueprint-assigned. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Grenadier")
	TSubclassOf<AEnemyGrenadeProjectile> GrenadeProjectileClass;

	// --- Officer aura (Phase 3) ---

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Officer")
	bool bHasCommandAura = false;

	/** Radius (cm) within which nearby allies receive the spread multiplier buff. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Officer", meta = (ClampMin = "100.0"))
	float AuraRadius = 1500.f;

	/** Aim spread multiplier applied to buffed allies (< 1 = tighter spread). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Officer", meta = (ClampMin = "0.1"))
	float AuraSpreadMultiplier = 0.75f;

	// --- Melee (Phase 3 — Rusher, any archetype that can melee) ---

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Melee")
	bool bCanMelee = false;

	/** Distance (cm) within which PerformMelee can connect. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Melee", meta = (ClampMin = "1.0"))
	float MeleeRange = 180.f;

	/** Damage dealt per melee strike. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Melee", meta = (ClampMin = "1.0"))
	float MeleeDamage = 35.f;

	/** Minimum seconds between melee strikes. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Melee", meta = (ClampMin = "0.1"))
	float MeleeCooldown = 1.5f;

	// --- Sniper (Phase 3) ---

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Sniper")
	bool bIsSniper = false;

	/** Seconds the aim laser shows before the shot fires. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Sniper", meta = (ClampMin = "0.1"))
	float SniperAimTelegraphTime = 2.0f;

	/** Minimum seconds between sniper shots. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Sniper", meta = (ClampMin = "0.5"))
	float SniperShotCooldown = 4.f;

	/** Number of shots before the sniper relocates to a new perch. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Sniper", meta = (ClampMin = "1"))
	int32 SniperRelocateAfterShots = 2;

	/** If true the sniper immediately relocates when damaged. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Sniper")
	bool bSniperRelocateWhenDamaged = true;

	/** Radius (cm) for EQS perch search. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Sniper", meta = (ClampMin = "100.0"))
	float SniperPerchSearchRadius = 3000.f;

	// --- Heavy (Phase 3) ---

	/** Maximum degrees per second the character can rotate toward its desired facing.
	 *  0 = no clamp (default for all non-heavy archetypes). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Heavy", meta = (ClampMin = "0.0"))
	float TurnRateDegPerSec = 0.f;

	// --- Suppression (Phase 4) ---

	/** Divisor for incoming suppression — higher = harder to suppress (heavy ~3, sniper ~0.5). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Suppression", meta = (ClampMin = "0.1"))
	float SuppressionResistance = 1.f;

	/** Extra spread (degrees) added at full suppression (SuppressionComponent 1.0). Applied by character spread calc. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Suppression", meta = (ClampMin = "0.0"))
	float SuppressionSpreadPenaltyDeg = 4.f;

	// --- Morale (Phase 4) ---

	/** If true, morale events are ignored and the enemy stays Confident permanently (rusher, shield-up). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Morale")
	bool bFearless = false;

	/** Morale can never drop below this (0-100). Officer aura raises allies' floor in P5. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Morale", meta = (ClampMin = "0.0", ClampMax = "100.0"))
	float MoraleFloor = 0.f;

	/** Divisor for morale event deltas — higher = events hit softer (heavy high, sniper < 1 = brittle). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Morale", meta = (ClampMin = "0.1"))
	float MoraleEventResistance = 1.f;

	/** Morale at or below which the enemy becomes Shaken (0-100). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Morale", meta = (ClampMin = "1.0", ClampMax = "99.0"))
	float ShakenThreshold = 60.f;

	/** Morale at or below which the enemy becomes Broken (0-100). Must be < ShakenThreshold. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Morale", meta = (ClampMin = "1.0", ClampMax = "99.0"))
	float BrokenThreshold = 30.f;

	/** Morale points recovered per second when no loss event has occurred recently. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Morale", meta = (ClampMin = "0.0"))
	float MoraleRecoveryPerSecond = 2.f;

	// --- Morale event weights (design §7 — positive = morale loss, negative = morale gain) ---

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Morale|Events", meta = (ClampMin = "0.0"))
	float MoraleLossAllyDied = 15.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Morale|Events", meta = (ClampMin = "0.0"))
	float MoraleLossOfficerDied = 30.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Morale|Events", meta = (ClampMin = "0.0"))
	float MoraleLossSustainedSuppression = 10.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Morale|Events", meta = (ClampMin = "0.0"))
	float MoraleLossLowHealth = 20.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Morale|Events", meta = (ClampMin = "0.0"))
	float MoraleLossFlanked = 10.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Morale|Events", meta = (ClampMin = "0.0"))
	float MoraleGainDamagedTarget = 5.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Morale|Events", meta = (ClampMin = "0.0"))
	float MoraleGainTargetDowned = 15.f;

	// --- Squad Coordination (Phase 5) ---

	/** Minimum spacing (cm) between squad members when scoring cover slots. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Squad", meta = (ClampMin = "0.0"))
	float MinAllySpacing = 300.f;

	// --- Threat-Scored Targeting (Phase 5) ---

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Targeting", meta = (ClampMin = "0.0"))
	float ThreatWeightProximity = 1.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Targeting", meta = (ClampMin = "0.0"))
	float ThreatWeightLOS = 0.75f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Targeting", meta = (ClampMin = "0.0"))
	float ThreatWeightRecentDamage = 1.5f;

	/** Incumbent target must be beaten by this factor before switching. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Targeting", meta = (ClampMin = "1.0"))
	float TargetSwitchHysteresis = 1.25f;

	// --- Flanking (Phase 5) ---

	/** Minimum seconds between flank attempts (cooldown tracked in BT NodeMemory). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Squad", meta = (ClampMin = "0.0"))
	float FlankAttemptCooldown = 8.f;

	/** Health fraction below which a flanker aborts the move. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Squad", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FlankAbortHealthFraction = 0.35f;

	// --- Officer Rally (Phase 5) ---

	/** Morale points added to each living squad member on rally. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Officer|Rally", meta = (ClampMin = "0.0"))
	float RallyMoraleBoost = 30.f;

	/** Temporary morale floor raise applied on rally. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Officer|Rally", meta = (ClampMin = "0.0"))
	float RallyFloorRaise = 20.f;

	/** Seconds the rally floor raise lasts before expiring. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Officer|Rally", meta = (ClampMin = "1.0"))
	float RallyFloorDuration = 20.f;

	/** Minimum seconds between officer rally calls. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Officer|Rally", meta = (ClampMin = "0.0"))
	float RallyCooldown = 25.f;

	// --- Focus Fire (Phase 5) ---

	/** Minimum seconds between officer focus-fire calls. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Officer", meta = (ClampMin = "0.0"))
	float FocusCallCooldown = 10.f;

	// --- Bounding Overwatch (Phase 7 — officer-only) ---

	/** Minimum seconds between bounding overwatch attempts (officer-side cooldown). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Officer|Bounding", meta = (ClampMin = "0.0"))
	float BoundingCooldown = 20.f;

	/** Minimum number of living squad members (including officer) required to start a bounding maneuver. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Officer|Bounding", meta = (ClampMin = "3"))
	int32 BoundingMinSquadSize = 3;

	// --- Move And Shoot (Phase 2 — tight cover-anchored strafe-while-firing) ---

	/** Enables the tight cover-anchored strafe-while-firing combat behaviour. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|MoveAndShoot")
	bool bUsesMoveAndShoot = false;

	/** Below this range the point-blank fire-in-place branch handles combat. Must be < MoveAndShootMaxRange (and >= PointBlankFireRange). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|MoveAndShoot", meta = (ClampMin = "0.0"))
	float MoveAndShootMinRange = 700.f;

	/** Above this range seek cover or advance. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|MoveAndShoot", meta = (ClampMin = "100.0"))
	float MoveAndShootMaxRange = 1600.f;

	/** Range band hysteresis (cm) — TickTask exit gate widens by this amount on each side to prevent boundary churn. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|MoveAndShoot", meta = (ClampMin = "0.0"))
	float MoveAndShootRangeHysteresis = 150.f;

	/** Seconds without LOS before the task gives up and lets the selector re-seek cover/advance. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|MoveAndShoot", meta = (ClampMin = "0.1"))
	float MoveAndShootNoLosGiveUpTime = 2.f;

	/** Tight circle radius (cm) around the anchor for strafe picks. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|MoveAndShoot", meta = (ClampMin = "50.0"))
	float StrafeRadius = 300.f;

	/** Minimum seconds between strafe repicks. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|MoveAndShoot", meta = (ClampMin = "0.1"))
	float StrafeIntervalMin = 1.0f;

	/** Maximum seconds between strafe repicks. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|MoveAndShoot", meta = (ClampMin = "0.1"))
	float StrafeIntervalMax = 2.2f;

	/** Movement speed (cm/s) while strafe-firing (slower than CombatSpeed for a deliberate look). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|MoveAndShoot", meta = (ClampMin = "1.0"))
	float StrafeWalkSpeed = 250.f;

	/** Nav projection extent (cm) for strafe point validation. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|MoveAndShoot", meta = (ClampMin = "1.0"))
	float StrafeNavProjectExtent = 200.f;

	/** Attempts to find an LOS-valid strafe point per repick. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|MoveAndShoot", meta = (ClampMin = "1"))
	int32 StrafeLosRetryCount = 4;

	// --- Perception: Shot-At Reaction (Part A) ---

	/** When true, near-miss rounds escalate this enemy's awareness toward the shooter. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Perception",
		meta = (ToolTip = "If true, near-miss gunfire can escalate awareness to Searching (LOS-gated to Combat). Disable for deaf/heavy variants."))
	bool bReactsToBeingShotAt = true;

	/** Minimum suspicion value set on the shooter's track when a near-miss is received below Combat.
	 *  Must be >= SearchingThreshold so the enemy reliably transitions to Searching. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Perception",
		meta = (ClampMin = "65.0", ClampMax = "99.0",
		ToolTip = "Suspicion floor applied to the shooter when a near-miss is received. Set >= SearchingThreshold."))
	float ShotAtSuspicionFloor = 80.f;

	// --- Cover Flank Break (Part B) ---

	/** When true, the enemy will leave a compromised cover slot and seek a new one when flanked. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Cover",
		meta = (ToolTip = "Enable cover-flank detection: enemy relocates when the current slot no longer protects against the combat target."))
	bool bCoverFlankBreakEnabled = true;

	/** Seconds between flank-compromise evaluations while in the peek-fire loop. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Cover",
		meta = (ClampMin = "0.1", ToolTip = "How often (seconds) the flank-compromise check runs inside CombatFire."))
	float CoverCompromiseEvalInterval = 0.4f;

	/** Minimum seconds after physically arriving at a cover slot before a flank-break relocate is allowed. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Cover",
		meta = (ClampMin = "0.0", ToolTip = "Dwell time at a slot (seconds) before flank-break can trigger."))
	float CoverCompromiseMinDwell = 1.0f;

	/** Minimum seconds between successive cover relocations (prevents A→B→A ping-pong). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Cover",
		meta = (ClampMin = "0.5", ToolTip = "Per-enemy cooldown (seconds) between flank-triggered cover relocations."))
	float CoverRelocateCooldown = 3.5f;

	/** Minimum seconds between open-ground lateral repositions when fighting with no cover.
	 *  Applies in the no-cover CombatFire path and as fallback when flank-break finds no new slot. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Cover",
		meta = (ClampMin = "0.5", ToolTip = "Rate-limit (seconds) for open-ground lateral strafes between bursts."))
	float OpenGroundStrafeInterval = 2.5f;

	/** Radius (cm) of the random lateral reposition pick when fighting in the open. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Cover",
		meta = (ClampMin = "50.0", ToolTip = "Max lateral distance (cm) for an open-ground strafe reposition."))
	float OpenGroundStrafeRadius = 350.f;

	/** Extra degrees added to the slot's fire arc when testing whether the target is outside it.
	 *  A positive value makes the compromise test more lenient (arc effectively wider). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Cover",
		meta = (ClampMin = "0.0", ClampMax = "45.0",
		ToolTip = "Slack added to the slot fire arc before declaring the target outside it."))
	float CoverFlankArcSlackDeg = 15.f;

	/** Continuous seconds without eye->target LOS before an active burst is cut. Sized above the
	 *  perception service interval (0.25s) so a single stale trace does not chop the burst. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Cover", meta = (ClampMin = "0.0",
		ToolTip = "Grace period (s) LOS may be absent mid-burst before firing stops. Keep > perception interval (0.25)."))
	float FireLosLostGrace = 0.35f;

	/** Max seconds Expose waits for LOS before giving up to Recover (anti-stuck). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Cover", meta = (ClampMin = "0.1",
		ToolTip = "Cap (s) on how long Expose waits for LOS before recovering. Stops a permanently-blocked enemy hanging in Expose."))
	float ExposeLosWaitMax = 1.5f;

	/** Consecutive Expose-phase LOS timeouts before the enemy treats the slot as unworkable and
	 *  forces a flank-break relocate (same path as a compromise). Resets to 0 when fire is opened. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Cover", meta = (ClampMin = "1",
		ToolTip = "How many consecutive Expose LOS-timeouts (no shot fired) before the enemy relocates off this slot."))
	int32 MaxExposeLosTimeouts = 2;

	/** Flank-break relocate only accepts a slot whose hunkered body is geometry-shielded from the
	 *  threat. Disable to fall back to can-shoot-only selection. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Cover",
		meta = (ToolTip = "Require the relocate target slot to actually protect the body, not just shoot the threat."))
	bool bRelocateRequiresBodyProtection = true;

	/** Score bonus for a relocate candidate whose body is geometry-protected. Large vs proximity/
	 *  distance terms so protection dominates the pick. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Cover", meta = (ClampMin = "0.0",
		ToolTip = "Score weight for body-protected relocate candidates. Should exceed combined proximity+distance weights."))
	float ProtectiveCoverScoreBonus = 2.f;

	/** Retreat-strafe fallback (no protective cover) prefers points this far (cm) further from the
	 *  threat and off its current bearing, so the enemy breaks contact rather than strafing in place. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Cover", meta = (ClampMin = "0.0",
		ToolTip = "Min extra distance (cm) away from the threat a fallback strafe should add when no protective cover exists."))
	float FlankBreakRetreatBias = 250.f;

	// --- Cover Positioning & Advance Fire ---

	/** Extra depth (cm) behind the cover line the enemy targets, beyond capsule radius.
	 *  Arrival = cover-line point - ForwardDirection * (CapsuleRadius + CoverStandoffPadding). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Cover", meta = (ClampMin = "0.0",
		ToolTip = "Extra standoff padding behind the cover wall surface, in cm, added on top of capsule radius."))
	float CoverStandoffPadding = 25.f;

	/** When true the enemy fires at the combat target while advancing to cover (BTTask_EnemyMoveToCover only). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Cover",
		meta = (ToolTip = "Allow the enemy to fire while running to a cover slot. Uses separate burst timing (AdvanceFireBurstPause*)."))
	bool bFireWhileAdvancing = true;

	/** Additional aim spread (degrees) applied while advancing to cover and firing on the move. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Cover", meta = (ClampMin = "0.0",
		ToolTip = "Additive spread on top of normal combat spread while the enemy is running to cover."))
	float AdvanceFireExtraSpreadDeg = 5.f;

	/** Minimum inter-burst pause (seconds) for the advance-fire sub-loop. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Cover", meta = (ClampMin = "0.05",
		ToolTip = "Shortest inter-burst pause while advancing to cover. Independent of BurstPauseMin."))
	float AdvanceFireBurstPauseMin = 1.2f;

	/** Maximum inter-burst pause (seconds) for the advance-fire sub-loop. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Cover", meta = (ClampMin = "0.05",
		ToolTip = "Longest inter-burst pause while advancing to cover. Independent of BurstPauseMax."))
	float AdvanceFireBurstPauseMax = 3.0f;
};
