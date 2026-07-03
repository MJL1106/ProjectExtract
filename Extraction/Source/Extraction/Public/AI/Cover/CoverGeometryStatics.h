// Stateless geometry helpers for AICS FCoverData — all cover-position math in one place.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "AI/Cover/CoverPoseTypes.h"
#include "CoverSystemPublicData.h"
#include "CoverGeometryStatics.generated.h"

UCLASS()
class EXTRACTION_API UCoverGeometryStatics : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	/** Derive the effective cover height from baked FCoverData flags.
	 *  Stand when a standing side-peek exists AND no front-crouch (tall wall, side-peek only).
	 *  Crouch otherwise. This replaces all raw bCrouchedCover height checks. */
	UFUNCTION(BlueprintPure, Category = "Cover|Geometry")
	static ECoverHeight GetCoverHeight(const FCoverData& Data);

	/** Position behind cover wall at standoff distance, on the cover's Z plane. */
	UFUNCTION(BlueprintPure, Category = "Cover|Geometry")
	static FVector GetHunkerPosition(const FCoverData& Data, float Standoff);

	/** Approach waypoint: hunker XY with caller's Z (preserves nav height). */
	UFUNCTION(BlueprintPure, Category = "Cover|Geometry")
	static FVector GetApproachPosition(const FCoverData& Data, const FVector& PawnLoc, float Standoff);

	/** Lateral peek position for a given lean side. */
	UFUNCTION(BlueprintPure, Category = "Cover|Geometry")
	static FVector GetLeanPeekPosition(const FCoverData& Data, ECoverLean Lean, float LeanOffset = 65.f);

	/** Pick the best valid lean side for the current stance vs the threat direction. */
	UFUNCTION(BlueprintPure, Category = "Cover|Geometry")
	static ECoverLean ResolveLeanSide(const FCoverData& Data, bool bCrouched, const FVector& ThreatLoc);

	/** True when a chest-height trace from the hunker position to ThreatLoc is BLOCKED by geometry
	 *  (i.e. the cover wall protects the body). Ported from IsSlotBodyProtected. */
	UFUNCTION(BlueprintPure, Category = "Cover|Geometry", meta = (WorldContext = "WorldContextObject"))
	static bool IsThreatCovered(const UObject* WorldContextObject, const FCoverData& Data,
		const FVector& ThreatLoc, float Standoff, float ChestHeight,
		const AActor* IgnoreThreatActor, const AActor* IgnorePawn);

	/** Outward fire direction (away from the wall). */
	UFUNCTION(BlueprintPure, Category = "Cover|Geometry")
	static FVector GetFireArcForward(const FCoverData& Data);

	/** True when two cover points share the same wall (within angular tolerance). */
	UFUNCTION(BlueprintPure, Category = "Cover|Geometry")
	static bool IsSameWall(const FCoverData& A, const FCoverData& B, float CosThreshold = 0.9397f);

	/** True when the cover point carries a stance-matched side flag on the threat-facing side
	 *  (i.e. the lateral direction toward ThreatLoc has a valid lean flag for the given stance). */
	UFUNCTION(BlueprintPure, Category = "Cover|Geometry")
	static bool HasThreatFacingSideFlag(const FCoverData& Data, const FVector& ThreatLoc, bool bCrouched);

	/** True when the pawn can see the threat from its resolved lean-peek position. */
	UFUNCTION(BlueprintPure, Category = "Cover|Geometry", meta = (WorldContext = "WorldContextObject"))
	static bool CanPeekShoot(const UObject* WorldContextObject, const FCoverData& Data,
		bool bCrouched, const FVector& ThreatLoc, float EyeHeight,
		const AActor* IgnoreThreatActor, const AActor* IgnorePawn,
		float LeanOffset = 65.f);
};
