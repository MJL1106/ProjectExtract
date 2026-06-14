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
	 * Result = GetLocationAtAlpha(Alpha) - GetForwardDirection() * Standoff.
	 * Standoff should be capsule radius + DA->CoverStandoffPadding.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cover|Geometry")
	FVector GetBehindCoverPosition(float Alpha, float Standoff) const;

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
