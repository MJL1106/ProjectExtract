// ABreachableDoor — a simple hinged door that implements IBreachable.
// The companion (or any breacher) triggers a smooth open via Breach().
// Mesh is assigned in the Blueprint subclass — no /Game/ paths in C++.

#pragma once

#include "CoreMinimal.h"
#include "World/DoorBase.h"
#include "BreachableDoor.generated.h"

UENUM(BlueprintType)
enum class EDoorState : uint8
{
	Closed,
	Opening,
	Open,
};

UCLASS(Blueprintable, HideCategories = (Replication, Input, LOD, Cooking))
class EXTRACTION_API ABreachableDoor : public ADoorBase
{
	GENERATED_BODY()

public:
	ABreachableDoor();

	// --- IBreachable ---
	virtual void Breach_Implementation(AActor* Breacher) override;
	virtual bool CanBreach_Implementation() const override;
	virtual bool GetBreachStandPoint_Implementation(const AActor* Breacher, FVector& OutLocation, FRotator& OutFacing) const override;
	virtual bool GetPostBreachPoint_Implementation(const AActor* Breacher, bool bEnterRoom, FVector& OutLocation) const override;

	/** Mid-swing counts as open: the doorway is already parting and this door never re-closes. */
	virtual bool IsOpenForAcoustics() const override { return DoorState != EDoorState::Closed; }

	/** Instantly poses the leaf fully open (no swing), unlocking first. Fires the one-shot
	 *  OnDoorOpened broadcast — callers fast-forwarding a flow must set their step BEFORE this. */
	virtual void ForceOpenInstant() override;

	/** Keycard id this door's lock wants — the checkpoint fast-forward re-grants it on resume. */
	FName GetRequiredKeycardId() const { return RequiredKeycardId; }

	// --- Keycard lock ---

	/** True while the lock is engaged (starts locked and not yet unlocked). Locked doors are
	 *  not offered for companion breach — the player unlocks them via Interact + keycard. */
	UFUNCTION(BlueprintPure, Category = "Door|Lock")
	bool IsLocked() const { return bStartsLocked && !bUnlocked; }

	/** Player Interact on a locked door: consumes nothing — if the mission inventory holds
	 *  RequiredKeycardId the door unlocks and swings open, otherwise a "requires keycard"
	 *  toast is raised. No-op when the door isn't locked. */
	UFUNCTION(BlueprintCallable, Category = "Door|Lock")
	void TryUnlock(AActor* UnlockInstigator);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	/** Exact leaf identity — only DoorMesh is the barrier; the BP-added frame (whatever its
	 *  mobility) is trim and must not block pawns. The leaf must stay a single mesh: extra
	 *  BP-added leaf geometry would be treated as trim, so bake it into the DoorMesh asset. */
	virtual bool IsDoorLeafMesh(const UStaticMeshComponent& Mesh) const override { return &Mesh == DoorMesh; }

private:
	/** Scene component at the hinge edge — actor root. Stays rigid so the whole actor can be rotated in the level. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Door", meta = (AllowPrivateAccess))
	TObjectPtr<USceneComponent> HingeRoot;

	/** Sub-pivot that actually swings during a breach. Child of HingeRoot; DoorMesh is attached here. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Door", meta = (AllowPrivateAccess))
	TObjectPtr<USceneComponent> LeafPivot;

	/** The door panel. Assign the mesh in the Blueprint subclass. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Door", meta = (AllowPrivateAccess))
	TObjectPtr<UStaticMeshComponent> DoorMesh;

	/** Yaw rotation when fully open (degrees, relative to closed). Sign sets the swing
	 *  direction: positive opens one way, negative the other. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door|Opening", meta = (AllowPrivateAccess, ClampMin = "-180.0", ClampMax = "180.0"))
	float OpenAngle = -90.f;

	/** Time to swing from closed to open (seconds). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door|Opening", meta = (AllowPrivateAccess, ClampMin = "0.1"))
	float OpenDuration = 0.8f;

	/** When true the door starts locked and needs RequiredKeycardId to open. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door|Lock", meta = (AllowPrivateAccess))
	bool bStartsLocked = false;

	/** Keycard id that opens this door (matches FLootGrant::KeycardId on the card). Kept, not consumed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door|Lock", meta = (AllowPrivateAccess, EditCondition = "bStartsLocked"))
	FName RequiredKeycardId = NAME_None;

	/** Runtime: set once the player unlocks with the keycard. */
	bool bUnlocked = false;

	EDoorState DoorState = EDoorState::Closed;

	/** Elapsed time since the swing started. */
	float SwingElapsed = 0.f;

	/** Yaw at the start of the swing (world yaw of LeafPivot). */
	float ClosedYaw = 0.f;

	// Doorway frame captured at BeginPlay while the leaf is closed — the panel's live bounds
	// rotate with the swing, so post-breach geometry must come from this snapshot.
	FVector PanelClosedCenter = FVector::ZeroVector;
	FVector PanelOpenCenter = FVector::ZeroVector;
	float PanelHalfThicknessCached = 5.f;
	bool bPanelThinAxisX = true;

	/** Begin the opening swing. Enables Tick for the duration. */
	void BeginSwing();

	/** Finalise the open state. Disables Tick. */
	void FinishSwing();
};
