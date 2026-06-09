// UFootstepNoiseComponent — distance-accumulating AI-hearing noise emitter.

#include "FootstepNoiseComponent.h"
#include "Character/ExtractionPlayerInterface.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Perception/AISense_Hearing.h"
#include "TimerManager.h"
#include "Engine/World.h"

UFootstepNoiseComponent::UFootstepNoiseComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UFootstepNoiseComponent::BeginPlay()
{
	Super::BeginPlay();

	const AActor* Owner = GetOwner();
	if (!IsValid(Owner) || !Owner->HasAuthority()) return;

	UWorld* World = GetWorld();
	if (!IsValid(World)) return;

	LastLocation = Owner->GetActorLocation();

	const float InitialDelay = FMath::RandRange(0.f, UpdateInterval);
	World->GetTimerManager().SetTimer(
		NoiseTimerHandle,
		this,
		&UFootstepNoiseComponent::TickNoise,
		UpdateInterval,
		true,
		InitialDelay);
}

void UFootstepNoiseComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (const UWorld* World = GetWorld())
		World->GetTimerManager().ClearAllTimersForObject(this);

	Super::EndPlay(EndPlayReason);
}

void UFootstepNoiseComponent::TickNoise()
{
	ACharacter* OwnerChar = Cast<ACharacter>(GetOwner());
	if (!IsValid(OwnerChar)) return;

	const FVector CurrentLocation = OwnerChar->GetActorLocation();
	const FVector Delta = CurrentLocation - LastLocation;
	LastLocation = CurrentLocation;

	const UCharacterMovementComponent* MoveComp = OwnerChar->GetCharacterMovement();
	if (!IsValid(MoveComp) || !MoveComp->IsMovingOnGround()) return;

	// A delta larger than any single interval of walking = teleport (TeleportVolume), not footsteps.
	if (Delta.Size2D() > StepDistance * 2.f)
	{
		AccumulatedDistance = 0.f;
		return;
	}

	AccumulatedDistance += Delta.Size2D();
	if (AccumulatedDistance < StepDistance) return;
	AccumulatedDistance = FMath::Fmod(AccumulatedDistance, StepDistance);

	float Loudness = 0.f;
	float Range = 0.f;
	PickNoiseProfile(Loudness, Range);
	if (Range <= 0.f || Loudness <= 0.f) return;

	UAISense_Hearing::ReportNoiseEvent(GetWorld(), CurrentLocation, Loudness, OwnerChar, Range, TEXT("Footstep"));
}

void UFootstepNoiseComponent::PickNoiseProfile(float& OutLoudness, float& OutRange) const
{
	const ACharacter* OwnerChar = Cast<ACharacter>(GetOwner());

	bool bQuiet = OwnerChar && OwnerChar->bIsCrouched;
	if (const IExtractionPlayerInterface* PlayerInterface = Cast<IExtractionPlayerInterface>(OwnerChar))
		bQuiet |= PlayerInterface->GetIsProne();

	if (bQuiet)
	{
		OutLoudness = QuietLoudness;
		OutRange = QuietRange;
		return;
	}

	const float Speed = OwnerChar ? OwnerChar->GetVelocity().Size2D() : 0.f;
	const bool bSprinting = Speed >= SprintSpeedThreshold;
	OutLoudness = bSprinting ? SprintLoudness : WalkLoudness;
	OutRange = bSprinting ? SprintRange : WalkRange;
}
