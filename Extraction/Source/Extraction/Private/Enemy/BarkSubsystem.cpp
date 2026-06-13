// UBarkSubsystem — bark routing with dedup.

#include "BarkSubsystem.h"
#include "BarkSetData.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "Engine/World.h"

void UBarkSubsystem::RequestBark(const AActor* Speaker, const UBarkSetData* BarkSet, EBarkType Type, const FText& SpeakerName)
{
	if (!IsValid(Speaker) || !IsValid(BarkSet)) return;

	const FBarkDefinition* Def = BarkSet->Barks.Find(Type);
	if (!Def || Def->Lines.Num() == 0) return;

	const UWorld* World = GetWorld();
	if (!IsValid(World)) return;
	const float Now = World->GetTimeSeconds();

	if (const float* GlobalLast = LastBarkTimePerType.Find(Type))
		if (Now - *GlobalLast < GlobalTypeDedupWindow) return;

	const TPair<FObjectKey, uint8> SpeakerKey(FObjectKey(Speaker), static_cast<uint8>(Type));
	if (const float* SpeakerLast = LastBarkTimePerSpeaker.Find(SpeakerKey))
		if (Now - *SpeakerLast < Def->CooldownSeconds) return;

	LastBarkTimePerType.Add(Type, Now);
	LastBarkTimePerSpeaker.Add(SpeakerKey, Now);

	const FText& Line = Def->Lines[FMath::RandRange(0, Def->Lines.Num() - 1)];
	OnBarkRequested.Broadcast(SpeakerName, Line);

	if (IsValid(Def->Sound))
		UGameplayStatics::PlaySoundAtLocation(World, Def->Sound, Speaker->GetActorLocation());
}
