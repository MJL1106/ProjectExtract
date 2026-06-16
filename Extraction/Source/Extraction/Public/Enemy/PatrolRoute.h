// Designer-placed patrol waypoints for enemy characters.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PatrolRoute.generated.h"

class UBillboardComponent;

UENUM(BlueprintType)
enum class EPatrolOrder : uint8
{
	Loop      UMETA(DisplayName = "Loop"),
	PingPong  UMETA(DisplayName = "Ping-Pong"),
	Shuffle   UMETA(DisplayName = "Shuffle"),
};

UCLASS(Blueprintable)
class EXTRACTION_API APatrolRoute : public AActor
{
	GENERATED_BODY()

public:
	APatrolRoute();

	/** Patrol waypoints in actor-local space. MakeEditWidget lets designers drag them in the viewport. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Patrol", meta = (MakeEditWidget))
	TArray<FVector> Points;

	/** Order in which waypoints are visited. Loop and PingPong always start at point 0; Shuffle picks a random start. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Patrol")
	EPatrolOrder PatrolOrder = EPatrolOrder::Loop;

	/** Seconds to wait at each waypoint before moving on. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Patrol", meta = (ClampMin = "0.0"))
	float WaitAtPointSeconds = 2.f;

	/** When greater than WaitAtPointSeconds, each stop rolls a wait in [WaitAtPointSeconds, WaitAtPointMaxSeconds]. 0 = fixed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Patrol", meta = (ClampMin = "0.0"))
	float WaitAtPointMaxSeconds = 0.f;

	/** Radius around each waypoint to pick a random nav-reachable destination. 0 = exact point. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Patrol", meta = (ClampMin = "0.0"))
	float WanderRadius = 0.f;

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
