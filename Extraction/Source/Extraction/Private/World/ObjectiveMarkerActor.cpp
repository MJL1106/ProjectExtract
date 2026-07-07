// AObjectiveMarkerActor implementation.

#include "World/ObjectiveMarkerActor.h"
#include "Game/ObjectiveSubsystem.h"
#include "Engine/World.h"

AObjectiveMarkerActor::AObjectiveMarkerActor()
{
	PrimaryActorTick.bCanEverTick = false;

	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

void AObjectiveMarkerActor::BeginPlay()
{
	Super::BeginPlay();

	if (bActivateOnBeginPlay)
		Activate();
}

void AObjectiveMarkerActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Deactivate();
	Super::EndPlay(EndPlayReason);
}

void AObjectiveMarkerActor::Activate()
{
	UWorld* World = GetWorld();
	UObjectiveSubsystem* Subsystem = World ? World->GetSubsystem<UObjectiveSubsystem>() : nullptr;
	if (!Subsystem) return;

	Subsystem->AddObjective(GetEffectiveId(), Label, GetActorLocation(), this);
}

void AObjectiveMarkerActor::Deactivate()
{
	UWorld* World = GetWorld();
	UObjectiveSubsystem* Subsystem = World ? World->GetSubsystem<UObjectiveSubsystem>() : nullptr;
	if (!Subsystem) return;

	Subsystem->RemoveObjective(GetEffectiveId());
}
