#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "CoverRegistrySubsystem.generated.h"

class AAICoverSlot;

DECLARE_LOG_CATEGORY_EXTERN(LogCoverRegistry, Log, All);

UCLASS()
class EXTRACTION_API UCoverRegistrySubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	void RegisterSlot(AAICoverSlot* Slot);
	void UnregisterSlot(AAICoverSlot* Slot);

	void GetSlotsInRadius(const FVector& Origin, float Radius, TArray<AAICoverSlot*>& OutSlots) const;

	/**
	 * OutScore receives the best slot's score; pass nullptr to ignore.
	 * QuerierPawn + PostVacateCooldown (P4): if both are set, any slot that QuerierPawn just
	 * vacated via MarkVacatedForSwitch is excluded until the cooldown elapses (prevents snap-back).
	 */
	AAICoverSlot* FindBestCoverFor(const FVector& QuerierLoc, AActor* Target, float MaxRadius, float* OutScore = nullptr,
		const AActor* QuerierPawn = nullptr, float PostVacateCooldown = 0.f) const;

	/**
	 * Scores a single slot using the same formula as FindBestCoverFor.
	 * Returns -1 if Slot or Target is invalid.
	 * Exposed so callers (e.g. BTService_CoverSwitchMonitor) can compare the current
	 * slot's score against a candidate without re-running the full search.
	 * Pass a precomputed DistToQuerier >= 0 to skip the internal recompute (used by the
	 * picker loop which already has it); pass the default -1.f to let it compute.
	 */
	static float ScoreSlotFor(const AAICoverSlot* Slot, const FVector& QuerierLoc, const AActor* Target, float MaxRadius, float PrecomputedDist = -1.f);

private:
	TArray<TWeakObjectPtr<AAICoverSlot>> RegisteredSlots;

	bool bReserved = false;

	void PruneStaleSlots();
};
