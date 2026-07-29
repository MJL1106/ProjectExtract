// USurfaceAudioBank — the single designer-assigned data asset for world audio: per-surface
// footsteps and bullet impacts plus the global movement/foley/combat one-shots. C++ reads it
// through UGameAudioSubsystem; the asset path lives in Project Settings (UExtractionAudioSettings).

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Chaos/ChaosEngineInterface.h"
#include "Companion/CompanionCommandTypes.h"
#include "SurfaceAudioBank.generated.h"

class UReverbEffect;
class USoundAttenuation;
class USoundBase;
class USoundMix;

USTRUCT(BlueprintType)
struct FSurfaceSoundSet
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, Category = "Surface")
	TObjectPtr<USoundBase> FootstepWalk;

	UPROPERTY(EditDefaultsOnly, Category = "Surface")
	TObjectPtr<USoundBase> FootstepRun;

	/** Null = FootstepWalk played at the component's crouch volume multiplier. */
	UPROPERTY(EditDefaultsOnly, Category = "Surface")
	TObjectPtr<USoundBase> FootstepCrouch;

	UPROPERTY(EditDefaultsOnly, Category = "Surface")
	TObjectPtr<USoundBase> Land;

	UPROPERTY(EditDefaultsOnly, Category = "Surface")
	TObjectPtr<USoundBase> BulletImpact;
};

UCLASS(BlueprintType)
class EXTRACTION_API USurfaceAudioBank : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Per-surface sets keyed by the phys-material surface type (Project Settings → Physics).
	 *  A surface missing here falls back to DefaultSurface. */
	UPROPERTY(EditDefaultsOnly, Category = "Surfaces")
	TMap<TEnumAsByte<EPhysicalSurface>, FSurfaceSoundSet> Surfaces;

	/** Fallback for unmapped surfaces and traces that return no phys material (concrete picks). */
	UPROPERTY(EditDefaultsOnly, Category = "Surfaces")
	FSurfaceSoundSet DefaultSurface;

	// --- Bullet feel ---

	/** Forced onto AI (enemy/companion) fire reports at the muzzle — the fire cues themselves are
	 *  authored 2D for the local player's own gun and would otherwise play flat at any distance. */
	UPROPERTY(EditDefaultsOnly, Category = "Bullets")
	TObjectPtr<USoundAttenuation> GunfireAttenuation;

	/** Volume scale on AI fire reports. The KINEMATION cues are mixed hot for the player's own
	 *  gun — several enemies at full scale slam the master limiter, which ducks the whole mix. */
	UPROPERTY(EditDefaultsOnly, Category = "Bullets", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AIFireVolume = 0.5f;

	/** Bullet-into-body crack, played once per victim per shot for every shooter. */
	UPROPERTY(EditDefaultsOnly, Category = "Bullets")
	TObjectPtr<USoundBase> FleshImpact;

	/** Low "meat thump" layered under FleshImpact. */
	UPROPERTY(EditDefaultsOnly, Category = "Bullets")
	TObjectPtr<USoundBase> FleshImpactLayer;

	/** Distinct crack for head hits (sharper + helmet tick). Null = FleshImpact for all hits. */
	UPROPERTY(EditDefaultsOnly, Category = "Bullets")
	TObjectPtr<USoundBase> HeadshotImpact;

	/** Volume of the 2D flesh-hit confirm when the local player is the shooter (hit feedback
	 *  reads reliably at any range instead of fading with distance). */
	UPROPERTY(EditDefaultsOnly, Category = "Bullets", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FleshFeedbackVolume = 0.7f;

	/** Crack of an enemy shot passing the player's head (near-miss hook). */
	UPROPERTY(EditDefaultsOnly, Category = "Bullets")
	TObjectPtr<USoundBase> Flyby;

	/** Max distance (cm) from the player's head an enemy shot may pass and still crack. */
	UPROPERTY(EditDefaultsOnly, Category = "Bullets", meta = (ClampMin = "0.0"))
	float FlybyRadius = 350.f;

	/** Brass-on-floor tinkle scheduled shortly after each player shot. */
	UPROPERTY(EditDefaultsOnly, Category = "Bullets")
	TObjectPtr<USoundBase> ShellBounce;

	UPROPERTY(EditDefaultsOnly, Category = "Bullets", meta = (ClampMin = "0.0"))
	float ShellDelayMin = 0.35f;

	UPROPERTY(EditDefaultsOnly, Category = "Bullets", meta = (ClampMin = "0.0"))
	float ShellDelayMax = 0.5f;

	UPROPERTY(EditDefaultsOnly, Category = "Bullets", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ShellVolume = 0.4f;

	// --- Movement foley ---

	UPROPERTY(EditDefaultsOnly, Category = "Foley")
	TObjectPtr<USoundBase> JumpFoley;

	UPROPERTY(EditDefaultsOnly, Category = "Foley")
	TObjectPtr<USoundBase> CrouchFoley;

	/** Gear-rattle accent layered over the surface land cue. */
	UPROPERTY(EditDefaultsOnly, Category = "Foley")
	TObjectPtr<USoundBase> LandRattleLayer;

	/** Looping kit rattle while sprinting; started/stopped by UFootstepNoiseComponent. */
	UPROPERTY(EditDefaultsOnly, Category = "Foley")
	TObjectPtr<USoundBase> SprintRattleLoop;

	/** Slide body-foley: spawned attached on slide start and FADED OUT on slide end, so the sound
	 *  can never outlast the slide. Null = silent (kit BP cues must also be nulled when this is set). */
	UPROPERTY(EditDefaultsOnly, Category = "Foley")
	TObjectPtr<USoundBase> SlideFoley;

	/** Gear-settle on slide end. */
	UPROPERTY(EditDefaultsOnly, Category = "Foley")
	TObjectPtr<USoundBase> SlideStopFoley;

	UPROPERTY(EditDefaultsOnly, Category = "Foley")
	TObjectPtr<USoundBase> WeaponSwitchFoley;

	UPROPERTY(EditDefaultsOnly, Category = "Foley")
	TObjectPtr<USoundBase> ThrowFoley;

	/** Seconds after the throw press before ThrowFoley plays, tuned to the kit release frame. */
	UPROPERTY(EditDefaultsOnly, Category = "Foley", meta = (ClampMin = "0.0"))
	float ThrowFoleyDelay = 0.25f;

	/** Minimum seconds between ThrowFoley plays — the throw press is a BP event that can reject
	 *  (mid-throw re-press, empty), so spam presses must not stack foley. */
	UPROPERTY(EditDefaultsOnly, Category = "Foley", meta = (ClampMin = "0.0"))
	float ThrowFoleyCooldown = 1.0f;

	// --- Pickups ---

	/** Corpse-gun swap grab (ReplaceSlotWeapon). */
	UPROPERTY(EditDefaultsOnly, Category = "Pickups")
	TObjectPtr<USoundBase> PickupWeapon;

	/** Ammo drop collect (AAmmoPickup). */
	UPROPERTY(EditDefaultsOnly, Category = "Pickups")
	TObjectPtr<USoundBase> PickupAmmo;

	/** Loot grab (ALootPickup). */
	UPROPERTY(EditDefaultsOnly, Category = "Pickups")
	TObjectPtr<USoundBase> PickupLoot;

	// --- Doors ---

	/** Per-type breach accent played at the door's acoustic portal on companion breach, layered
	 *  over the door's own OpenSound (Loud = slam, Tactical = latch, Quiet = slow slide). */
	UPROPERTY(EditDefaultsOnly, Category = "Doors")
	TMap<EBreachType, TObjectPtr<USoundBase>> BreachSounds;

	/** Body/boot hitting the door, layered under the BreachSounds accent at the same portal point.
	 *  One cue for every type — the impact is the same physical event and only its force changes,
	 *  so the per-type difference is volume rather than three near-identical assets. */
	UPROPERTY(EditDefaultsOnly, Category = "Doors")
	TObjectPtr<USoundBase> BreachImpact;

	/** Per-type force of BreachImpact (a Quiet shove lands far softer than a Loud kick).
	 *  A type missing here uses BreachImpactVolume. */
	UPROPERTY(EditDefaultsOnly, Category = "Doors", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	TMap<EBreachType, float> BreachImpactVolumes;

	/** Fallback volume for breach types absent from BreachImpactVolumes. */
	UPROPERTY(EditDefaultsOnly, Category = "Doors", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BreachImpactVolume = 1.f;

	// --- Reverb ---

	/** Always-on world reverb activated by the subsystem for every map (sounds opt in via their
	 *  attenuation's reverb send). Null = dry world. */
	UPROPERTY(EditDefaultsOnly, Category = "Reverb")
	TObjectPtr<UReverbEffect> AmbientReverb;

	UPROPERTY(EditDefaultsOnly, Category = "Reverb", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AmbientReverbVolume = 0.4f;

	// --- DBNO ---

	/** 2D looping heartbeat while the local player is downed. */
	UPROPERTY(EditDefaultsOnly, Category = "DBNO")
	TObjectPtr<USoundBase> HeartbeatLoop;

	/** Pushed while downed — ducks/muffles the SFX classes under the heartbeat. */
	UPROPERTY(EditDefaultsOnly, Category = "DBNO")
	TObjectPtr<USoundMix> DBNOSoundMix;
};
