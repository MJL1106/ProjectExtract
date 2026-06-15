// UBarkSubsystem — bark routing with dedup.

#include "BarkSubsystem.h"
#include "BarkSetData.h"
#include "EnemyDebug.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "DrawDebugHelpers.h"

DEFINE_LOG_CATEGORY(LogEnemyBark);

void UBarkSubsystem::RequestBark(const AActor* Speaker, const UBarkSetData* BarkSet, EBarkType Type, const FText& SpeakerName)
{
	if (!IsValid(Speaker) || !IsValid(BarkSet)) return;

	const bool bDebug = IsEnemyBarkDebugEnabled();

	const FBarkDefinition* Def = BarkSet->Barks.Find(Type);
	if (!Def || Def->Lines.Num() == 0)
	{
		if (bDebug)
		{
			const FString TypeStr = UEnum::GetValueAsString(Type);
			const FString SpeakerStr = SpeakerName.ToString();
			UE_LOG(LogEnemyBark, Warning, TEXT("DROP NoLines type=%s speaker=%s"), *TypeStr, *SpeakerStr);
			ShowBarkScreenMessage(Speaker, Type, FString::Printf(TEXT("DROP NoLines %s [%s]"), *TypeStr, *SpeakerStr), FColor::Silver);
		}
		return;
	}

	const UWorld* World = GetWorld();
	if (!IsValid(World)) return;
	const float Now = World->GetTimeSeconds();

	if (const float* GlobalLast = LastBarkTimePerType.Find(Type))
	{
		const float Since = Now - *GlobalLast;
		if (Since < GlobalTypeDedupWindow)
		{
			if (bDebug)
			{
				const FString TypeStr = UEnum::GetValueAsString(Type);
				UE_LOG(LogEnemyBark, Log, TEXT("DROP GlobalDedup type=%s (since=%.2fs < window=%.2fs)"), *TypeStr, Since, GlobalTypeDedupWindow);
				ShowBarkScreenMessage(Speaker, Type, FString::Printf(TEXT("DROP GlobalDedup %s (%.1fs<%.1fs)"), *TypeStr, Since, GlobalTypeDedupWindow), FColor::Silver);
			}
			return;
		}
	}

	const TPair<FObjectKey, uint8> SpeakerKey(FObjectKey(Speaker), static_cast<uint8>(Type));
	if (const float* SpeakerLast = LastBarkTimePerSpeaker.Find(SpeakerKey))
	{
		const float Since = Now - *SpeakerLast;
		if (Since < Def->CooldownSeconds)
		{
			if (bDebug)
			{
				const FString TypeStr = UEnum::GetValueAsString(Type);
				const FString SpeakerStr = SpeakerName.ToString();
				UE_LOG(LogEnemyBark, Log, TEXT("DROP Cooldown type=%s speaker=%s (since=%.2fs < cd=%.2fs)"), *TypeStr, *SpeakerStr, Since, Def->CooldownSeconds);
				ShowBarkScreenMessage(Speaker, Type, FString::Printf(TEXT("DROP CD %s [%s] (%.1fs<%.1fs)"), *TypeStr, *SpeakerStr, Since, Def->CooldownSeconds), FColor::Silver);
			}
			return;
		}
	}

	LastBarkTimePerType.Add(Type, Now);
	LastBarkTimePerSpeaker.Add(SpeakerKey, Now);

	const FText& Line = Def->Lines[FMath::RandRange(0, Def->Lines.Num() - 1)];
	OnBarkRequested.Broadcast(SpeakerName, Line);

	if (bDebug)
	{
		const FString TypeStr = UEnum::GetValueAsString(Type);
		const FString SpeakerStr = SpeakerName.ToString();
		UE_LOG(LogEnemyBark, Log, TEXT("FIRED type=%s speaker=%s line=\"%s\""), *TypeStr, *SpeakerStr, *Line.ToString());
		ShowBarkScreenMessage(Speaker, Type, FString::Printf(TEXT("FIRED %s [%s] \"%s\""), *TypeStr, *SpeakerStr, *Line.ToString()), FColor::Green);

		// Draw text above speaker's head in-world for 2 seconds
		if (IsValid(World))
		{
			const FVector HeadLocation = Speaker->GetActorLocation() + FVector(0.f, 0.f, 100.f);
			DrawDebugString(World, HeadLocation, FString::Printf(TEXT("[%s] %s"), *TypeStr, *Line.ToString()), nullptr, FColor::Green, 2.f, true);
		}
	}

	if (IsValid(Def->Sound))
		UGameplayStatics::PlaySoundAtLocation(World, Def->Sound, Speaker->GetActorLocation());
}

void UBarkSubsystem::ShowBarkScreenMessage(const AActor* Speaker, EBarkType Type, const FString& Message, const FColor& Color)
{
	if (!GEngine) return;

	// Unique key per speaker+type so rapid-fire spam coalesces into a single on-screen slot
	const uint64 Key = GetTypeHash(Speaker) ^ (static_cast<uint64>(Type) << 32);
	GEngine->AddOnScreenDebugMessage(static_cast<int32>(Key & 0x7FFFFFFF), 4.f, Color, Message);
}
