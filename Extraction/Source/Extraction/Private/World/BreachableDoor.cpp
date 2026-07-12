// ABreachableDoor — simple hinged door with IBreachable.

#include "World/BreachableDoor.h"
#include "Game/MissionInventorySubsystem.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"

DEFINE_LOG_CATEGORY_STATIC(LogBreachableDoor, Log, All);

ABreachableDoor::ABreachableDoor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false; // only ticks while opening

	HingeRoot = CreateDefaultSubobject<USceneComponent>(TEXT("HingeRoot"));
	SetRootComponent(HingeRoot);
	DoorwayTrigger->SetupAttachment(HingeRoot);

	LeafPivot = CreateDefaultSubobject<USceneComponent>(TEXT("LeafPivot"));
	LeafPivot->SetupAttachment(HingeRoot);

	DoorMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DoorMesh"));
	DoorMesh->SetupAttachment(LeafPivot);
	// Ensure the door is visible to Visibility traces (camera ping).
	DoorMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	DoorMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	// The navmesh flows through the doorway — the closed leaf blocks pawns physically, not the
	// path. Locked doors re-enable this at BeginPlay so enemies route around them instead.
	DoorMesh->SetCanEverAffectNavigation(false);
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

	// Snapshot the doorway frame while the leaf is guaranteed closed (see header note) — the
	// panel's live bounds rotate with the swing, so post-breach geometry must come from this.
	if (DoorMesh)
	{
		const FBoxSphereBounds LocalBounds = DoorMesh->CalcBounds(DoorMesh->GetComponentTransform() * GetActorTransform().Inverse());
		PanelClosedCenter = GetActorTransform().TransformPosition(LocalBounds.Origin);
		PanelHalfThicknessCached = FMath::Min(LocalBounds.BoxExtent.X, LocalBounds.BoxExtent.Y);
		bPanelThinAxisX = LocalBounds.BoxExtent.X <= LocalBounds.BoxExtent.Y;

		// Where the panel centre ends up at full open: rotated around the hinge by OpenAngle.
		const FVector Hinge = LeafPivot->GetComponentLocation();
		PanelOpenCenter = Hinge + (PanelClosedCenter - Hinge).RotateAngleAxis(OpenAngle, FVector::UpVector);
	}

	// Locked doors carve the navmesh (dynamic runtime generation) so enemies route around them
	// rather than pathing into a door they can't open; unlocked doors are nav-transparent.
	// Set unconditionally on the instance: a serialized BP/instance value of the EditAnywhere
	// bCanEverAffectNavigation flag would silently mask the constructor default otherwise.
	if (DoorMesh)
		DoorMesh->SetCanEverAffectNavigation(IsLocked());

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
	if (IsExternalGateLocked()) return false;
	// Locked doors aren't offered for companion breach — unlock with the keycard first.
	return DoorState == EDoorState::Closed && !IsLocked();
}

void ABreachableDoor::TryUnlock(AActor* UnlockInstigator)
{
	if (IsExternalGateLocked()) return;
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

bool ABreachableDoor::GetBreachStandPoint_Implementation(const AActor* Breacher, FVector& OutLocation, FRotator& OutFacing) const
{
	if (!IsValid(Breacher) || !DoorMesh || DoorState != EDoorState::Closed) return false;

	// The door panel is thin: its plane normal is whichever horizontal actor axis spans the
	// smaller extent of the panel. Measured in actor space so a yawed placement can't inflate
	// the world AABB and flip the axis pick.
	const FBoxSphereBounds MeshBounds = DoorMesh->CalcBounds(DoorMesh->GetComponentTransform() * GetActorTransform().Inverse());
	const FVector Center = GetActorTransform().TransformPosition(MeshBounds.Origin);
	const FVector LocalExtent = MeshBounds.BoxExtent;

	FVector Normal = (LocalExtent.X <= LocalExtent.Y) ? GetActorForwardVector() : GetActorRightVector();
	const float PanelHalfThickness = FMath::Min(LocalExtent.X, LocalExtent.Y);

	// Flip the normal onto the breacher's side of the door.
	if (FVector::DotProduct(Breacher->GetActorLocation() - Center, Normal) < 0.f)
		Normal = -Normal;

	// Standoff adapts to the door and the breacher: panel surface + capsule + reach gap.
	float CapsuleRadius = 35.f;
	if (const ACharacter* Character = Cast<ACharacter>(Breacher))
		if (const UCapsuleComponent* Capsule = Character->GetCapsuleComponent())
			CapsuleRadius = Capsule->GetScaledCapsuleRadius();

	OutLocation = Center + Normal * (PanelHalfThickness + CapsuleRadius + BreachReachGap);
	OutLocation.Z = Breacher->GetActorLocation().Z; // caller keeps its own floor height
	OutFacing = (-Normal).Rotation();
	return true;
}

bool ABreachableDoor::GetPostBreachPoint_Implementation(const AActor* Breacher, bool bEnterRoom, FVector& OutLocation) const
{
	if (!IsValid(Breacher) || !DoorMesh) return false;

	// Doorway frame from the BeginPlay snapshot — the live panel bounds have rotated by now.
	FVector Normal = bPanelThinAxisX ? GetActorForwardVector() : GetActorRightVector();
	if (FVector::DotProduct(Breacher->GetActorLocation() - PanelClosedCenter, Normal) < 0.f)
		Normal = -Normal;
	const FVector Lateral = FVector::CrossProduct(FVector::UpVector, Normal).GetSafeNormal();

	// The open leaf occupies the hinge-side region (inside or outside depending on swing
	// direction) — both points step laterally AWAY from wherever it ends up.
	const float LeafSide = FVector::DotProduct(PanelOpenCenter - PanelClosedCenter, Lateral);
	const float AwaySign = (LeafSide >= 0.f) ? -1.f : 1.f;

	if (bEnterRoom)
		return PickNavigableEnterPoint(PanelClosedCenter, Normal, Lateral, AwaySign, Breacher->GetActorLocation().Z, OutLocation);

	float CapsuleRadius = 35.f;
	if (const ACharacter* Character = Cast<ACharacter>(Breacher))
		if (const UCapsuleComponent* Capsule = Character->GetCapsuleComponent())
			CapsuleRadius = Capsule->GetScaledCapsuleRadius();

	const float Standoff = PanelHalfThicknessCached + CapsuleRadius + BreachReachGap;
	OutLocation = PanelClosedCenter + Normal * (Standoff + 40.f) + Lateral * AwaySign * PostBreachLateralOffset;
	OutLocation.Z = Breacher->GetActorLocation().Z;
	return true;
}

void ABreachableDoor::Breach_Implementation(AActor* Breacher)
{
	if (IsExternalGateLocked()) return;

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

void ABreachableDoor::ForceOpenInstant()
{
	if (DoorState == EDoorState::Open) return;

	bUnlocked = true;
	if (DoorState == EDoorState::Closed)
		BeginSwing(); // captures ClosedYaw + drops pawn blocking/nav
	FinishSwing();
	UE_LOG(LogBreachableDoor, Log, TEXT("%s: ForceOpenInstant (checkpoint fast-forward)"), *GetName());
}

// --- Internal ---

void ABreachableDoor::BeginSwing()
{
	DoorState = EDoorState::Opening;
	SwingElapsed = 0.f;
	ClosedYaw = LeafPivot->GetComponentRotation().Yaw;
	SetActorTickEnabled(true);

	// The leaf stops blocking pawns for good once it starts moving: an arriving pawn walks
	// through mid-swing without a path stall, and a leaf swinging toward the opener clips
	// past instead of launching it. Doors never close, so there is nothing to restore. Nav
	// relevance drops too — a formerly-locked leaf must not keep carving at its open rest.
	if (DoorMesh)
	{
		DoorMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
		DoorMesh->SetCanEverAffectNavigation(false);
	}
}

void ABreachableDoor::FinishSwing()
{
	DoorState = EDoorState::Open;
	SetActorTickEnabled(false);

	// Snap to exact final rotation.
	FRotator FinalRot = LeafPivot->GetComponentRotation();
	FinalRot.Yaw = ClosedYaw + OpenAngle;
	LeafPivot->SetWorldRotation(FinalRot);
	BroadcastDoorOpenedOnce();

	UE_LOG(LogBreachableDoor, Log, TEXT("%s: Swing complete — door is now Open"), *GetName());
}
