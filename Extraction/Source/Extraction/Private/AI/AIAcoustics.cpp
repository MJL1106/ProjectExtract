// AIAcoustics — trace-based acoustic occlusion for AI hearing.

#include "AI/AIAcoustics.h"
#include "World/Breachable.h"
#include "World/DoorBase.h"
#include "World/DoorRegistrySubsystem.h"
#include "Engine/HitResult.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "NavigationSystem.h"
#include "NavigationData.h"

namespace AIAcoustics
{

EDoorClass ClassifyDoor(ADoorBase* Door)
{
	if (!IsValid(Door)) return EDoorClass::Blocking;
	if (Door->IsOpenForAcoustics()) return EDoorClass::Open;
	if (IBreachable::Execute_CanBreach(Door)) return EDoorClass::Openable;
	return EDoorClass::Blocking; // locked — a wall as far as the AI is concerned
}

float DoorMultiplier(EDoorClass DoorClass, float ThroughDoorMult)
{
	switch (DoorClass)
	{
	case EDoorClass::Open:     return 1.f;
	case EDoorClass::Openable: return ThroughDoorMult;
	default:                   return 0.f;
	}
}

bool TraceSolidBlocker(const UWorld* World, const FVector& Start, const FVector& End,
	TConstArrayView<const AActor*> IgnoreActors, int32 InMaxPawnSkips, FHitResult& OutBlock)
{
	if (!World) return true;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(AIAcoustics), /*bTraceComplex*/ false);
	for (const AActor* Ignore : IgnoreActors)
		Params.AddIgnoredActor(Ignore);

	for (int32 Skip = 0; Skip <= InMaxPawnSkips; ++Skip)
	{
		if (!World->LineTraceSingleByChannel(OutBlock, Start, End, ECC_Visibility, Params))
			return false;

		// Bodies don't stop sound — step past and re-trace.
		if (Cast<APawn>(OutBlock.GetActor()))
		{
			Params.AddIgnoredActor(OutBlock.GetActor());
			continue;
		}
		return true;
	}
	return true; // degenerate pawn stack — treat as blocked
}

float ComputeMultiplier(UWorld* World, const FVector& ListenerEye, const FVector& StimLoc,
	const AActor* Listener, const AActor* Instigator, float ThroughDoorMult)
{
	if (!World) return 1.f;

	TArray<const AActor*, TInlineAllocator<4>> Ignore;
	Ignore.Add(Listener);
	Ignore.Add(Instigator);

	FHitResult Block;
	if (!TraceSolidBlocker(World, ListenerEye, StimLoc, Ignore, MaxPawnSkips, Block))
		return 1.f;

	// Direct line blocked by a door leaf: the door itself is the medium.
	if (ADoorBase* Door = Cast<ADoorBase>(Block.GetActor()))
	{
		const float Mult = DoorMultiplier(ClassifyDoor(Door), ThroughDoorMult);
		if (Mult > 0.f) return Mult;
		// Locked door = wall; fall through to the portal search — another door may connect.
	}

	// Direct line walled off: does an openable door connect the two spaces? Both portal segments
	// ignore the candidate door itself — its leaf is the pass-through medium, not a blocker.
	UDoorRegistrySubsystem* Registry = World->GetSubsystem<UDoorRegistrySubsystem>();
	if (!Registry) return 0.f;

	TArray<ADoorBase*> Portals;
	Registry->CollectPortalCandidates(StimLoc, ListenerEye, PortalCandidateCount, Portals);

	// One extra slot reused per candidate — the door's own leaf is the pass-through medium.
	TArray<const AActor*, TInlineAllocator<4>> PortalIgnore = Ignore;
	PortalIgnore.Add(nullptr);

	for (ADoorBase* Door : Portals)
	{
		// Zero-multiplier candidates (Blocking always; Openable too when ThroughDoorMult is 0) must
		// be skipped, not returned — returning would mask a clear open-door portal later in the list.
		const EDoorClass DoorClass = ClassifyDoor(Door);
		if (DoorMultiplier(DoorClass, ThroughDoorMult) <= 0.f) continue;

		PortalIgnore.Last() = Door;

		const FVector PortalPoint = Door->GetAcousticPortalPoint();
		if (TraceSolidBlocker(World, StimLoc, PortalPoint, PortalIgnore, MaxPawnSkips, Block)) continue;
		if (TraceSolidBlocker(World, PortalPoint, ListenerEye, PortalIgnore, MaxPawnSkips, Block)) continue;

		return DoorMultiplier(DoorClass, ThroughDoorMult);
	}

	// No door connects the spaces — but a doorless route can still carry the sound: an open-ended
	// wall, a corridor corner, a gap in set dressing. Ask the navmesh for a same-storey walking
	// route that isn't a huge detour. Cross-floor stays silent by construction: the slab walls the
	// direct line (that's how we got here), and a stair route either crosses the Z gate or blows
	// the detour budget.
	if (IsValid(Listener))
	{
		const FVector ListenerLoc = Listener->GetActorLocation();

		// Storey gate on FEET, not raw stimulus Z: a gunshot stimulus sits at the muzzle
		// (~150cm above the shooter's feet) and the listener location is capsule centre —
		// comparing those raw would shave nearly a full storey off the gate and let gunfire
		// from the floor below read as same-storey. Normalise both ends to the ground.
		const float ListenerFeetZ = ListenerLoc.Z - Listener->GetSimpleCollisionHalfHeight();
		float StimFeetZ = StimLoc.Z;
		if (const APawn* InstigatorPawn = Cast<APawn>(Instigator))
			StimFeetZ = InstigatorPawn->GetActorLocation().Z - InstigatorPawn->GetSimpleCollisionHalfHeight();

		if (FMath::Abs(StimFeetZ - ListenerFeetZ) <= SameStoreyZThreshold)
		{
			UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
			const ANavigationData* NavData = NavSys ? NavSys->GetDefaultNavDataInstance() : nullptr;
			if (NavData)
			{
				const float Direct = FMath::Max(FVector::Dist(StimLoc, ListenerLoc), 1.f);
				const float Budget = Direct * NavDetourRatioMax + NavDetourSlack;

				FPathFindingQuery Query(Listener, *NavData, ListenerLoc, StimLoc);
				Query.bAllowPartialPaths = false;
				// The common outcome here is "no route" (sealed room) — cap the search so a
				// failed A* stops at the budget instead of exhausting the reachable navmesh.
				Query.CostLimit = Budget;

				const FPathFindingResult Result = NavSys->FindPathSync(Query, EPathFindingMode::Regular);
				if (Result.IsSuccessful() && Result.Path.IsValid() && !Result.Path->IsPartial())
				{
					if (Result.Path->GetLength() <= Budget)
						return NavDetourMult;
				}
			}
		}
	}

	return 0.f;
}

}
