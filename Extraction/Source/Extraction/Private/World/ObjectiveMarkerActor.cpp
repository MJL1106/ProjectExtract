// AObjectiveMarkerActor implementation.

#include "World/ObjectiveMarkerActor.h"
#include "Game/ObjectiveSubsystem.h"
#include "Game/MissionInventorySubsystem.h"
#include "Character/ExtractionPlayer.h"
#include "Components/SphereComponent.h"
#include "Engine/World.h"

AObjectiveMarkerActor::AObjectiveMarkerActor()
{
	PrimaryActorTick.bCanEverTick = false;

	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));

	CompletionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("CompletionSphere"));
	CompletionSphere->SetupAttachment(GetRootComponent());
	CompletionSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CompletionSphere->SetCollisionObjectType(ECC_WorldDynamic);
	CompletionSphere->SetCollisionResponseToAllChannels(ECR_Ignore);
	CompletionSphere->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
}

void AObjectiveMarkerActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// Keep the editor-viewport sphere matching CompletionRadius for designer visualisation.
	CompletionSphere->SetSphereRadius(FMath::Max(CompletionRadius, 0.f));
}

void AObjectiveMarkerActor::BeginPlay()
{
	Super::BeginPlay();

	CompletionSphere->OnComponentBeginOverlap.AddDynamic(this, &AObjectiveMarkerActor::OnCompletionSphereOverlap);

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
	if (bCompleted) return;

	UWorld* World = GetWorld();
	UObjectiveSubsystem* Objectives = World ? World->GetSubsystem<UObjectiveSubsystem>() : nullptr;
	if (!Objectives) return;

	// Card already held — complete straight away without flashing a marker for one frame.
	if (RequiredKeycardId != NAME_None)
	{
		const UMissionInventorySubsystem* Inventory = World->GetSubsystem<UMissionInventorySubsystem>();
		if (Inventory && Inventory->HasKeycard(RequiredKeycardId))
		{
			Complete();
			return;
		}
	}

	// Static marker (no target actor): the placed location is WYSIWYG. Passing `this` would pull
	// the resolved height off the actor's bounds, which the completion sphere radius inflates.
	Objectives->AddObjective(GetEffectiveId(), Label, GetActorLocation());
	bActive = true;

	// Arm location completion.
	if (CompletionRadius > 0.f)
	{
		CompletionSphere->SetSphereRadius(CompletionRadius);
		CompletionSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	}

	// Arm item completion.
	if (RequiredKeycardId != NAME_None)
	{
		if (UMissionInventorySubsystem* Inventory = World->GetSubsystem<UMissionInventorySubsystem>())
		{
			if (!Inventory->OnKeycardRecorded.IsAlreadyBound(this, &AObjectiveMarkerActor::OnKeycardRecorded))
				Inventory->OnKeycardRecorded.AddDynamic(this, &AObjectiveMarkerActor::OnKeycardRecorded);
		}
	}
}

void AObjectiveMarkerActor::Deactivate()
{
	bActive = false;
	CompletionSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	UWorld* World = GetWorld();
	if (!World) return;

	if (UMissionInventorySubsystem* Inventory = World->GetSubsystem<UMissionInventorySubsystem>())
		Inventory->OnKeycardRecorded.RemoveDynamic(this, &AObjectiveMarkerActor::OnKeycardRecorded);

	if (UObjectiveSubsystem* Objectives = World->GetSubsystem<UObjectiveSubsystem>())
		Objectives->RemoveObjective(GetEffectiveId());
}

void AObjectiveMarkerActor::OnCompletionSphereOverlap(UPrimitiveComponent* /*OverlappedComponent*/,
	AActor* OtherActor, UPrimitiveComponent* /*OtherComp*/, int32 /*OtherBodyIndex*/,
	bool /*bFromSweep*/, const FHitResult& /*SweepResult*/)
{
	if (!bActive || !Cast<AExtractionPlayer>(OtherActor)) return;
	Complete();
}

void AObjectiveMarkerActor::OnKeycardRecorded(FName KeycardId)
{
	if (!bActive || KeycardId != RequiredKeycardId) return;
	Complete();
}

void AObjectiveMarkerActor::Complete()
{
	if (bCompleted) return;
	bCompleted = true;

	if (bToastOnComplete)
	{
		UWorld* World = GetWorld();
		if (UMissionInventorySubsystem* Inventory = World ? World->GetSubsystem<UMissionInventorySubsystem>() : nullptr)
			Inventory->OnLootNotify.Broadcast(Label.IsEmpty()
				? NSLOCTEXT("Objective", "CompletedPlain", "Objective complete")
				: FText::Format(NSLOCTEXT("Objective", "Completed", "Objective complete: {0}"), Label));
	}

	// Deactivate BEFORE the broadcast: a chained handler that activates the next objective under
	// the same id must not have its fresh registration wiped by this marker's RemoveObjective.
	Deactivate();
	OnObjectiveCompleted.Broadcast();
}
