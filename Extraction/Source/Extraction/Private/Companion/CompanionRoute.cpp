// ACompanionRoute — designer-placed one-shot companion route.

#include "Companion/CompanionRoute.h"
#include "Companion/CompanionRouteVisComponent.h"
#include "Components/BillboardComponent.h"
#include "DrawDebugHelpers.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

static TAutoConsoleVariable<int32> CVarCompanionRouteDebug(
	TEXT("companion.RouteDebug"),
	0,
	TEXT("Draw debug visualisation for companion routes (0 = off, 1 = on)."),
	ECVF_Cheat);

static void OnRouteDebugCVarChanged(IConsoleVariable* CVar)
{
	const bool bEnabled = (CVar->GetInt() != 0);
	for (TObjectIterator<ACompanionRoute> It; It; ++It)
	{
		if (It->IsTemplate() || !IsValid(*It)) continue;
		It->SetActorTickEnabled(bEnabled);
	}
}

const FCompanionRouteWaypoint ACompanionRoute::DefaultWaypoint = FCompanionRouteWaypoint();

// ---------------------------------------------------------------------

ACompanionRoute::ACompanionRoute()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));

	Billboard = CreateDefaultSubobject<UBillboardComponent>(TEXT("Billboard"));
	Billboard->SetupAttachment(RootComponent);
	Billboard->SetHiddenInGame(true);

	RouteVisComponent = CreateDefaultSubobject<UCompanionRouteVisComponent>(TEXT("RouteVis"));
	RouteVisComponent->SetupAttachment(RootComponent);
}

// ---------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------

void ACompanionRoute::BeginPlay()
{
	Super::BeginPlay();

	// Resolve the cvar by name with a null-guard. A Live Coding patch can leave the static
	// TAutoConsoleVariable's internal IConsoleVariable* dangling/null (this crashed BeginPlay on
	// 2026-06-26); a console-manager lookup + guard is robust to that.
	IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("companion.RouteDebug"));
	if (!CVar) return;

	// Honour the current cvar state at spawn time.
	SetActorTickEnabled(CVar->GetInt() != 0);

	// SetOnChangedCallback replaces (not appends), so re-calling across PIE restarts is safe.
	CVar->SetOnChangedCallback(FConsoleVariableDelegate::CreateStatic(&OnRouteDebugCVarChanged));
}

void ACompanionRoute::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
}

// ---------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------

int32 ACompanionRoute::NumPoints() const
{
	return Waypoints.Num();
}

FVector ACompanionRoute::GetWorldPoint(int32 Index) const
{
	if (Waypoints.Num() == 0) return GetActorLocation();
	const int32 Idx = FMath::Clamp(Index, 0, Waypoints.Num() - 1);
	return GetActorTransform().TransformPosition(Waypoints[Idx].Location);
}

// Bounds-center reads as "the door" better than an actor's floor-level origin; fall back to
// actor location if the bounding box is degenerate (e.g. a component-less actor).
static FVector GetActorAimPoint(const AActor& Actor)
{
	const FBox Bounds = Actor.GetComponentsBoundingBox();
	return Bounds.IsValid ? Bounds.GetCenter() : Actor.GetActorLocation();
}

FVector ACompanionRoute::GetWaypointAimWorld(int32 Index) const
{
	if (Waypoints.Num() == 0) return GetActorLocation() + FVector::ForwardVector * LookAheadDistance;

	const int32 Idx = FMath::Clamp(Index, 0, Waypoints.Num() - 1);
	const FCompanionRouteWaypoint& WP = Waypoints[Idx];

	// Highest priority: the waypoint's own authored focus actor.
	if (IsValid(WP.AimTargetActor))
	{
		return GetActorAimPoint(*WP.AimTargetActor);
	}

	// Explicit aim override — transform the authored local-space target.
	if (WP.bOverrideAim)
	{
		return GetActorTransform().TransformPosition(WP.AimTargetLocal);
	}

	// Route-wide focus actor — used by every leg that authors nothing of its own.
	if (IsValid(AimTargetActor))
	{
		return GetActorAimPoint(*AimTargetActor);
	}

	// Procedural look-ahead toward the next waypoint.
	const FVector Current = GetWorldPoint(Idx);

	if (Waypoints.Num() == 1)
	{
		return Current + GetActorForwardVector() * LookAheadDistance;
	}

	FVector Direction;
	const bool bIsLast = (Idx == Waypoints.Num() - 1);

	if (bIsLast)
	{
		// Last waypoint: use direction from previous → this.
		Direction = (Current - GetWorldPoint(Idx - 1)).GetSafeNormal();
	}
	else
	{
		// Interior / first waypoint: use direction toward the next.
		Direction = (GetWorldPoint(Idx + 1) - Current).GetSafeNormal();
	}

	if (Direction.IsNearlyZero())
	{
		Direction = GetActorForwardVector();
	}

	return Current + Direction * LookAheadDistance;
}

bool ACompanionRoute::HasAuthoredAim(int32 Index) const
{
	return GetWaypoint(Index).HasAuthoredAim() || IsValid(AimTargetActor);
}

const FCompanionRouteWaypoint& ACompanionRoute::GetWaypoint(int32 Index) const
{
	if (Waypoints.Num() == 0) return DefaultWaypoint;
	const int32 Idx = FMath::Clamp(Index, 0, Waypoints.Num() - 1);
	return Waypoints[Idx];
}

void ACompanionRoute::NotifyWaypointReached(int32 Index)
{
	OnWaypointReached.Broadcast(Index);
}

void ACompanionRoute::NotifyRouteCompleted(bool bAborted)
{
	OnRouteCompleted.Broadcast(bAborted);
}

// ---------------------------------------------------------------------
// Tick — debug draw only
// ---------------------------------------------------------------------

void ACompanionRoute::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (CVarCompanionRouteDebug.GetValueOnGameThread() == 0) return;

	DrawDebugRoute();
}

// ---------------------------------------------------------------------
// Debug visualisation
// ---------------------------------------------------------------------

void ACompanionRoute::DrawDebugRoute() const
{
	const UWorld* World = GetWorld();
	if (!IsValid(World)) return;

	auto StanceColor = [](ECompanionRouteStance S) -> FColor
	{
		switch (S)
		{
		case ECompanionRouteStance::Relaxed: return FColor::Green;
		case ECompanionRouteStance::Alert:   return FColor::Yellow;
		case ECompanionRouteStance::Crouch:  return FColor::Cyan;
		default:                             return FColor::White;
		}
	};

	const float SphereRadius = 20.f;
	const float ArrowSize    = 15.f;
	const int32 Num          = Waypoints.Num();

	// Pre-compute world positions so each segment doesn't call GetWorldPoint twice.
	TArray<FVector> WorldPoints;
	WorldPoints.Reserve(Num);
	for (int32 i = 0; i < Num; ++i)
	{
		WorldPoints.Add(GetWorldPoint(i));
	}

	for (int32 i = 0; i < Num; ++i)
	{
		const FVector& WorldPt = WorldPoints[i];
		const FVector AimWorld = GetWaypointAimWorld(i);
		const FColor  Color    = StanceColor(Waypoints[i].Stance);

		DrawDebugSphere(World, WorldPt, SphereRadius, 12, Color, false, -1.f);

		if (i < Num - 1)
		{
			DrawDebugLine(World, WorldPt, WorldPoints[i + 1], Color, false, -1.f);
		}

		DrawDebugDirectionalArrow(World, WorldPt, AimWorld, ArrowSize, FColor::Orange, false, -1.f);
	}
}

// ---------------------------------------------------------------------
// Editor authoring UX
// ---------------------------------------------------------------------

#if WITH_EDITOR
void ACompanionRoute::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent)
{
	Super::PostEditChangeChainProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.GetMemberPropertyName() != GET_MEMBER_NAME_CHECKED(ACompanionRoute, Waypoints))
		return;

	const bool bIsAdd = PropertyChangedEvent.ChangeType == EPropertyChangeType::ArrayAdd;
	const bool bIsDuplicate = PropertyChangedEvent.ChangeType == EPropertyChangeType::Duplicate;
	if (!bIsAdd && !bIsDuplicate) return;

	const int32 NewIndex = PropertyChangedEvent.GetArrayIndex(TEXT("Waypoints"));
	if (!Waypoints.IsValidIndex(NewIndex)) return;

	const int32 PrevIndex = NewIndex - 1;

	// ArrayAdd starts from a blank default-constructed entry — copy the previous waypoint's full
	// settings so the new leg inherits stance/aim/dwell/speed authoring. Duplicate already carries
	// a copy of the source entry's settings, so this step is add-only.
	if (bIsAdd && Waypoints.IsValidIndex(PrevIndex))
		Waypoints[NewIndex] = Waypoints[PrevIndex];

	// Offset along the last leg's direction so the new/duplicated widget doesn't land stacked on
	// the entry it was copied from. Fewer than 2 prior points to derive a direction from: local +X.
	FVector OffsetDir = FVector::ForwardVector;
	const int32 PrevPrevIndex = PrevIndex - 1;
	if (Waypoints.IsValidIndex(PrevIndex) && Waypoints.IsValidIndex(PrevPrevIndex))
		OffsetDir = (Waypoints[PrevIndex].Location - Waypoints[PrevPrevIndex].Location).GetSafeNormal();

	if (OffsetDir.IsNearlyZero())
		OffsetDir = FVector::ForwardVector;

	Waypoints[NewIndex].Location += OffsetDir * NewWaypointOffsetDistance;
}
#endif
