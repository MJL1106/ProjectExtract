// ABreachableDoor — simple hinged door with IBreachable.

#include "World/BreachableDoor.h"
#include "Components/StaticMeshComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogBreachableDoor, Log, All);

ABreachableDoor::ABreachableDoor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false; // only ticks while opening

	HingeRoot = CreateDefaultSubobject<USceneComponent>(TEXT("HingeRoot"));
	SetRootComponent(HingeRoot);

	DoorMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DoorMesh"));
	DoorMesh->SetupAttachment(HingeRoot);
	// Ensure the door is visible to Visibility traces (camera ping).
	DoorMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	DoorMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
}

void ABreachableDoor::BeginPlay()
{
	Super::BeginPlay();
	ClosedYaw = HingeRoot->GetComponentRotation().Yaw;
}

void ABreachableDoor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// No FTimerHandles to clear — swing is driven by gated Tick.
	Super::EndPlay(EndPlayReason);
}

void ABreachableDoor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (DoorState != EDoorState::Opening) return;

	SwingElapsed += DeltaSeconds;
	const float Alpha = FMath::Clamp(SwingElapsed / OpenDuration, 0.f, 1.f);
	const float EasedAlpha = FMath::InterpEaseInOut(0.f, 1.f, Alpha, 2.f);
	const float CurrentYaw = ClosedYaw + OpenAngle * EasedAlpha;

	FRotator NewRot = HingeRoot->GetComponentRotation();
	NewRot.Yaw = CurrentYaw;
	HingeRoot->SetWorldRotation(NewRot);

	if (Alpha >= 1.f) FinishSwing();
}

// --- IBreachable ---

bool ABreachableDoor::CanBreach_Implementation() const
{
	return DoorState == EDoorState::Closed;
}

void ABreachableDoor::Breach_Implementation(AActor* Breacher)
{
	if (DoorState != EDoorState::Closed)
	{
		UE_LOG(LogBreachableDoor, Warning, TEXT("%s: Breach called but door is not Closed (state=%d)"),
			*GetName(), static_cast<uint8>(DoorState));
		return;
	}

	UE_LOG(LogBreachableDoor, Log, TEXT("%s: Breach by %s — beginning swing (angle=%.1f dur=%.2fs)"),
		*GetName(), *GetNameSafe(Breacher), OpenAngle, OpenDuration);

	BeginSwing();
}

// --- Internal ---

void ABreachableDoor::BeginSwing()
{
	DoorState = EDoorState::Opening;
	SwingElapsed = 0.f;
	ClosedYaw = HingeRoot->GetComponentRotation().Yaw;
	SetActorTickEnabled(true);
}

void ABreachableDoor::FinishSwing()
{
	DoorState = EDoorState::Open;
	SetActorTickEnabled(false);

	// Snap to exact final rotation.
	FRotator FinalRot = HingeRoot->GetComponentRotation();
	FinalRot.Yaw = ClosedYaw + OpenAngle;
	HingeRoot->SetWorldRotation(FinalRot);

	UE_LOG(LogBreachableDoor, Log, TEXT("%s: Swing complete — door is now Open"), *GetName());
}
