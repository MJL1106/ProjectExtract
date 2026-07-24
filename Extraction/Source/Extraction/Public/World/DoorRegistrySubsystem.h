// World registry of every ADoorBase, so AI acoustics can find candidate door "portals" between
// a noise and a listener without iterating the world per stimulus. Doors register in
// ADoorBase::BeginPlay and unregister in ADoorBase::EndPlay.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "DoorRegistrySubsystem.generated.h"

class ADoorBase;

UCLASS()
class EXTRACTION_API UDoorRegistrySubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	void Register(ADoorBase* Door);
	void Unregister(ADoorBase* Door);

	/** Up to MaxCount doors, sorted ascending by portal path length dist(A,door)+dist(door,B). */
	void CollectPortalCandidates(const FVector& A, const FVector& B, int32 MaxCount, TArray<ADoorBase*>& OutDoors) const;

	/** True when the straight segment From->To crosses the doorway plane of any CLOSED door.
	 *  The cover pickers' closed-door reject: a cover candidate whose direct line crosses a
	 *  closed door means retreating out of the fight space (auto-opening the door en route, or
	 *  shoving against it when locked) — never a valid pick. Open doors don't block. Pure math,
	 *  no traces; straight-segment approximation by design (same precedent as
	 *  CompanionCover::PathPassesNearThreat). */
	bool AnyClosedDoorBlocksSegment(const FVector& From, const FVector& To) const;

	/** Claim the right to play a door-open sound at Location. False when another door open-sound
	 *  already played within OpenSoundDedupeRadius in the last OpenSoundDedupeWindow — entrance
	 *  prefabs stack several door actors on one doorway, and a player swing can land in the same
	 *  beat as an AI auto-open; both must collapse to a single audible open. */
	bool ClaimOpenSoundPlay(const FVector& Location);

private:
	/** Doors within this range (cm) share one open-sound claim per window. */
	static constexpr float OpenSoundDedupeRadius = 300.f;

	/** Seconds a claim suppresses further open sounds inside the radius. */
	static constexpr double OpenSoundDedupeWindow = 0.4;

	FVector LastOpenSoundLocation = FVector::ZeroVector;
	double LastOpenSoundTime = -1.0;

	/** Weak — a door destroyed without EndPlay (world teardown) just goes stale and is skipped. */
	TArray<TWeakObjectPtr<ADoorBase>> Doors;
};
