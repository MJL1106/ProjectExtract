// ACompanionModeDoorGate -- placeable trigger that locks a target door until the player sets
// the companion to a required mode. Server-authoritative, non-ticking. Designers place it
// near the gated door, wire TargetDoor to the level instance, and pick the RequiredMode.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Companion/CompanionTypes.h"
#include "CompanionModeDoorGate.generated.h"

class ADoorBase;
class UBoxComponent;
class UCompanionCommandComponent;

UCLASS(Blueprintable, HideCategories = (Rendering, Replication, Input, LOD, Cooking))
class EXTRACTION_API ACompanionModeDoorGate : public AActor
{
	GENERATED_BODY()

public:
	ACompanionModeDoorGate();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** The door this gate locks. Checkpoint fast-forward matches gates to force-opened doors. */
	ADoorBase* GetTargetDoor() const { return TargetDoor; }

	/** Checkpoint fast-forward: retire the gate (clear the door lock, kill trigger + objective).
	 *  Safe pre-BeginPlay — BeginPlay skips its lock when the gate is already unlocked. */
	void ForceUnlockForCheckpoint();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** The door this gate locks until the required mode is set. Wire to a level instance. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Gate")
	TObjectPtr<ADoorBase> TargetDoor;

	/** Companion mode the player must select to unlock the door. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gate")
	ECompanionMode RequiredMode = ECompanionMode::Stealth;

	/** Objective id registered with UObjectiveSubsystem while the gate is active. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gate|Objective")
	FName ObjectiveId = TEXT("CompanionModeGate");

	/** Short label shown on the HUD waypoint (e.g. "Set Stealth Mode"). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gate|Objective")
	FText ObjectiveLabel;

private:
	UPROPERTY(VisibleAnywhere, Category = "Gate")
	TObjectPtr<USceneComponent> Root;

	/** Overlap volume the player walks into to activate the objective + mode listener. */
	UPROPERTY(VisibleAnywhere, Category = "Gate")
	TObjectPtr<UBoxComponent> TriggerBox;

	UPROPERTY(ReplicatedUsing = OnRep_Unlocked, Transient)
	bool bUnlocked = false;

	/** Guard: true once we have bound the mode delegate, prevents double-bind on re-overlap. */
	bool bDelegateBound = false;

	/** Guard: true once the objective has been added for the local player. */
	bool bObjectiveAdded = false;

	/** Weak ref to the bound component so we can unbind cleanly. */
	TWeakObjectPtr<UCompanionCommandComponent> BoundCommandComp;

	UFUNCTION()
	void OnTriggerOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep,
		const FHitResult& SweepResult);

	UFUNCTION()
	void OnCompanionModeChanged(ECompanionMode NewMode);

	UFUNCTION()
	void OnRep_Unlocked();

	/** Shared unlock path: clears the door gate, removes the objective, disables the box, unbinds. */
	void PerformUnlock();

	void RemoveObjectiveLocal();
	void UnbindModeDelegate();
};
