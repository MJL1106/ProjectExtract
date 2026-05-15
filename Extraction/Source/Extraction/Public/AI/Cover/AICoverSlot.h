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

	// --- Claim API (server-only, non-replicated) ---

	bool IsClaimed() const;
	bool IsClaimedBy(AActor* Claimer) const;
	bool TryClaim(AActor* Claimer);
	void Release(AActor* Claimer);

	// --- Geometry helpers ---

	bool IsTargetInFireArc(const FVector& TargetLoc) const;
	bool CanStandFireFrom() const;
	FVector GetStandPosition() const;

private:
	// Server-only — intentionally not replicated
	TWeakObjectPtr<AActor> ClaimedBy;

	// Debug viz (editor only)
	UPROPERTY(VisibleAnywhere, Category = "Cover|Debug")
	TObjectPtr<UArrowComponent> ForwardArrow;

#if WITH_EDITORONLY_DATA
	UPROPERTY(VisibleAnywhere, Category = "Cover|Debug")
	TObjectPtr<UBoxComponent> DebugBox;
#endif

	void UpdateDebugViz();
};
