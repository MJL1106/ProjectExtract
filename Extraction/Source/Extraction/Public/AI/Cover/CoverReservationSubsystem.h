// Post-vacate cooldown and intended-occupancy tracking for AICS covers.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "CoverSystemPublicData.h"
#include "CoverReservationSubsystem.generated.h"

class ACoverSystem;

USTRUCT()
struct FVacateStamp
{
	GENERATED_BODY()

	TWeakObjectPtr<AController> Vacater;
	double VacateTime = 0.0;
};

UCLASS()
class EXTRACTION_API UCoverReservationSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Deinitialize() override;

	// --- Post-vacate cooldown ---

	/** Stamps this cover as just-vacated by Controller. Prunes stale entries opportunistically. */
	void MarkVacated(const FCoverHandle& Handle, AController* Controller);

	/** True if Handle was vacated by Controller less than CooldownSeconds ago. */
	bool IsOnPostVacateCooldown(const FCoverHandle& Handle, const AController* Controller, float CooldownSeconds) const;

	// --- Intended occupancy (where agents are heading) ---

	/** Declares that Controller intends to occupy Handle (set on move-start). */
	void SetIntendedCover(AController* Controller, const FCoverHandle& Handle);

	/** Clears Controller's intention (on arrival, abort, or task end). */
	void ClearIntendedCover(AController* Controller);

	/** Fills Out with live FCover data for all currently-intended covers (skips stale controllers).
	 *  @param ExcludeController  Skip this controller's intent (pass the querier to avoid spacing off self).
	 *  @param RequiredControllerClass  If non-null, only include intents whose controller IsA this class. */
	void GetIntendedCovers(TArray<FCover>& Out, const AController* ExcludeController = nullptr,
		const UClass* RequiredControllerClass = nullptr);

	/** Fills Out with (intending controller's pawn, live cover) for every current intent except
	 *  ExcludeController's. Pawn may be null for a controller mid-repossession — callers filter.
	 *  Skips stale controllers (pruned in place, same as GetIntendedCovers). */
	void GetIntendedCoverOwners(TArray<TPair<APawn*, FCover>>& Out, const AController* ExcludeController);

	/** True when any controller OTHER than IgnoreController has declared intent for Handle. O(1) via reverse index. */
	bool IsCoverIntendedByOther(const FCoverHandle& Handle, const AController* IgnoreController) const;

	/** True when Controller currently has a valid intended cover stamped (heading to cover but not yet arrived). */
	bool HasIntendedCover(const AController* Controller) const;

	/** True + fills OutHandle when Controller has a valid intended cover stamped. */
	bool GetIntendedCover(const AController* Controller, FCoverHandle& OutHandle) const;

private:

	/** Handle -> per-vacater stamps (multiple agents can vacate the same point). Pruned on MarkVacated writes. */
	TMap<FCoverHandle, TArray<FVacateStamp>> VacateMap;

	/** Controller -> intended cover handle. */
	TMap<TWeakObjectPtr<AController>, FCoverHandle> IntendedMap;

	/** Fix 8: reverse index — Handle -> count of controllers intending that handle.
	 *  Maintained by SetIntendedCover/ClearIntendedCover for O(1) IsCoverIntendedByOther. */
	TMap<FCoverHandle, int32> IntendedHandleRefCount;

	/** Hard ceiling for pruning: entries older than this are removed regardless of controller state. */
	static constexpr double MaxVacateStaleness = 30.0;

	/** Minimum interval between prune scans. */
	static constexpr double PruneThrottleInterval = 1.0;

	/** Last time PruneStaleVacateEntries ran (throttled). */
	double LastPruneTime = -PruneThrottleInterval;

	void PruneStaleVacateEntries();
};
