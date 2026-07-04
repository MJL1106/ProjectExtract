// Per-archetype tuning data asset — all gameplay numbers that vary between archetypes live here.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "EnemyTypes.h"
#include "EnemyArchetypeData.generated.h"

class AWeaponBase;
class UBehaviorTree;
class UBarkSetData;
class UAnimMontage;
class AEnemyGrenadeProjectile;
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

	/** Headshot damage as a fraction of this enemy's max health. 0 = disabled (falls back to the weapon DamageType HeadMultiplier). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Stats", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float HeadshotMaxHealthFraction = 0.65f;

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

	/** When true, enemies fighting WITHOUT cover try to claim one on CombatReseekCooldown even at
	 *  Confident morale (Shaken/Broken already reseek in their hold branches). Turn off for
	 *  commit-rush archetypes that should stay in the open. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Cover")
	bool bCombatReseeksCover = true;

	/** Seconds between exposed-in-combat cover reseek attempts (bCombatReseeksCover). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Cover", meta = (ClampMin = "0.5"))
	float CombatReseekCooldown = 5.f;

	/** Within this range the enemy skips cover-seeking and fires in place (selector falls through to the open-ground fire branch). 0 = always seek cover. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Combat", meta = (ClampMin = "0.0"))
	float PointBlankFireRange = 0.f;

	/** How long the enemy will search before returning to Unaware. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Combat", meta = (ClampMin = "1.0"))
	float SearchDuration = 8.f;

	/** Seconds of no LOS before transitioning from Combat to Searching. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Combat", meta = (ClampMin = "0.0"))
	float LostContactGrace = 45.f;

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

	// --- Shotgun (commit-to-rush gate) ---

	/** Distance (cm) within which the shotgunner commits to a rush at the target. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Shotgun",
		meta = (ClampMin = "0.0", ToolTip = "Distance (cm) at which the shotgunner commits to rushing the target."))
	float ShotgunCommitRange = 800.f;

	/** Hysteresis band (cm) — the target must open distance past CommitRange + this before the rush drops back to hold. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Shotgun",
		meta = (ClampMin = "0.0", ToolTip = "Extra distance (cm) beyond CommitRange before the rush is cancelled (prevents flip-flopping at the boundary)."))
	float ShotgunCommitHysteresis = 200.f;

	/** Health fraction below which the shotgunner breaks off a rush mid-charge and retreats to cover. 0 = never abort. Must be < 1.0 or the health gate is always tripped and the enemy can never rush. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Shotgun",
		meta = (ClampMin = "0.0", ClampMax = "0.95", ToolTip = "Health fraction (0-0.95) below which the shotgunner aborts a rush and returns to cover. 0 disables the health abort."))
	float ShotgunRushAbortHealthFraction = 0.35f;

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

	/** Montage played when the grenadier is crouched behind low cover. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Grenadier")
	TObjectPtr<UAnimMontage> GrenadeThrowCrouchMontage;

	/** Pool of throw montages when standing — one is chosen at random per throw. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Grenadier")
	TArray<TObjectPtr<UAnimMontage>> GrenadeThrowStandMontages;

	/** Mesh socket from which the grenade is launched at the release notify frame. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Grenadier")
	FName GrenadeThrowSocket = TEXT("GrenadeSocket");

	/** Scales the throw landing distance toward the thrower along the horizontal: 1.0 = land at the
	 *  target's last-known spot; 0.8 = 20% shorter. Keeps the target's ground Z (pulls X/Y only). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Grenadier", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float GrenadeLandingDistanceScale = 1.0f;

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

	/** Distance (cm) the rusher stops short of the target when closing to melee. The engine adds the
	 *  target's capsule radius (~34 cm) to this, so the effective stop is ~MeleeApproachDistance + 34.
	 *  Keep it ~one capsule-radius below MeleeRange (e.g. <= 145 when MeleeRange = 180) so the strike
	 *  still connects; the 70 floor keeps the capsules from overlapping (the burrow bug this fixes). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Melee", meta = (ClampMin = "70.0"))
	float MeleeApproachDistance = 120.f;

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

	/** If true, morale events are ignored and the enemy stays Confident permanently (e.g. rusher archetype). */
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

	/** Seconds after a bounding maneuver ends during which the grunt fights in place
	 *  (Flank/seek-cover branches skipped) instead of cold re-acquiring — smooths the
	 *  handoff when the maneuver is torn down (e.g. officer dies mid-maneuver). 0 = disabled. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Squad", meta = (ClampMin = "0.0"))
	float ManeuverDisengageHoldTime = 3.0f;

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

	/** Threat-score multiplier for companion candidates once in Combat. 1 = equal priority to the
	 *  player, 0 = never target the companion. Companions still cannot TRIGGER combat on an unaware
	 *  enemy regardless of this value (stealth rule). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Targeting", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CompanionThreatScoreMultiplier = 0.75f;

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

	/** When true, beyond MoveAndShootMaxRange the enemy advances toward the target at CombatSpeed while
	 *  firing (relentless pursuer — shotgun) instead of failing out. Grunts leave false to fall through
	 *  to their cover branch. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|MoveAndShoot")
	bool bMoveAndShootPursue = false;

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

	/** Half-angle (degrees) of the cover's usable fire arc — replaces the old per-slot FireArcDegrees.
	 *  60 = 120° total arc (old default). Used by the compromise arc test in BTTask_EnemyCombatFire. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Cover",
		meta = (ClampMin = "10.0", ClampMax = "180.0",
		ToolTip = "Half-angle of the cover fire arc (degrees). 60 = old 120 degree default."))
	float CoverFlankArcHalfAngleDeg = 60.f;

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

	/** Ladder stage 0 threshold: consecutive Expose-phase LOS timeouts on the first peek side
	 *  before the failed-peek ladder advances to the next stage. Resets to 0 when fire is opened. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Cover", meta = (ClampMin = "1",
		ToolTip = "Ladder stage 0: consecutive Expose LOS-timeouts (no shot fired) on the first side before the failed-peek ladder advances."))
	int32 MaxExposeLosTimeouts = 2;

	/** Ladder stage 2: consecutive LOS-timeouts on the second side before advancing. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Cover", meta = (ClampMin = "1",
		ToolTip = "LOS-timeout threshold on the second side of the failed-peek ladder before the enemy advances to the next stage."))
	int32 LadderSecondSideTimeouts = 1;

	/** Ladder stage 1/3: consecutive LOS-timeouts while peeking over top before advancing. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Cover", meta = (ClampMin = "1",
		ToolTip = "LOS-timeout threshold for over-top peek stages of the failed-peek ladder before advancing."))
	int32 LadderOverTopTimeouts = 1;

	// --- Cover Peek (vision cone + endpoint options) ---

	/** Half-angle (degrees) of the engagement cone centred on the current peek direction while the
	 *  enemy is posed in cover. Targets outside this cone are treated as no-LOS for fire decisions
	 *  only — perception and flank-break remain live. 180 = disabled (full sweep). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Cover|Peek",
		meta = (ClampMin = "5.0", ClampMax = "180.0",
		ToolTip = "Half-angle of the in-cover engagement cone (degrees). Targets outside it are treated as out-of-LOS for fire decisions. 180 disables the cone."))
	float CoverPeekConeHalfAngleDeg = 75.f;

	/** DEPRECATED — no longer read by the fire test. The peek cone is a single unbiased cone about
	 *  the fire-arc forward so fire reach always matches the torso aim-offset reach (a lean-biased
	 *  extension let the enemy fire at bearings the gun model could not visually point at). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Cover|Peek",
		meta = (ClampMin = "0.0", ClampMax = "85.0",
		ToolTip = "DEPRECATED: unused. The peek cone is unbiased; fire reach matches torso aim reach."))
	float CoverPeekConeBiasDeg = 20.f;

	/** Seconds bRelocatePending may linger before the task force-executes the relocate even if the
	 *  enemy is still firing. Ensures a flanked enemy that keeps getting LOS eventually breaks cover. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Cover|Peek",
		meta = (ClampMin = "0.5",
		ToolTip = "Max seconds a flank-relocate stays pending before force-executing (stops a continuously-firing flanked enemy deferring forever)."))
	float CoverRelocatePendingTimeout = 2.f;

	/** Probability (0-1) that a crouch-cover peek at a wall end-point will swap from a side peek to
	 *  a stand-up over-top peek. Applied at Expose entry and in the failed-peek escalation ladder.
	 *  0 = never stand up; 1 = always stand up when the over-top path is physically valid. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Cover|Peek",
		meta = (ClampMin = "0.0", ClampMax = "1.0",
		ToolTip = "Roll chance for a crouch end-point to stand up and peek over the top instead of leaning sideways."))
	float CoverEndpointStandPeekChance = 0.3f;

	/** When true, a compromised cover is broken instantly (bypassing debounce/cooldown/safe-phase gates)
	 *  when the enemy has effective LOS to the threat (cone-gated, i.e. they can actually see them).
	 *  The slow-path debounce still applies when the enemy cannot see the threat (flanked from behind). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Cover",
		meta = (ToolTip = "Instant cover break when compromised AND the enemy has effective LOS to the threat. Slow path kept for blind flanks."))
	bool bCoverInstantBreakWithLOS = true;

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

	// --- Cover Scoring (shared scorer — C++ picker paths AND the EQS scoring test) ---

	/** Weight for closeness of the candidate to the enemy (1 at the enemy, 0 at CoverSearchRadius). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Cover|Scoring", meta = (ClampMin = "0.0"))
	float CoverScoreProximityWeight = 1.f;

	/** Weight for the candidate sitting at/beyond the ideal engagement distance from the threat. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Cover|Scoring", meta = (ClampMin = "0.0"))
	float CoverScoreDistanceBandWeight = 0.7f;

	/** Flat additive term (legacy cover bonus). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Cover|Scoring", meta = (ClampMin = "0.0"))
	float CoverScoreFlatBonus = 0.15f;

	/** Distance (cm) to the threat below which the distance-band term scores 0. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Cover|Scoring", meta = (ClampMin = "0.0"))
	float CoverScoreIdealDistMin = 500.f;

	/** Range (cm) over which the distance-band term ramps from 0 to 1 past CoverScoreIdealDistMin. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Cover|Scoring", meta = (ClampMin = "1.0"))
	float CoverScoreIdealDistRange = 700.f;

	/** Bonus for a candidate with a stance-matched lean flag on the threat-facing side (a corner the
	 *  enemy can peek around toward the threat). 0 = off. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Cover|Scoring", meta = (ClampMin = "0.0"))
	float CoverScoreSideFlagWeight = 0.f;

	/** Fraction of CoverScoreSideFlagWeight granted when the candidate has a lean flag only on the
	 *  side facing away from the threat. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Cover|Scoring", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CoverScoreSideFlagOpposite = 0.35f;

	/** Bonus for a candidate with at least one other free cover point within
	 *  CoverScoreRetreatViabilityRadius (an escape option exists). 0 = off. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Cover|Scoring", meta = (ClampMin = "0.0"))
	float CoverScoreRetreatViabilityWeight = 0.f;

	/** Radius (cm) of the retreat-viability neighbour search. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Cover|Scoring", meta = (ClampMin = "100.0"))
	float CoverScoreRetreatViabilityRadius = 600.f;

	/** A relocate candidate must beat the CURRENT cover's score by this margin before a voluntary
	 *  (score-driven) relocate is taken. Never applied to compromise/flank breaks. 0 = off. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Cover|Scoring", meta = (ClampMin = "0.0"))
	float CoverScoreStickinessMargin = 0.f;

	// --- Cover Path Exposure (route-safety penalty on relocate/reseek picks) ---

	/** Score penalty at 100% route exposure (scaled by the exposed fraction of route samples).
	 *  0 = off (no route traces run). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Cover|PathExposure", meta = (ClampMin = "0.0"))
	float PathExposureWeight = 0.f;

	/** Sample points traced along the route to each evaluated candidate. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Cover|PathExposure", meta = (ClampMin = "1", ClampMax = "8"))
	int32 PathExposureSampleCount = 3;

	/** Hard cap on route-exposure traces per selection round (spread across the top candidates). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Cover|PathExposure", meta = (ClampMin = "3", ClampMax = "64"))
	int32 PathExposureMaxTracesPerSelection = 24;

	// --- Cover Blind Fire (suppression response) ---

	/** Weight for hiding when suppressed (stay hunkered, wait). Default is the only non-zero
	 *  weight, so all existing archetypes keep today's hide-only behaviour. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Cover|BlindFire", meta = (ClampMin = "0.0"))
	float SuppressedHideWeight = 1.f;

	/** Weight for blind-firing when suppressed (fire from hunker without LOS). 0 = never blind-fire. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Cover|BlindFire", meta = (ClampMin = "0.0"))
	float SuppressedBlindFireWeight = 0.f;

	/** Extra spread (degrees) applied during a blind-fire burst. Restored to 0 after burst ends. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Cover|BlindFire", meta = (ClampMin = "0.0"))
	float BlindFireExtraSpreadDeg = 12.f;

	/** Minimum blind-fire burst duration (seconds). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Cover|BlindFire", meta = (ClampMin = "0.05"))
	float BlindFireBurstMin = 0.3f;

	/** Maximum blind-fire burst duration (seconds). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Cover|BlindFire", meta = (ClampMin = "0.05"))
	float BlindFireBurstMax = 0.8f;

	// --- Cover Shuffle (move within cover) ---

	/** Weight (0-1) for shuffling to a neighbouring cover point during the Pause phase. 0 = never shuffle. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Cover|Shuffle", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CoverShuffleWeight = 0.f;

	/** Minimum distance (cm) for a shuffle destination from the current point. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Cover|Shuffle", meta = (ClampMin = "0.0"))
	float ShuffleDistanceMin = 50.f;

	/** Maximum distance (cm) for a shuffle destination from the current point. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Cover|Shuffle", meta = (ClampMin = "1.0"))
	float ShuffleDistanceMax = 125.f;

	/** Composite shuffle score weight for having a threat-facing side flag. 0 = ignore flags. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Cover|Shuffle", meta = (ClampMin = "0.0"))
	float ShuffleSideFlagWeight = 1.5f;

	/** Composite shuffle score weight for lateral projection toward the wall end. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Cover|Shuffle", meta = (ClampMin = "0.0"))
	float ShuffleWallEndWeight = 0.75f;

	/** Composite shuffle score weight for angle-to-threat improvement vs current point. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Cover|Shuffle", meta = (ClampMin = "0.0"))
	float ShuffleAngleWeight = 1.0f;

	/** Composite shuffle score penalty per unit of normalized distance. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Cover|Shuffle", meta = (ClampMin = "0.0"))
	float ShuffleDistancePenalty = 0.5f;

	/** MaxWalkSpeed cap (cm/s) applied while a same-wall cover move (shuffle or ladder reposition)
	 *  is in flight. Walk clip stride matches this speed — higher speeds cause foot-sliding.
	 *  Original MaxWalkSpeed is restored on arrival, abort, task teardown, death, or relocate. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Cover|Shuffle",
		meta = (ClampMin = "50.0",
		ToolTip = "Walk speed (cm/s) used during same-wall cover shuffles. Matches the walk clip stride to prevent foot-sliding."))
	float CoverMoveSpeed = 170.f;

	/** Multiplier on the effective CoverShuffleWeight at Pause-exit when the current point
	 *  lacks a threat-facing side flag but a flagged same-wall candidate exists. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Cover|Shuffle", meta = (ClampMin = "1.0"))
	float CoverAngleShuffleMultiplier = 2.0f;

	// --- Cover Timing (peek/hide variance) ---

	/** Seconds the enemy holds its idle-side swap pose before executing a cover move that
	 *  requires turning around (move direction != current LastCoverSide). Lets the idle
	 *  montage blend to the new side before the walk starts. 0 = instant move. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Cover|Timing", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float CoverMoveSideSwapDelay = 0.5f;

	/** Minimum Expose-phase settle duration (seconds) before opening fire. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Cover|Timing", meta = (ClampMin = "0.05"))
	float ExposePhaseMin = 0.15f;

	/** Maximum Expose-phase settle duration (seconds). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Cover|Timing", meta = (ClampMin = "0.05"))
	float ExposePhaseMax = 0.45f;

	/** Minimum Recover-phase re-crouch settle duration (seconds). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Cover|Timing", meta = (ClampMin = "0.05"))
	float RecoverPhaseMin = 0.10f;

	/** Maximum Recover-phase re-crouch settle duration (seconds). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Cover|Timing", meta = (ClampMin = "0.05"))
	float RecoverPhaseMax = 0.40f;

	/** Completed peek-fire cycles at a cover before shuffle/relocate may move on. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Cover|Timing", meta = (ClampMin = "0"))
	int32 MinPeekCyclesBeforeRelocate = 1;

	/** Per-pause probability of a long-hide feint (multiplied into pause duration). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Cover|Timing", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float LongHideChance = 0.15f;

	/** Multiplier on pause duration when the long-hide feint triggers. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Cover|Timing", meta = (ClampMin = "1.0"))
	float LongHideMultiplier = 2.5f;

	// --- Cover Reload (tucked reload gate) ---

	/** When true, the enemy proactively reloads behind cover during the Pause phase
	 *  if ammo is below TuckedReloadAmmoFraction. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Cover|Reload")
	bool bReloadWhileTuckedWhenLow = true;

	/** Ammo fraction (0-1) below which a proactive tucked reload triggers during Pause. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Cover|Reload", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float TuckedReloadAmmoFraction = 0.34f;

	// --- Cover Positioning & Advance Fire ---

	/** Extra depth (cm) behind the cover line the enemy targets, beyond capsule radius.
	 *  Arrival = cover-line point - ForwardDirection * (CapsuleRadius + CoverStandoffPadding). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Cover", meta = (ClampMin = "0.0",
		ToolTip = "Extra standoff padding behind the cover wall surface, in cm, added on top of capsule radius."))
	float CoverStandoffPadding = 25.f;

	/** Lateral gap (cm) between the capsule's edge and the wall's actual end at endpoint covers.
	 *  Arrival positions are corner-snapped so the shoulder sits this far inside the edge regardless
	 *  of how far from the corner the bake left the point (fixes too-deep / past-the-edge covers). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Cover", meta = (ClampMin = "0.0", ClampMax = "100.0",
		ToolTip = "Lateral gap in cm between the capsule edge and the wall corner at endpoint covers."))
	float CoverCornerGap = 5.f;

	/** When true the enemy fires at the combat target while advancing to cover (BTTask_EnemyMoveToCover only). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Cover",
		meta = (ToolTip = "Allow the enemy to fire while running to a cover slot. Uses separate burst timing (AdvanceFireBurstPause*)."))
	bool bFireWhileAdvancing = true;

	/** Seconds a cover move may make no meaningful progress toward its arrival anchor before the
	 *  enemy abandons it (releases the slot and recovers) instead of stalling stuck in the open. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Cover", meta = (ClampMin = "0.25",
		ToolTip = "Seconds a cover move may make no meaningful progress toward its arrival anchor before the enemy abandons it (releases the slot and recovers) instead of stalling stuck in the open."))
	float CoverMoveStallTimeout = 1.25f;

	/** Minimum distance (cm) a cover move must close toward its anchor to count as progress and
	 *  reset the stall timer; below this the move is considered stalled. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Cover", meta = (ClampMin = "1.0",
		ToolTip = "Minimum distance (cm) a cover move must close toward its anchor to count as progress and reset the stall timer; below this the move is considered stalled."))
	float CoverMoveStallProgressEpsilon = 12.f;

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

	// --- Posture (pressure/advance — Hold / Press / FallBack) ---

	/** Master gate for the posture system. Off = enemy holds today's fixed engagement band. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Posture")
	bool bPostureSystemEnabled = false;

	/** Whether this archetype may enter Press at all (snipers: false). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Posture")
	bool bCanPress = true;

	/** Seconds between posture evaluations (staggered per enemy by a random phase). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Posture", meta = (ClampMin = "0.1"))
	float PostureEvalInterval = 0.5f;

	/** Aggression (0-1) at or above which Press is entered (requires Confident morale, unsuppressed). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Posture", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PressEnterThreshold = 0.65f;

	/** Aggression below which Press is abandoned (hysteresis — keep below PressEnterThreshold). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Posture", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PressExitThreshold = 0.45f;

	/** Aggression at or below which the enemy falls back (also forced by Broken morale). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Posture", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FallBackEnterThreshold = 0.25f;

	/** Continuous seconds in Press before a proactive advance to closer cover is requested. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Posture", meta = (ClampMin = "0.5"))
	float PressAdvanceHoldTime = 2.5f;

	/** Minimum seconds between committed advance relocates. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Posture", meta = (ClampMin = "1.0"))
	float AdvanceRelocateCooldown = 12.f;

	/** Weight: the threat hasn't damaged this enemy within PostureRecentDamageWindow. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Posture", meta = (ClampMin = "0.0"))
	float PostureWeightNoDamageRecently = 1.f;

	/** Weight: the threat is fully passive (no recent damage AND own suppression low). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Posture", meta = (ClampMin = "0.0"))
	float PostureWeightThreatPassive = 1.f;

	/** Weight: squad numeric advantage (living members). 0 = ignore squad size. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Posture", meta = (ClampMin = "0.0"))
	float PostureWeightSquadAdvantage = 0.f;

	/** Seconds of no incoming damage from the threat before it starts reading as passive. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Posture", meta = (ClampMin = "1.0"))
	float PostureRecentDamageWindow = 4.f;

	/** Delta (cm, negative = closer) applied to CoverScoreIdealDistMin while Pressing. Press also
	 *  flips the distance-band term to prefer covers NEARER the threat, down to the shifted min. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Posture")
	float PosturePressDistMinDelta = -300.f;

	/** Delta (cm) applied to CoverScoreIdealDistMin while falling back — deeper covers score higher. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Posture", meta = (ClampMin = "0.0"))
	float PostureFallBackDistMinDelta = 400.f;

	/** Minimum distance (cm) an advance relocate must close toward the threat vs the current cover. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Posture", meta = (ClampMin = "50.0"))
	float PostureAdvanceMinGain = 200.f;
};
