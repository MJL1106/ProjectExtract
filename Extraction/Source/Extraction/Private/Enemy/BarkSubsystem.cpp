// UBarkSubsystem — one-voice-at-a-time bark arbitration with 3D VO playback.

#include "BarkSubsystem.h"
#include "BarkSetData.h"
#include "EnemyCharacter.h"
#include "Components/AudioComponent.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY(LogEnemyBark);

void UBarkSubsystem::RequestBark(const AActor* Speaker, const UBarkSetData* BarkSet, EBarkType Type, FName Context)
{
	if (!IsValid(Speaker) || !IsValid(BarkSet)) return;

	// A victim frozen in a takedown finisher is seconds from death — it calling "man down" about
	// the other half of a double takedown (or anything else) reads as a bug. Drop its barks.
	if (const AEnemyCharacter* SpeakerEnemy = Cast<AEnemyCharacter>(Speaker))
		if (SpeakerEnemy->IsTakedownPending()) return;

	RequestBarkInternal(Speaker, BarkSet->Barks.Find(Type), BarkSet->Attenuation, BarkSet->VolumeMultiplier,
		EBarkChannel::Enemy, static_cast<uint8>(Type), Context);
}

void UBarkSubsystem::RequestCompanionBark(const AActor* Speaker, const UCompanionBarkSetData* BarkSet, ECompanionBarkType Type, FName Context)
{
	if (!IsValid(Speaker) || !IsValid(BarkSet)) return;

	RequestBarkInternal(Speaker, BarkSet->Barks.Find(Type), BarkSet->Attenuation, BarkSet->VolumeMultiplier,
		EBarkChannel::Companion, static_cast<uint8>(Type), Context);
}

float UBarkSubsystem::RequestScriptedLine(const AActor* Speaker, USoundBase* Sound, USoundAttenuation* Attenuation, float VolumeMultiplier)
{
	if (!IsValid(Speaker) || !IsValid(Sound)) return 0.f;

	const UWorld* World = GetWorld();
	if (!IsValid(World)) return 0.f;

	// Telegraph-style interrupt: scripted dialogue always wins the channel.
	UAudioComponent* CurrentVoice = ActiveVoice.Get();
	if (IsValid(CurrentVoice) && CurrentVoice->IsPlaying())
		CurrentVoice->FadeOut(InterruptFadeSeconds, 0.f);

	const float Now = World->GetTimeSeconds();
	LastVoiceEndTime = Now;

	const float SoundDuration = Sound->GetDuration();
	const float ReservedDuration = (SoundDuration > 0.f && SoundDuration < MaxTrustedScriptedDuration)
		? SoundDuration : FallbackScriptedDuration;
	ScriptedBusyUntilTime = Now + ReservedDuration;

	UAudioComponent* Voice = UGameplayStatics::SpawnSoundAttached(Sound, Speaker->GetRootComponent(), NAME_None,
		FVector::ZeroVector, EAttachLocation::KeepRelativeOffset, false, VolumeMultiplier, 1.f, 0.f, Attenuation);
	if (Voice)
	{
		Voice->OnAudioFinishedNative.AddUObject(this, &UBarkSubsystem::HandleVoiceFinished);
		ActiveVoice = Voice;
	}

	return ReservedDuration;
}

bool UBarkSubsystem::IsScriptedLineActive() const
{
	const UWorld* World = GetWorld();
	return IsValid(World) && World->GetTimeSeconds() < ScriptedBusyUntilTime;
}

void UBarkSubsystem::RequestBarkInternal(const AActor* Speaker, const FBarkDefinition* Def, USoundAttenuation* Attenuation,
	float VolumeMultiplier, EBarkChannel Channel, uint8 RawType, FName Context)
{
	const uint16 TypeKey = MakeTypeKey(Channel, RawType);

	if (!Def || Def->Variants.Num() == 0) return;

	// Context filter: untagged variants are always eligible; tagged variants only when the trigger
	// asked for that tag (a "Sniper — get down!" line must never fire about a Heavy).
	TArray<const FBarkVariant*, TInlineAllocator<8>> Eligible;
	for (const FBarkVariant& Variant : Def->Variants)
		if (Variant.Context.IsNone() || Variant.Context == Context)
			Eligible.Add(&Variant);

	if (Eligible.Num() == 0) return;

	const UWorld* World = GetWorld();
	if (!IsValid(World)) return;
	const float Now = World->GetTimeSeconds();

	// Earshot gate — out-of-range barks are skipped BEFORE any cooldown stamp, so the same enemy
	// can still bark the moment the player closes in. Null pawn (dead/travelling) = no gate.
	if (const APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(World, 0))
	{
		const float DistSq = FVector::DistSquared(PlayerPawn->GetActorLocation(), Speaker->GetActorLocation());
		if (DistSq > FMath::Square(MaxAudibleRange)) return;
	}

	// --- Voice channel arbitration: one line in the world at a time. ---
	// A live line blocks everything below priority 2; a priority-2 telegraph interrupts it with a
	// quick fade instead of talking over it. After a line ends, normal barks wait out a short gap
	// and ambient (priority 0) needs a genuine lull.
	UAudioComponent* CurrentVoice = ActiveVoice.Get();
	const bool bVoiceLive = IsValid(CurrentVoice) && CurrentVoice->IsPlaying();

	if (bVoiceLive && Def->Priority < 2) return;

	if (!bVoiceLive)
	{
		const float RequiredGap = Def->Priority >= 2 ? PriorityPostVoiceGapSeconds
			: Def->Priority == 0 ? AmbientLullSeconds : PostVoiceGapSeconds;
		if ((Now - LastVoiceEndTime) < RequiredGap) return;
	}

	if (const float* GlobalLast = LastBarkTimePerType.Find(TypeKey))
		if ((Now - *GlobalLast) < GlobalTypeDedupWindow) return;

	const TPair<FObjectKey, uint16> SpeakerKey(FObjectKey(Speaker), TypeKey);
	if (const float* SpeakerLast = LastBarkTimePerSpeaker.Find(SpeakerKey))
		if ((Now - *SpeakerLast) < Def->CooldownSeconds) return;

	// Telegraph interrupt: fade the losing line out now that this bark has fully qualified.
	if (bVoiceLive)
		CurrentVoice->FadeOut(InterruptFadeSeconds, 0.f);

	LastBarkTimePerType.Add(TypeKey, Now);
	LastBarkTimePerSpeaker.Add(SpeakerKey, Now);

	const FBarkVariant& Picked = *Eligible[FMath::RandRange(0, Eligible.Num() - 1)];

	// A variant with no imported VO still stamps cooldowns (content-pending state) but frees the
	// channel immediately.
	LastVoiceEndTime = Now;
	if (IsValid(Picked.Sound))
	{
		USceneComponent* AttachTo = Speaker->GetRootComponent();
		UAudioComponent* Voice = UGameplayStatics::SpawnSoundAttached(Picked.Sound, AttachTo, NAME_None,
			FVector::ZeroVector, EAttachLocation::KeepRelativeOffset, false, VolumeMultiplier, 1.f, 0.f, Attenuation);
		if (Voice)
		{
			Voice->OnAudioFinishedNative.AddUObject(this, &UBarkSubsystem::HandleVoiceFinished);
			ActiveVoice = Voice;
		}
	}
}

void UBarkSubsystem::HandleVoiceFinished(UAudioComponent* Voice)
{
	if (Voice != ActiveVoice.Get()) return;

	if (const UWorld* World = GetWorld())
		LastVoiceEndTime = World->GetTimeSeconds();
	ActiveVoice.Reset();
}
