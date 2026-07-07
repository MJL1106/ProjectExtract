// ABreachableDoor — simple hinged door with IBreachable.

#include "World/BreachableDoor.h"
#include "Game/MissionInventorySubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY_STATIC(LogBreachableDoor, Log, All);

ABreachableDoor::ABreachableDoor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false; // only ticks while opening

	HingeRoot = CreateDefaultSubobject<USceneComponent>(TEXT("HingeRoot"));
	SetRootComponent(HingeRoot);

	LeafPivot = CreateDefaultSubobject<USceneComponent>(TEXT("LeafPivot"));
	LeafPivot->SetupAttachment(HingeRoot);

	DoorMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DoorMesh"));
	DoorMesh->SetupAttachment(LeafPivot);
	// Ensure the door is visible to Visibility traces (camera ping).
	DoorMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	DoorMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
}

void ABreachableDoor::BeginPlay()
{
	Super::BeginPlay();

	// Self-heal: instances placed before LeafPivot existed kept DoorMesh attached to
	// HingeRoot. Re-attach under LeafPivot so the breach swing actually moves the panel.
	if (DoorMesh && LeafPivot && DoorMesh->GetAttachParent() != LeafPivot)
	{
		DoorMesh->AttachToComponent(LeafPivot, FAttachmentTransformRules::KeepRelativeTransform);
	}

	ClosedYaw = LeafPivot->GetComponentRotation().Yaw;

	// Fail loud: a locked door with no keycard id can never be opened by anything.
	if (bStartsLocked && RequiredKeycardId.IsNone())
		UE_LOG(LogBreachableDoor, Warning,
			TEXT("%s: bStartsLocked with no RequiredKeycardId — this door is permanently sealed. Set the id or unlock it."),
			*GetName());
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

	FRotator NewRot = LeafPivot->GetComponentRotation();
	NewRot.Yaw = CurrentYaw;
	LeafPivot->SetWorldRotation(NewRot);

	if (Alpha >= 1.f) FinishSwing();
}

// --- IBreachable ---

bool ABreachableDoor::CanBreach_Implementation() const
{
	// Locked doors aren't offered for companion breach — unlock with the keycard first.
	return DoorState == EDoorState::Closed && !IsLocked();
}

void ABreachableDoor::TryUnlock(AActor* UnlockInstigator)
{
	if (!IsLocked() || DoorState != EDoorState::Closed) return;

	UWorld* World = GetWorld();
	UMissionInventorySubsystem* Subsystem = World ? World->GetSubsystem<UMissionInventorySubsystem>() : nullptr;
	if (!Subsystem) return;

	if (!Subsystem->HasKeycard(RequiredKeycardId))
	{
		Subsystem->OnLootNotify.Broadcast(FText::Format(
			NSLOCTEXT("Door", "RequiresKeycard", "Locked (requires keycard: {0})"),
			FText::FromName(RequiredKeycardId)));
		return;
	}

	bUnlocked = true;
	Subsystem->OnLootNotify.Broadcast(NSLOCTEXT("Door", "Unlocked", "Door unlocked"));
	UE_LOG(LogBreachableDoor, Log, TEXT("%s: unlocked by %s with keycard %s"),
		*GetName(), *GetNameSafe(UnlockInstigator), *RequiredKeycardId.ToString());
	BeginSwing();
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
	ClosedYaw = LeafPivot->GetComponentRotation().Yaw;
	SetActorTickEnabled(true);
}

void ABreachableDoor::FinishSwing()
{
	DoorState = EDoorState::Open;
	SetActorTickEnabled(false);

	// Snap to exact final rotation.
	FRotator FinalRot = LeafPivot->GetComponentRotation();
	FinalRot.Yaw = ClosedYaw + OpenAngle;
	LeafPivot->SetWorldRotation(FinalRot);

	UE_LOG(LogBreachableDoor, Log, TEXT("%s: Swing complete — door is now Open"), *GetName());
}
