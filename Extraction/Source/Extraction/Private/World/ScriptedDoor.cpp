// AScriptedDoor — Blueprint-animated door with C++ IBreachable state.

#include "World/ScriptedDoor.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogScriptedDoor, Log, All);

AScriptedDoor::AScriptedDoor()
{
	DoorRoot = CreateDefaultSubobject<USceneComponent>(TEXT("DoorRoot"));
	SetRootComponent(DoorRoot);
	DoorwayTrigger->SetupAttachment(DoorRoot);
	// Static, not the USceneComponent Movable default: the pack doors' frame meshes are Static,
	// and UE silently refuses to attach a Static child to a Movable parent — frames would spawn
	// detached at the world origin. The leaves are Movable children, which is always legal.
	DoorRoot->SetMobility(EComponentMobility::Static);
}

void AScriptedDoor::BeginPlay()
{
	Super::BeginPlay();

	bDoorOpen = bStartsOpen;

	// The navmesh flows through the doorway regardless of door state — the closed leaf blocks
	// pawns physically until something opens it. Set on every mesh (panel, frame, glass) at
	// runtime so serialized BP/instance values can't mask it. Pack doors have no lock concept.
	TInlineComponentArray<UStaticMeshComponent*> Meshes(this);
	for (UStaticMeshComponent* Mesh : Meshes)
		if (Mesh) Mesh->SetCanEverAffectNavigation(false);
}

void AScriptedDoor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(NotifyTimeoutHandle);
	Super::EndPlay(EndPlayReason);
}

// --- IBreachable ---

bool AScriptedDoor::CanBreach_Implementation() const
{
	return !bDoorOpen && !bOpenInFlight;
}

void AScriptedDoor::Breach_Implementation(AActor* Breacher)
{
	if (bDoorOpen || bOpenInFlight) return;

	bOpenInFlight = true;
	ApplyPawnPassThrough();
	GetWorldTimerManager().SetTimer(NotifyTimeoutHandle, this, &AScriptedDoor::HandleNotifyTimeout, BreachNotifyTimeout, false);

	UE_LOG(LogScriptedDoor, Log, TEXT("%s: Breach by %s — firing OnBreachRequested"), *GetName(), *GetNameSafe(Breacher));
	OnBreachRequested(Breacher);
}

void AScriptedDoor::NotifyDoorStateChanged(bool bNowOpen)
{
	GetWorldTimerManager().ClearTimer(NotifyTimeoutHandle);
	bDoorOpen = bNowOpen;
	bOpenInFlight = false;
	RestorePawnCollision();

	// Re-closed with an AI pawn still standing in the trigger: no new BeginOverlap will fire,
	// so re-run the auto-open filter for everyone already inside. First close also backfills
	// the closed-bounds snapshot for bStartsOpen doors.
	if (!bNowOpen)
	{
		if (!HasClosedBoundsSnapshot())
			SnapshotClosedBounds();
		RescanDoorwayForAutoOpen();
	}
}

// --- Internal ---

void AScriptedDoor::ApplyPawnPassThrough()
{
	SavedPawnResponses.Reset();

	TInlineComponentArray<UStaticMeshComponent*> Meshes(this);
	SavedPawnResponses.Reserve(Meshes.Num());
	for (UStaticMeshComponent* Mesh : Meshes)
	{
		if (!Mesh) continue;
		SavedPawnResponses.Add(Mesh, Mesh->GetCollisionResponseToChannel(ECC_Pawn));
		Mesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	}
}

void AScriptedDoor::RestorePawnCollision()
{
	for (const TPair<TObjectPtr<UStaticMeshComponent>, TEnumAsByte<ECollisionResponse>>& Pair : SavedPawnResponses)
		if (Pair.Key) Pair.Key->SetCollisionResponseToChannel(ECC_Pawn, Pair.Value);
	SavedPawnResponses.Reset();
}

void AScriptedDoor::HandleNotifyTimeout()
{
	if (!bNotifyWiringWarned)
	{
		bNotifyWiringWarned = true;
		UE_LOG(LogScriptedDoor, Warning,
			TEXT("%s: no NotifyDoorStateChanged within %.1fs of Breach — is the BP wiring missing? Restoring collision."),
			*GetName(), BreachNotifyTimeout);
	}
	bOpenInFlight = false;
	RestorePawnCollision();
}
