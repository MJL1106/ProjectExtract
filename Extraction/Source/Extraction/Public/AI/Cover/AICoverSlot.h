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

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cover|Config", meta = (ClampMin = "50.0"))
	float SubSlotSpacing = 100.f;

	/** Endpoint sub-slots are inset inward by this distance so the companion's home is behind the wall, not on the edge. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cover|Config", meta = (ClampMin = "0.0", ClampMax = "200.0"))
	float CornerInsetDistance = 40.f;

	// --- Claim API (server-only, non-replicated) ---

	bool IsClaimed() const;
	bool IsClaimedBy(AActor* Claimer) const;
	bool TryClaim(AActor* Claimer);
	void Release(AActor* Claimer);

	// --- Geometry helpers ---

	bool IsTargetInFireArc(const FVector& TargetLoc) const;

	// Crouch cover = chest-high wall, can fire over the top.
	bool CanStandFireOver() const;

	FVector GetStandPosition() const;

	float GetCoverLineHalfLength() const;
	int32 GetSubSlotCount() const;
	FVector GetSubSlotLocation(int32 Index) const;
	bool IsSubSlotPeekableCorner(int32 Index) const;

private:
	// Server-only — intentionally not replicated
	TWeakObjectPtr<AActor> ClaimedBy;

	// Debug viz — kept out of editor-only guard so extent is available at runtime
	UPROPERTY(VisibleAnywhere, Category = "Cover|Debug")
	TObjectPtr<UArrowComponent> ForwardArrow;

	UPROPERTY(VisibleAnywhere, Category = "Cover|Debug")
	TObjectPtr<UBoxComponent> CoverBoundsBox;

	void UpdateDebugViz();
};
