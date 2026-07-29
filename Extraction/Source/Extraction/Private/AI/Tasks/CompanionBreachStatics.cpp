// Shared breach-door mechanics for the companion command tasks.

#include "CompanionBreachStatics.h"
#include "AI/CompanionAIController.h"
#include "AI/CompanionTuningDataAsset.h"
#include "Companion/CompanionCharacter.h"
#include "World/Breachable.h"
#include "World/DoorBase.h"
#include "Audio/GameAudioSubsystem.h"
#include "Audio/SurfaceAudioBank.h"
#include "Perception/AISense_Hearing.h"

FVector CompanionBreachStatics::ResolveInteriorAnchor(AActor* Door, APawn* Breacher)
{
	FVector Anchor = IsValid(Door) ? Door->GetActorLocation() : FVector::ZeroVector;
	if (!IsValid(Door)) return Anchor;

	if (!IBreachable::Execute_GetPostBreachPoint(Door, Breacher, true, Anchor))
	{
		const ADoorBase* DoorBase = Cast<ADoorBase>(Door);
		Anchor = DoorBase ? DoorBase->GetAcousticPortalPoint() : Door->GetActorLocation();
	}
	return Anchor;
}

bool CompanionBreachStatics::OpenBreachDoor(ACompanionAIController* AIC, AActor* Door,
	EBreachType BreachType, const FVector& SearchRoomAnchor, float SearchRoomExposureRadius)
{
	if (!IsValid(AIC) || !IsValid(Door)) return false;

	// The door may have been opened by something else during the wind-up — swing/noise only once.
	if (!IBreachable::Execute_CanBreach(Door)) return false;

	if (ACompanionCharacter* Companion = Cast<ACompanionCharacter>(AIC->GetPawn()))
	{
		Companion->BeginSearchRoomExposure(SearchRoomAnchor, SearchRoomExposureRadius,
			BreachType == EBreachType::Quiet);
	}

	IBreachable::Execute_Breach(Door, AIC->GetPawn());

	// Doorway centre, not actor location — the swing-door root is the hinge, which can sit
	// inside the frame/wall and wrongly read as occluded to acoustic listener traces.
	const ADoorBase* DoorBase = Cast<ADoorBase>(Door);
	const FVector NoiseLocation = DoorBase ? DoorBase->GetAcousticPortalPoint() : Door->GetActorLocation();

	const UCompanionTuningDataAsset* Tuning = AIC->GetTuning();
	const FBreachNoiseProfile* Profile = Tuning ? Tuning->BreachNoise.Find(BreachType) : nullptr;
	if (Profile && Profile->Loudness > 0.f && Profile->MaxRange > 0.f)
	{
		UAISense_Hearing::ReportNoiseEvent(AIC->GetWorld(), NoiseLocation,
			Profile->Loudness, AIC->GetPawn(), Profile->MaxRange, TEXT("Breach"));
	}

	// Per-type breach accent (slam / latch / slow slide) layered over the door's own OpenSound,
	// plus the body-into-door thump — only the forced open path gets it, never a passive auto-open.
	if (UGameAudioSubsystem* AudioSys = AIC->GetWorld()->GetSubsystem<UGameAudioSubsystem>())
	{
		if (const USurfaceAudioBank* Bank = AudioSys->GetBank())
		{
			AudioSys->PlayAt(Bank->BreachSounds.FindRef(BreachType), NoiseLocation);

			const float* ImpactVolume = Bank->BreachImpactVolumes.Find(BreachType);
			AudioSys->PlayAt(Bank->BreachImpact, NoiseLocation,
				ImpactVolume ? *ImpactVolume : Bank->BreachImpactVolume);
		}
	}

	return true;
}
