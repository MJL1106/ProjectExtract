// AScriptedDoor — C++ parent for doors whose opening logic lives in Blueprint (the UWC
// skyscraper pack doors). C++ owns the IBreachable state and the AI auto-open trigger
// (inherited from ADoorBase); the Blueprint owns the actual animation:
//
//   C++ -> BP : Breach fires OnBreachRequested — wire it into the BP's existing open path
//               (the DoorRotator timeline). Do NOT override the Breach interface event in BP.
//   BP -> C++ : call NotifyDoorStateChanged(true) when the door reaches fully open and
//               NotifyDoorStateChanged(false) when it closes again — from EVERY path that
//               flips the BP's own IsDoorOpen, player-driven included. This keeps CanBreach
//               honest, so enemies can re-open a door the player closed.

#pragma once

#include "CoreMinimal.h"
#include "World/DoorBase.h"
#include "ScriptedDoor.generated.h"

class UStaticMeshComponent;

UCLASS(Blueprintable)
class EXTRACTION_API AScriptedDoor : public ADoorBase
{
	GENERATED_BODY()

public:
	AScriptedDoor();

	// --- IBreachable ---
	virtual void Breach_Implementation(AActor* Breacher) override;
	virtual bool CanBreach_Implementation() const override;

	/** Open, or an AI-initiated open is in flight (the BP timeline is already swinging). */
	virtual bool IsOpenForAcoustics() const override { return bDoorOpen || bOpenInFlight; }

	/** Checkpoint fast-forward: drives the BP's breach/open path (the leaf swings once at level
	 *  start — C++ can't pose a BP-owned timeline leaf instantly). */
	virtual void ForceOpenInstant() override;

	/** Blueprint reports its door state here (see class comment for the wiring contract). */
	UFUNCTION(BlueprintCallable, Category = "Door|State")
	void NotifyDoorStateChanged(bool bNowOpen);

	/** A player-driven swing (open or close) is starting. Call from the door BP at the start of
	 *  every E-press path, before the timeline plays — makes the leaf pawn-passable for the swing
	 *  exactly like an AI breach, so it can't launch the opener. Collision restores when the BP
	 *  reports the swing's end via NotifyDoorStateChanged (or the timeout fires). */
	UFUNCTION(BlueprintCallable, Category = "Door|State")
	void NotifySwingStarting();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual bool IsClosedAtBeginPlay() const override { return !bStartsOpen; }

	/** Breach was requested (AI auto-open, companion breach, or ping). Implement in the door
	 *  Blueprint by driving its existing open logic. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Door")
	void OnBreachRequested(AActor* Breacher);

private:
	/** Explicit native root so reparented BP-only doors keep a stable hierarchy — without it,
	 *  the SCS root fallback can promote DoorwayTrigger to actor root. The BP's own component
	 *  tree attaches under this at identity. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Door", meta = (AllowPrivateAccess))
	TObjectPtr<USceneComponent> DoorRoot;

	/** Set for doors placed already-open in the level, so CanBreach starts honest. The BP's own
	 *  state must match (its open flag set / leaf posed open). */
	UPROPERTY(EditAnywhere, Category = "Door|State", meta = (AllowPrivateAccess))
	bool bStartsOpen = false;

	/** If the Blueprint hasn't reported fully-open this long after a breach, assume the wiring
	 *  is missing: restore pawn collision and make the door breachable again (with a warning). */
	UPROPERTY(EditAnywhere, Category = "Door|State", meta = (AllowPrivateAccess, ClampMin = "1.0"))
	float BreachNotifyTimeout = 5.f;

	/** Pawn responses captured when a breach starts, restored when the BP reports a state
	 *  change (or the timeout fires). Keyed per mesh so glass/overlap setups restore exactly. */
	UPROPERTY(Transient)
	TMap<TObjectPtr<UStaticMeshComponent>, TEnumAsByte<ECollisionResponse>> SavedPawnResponses;

	/** C++-tracked mirror of the Blueprint's open state, fed by NotifyDoorStateChanged. */
	bool bDoorOpen = false;

	/** True from Breach until the BP reports back (or the timeout fires) — blocks re-breach. */
	bool bOpenInFlight = false;

	/** One-shot guard so a missing-wiring door doesn't spam the timeout warning per pawn entry. */
	bool bNotifyWiringWarned = false;

	FTimerHandle NotifyTimeoutHandle;

	/** Re-arms RestorePawnCollision while a pawn is still inside a leaf's sweep volume. */
	FTimerHandle RestoreRetryHandle;

	/** Meshes stop blocking pawns while the BP timeline swings, so path-following AI never
	 *  stalls against a half-open leaf. Restored by RestorePawnCollision. */
	void ApplyPawnPassThrough();
	void RestorePawnCollision();
	void HandleNotifyTimeout();

	/** True if any pawn overlaps a movable leaf's bounds — re-blocking would launch it. */
	bool IsAnyPawnOverlappingLeaf() const;
};
