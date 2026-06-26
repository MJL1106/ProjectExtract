// ABreachableDoor — a simple hinged door that implements IBreachable.
// The companion (or any breacher) triggers a smooth open via Breach().
// Mesh is assigned in the Blueprint subclass — no /Game/ paths in C++.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "World/Breachable.h"
#include "BreachableDoor.generated.h"

UENUM(BlueprintType)
enum class EDoorState : uint8
{
	Closed,
	Opening,
	Open,
};

UCLASS(Blueprintable, HideCategories = (Replication, Input, LOD, Cooking))
class EXTRACTION_API ABreachableDoor : public AActor, public IBreachable
{
	GENERATED_BODY()

public:
	ABreachableDoor();

	// --- IBreachable ---
	virtual void Breach_Implementation(AActor* Breacher) override;
	virtual bool CanBreach_Implementation() const override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

private:
	/** Scene component at the hinge edge — the door mesh is offset so it swings about this pivot. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Door", meta = (AllowPrivateAccess))
	TObjectPtr<USceneComponent> HingeRoot;

	/** The door panel. Assign the mesh in the Blueprint subclass. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Door", meta = (AllowPrivateAccess))
	TObjectPtr<UStaticMeshComponent> DoorMesh;

	/** Yaw rotation when fully open (degrees, relative to closed). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door|Opening", meta = (AllowPrivateAccess, ClampMin = "1.0", ClampMax = "180.0"))
	float OpenAngle = 90.f;

	/** Time to swing from closed to open (seconds). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door|Opening", meta = (AllowPrivateAccess, ClampMin = "0.1"))
	float OpenDuration = 0.8f;

	EDoorState DoorState = EDoorState::Closed;

	/** Elapsed time since the swing started. */
	float SwingElapsed = 0.f;

	/** Yaw at the start of the swing (world yaw of HingeRoot). */
	float ClosedYaw = 0.f;

	/** Begin the opening swing. Enables Tick for the duration. */
	void BeginSwing();

	/** Finalise the open state. Disables Tick. */
	void FinishSwing();
};
