// AScriptedDoor — Blueprint-animated door with C++ IBreachable state.

#include "World/ScriptedDoor.h"
#include "CollisionQueryParams.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogScriptedDoor, Log, All);

/** Cadence for re-testing a deferred pawn-collision restore while a pawn blocks the leaf. */
static constexpr float RestoreRetryInterval = 0.2f;

AScriptedDoor::AScriptedDoor()
{
	bReplicates = true;

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
	// pawns physically until something opens it. Only movable meshes are safeguarded and
	// replicated at runtime; static frame and wall geometry remains navigation-relevant.
	TInlineComponentArray<UStaticMeshComponent*> Meshes(this);
	for (UStaticMeshComponent* Mesh : Meshes)
	{
		if (Mesh && Mesh->GetMobility() == EComponentMobility::Movable)
		{
			Mesh->SetCanEverAffectNavigation(false);
			Mesh->SetIsReplicated(true);
		}
	}
}

void AScriptedDoor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(NotifyTimeoutHandle);
	GetWorldTimerManager().ClearTimer(RestoreRetryHandle);
	Super::EndPlay(EndPlayReason);
}

// --- IBreachable ---

bool AScriptedDoor::CanBreach_Implementation() const
{
	if (IsExternalGateLocked()) return false;
	return !bDoorOpen && !bOpenInFlight;
}

void AScriptedDoor::Breach_Implementation(AActor* Breacher)
{
	if (IsExternalGateLocked()) return;
	if (!HasAuthority() || bDoorOpen || bOpenInFlight) return;

	bOpenInFlight = true;
	ApplyPawnPassThrough();
	GetWorldTimerManager().SetTimer(NotifyTimeoutHandle, this, &AScriptedDoor::HandleNotifyTimeout, BreachNotifyTimeout, false);

	if (!bSilentOpen && IsValid(OpenSound))
		UGameplayStatics::PlaySoundAtLocation(GetWorld(), OpenSound, GetAcousticPortalPoint());
	bSilentOpen = false;

	UE_LOG(LogScriptedDoor, Log, TEXT("%s: Breach by %s — firing OnBreachRequested"), *GetName(), *GetNameSafe(Breacher));
	OnBreachRequested(Breacher);
}

void AScriptedDoor::ForceOpenInstant()
{
	if (bDoorOpen || bOpenInFlight) return;
	// A checkpoint fast-forward outranks any level-script gate still holding the door
	// (the flow also retires the gate actor itself so its trigger/objective can't reappear).
	SetExternalGateLocked(false);
	bSilentOpen = true;
	Breach_Implementation(nullptr);
}

void AScriptedDoor::NotifySwingStarting()
{
	// No CanBreach gating: unlike a breach, a player swing may be an open OR a close.
	UE_LOG(LogScriptedDoor, Log, TEXT("%s: NotifySwingStarting — player swing, applying pawn pass-through"), *GetName());
	bOpenInFlight = true;
	ApplyPawnPassThrough();
	GetWorldTimerManager().SetTimer(NotifyTimeoutHandle, this, &AScriptedDoor::HandleNotifyTimeout, BreachNotifyTimeout, false);

	// Player E-press swing — a close creaks the same as an open, which is the desired read.
	if (IsValid(OpenSound))
		UGameplayStatics::PlaySoundAtLocation(GetWorld(), OpenSound, GetAcousticPortalPoint());
}

void AScriptedDoor::NotifyDoorStateChanged(bool bNowOpen)
{
	if (bNowOpen && IsExternalGateLocked())
		UE_LOG(LogScriptedDoor, Warning, TEXT("%s: opened while externally gate-locked — an ungated BP path bypassed the gate."), *GetName());

	// Tripwire: a swing that finishes with no pass-through active usually means the BP never
	// called NotifySwingStarting — that door's blocking leaves swing into pawns and can launch
	// them. (Also fires if a >BreachNotifyTimeout swing timed out before this notify arrived.)
	if (!bOpenInFlight && SavedPawnResponses.IsEmpty())
		UE_LOG(LogScriptedDoor, Warning,
			TEXT("%s: swing finished (bNowOpen=%d) with no swing-start recorded — NotifySwingStarting missing from the E-press path (or the swing outlived the %.1fs timeout). Pawn pass-through was NOT active; this door can launch pawns."),
			*GetName(), bNowOpen ? 1 : 0, BreachNotifyTimeout);

	GetWorldTimerManager().ClearTimer(NotifyTimeoutHandle);
	bDoorOpen = bNowOpen;
	bOpenInFlight = false;
	RestorePawnCollision();
	if (bNowOpen)
		BroadcastDoorOpenedOnce();

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
	GetWorldTimerManager().ClearTimer(RestoreRetryHandle);

	TInlineComponentArray<UStaticMeshComponent*> Meshes(this);
	SavedPawnResponses.Reserve(Meshes.Num());
	for (UStaticMeshComponent* Mesh : Meshes)
	{
		if (!Mesh) continue;
		// A new swing can start while a deferred restore still holds the old saved responses —
		// keep the first snapshot so Ignore is never recorded as the response to restore to.
		if (!SavedPawnResponses.Contains(Mesh))
			SavedPawnResponses.Add(Mesh, Mesh->GetCollisionResponseToChannel(ECC_Pawn));
		Mesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	}

	UE_LOG(LogScriptedDoor, Log, TEXT("%s: pawn pass-through ON — %d mesh(es) set to Ignore Pawn"), *GetName(), SavedPawnResponses.Num());
}

void AScriptedDoor::RestorePawnCollision()
{
	if (SavedPawnResponses.IsEmpty()) return;

	// A leaf that re-blocks while a pawn is still inside it launches that pawn on depenetration
	// (the close-onto-the-player case) — hold pass-through until the doorway is clear.
	if (IsAnyPawnOverlappingLeaf())
	{
		UE_LOG(LogScriptedDoor, Log, TEXT("%s: pawn collision restore DEFERRED — pawn still inside a leaf, retrying in %.1fs"), *GetName(), RestoreRetryInterval);
		GetWorldTimerManager().SetTimer(RestoreRetryHandle, this, &AScriptedDoor::RestorePawnCollision, RestoreRetryInterval, false);
		return;
	}

	GetWorldTimerManager().ClearTimer(RestoreRetryHandle);
	for (const TPair<TObjectPtr<UStaticMeshComponent>, TEnumAsByte<ECollisionResponse>>& Pair : SavedPawnResponses)
		if (Pair.Key) Pair.Key->SetCollisionResponseToChannel(ECC_Pawn, Pair.Value);
	UE_LOG(LogScriptedDoor, Log, TEXT("%s: pawn collision RESTORED on %d mesh(es)"), *GetName(), SavedPawnResponses.Num());
	SavedPawnResponses.Reset();
}

bool AScriptedDoor::IsAnyPawnOverlappingLeaf() const
{
	UWorld* World = GetWorld();
	if (!World) return false;

	for (const TPair<TObjectPtr<UStaticMeshComponent>, TEnumAsByte<ECollisionResponse>>& Pair : SavedPawnResponses)
	{
		const UStaticMeshComponent* Mesh = Pair.Key;
		// Static frame meshes never move into a pawn; only the swinging leaves can trap one.
		if (!Mesh || Mesh->GetMobility() != EComponentMobility::Movable) continue;

		const FBoxSphereBounds Bounds = Mesh->Bounds;
		if (World->OverlapAnyTestByObjectType(Bounds.Origin, FQuat::Identity,
				FCollisionObjectQueryParams(ECC_Pawn), FCollisionShape::MakeBox(Bounds.BoxExtent),
				FCollisionQueryParams(SCENE_QUERY_STAT(DoorLeafPawnClear), false, this)))
			return true;
	}
	return false;
}

void AScriptedDoor::HandleNotifyTimeout()
{
	if (!bNotifyWiringWarned)
	{
		bNotifyWiringWarned = true;
		UE_LOG(LogScriptedDoor, Warning,
			TEXT("%s: no NotifyDoorStateChanged within %.1fs of Breach/NotifySwingStarting — is the BP wiring missing? Restoring collision."),
			*GetName(), BreachNotifyTimeout);
	}
	bOpenInFlight = false;
	RestorePawnCollision();
}
