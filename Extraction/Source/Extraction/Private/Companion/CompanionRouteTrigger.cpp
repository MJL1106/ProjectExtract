// ACompanionRouteTrigger — overlap volume that fires a companion route.

#include "Companion/CompanionRouteTrigger.h"
#include "Components/BoxComponent.h"
#include "Components/BillboardComponent.h"
#include "Companion/CompanionRoute.h"
#include "Companion/CompanionCharacter.h"
#include "AI/CompanionAIController.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"

DEFINE_LOG_CATEGORY_STATIC(LogCompanionRouteTrigger, Log, All);

// ---------------------------------------------------------------------

ACompanionRouteTrigger::ACompanionRouteTrigger()
{
	PrimaryActorTick.bCanEverTick = false;

	TriggerBox = CreateDefaultSubobject<UBoxComponent>(TEXT("TriggerBox"));
	TriggerBox->SetCollisionProfileName(TEXT("Trigger"));
	TriggerBox->InitBoxExtent(FVector(150.f, 150.f, 100.f));
	SetRootComponent(TriggerBox);

	Billboard = CreateDefaultSubobject<UBillboardComponent>(TEXT("Billboard"));
	Billboard->SetupAttachment(RootComponent);
	Billboard->SetHiddenInGame(true);
}

// ---------------------------------------------------------------------

void ACompanionRouteTrigger::BeginPlay()
{
	Super::BeginPlay();

	TriggerBox->OnComponentBeginOverlap.AddDynamic(this, &ACompanionRouteTrigger::OnTriggerOverlap);
}

void ACompanionRouteTrigger::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	TriggerBox->OnComponentBeginOverlap.RemoveDynamic(this, &ACompanionRouteTrigger::OnTriggerOverlap);
	Super::EndPlay(EndPlayReason);
}

// ---------------------------------------------------------------------

void ACompanionRouteTrigger::OnTriggerOverlap(
	UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
	bool bFromSweep, const FHitResult& SweepResult)
{
	if (bOneShot && bHasFired) return;

	// Only fire for the player pawn.
	APawn* OtherPawn = Cast<APawn>(OtherActor);
	if (!IsValid(OtherPawn) || !OtherPawn->IsPlayerControlled()) return;

	// Find the single companion in the world.
	const UWorld* World = GetWorld();
	if (!IsValid(World)) return;

	// Routes are player-story choreography -- primary companion only.
	ACompanionAIController* CompanionController = nullptr;
	if (ACompanionCharacter* Companion = ACompanionCharacter::GetPrimaryCompanion(GetWorld()))
		CompanionController = Companion->GetController<ACompanionAIController>();

	if (!IsValid(CompanionController))
	{
		UE_LOG(LogCompanionRouteTrigger, Warning, TEXT("%s: No CompanionAIController found — route not started."), *GetName());
		return;
	}

	// Checked below the controller resolve, not above it: Clear Facing Reference is an uninstall and
	// is meant to be placed with Route deliberately left unset.
	const bool bNeedsRoute = (Mode != ECompanionRouteTriggerMode::ClearFacingReference);
	if (bNeedsRoute && !IsValid(Route))
	{
		UE_LOG(LogCompanionRouteTrigger, Warning, TEXT("%s: Route is not set — trigger ignored."), *GetName());
		return;
	}

	switch (Mode)
	{
	case ECompanionRouteTriggerMode::ExecuteRoute:
		CompanionController->StartRoute(Route);
		break;

	case ECompanionRouteTriggerMode::SetFacingReference:
		CompanionController->SetFacingReferenceRoute(Route);
		break;

	case ECompanionRouteTriggerMode::ClearFacingReference:
		CompanionController->ClearFacingReferenceRoute();
		break;
	}

	if (bOneShot)
	{
		bHasFired = true;
		TriggerBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
}
