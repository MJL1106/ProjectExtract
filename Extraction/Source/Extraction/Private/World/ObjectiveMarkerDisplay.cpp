// AObjectiveMarkerDisplay implementation.

#include "World/ObjectiveMarkerDisplay.h"
#include "UI/ObjectiveMarker3DWidget.h"
#include "Components/WidgetComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"

AObjectiveMarkerDisplay::AObjectiveMarkerDisplay()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;
	bReplicates = false;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	MarkerWidgetComponent = CreateDefaultSubobject<UWidgetComponent>(TEXT("MarkerWidget"));
	MarkerWidgetComponent->SetupAttachment(Root);
	MarkerWidgetComponent->SetWidgetSpace(EWidgetSpace::World);
	MarkerWidgetComponent->SetDrawAtDesiredSize(true);
	MarkerWidgetComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MarkerWidgetComponent->SetGenerateOverlapEvents(false);
	MarkerWidgetComponent->SetTwoSided(true);
}

void AObjectiveMarkerDisplay::InitObjective(const FObjectiveMarker& InObjective)
{
	Objective = InObjective;

	// Re-push label to the widget -- BeginPlay runs inline during SpawnActor with a
	// default-constructed (empty) Objective, so the initial SetLabel collapses the block.
	if (UObjectiveMarker3DWidget* Widget = CachedWidget.Get())
		Widget->SetLabel(Objective.Label);

	// Force distance refresh on the next Tick.
	LastDisplayedDistanceMetres = INDEX_NONE;
}

void AObjectiveMarkerDisplay::BeginPlay()
{
	Super::BeginPlay();

	if (!WidgetClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("AObjectiveMarkerDisplay: WidgetClass is null -- "
			"marker will be invisible. Assign it in the BP subclass defaults."));
	}
	else
	{
		MarkerWidgetComponent->SetWidgetClass(WidgetClass);
		MarkerWidgetComponent->InitWidget();
	}

	// Cache the widget instance for distance updates.
	UUserWidget* RawWidget = MarkerWidgetComponent->GetWidget();
	CachedWidget = Cast<UObjectiveMarker3DWidget>(RawWidget);

	if (UObjectiveMarker3DWidget* Widget = CachedWidget.Get())
		Widget->SetLabel(Objective.Label);
}

void AObjectiveMarkerDisplay::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Resolve target location (handles moving targets).
	const FVector ResolvedLocation = Objective.ResolveLocation();
	SetActorLocation(ResolvedLocation);

	UpdateBillboardAndScale(DeltaSeconds);
	UpdateDistance();
}

void AObjectiveMarkerDisplay::UpdateBillboardAndScale(float DeltaSeconds)
{
	APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
	if (!IsValid(PC)) return;

	FVector CameraLocation;
	FRotator CameraRotation;
	PC->GetPlayerViewPoint(CameraLocation, CameraRotation);

	// Yaw-billboard toward camera (keep roll zero).
	const FVector ToCamera = CameraLocation - GetActorLocation();
	const FRotator LookAtRotation = ToCamera.Rotation();
	SetActorRotation(FRotator(0.f, LookAtRotation.Yaw, 0.f));

	// Distance-scale for consistent on-screen size.
	const float Distance = ToCamera.Size();
	const float RawScale = Distance / FMath::Max(ReferenceDistance, 1.f);
	const float ClampedScale = FMath::Clamp(RawScale, MinScale, MaxScale);
	SetActorScale3D(FVector(ClampedScale));
}

void AObjectiveMarkerDisplay::UpdateDistance()
{
	APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
	if (!IsValid(PC)) return;

	APawn* Pawn = PC->GetPawn();
	if (!IsValid(Pawn)) return;

	const int32 RoundedMetres = FMath::RoundToInt(
		FVector::Dist(Pawn->GetActorLocation(), GetActorLocation()) / 100.f);
	if (RoundedMetres == LastDisplayedDistanceMetres) return;

	LastDisplayedDistanceMetres = RoundedMetres;

	if (UObjectiveMarker3DWidget* Widget = CachedWidget.Get())
		Widget->SetDistance(RoundedMetres);
}
