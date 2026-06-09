// APatrolRoute — designer-placed patrol waypoints.

#include "PatrolRoute.h"
#include "Components/BillboardComponent.h"

APatrolRoute::APatrolRoute()
{
	PrimaryActorTick.bCanEverTick = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));

	Billboard = CreateDefaultSubobject<UBillboardComponent>(TEXT("Billboard"));
	Billboard->SetupAttachment(RootComponent);
	Billboard->SetHiddenInGame(true);
}

FVector APatrolRoute::GetWorldPoint(int32 Index) const
{
	if (Points.Num() == 0) return GetActorLocation();
	const int32 Idx = FMath::Clamp(Index, 0, Points.Num() - 1);
	return GetActorTransform().TransformPosition(Points[Idx]);
}
