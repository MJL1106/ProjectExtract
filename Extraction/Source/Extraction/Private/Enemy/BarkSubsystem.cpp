// UBarkSubsystem — one-voice-at-a-time bark arbitration with 3D VO playback.

#include "BarkSubsystem.h"
#include "BarkSetData.h"
#include "EnemyCharacter.h"
#include "EnemyDebug.h"
#include "Components/AudioComponent.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "DrawDebugHelpers.h"

DEFINE_LOG_CATEGORY(LogEnemyBark);

void UBarkSubsystem::RequestBark(const AActor* Speaker, const UBarkSetData* BarkSet, EBarkType Type, FName Context)
{
	if (!IsValid(Speaker) || !IsValid(BarkSet)) return;

	// A victim frozen in a takedown finisher is seconds from death — it calling "man down" about
	// the other half of a double takedown (or anything else) reads as a bug. Drop its barks.
	if (const AEnemyCharacter* SpeakerEnemy = Cast<AEnemyCharacter>(Speaker))
		if (SpeakerEnemy->IsTakedownPending()) return;

	// Enum-name stringification only matters for the debug channel — skip the per-request
	// allocation otherwise (damage-driven attempts arrive at bullet-hit frequency).
	RequestBarkInternal(Speaker, BarkSet->Barks.Find(Type), BarkSet->Attenuation, BarkSet->VolumeMultiplier,
		EBarkChannel::Enemy, static_cast<uint8>(Type),
		IsEnemyBarkDebugEnabled() ? UEnum::GetValueAsString(Type) : FString(), Context);
}

void UBarkSubsystem::RequestCompanionBark(const AActor* Speaker, const UCompanionBarkSetData* BarkSet, ECompanionBarkType Type, FName Context)
{
	if (!IsValid(Speaker) || !IsValid(BarkSet)) return;

	RequestBarkInternal(Speaker, BarkSet->Barks.Find(Type), BarkSet->Attenuation, BarkSet->VolumeMultiplier,
		EBarkChannel::Companion, static_cast<uint8>(Type),
		IsEnemyBarkDebugEnabled() ? UEnum::GetValueAsString(Type) : FString(), Context);
}

void UBarkSubsystem::RequestBarkInternal(const AActor* Speaker, const FBarkDefinition* Def, USoundAttenuation* Attenuation,
	float VolumeMultiplier, EBarkChannel Channel, uint8 RawType, const FString& TypeStr, FName Context)
{
	const bool bDebug = IsEnemyBarkDebugEnabled();
	const uint16 TypeKey = MakeTypeKey(Channel, RawType);

	if (!Def || Def->Variants.Num() == 0)
	{
		if (bDebug)
		{
			UE_LOG(LogEnemyBark, Warning, TEXT("DROP NoLines type=%s speaker=%s"), *TypeStr, *GetNameSafe(Speaker));
			ShowBarkScreenMessage(Speaker, TypeKey, FString::Printf(TEXT("DROP NoLines %s"), *TypeStr), FColor::Silver);
		}
		return;
	}

	// Context filter: untagged variants are always eligible; tagged variants only when the trigger
	// asked for that tag (a "Sniper — get down!" line must never fire about a Heavy).
	TArray<const FBarkVariant*, TInlineAllocator<8>> Eligible;
	for (const FBarkVariant& Variant : Def->Variants)
		if (Variant.Context.IsNone() || Variant.Context == Context)
			Eligible.Add(&Variant);

	if (Eligible.Num() == 0)
	{
		if (bDebug)
			UE_LOG(LogEnemyBark, Log, TEXT("DROP NoContextMatch type=%s ctx=%s"), *TypeStr, *Context.ToString());
		return;
	}

	const UWorld* World = GetWorld();
	if (!IsValid(World)) return;
	const float Now = World->GetTimeSeconds();

	// Earshot gate — out-of-range barks are skipped BEFORE any cooldown stamp, so the same enemy
	// can still bark the moment the player closes in. Null pawn (dead/travelling) = no gate.
	if (const APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(World, 0))
	{
		const float DistSq = FVector::DistSquared(PlayerPawn->GetActorLocation(), Speaker->GetActorLocation());
		if (DistSq > FMath::Square(MaxAudibleRange))
		{
			if (bDebug)
			{
				UE_LOG(LogEnemyBark, Log, TEXT("DROP OutOfRange type=%s (dist=%.0f > %.0f)"), *TypeStr, FMath::Sqrt(DistSq), MaxAudibleRange);
				ShowBarkScreenMessage(Speaker, TypeKey, FString::Printf(TEXT("DROP Range %s (%.0f>%.0f)"), *TypeStr, FMath::Sqrt(DistSq), MaxAudibleRange), FColor::Silver);
			}
			return;
		}
	}

	// --- Voice channel arbitration: one line in the world at a time. ---
	// A live line blocks everything below priority 2; a priority-2 telegraph interrupts it with a
	// quick fade instead of talking over it. After a line ends, normal barks wait out a short gap
	// and ambient (priority 0) needs a genuine lull.
	UAudioComponent* CurrentVoice = ActiveVoice.Get();
	const bool bVoiceLive = IsValid(CurrentVoice) && CurrentVoice->IsPlaying();

	if (bVoiceLive && Def->Priority < 2)
	{
		if (bDebug)
		{
			UE_LOG(LogEnemyBark, Log, TEXT("DROP VoiceBusy type=%s (prio=%d)"), *TypeStr, Def->Priority);
			ShowBarkScreenMessage(Speaker, TypeKey, FString::Printf(TEXT("DROP Busy %s"), *TypeStr), FColor::Silver);
		}
		return;
	}

	if (!bVoiceLive)
	{
		const float RequiredGap = Def->Priority >= 2 ? PriorityPostVoiceGapSeconds
			: Def->Priority == 0 ? AmbientLullSeconds : PostVoiceGapSeconds;
		const float SinceLast = Now - LastVoiceEndTime;
		if (SinceLast < RequiredGap)
		{
			if (bDebug)
			{
				UE_LOG(LogEnemyBark, Log, TEXT("DROP Gap type=%s (since=%.2fs < gap=%.2fs, prio=%d)"), *TypeStr, SinceLast, RequiredGap, Def->Priority);
				ShowBarkScreenMessage(Speaker, TypeKey, FString::Printf(TEXT("DROP Gap %s (%.1fs<%.1fs)"), *TypeStr, SinceLast, RequiredGap), FColor::Silver);
			}
			return;
		}
	}

	if (const float* GlobalLast = LastBarkTimePerType.Find(TypeKey))
	{
		const float Since = Now - *GlobalLast;
		if (Since < GlobalTypeDedupWindow)
		{
			if (bDebug)
			{
				UE_LOG(LogEnemyBark, Log, TEXT("DROP GlobalDedup type=%s (since=%.2fs < window=%.2fs)"), *TypeStr, Since, GlobalTypeDedupWindow);
				ShowBarkScreenMessage(Speaker, TypeKey, FString::Printf(TEXT("DROP GlobalDedup %s (%.1fs<%.1fs)"), *TypeStr, Since, GlobalTypeDedupWindow), FColor::Silver);
			}
			return;
		}
	}

	const TPair<FObjectKey, uint16> SpeakerKey(FObjectKey(Speaker), TypeKey);
	if (const float* SpeakerLast = LastBarkTimePerSpeaker.Find(SpeakerKey))
	{
		const float Since = Now - *SpeakerLast;
		if (Since < Def->CooldownSeconds)
		{
			if (bDebug)
			{
				UE_LOG(LogEnemyBark, Log, TEXT("DROP Cooldown type=%s speaker=%s (since=%.2fs < cd=%.2fs)"), *TypeStr, *GetNameSafe(Speaker), Since, Def->CooldownSeconds);
				ShowBarkScreenMessage(Speaker, TypeKey, FString::Printf(TEXT("DROP CD %s (%.1fs<%.1fs)"), *TypeStr, Since, Def->CooldownSeconds), FColor::Silver);
			}
			return;
		}
	}

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

	if (bDebug)
	{
		UE_LOG(LogEnemyBark, Log, TEXT("FIRED type=%s speaker=%s line=\"%s\" sound=%s"),
			*TypeStr, *GetNameSafe(Speaker), *Picked.Line.ToString(), *GetNameSafe(Picked.Sound));
		ShowBarkScreenMessage(Speaker, TypeKey, FString::Printf(TEXT("FIRED %s \"%s\""), *TypeStr, *Picked.Line.ToString()), FColor::Green);

		const FVector HeadLocation = Speaker->GetActorLocation() + FVector(0.f, 0.f, 100.f);
		DrawDebugString(GetWorld(), HeadLocation, FString::Printf(TEXT("[%s] %s"), *TypeStr, *Picked.Line.ToString()), nullptr, FColor::Green, 2.f, true);
	}
}

void UBarkSubsystem::HandleVoiceFinished(UAudioComponent* Voice)
{
	if (Voice != ActiveVoice.Get()) return;

	if (const UWorld* World = GetWorld())
		LastVoiceEndTime = World->GetTimeSeconds();
	ActiveVoice.Reset();
}

void UBarkSubsystem::ShowBarkScreenMessage(const AActor* Speaker, uint16 TypeKey, const FString& Message, const FColor& Color)
{
	if (!GEngine) return;

	// Unique key per speaker+type so rapid-fire spam coalesces into a single on-screen slot
	const uint64 Key = GetTypeHash(Speaker) ^ (static_cast<uint64>(TypeKey) << 32);
	GEngine->AddOnScreenDebugMessage(static_cast<int32>(Key & 0x7FFFFFFF), 4.f, Color, Message);
}
