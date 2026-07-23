// ADoorBase — shared spine for every door the AI can open. Owns the doorway approach trigger
// (enemies/companion push the door open on entry), and provides generic bounds-based breach
// stand/post-breach geometry so companion breach works on any subclass. Children supply the
// actual opening: ABreachableDoor swings in C++, AScriptedDoor defers to Blueprint logic.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "World/Breachable.h"
#include "DoorBase.generated.h"

class AEnemyCharacter;
class UBoxComponent;
class UStaticMeshComponent;
class USoundBase;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDoorOpened, AActor*, Door);

UCLASS(Abstract)
class EXTRACTION_API ADoorBase : public AActor, public IBreachable
{
	GENERATED_BODY()

public:
	ADoorBase();

	/** Fired once, when this door first reaches its fully-open state. */
	UPROPERTY(BlueprintAssignable, Category = "Door|Events")
	FOnDoorOpened OnDoorOpened;

	// --- IBreachable ---
	virtual void Breach_Implementation(AActor* Breacher) override;
	virtual bool CanBreach_Implementation() const override;
	virtual bool GetBreachStandPoint_Implementation(const AActor* Breacher, FVector& OutLocation, FRotator& OutFacing) const override;
	virtual bool GetPostBreachPoint_Implementation(const AActor* Breacher, bool bEnterRoom, FVector& OutLocation) const override;
	virtual bool ShouldForcePushThrough_Implementation() const override { return bForcePushThrough; }

	// --- External gate (level-script door lock) ---

	/** Lock or unlock from an external gate actor (e.g. ACompanionModeDoorGate). Authority-only. */
	UFUNCTION(BlueprintCallable, Category = "Door|Gate")
	void SetExternalGateLocked(bool bLocked);

	UFUNCTION(BlueprintPure, Category = "Door|Gate")
	bool IsExternalGateLocked() const { return bExternalGateLocked; }

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
#if WITH_DEV_AUTOMATION_TESTS
	bool TestHasBroadcastDoorOpened() const { return bDoorOpenedBroadcast; }
#endif

	/** While suppressed, the doorway trigger never auto-opens this door. Set by the companion
	 *  breach task while it owns the door — otherwise the companion walking to its stand point
	 *  trips the trigger and the door pops open with no montage, failing the commanded breach. */
	void SetAutoOpenSuppressed(bool bSuppressed) { bAutoOpenSuppressed = bSuppressed; }

	/** True while the doorway is acoustically clear — the leaf is open (or opening). Children
	 *  override; base doors report closed. Used by AIAcoustics door classification. */
	virtual bool IsOpenForAcoustics() const { return false; }

	/** Checkpoint fast-forward: put the door into its opened end-state (unlocking if needed).
	 *  Safe to call on an already-open door. Children override; base is a no-op. */
	virtual void ForceOpenInstant() {}

	/** World-space doorway centre for acoustic portal traces — the closed-bounds centre, NOT the
	 *  actor location (which sits at the hinge on swing doors). */
	FVector GetAcousticPortalPoint() const;

	/** True when AI pawns can push this door open via the doorway trigger. A search-through ping
	 *  on a door the companion cannot open would shove it against the closed leaf until timeout. */
	bool CanAutoOpenForAI() const { return bAutoOpenForAI; }

protected:
	virtual void PostInitializeComponents() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** True for meshes that are the door's swinging barrier — they keep blocking pawns. Non-leaf
	 *  trim (frames) drops pawn collision entirely: frame sill bars span the walkway, so once
	 *  door surfaces are unwalkable a blocking sill walls the doorway off instead of being
	 *  stepped over. Default heuristic: movable meshes are leaves, static meshes are trim. */
	virtual bool IsDoorLeafMesh(const UStaticMeshComponent& Mesh) const;

	/** Externally locked by a level-script gate (e.g. companion-mode prerequisite). While set,
	 *  CanBreach and TryUnlock refuse to open. Replicated so clients see the locked state. */
	UPROPERTY(Replicated, Transient)
	bool bExternalGateLocked = false;

	/** Approach volume spanning both sides of the doorway — AI pawns entering it push the door
	 *  open (see OnDoorwayOverlap). Created here, but every subclass ctor MUST SetupAttachment
	 *  it to its root (unattached components do NOT auto-parent — they stick at world origin).
	 *  Size/position the Box Extent per door in the BP. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Door|AutoOpen")
	TObjectPtr<UBoxComponent> DoorwayTrigger;

	/** When true, any AI pawn (enemy or companion) entering DoorwayTrigger pushes the door open
	 *  (CanBreach gates it — locked/already-open doors excluded). Patrol, search, chase, and
	 *  companion follow all qualify; no awareness gating. The player never auto-opens. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door|AutoOpen")
	bool bAutoOpenForAI = true;

	/** Played at the acoustic portal when the leaf starts swinging (any opener: AI auto-open,
	 *  keycard unlock, breach). The checkpoint fast-forward ForceOpenInstant stays silent. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door|Audio")
	TObjectPtr<USoundBase> OpenSound;

	/** When true the companion pushes through into the room after breaching this door even in
	 *  Normal/Stealth mode (Combat already does). Movement only — montage and noise stay
	 *  mode-appropriate. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door|Breach")
	bool bForcePushThrough = false;

	/** Clearance (cm) between the breacher's capsule and the door panel's surface at the breach
	 *  stand point. The full standoff is computed per door and per breacher: panel half-thickness
	 *  + capsule radius + this gap — so thick doors and big pawns still stand correctly. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door|Opening", meta = (ClampMin = "10.0"))
	float BreachReachGap = 25.f;

	/** How far past the doorway plane (cm) the post-breach ENTER point sits (Loud breach walks in). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door|Opening", meta = (ClampMin = "50.0"))
	float PostBreachEnterDepth = 250.f;

	/** Lateral offset (cm) from the doorway centre for both post-breach points — inside it clears
	 *  the swung leaf, outside it clears the opening. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door|Opening", meta = (ClampMin = "50.0"))
	float PostBreachLateralOffset = 170.f;

	/** Actor-local AABB of every static mesh on this door (panel + frame; the trigger is not a
	 *  mesh so it never pollutes the box). Prefers the closed-state BeginPlay snapshot — live
	 *  bounds rotate with an open leaf and would flip the thin-axis pick. False when no meshes. */
	bool GetDoorLocalBounds(FBox& OutLocalBox) const;

	/** Capsule radius for standoff math, with a sane fallback for non-Character breachers. */
	static float GetBreacherCapsuleRadius(const AActor* Breacher);

	/** Nav-projected enter-room point. A stairwell door's preferred lateral side can hang over a
	 *  stair void where goal projection fails and the enter move is rejected — so this tries the
	 *  preferred lateral sign, the mirrored sign, then the doorway centre, each projected with a
	 *  vertical extent generous enough to catch a landing above or below the breacher's floor.
	 *  False when no candidate lands on navmesh. */
	bool PickNavigableEnterPoint(const FVector& Center, const FVector& Normal, const FVector& Lateral,
		float PreferredLateralSign, float BreacherZ, FVector& OutLocation) const;

	/** Capture the closed-state mesh bounds now. Only call while the door is actually closed. */
	void SnapshotClosedBounds();

	bool HasClosedBoundsSnapshot() const { return ClosedDoorLocalBounds.IsValid != 0; }

	/** False for doors posed open at level start (bStartsOpen) — the BeginPlay snapshot is
	 *  skipped and taken on the first close instead, so "closed bounds" never hold an open leaf. */
	virtual bool IsClosedAtBeginPlay() const { return true; }

	/** Auto-open filter + gate for one actor (the OnDoorwayOverlap body). */
	void TryAutoOpenFor(AActor* OtherActor);

	/** Re-run the auto-open filter for every pawn already inside the trigger. Call after the
	 *  door becomes breachable again (a pack door re-closing) — no new BeginOverlap fires for
	 *  a pawn that never left the volume. */
	void RescanDoorwayForAutoOpen();

	/** Concrete doors call this from their fully-open completion path. */
	void BroadcastDoorOpenedOnce();

private:
	/** AI pawn walked into the doorway approach volume — push the door open if it can open. */
	UFUNCTION()
	void OnDoorwayOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	/** Pawn left the doorway approach volume — drop its doorway move-ignore pairs. */
	UFUNCTION()
	void OnDoorwayOverlapEnd(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

	/** Enemies currently inside DoorwayTrigger. Everyone in this list mutually move-ignores
	 *  everyone else, so a doorway funnel squeezes through instead of hard-stacking on capsules.
	 *  Enemy-vs-enemy only and scoped to the trigger volume — player/companion still block, and
	 *  movement everywhere else is untouched. Lever: the per-door trigger Box Extent. */
	TArray<TWeakObjectPtr<AEnemyCharacter>> EnemiesInDoorway;

	/** Add/remove the mutual move-ignore pairs between Enemy and every current list member. */
	void SetDoorwayIgnorePairs(AEnemyCharacter* Enemy, bool bIgnore);

	/** Closed-state mesh bounds snapshot (actor-local), taken at BeginPlay. */
	FBox ClosedDoorLocalBounds = FBox(ForceInit);

	/** See SetAutoOpenSuppressed. */
	bool bAutoOpenSuppressed = false;

	bool bDoorOpenedBroadcast = false;
};
