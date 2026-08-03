// UBarkSubsystem — the world's single voice channel. Arbitrates enemy AND companion barks:
// one VO line plays at a time (3D, attached to the speaker), priority-2 telegraphs interrupt,
// ambient chatter only surfaces in lulls, per-speaker/per-type cooldowns stop repeats.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "UObject/ObjectKey.h"
#include "EnemyTypes.h"
#include "Companion/CompanionBarkTypes.h"
#include "BarkSubsystem.generated.h"

class UBarkSetData;
class UCompanionBarkSetData;
class UAudioComponent;
class USoundBase;
class USoundAttenuation;
struct FBarkDefinition;

DECLARE_LOG_CATEGORY_EXTERN(LogEnemyBark, Log, All);

UCLASS()
class EXTRACTION_API UBarkSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Enemy bark: picks a variant for the type and plays it 3D at the speaker, honouring the
	 *  one-voice channel and cooldowns. Context filters tagged variants (see FBarkVariant::Context). */
	void RequestBark(const AActor* Speaker, const UBarkSetData* BarkSet, EBarkType Type, FName Context = NAME_None);

	/** Companion bark: same channel, separate type identity — a companion "Frag out!" never
	 *  dedups against an enemy GrenadeOut. */
	void RequestCompanionBark(const AActor* Speaker, const UCompanionBarkSetData* BarkSet, ECompanionBarkType Type, FName Context = NAME_None);

	/** Scripted one-off line (dialogue trigger volumes, the VIP rescue exchange): claims the voice
	 *  channel at telegraph priority — interrupts live chatter, skips dedup/cooldown bookkeeping
	 *  (the caller owns its own once-only state).
	 *  Returns the duration the channel is reserved for, so callers can chain the next beat off
	 *  the line ending; 0 when nothing played. Clamped/substituted the same way the busy window is,
	 *  so a streaming or looping asset can't hand back a nonsense wait. */
	float RequestScriptedLine(const AActor* Speaker, USoundBase* Sound, USoundAttenuation* Attenuation, float VolumeMultiplier);

	/** True while a scripted line is (estimated) still playing — trigger volumes check this so two
	 *  boxes never interleave their exchanges. */
	bool IsScriptedLineActive() const;

private:
	/** Same bark type from any speaker within this window is dropped (no five-voice "Contact!" stacks). */
	static constexpr float GlobalTypeDedupWindow = 1.5f;

	/** Minimum silence after a voice line ends before the next normal (priority 1) bark may start. */
	static constexpr float PostVoiceGapSeconds = 1.25f;

	/** Reduced post-line gap for priority >= 2 telegraphs (grenade out, man down, player down). */
	static constexpr float PriorityPostVoiceGapSeconds = 0.25f;

	/** Ambient/banter (priority 0) needs this much silence since the last line — lulls only. */
	static constexpr float AmbientLullSeconds = 6.f;

	/** Fade applied to the active line when a priority telegraph interrupts it. */
	static constexpr float InterruptFadeSeconds = 0.2f;

	/** Speakers farther than this from the local player are skipped entirely — no VO and no
	 *  cooldown stamp, so the same enemy can still bark once the player closes in. */
	static constexpr float MaxAudibleRange = 4000.f;

	/** Channel tag folded into the dedup keys so enemy and companion types stay distinct. */
	enum class EBarkChannel : uint8 { Enemy, Companion };

	void RequestBarkInternal(const AActor* Speaker, const FBarkDefinition* Def, USoundAttenuation* Attenuation,
		float VolumeMultiplier, EBarkChannel Channel, uint8 RawType, const FString& TypeStr, FName Context);

	void HandleVoiceFinished(UAudioComponent* Voice);

	/** Shows a coalescing on-screen debug message for a bark event (gated by caller). */
	void ShowBarkScreenMessage(const AActor* Speaker, uint16 TypeKey, const FString& Message, const FColor& Color);

	static uint16 MakeTypeKey(EBarkChannel Channel, uint8 RawType)
	{
		return (static_cast<uint16>(Channel) << 8) | RawType;
	}

	TMap<TPair<FObjectKey, uint16>, float> LastBarkTimePerSpeaker;
	TMap<uint16, float> LastBarkTimePerType;

	/** The line currently owning the voice channel; stale/finished = channel free. */
	TWeakObjectPtr<UAudioComponent> ActiveVoice;

	/** World time the last voice line finished (or fired, for lines with no audio yet). */
	float LastVoiceEndTime = -1000.f;

	/** World time the active scripted line is expected to end; 0 = none. Estimated from the sound's
	 *  duration at request time — durations beyond the cap (looping/procedural) use the fallback. */
	float ScriptedBusyUntilTime = 0.f;

	static constexpr float MaxTrustedScriptedDuration = 60.f;
	static constexpr float FallbackScriptedDuration = 4.f;
};
