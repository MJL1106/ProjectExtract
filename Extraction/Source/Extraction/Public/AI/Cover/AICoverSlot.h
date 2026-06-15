#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CoverSlotTypes.h"
#include "AICoverSlot.generated.h"

class UArrowComponent;
class UBoxComponent;

DECLARE_LOG_CATEGORY_EXTERN(LogCoverSlot, Log, All);

UCLASS(Blueprintable)
class EXTRACTION_API AAICoverSlot : public AActor
{
	GENERATED_BODY()

public:
	AAICoverSlot();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnConstruction(const FTransform& Transform) override;

public:
	// --- Cover metadata (designer-authored) ---

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cover|Config")
	ECoverHeight Height = ECoverHeight::Stand;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cover|Config")
	ECoverPeekSide PeekPreference = ECoverPeekSide::Either;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cover|Config")
	float FireArcDegrees = 120.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cover|Config")
	float CoverRadius = 60.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cover|Config")
	bool bIsPeekableCornerStart = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cover|Config")
	bool bIsPeekableCornerEnd = false;

	// --- Claim API (server-only, non-replicated) ---

	bool IsClaimed() const;
	bool IsClaimedBy(AActor* Claimer) const;
	bool TryClaim(AActor* Claimer);
	void Release(AActor* Claimer);

	// --- Post-vacate cooldown (P4, server-only, non-replicated) ---

	/** Stamps this slot as deliberately vacated by Vacater (used by the cover-switch path, NOT by normal Release). */
	void MarkVacatedForSwitch(AActor* Vacater);

	/** True if Pawn is the actor that just vacated this slot and Cooldown seconds have not yet elapsed. */
	bool IsOnPostVacateCooldownFor(const AActor* Pawn, float Cooldown) const;

	// --- Geometry helpers ---

	bool IsTargetInFireArc(const FVector& TargetLoc) const;

	/**
	 * World-space position behind the wall at the given Alpha.
	 * Standoff is clamped to CoverBoundsBox X half-extent so the result never
	 * exceeds the authored cover volume depth.
	 * Callers pass CapsuleRadius + DA->CoverStandoffPadding as Standoff.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cover|Geometry")
	FVector GetBehindCoverPosition(float Alpha, float Standoff) const;

	/**
	 * Stand-height slot only: behind-cover position AT the peekable corner nearest the threat,
	 * so a flush enemy lands at the corner (registry-proven LOS) instead of mid-wall.
	 * OutAlpha receives the chosen endpoint alpha (0=LeftEdge, 1=RightEdge). Falls back to the
	 * projected-alpha behind-cover position for non-Stand slots or slots with no peekable corner.
	 * Standoff is the same capsule+padding value passed to GetBehindCoverPosition.
	 */
	FVector GetStandPeekPosition(const FVector& ThreatLoc, float Standoff, float& OutAlpha) const;

	/**
	 * Returns true when the companion's hunker-position at (Alpha, Standoff) is geometry-shielded
	 * from ThreatLoc: a chest-height ECC_Visibility trace from GetBehindCoverPosition(...)+ChestHeight
	 * must be BLOCKED by something other than IgnoreThreat (i.e. a wall is interposed).
	 * IgnoreThreat + IgnorePawn are both excluded from the trace so neither self-hits nor the threat
	 * actor counts as the blocking object.
	 * Mirror of the enemy's file-static IsSlotBodyProtected in BTTask_EnemyCombatFire.cpp.
	 */
	bool IsBodyShieldedFrom(const FVector& ThreatLoc, float Alpha, float Standoff, float ChestHeight,
		const AActor* IgnoreThreat, const AActor* IgnorePawn) const;

	/**
	 * Returns true when at least one probe from the slot can see ThreatLoc.
	 * Checks eye-height centre and peekable-corner apexes for Stand-height slots.
	 * IgnoreActor is excluded from the trace (pass the threat actor so self-hits are ignored).
	 */
	static bool HasLOSToThreat(UWorld* World, const AAICoverSlot* Slot,
		const FVector& ThreatLoc, const AActor* IgnoreActor);

	// Crouch cover = chest-high wall, can fire over the top.
	bool CanStandFireOver() const;

	FVector GetStandPosition() const;

	float GetCoverLineHalfLength() const;

	// --- Continuous endpoint API ---

	/** World-space location of the left edge of the cover line. */
	FVector GetLeftEdge() const;

	/** World-space location of the right edge of the cover line. */
	FVector GetRightEdge() const;

	/** Normalized 2D direction from LeftEdge to RightEdge. */
	FVector GetLineDirection() const;

	/** 2D distance between LeftEdge and RightEdge in cm. */
	float GetLineLength() const;

	/**
	 * Perpendicular to the cover line, pointing away from the wall.
	 * Sign is disambiguated by the actor's forward vector so a slightly mis-rotated
	 * actor still yields the correct outward direction.
	 */
	FVector GetForwardDirection() const;

	/** World-space position at the given Alpha [0,1] along the cover line. */
	FVector GetLocationAtAlpha(float Alpha) const;

	/** Projects WorldLoc onto the cover line and returns its Alpha [0,1]. */
	float GetAlphaFromLocation(const FVector& WorldLoc) const;

	/** Returns true if WorldLoc projects onto this slot's cover line with perpendicular distance under the overlap threshold. */
	bool IsLocationOverlappingCoverLine(const FVector& WorldLoc) const;

	/**
	 * Returns true when Alpha is within Epsilon of an endpoint that is flagged as a peekable corner.
	 * Alpha=0 tests bIsPeekableCornerStart; Alpha=1 tests bIsPeekableCornerEnd.
	 */
	bool IsAlphaAtPeekableCorner(float Alpha, float Epsilon = 0.05f) const;

private:
	// Server-only — intentionally not replicated
	TWeakObjectPtr<AActor> ClaimedBy;

	// Post-vacate cooldown (P4) — server-only, non-replicated. Stamped by MarkVacatedForSwitch.
	TWeakObjectPtr<AActor> LastVacatedBy;
	double LastVacatedTime = -1.0e9;

	// Debug viz — kept out of editor-only guard so extent is available at runtime
	UPROPERTY(VisibleAnywhere, Category = "Cover|Debug")
	TObjectPtr<UArrowComponent> ForwardArrow;

	UPROPERTY(VisibleAnywhere, Category = "Cover|Debug")
	TObjectPtr<UBoxComponent> CoverBoundsBox;

	UPROPERTY(VisibleAnywhere, Category = "Cover|Endpoints")
	TObjectPtr<UArrowComponent> LeftEdgeArrow;

	UPROPERTY(VisibleAnywhere, Category = "Cover|Endpoints")
	TObjectPtr<UArrowComponent> RightEdgeArrow;

	void UpdateDebugViz();
};
