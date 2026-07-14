// AIAcoustics — trace-based acoustic occlusion for AI hearing.

#include "AI/AIAcoustics.h"
#include "World/Breachable.h"
#include "World/DoorBase.h"
#include "World/DoorRegistrySubsystem.h"
#include "Engine/HitResult.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"

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
		const EDoorClass DoorClass = ClassifyDoor(Door);
		if (DoorClass == EDoorClass::Blocking) continue;

		PortalIgnore.Last() = Door;

		const FVector PortalPoint = Door->GetAcousticPortalPoint();
		if (TraceSolidBlocker(World, StimLoc, PortalPoint, PortalIgnore, MaxPawnSkips, Block)) continue;
		if (TraceSolidBlocker(World, PortalPoint, ListenerEye, PortalIgnore, MaxPawnSkips, Block)) continue;

		return DoorMultiplier(DoorClass, ThroughDoorMult);
	}

	return 0.f;
}

}
