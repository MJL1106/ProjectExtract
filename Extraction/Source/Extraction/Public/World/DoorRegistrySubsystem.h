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

private:
	/** Weak — a door destroyed without EndPlay (world teardown) just goes stale and is skipped. */
	TArray<TWeakObjectPtr<ADoorBase>> Doors;
};
