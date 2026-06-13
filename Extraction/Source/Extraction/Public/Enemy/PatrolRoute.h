// Designer-placed patrol waypoints for enemy characters.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PatrolRoute.generated.h"

class UBillboardComponent;

UCLASS(Blueprintable)
class EXTRACTION_API APatrolRoute : public AActor
{
	GENERATED_BODY()

public:
	APatrolRoute();

	/** Patrol waypoints in actor-local space. MakeEditWidget lets designers drag them in the viewport. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Patrol", meta = (MakeEditWidget))
	TArray<FVector> Points;

	/** If true the route loops (last → first). If false it ping-pongs. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Patrol")
	bool bLoop = true;

	/** Seconds to wait at each waypoint before moving on. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Patrol", meta = (ClampMin = "0.0"))
	float WaitAtPointSeconds = 2.f;

	/** Returns the world-space position of waypoint at index i (clamped). */
	UFUNCTION(BlueprintPure, Category = "Patrol")
	FVector GetWorldPoint(int32 Index) const;

	/** Returns the number of waypoints. */
	UFUNCTION(BlueprintPure, Category = "Patrol")
	int32 NumPoints() const { return Points.Num(); }

private:
	UPROPERTY(VisibleAnywhere, Category = "Patrol")
	TObjectPtr<UBillboardComponent> Billboard;
};
