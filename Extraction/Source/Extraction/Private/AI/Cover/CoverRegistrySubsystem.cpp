#include "CoverRegistrySubsystem.h"
#include "AICoverSlot.h"
#include "CompanionAIController.h" // for LogCompanionAI

DEFINE_LOG_CATEGORY(LogCoverRegistry);

// Scoring weights — tune here, not in scoring logic
static constexpr float Weight_Proximity   = 1.0f;
static constexpr float Weight_Distance    = 0.7f;
static constexpr float Weight_StandFire   = 0.3f;

// Target distance scoring: sweet spot between MinIdealDist and MaxIdealDist (cm)
static constexpr float IdealDistMin       = 500.f;
static constexpr float IdealDistRange     = 700.f;

// Initial reservation size for the slot registry
static constexpr int32 InitialReserveSize = 32;

void UCoverRegistrySubsystem::RegisterSlot(AAICoverSlot* Slot)
{
	if (!IsValid(Slot))
		return;

	if (!bReserved)
	{
		RegisteredSlots.Reserve(InitialReserveSize);
		bReserved = true;
	}

	RegisteredSlots.AddUnique(Slot);
	UE_LOG(LogCoverRegistry, Verbose, TEXT("Registered cover slot: %s"), *Slot->GetName());
}

void UCoverRegistrySubsystem::UnregisterSlot(AAICoverSlot* Slot)
{
	if (!IsValid(Slot))
		return;

	RegisteredSlots.RemoveSingleSwap(Slot);
	UE_LOG(LogCoverRegistry, Verbose, TEXT("Unregistered cover slot: %s"), *Slot->GetName());
}

void UCoverRegistrySubsystem::GetSlotsInRadius(const FVector& Origin, float Radius, TArray<AAICoverSlot*>& OutSlots) const
{
	const float RadiusSq = Radius * Radius;

	for (const TWeakObjectPtr<AAICoverSlot>& WeakSlot : RegisteredSlots)
	{
		if (!WeakSlot.IsValid())
			continue;

		AAICoverSlot* Slot = WeakSlot.Get();
		if (!IsValid(Slot))
			continue;

		if (Slot->IsClaimed())
			continue;

		if (FVector::DistSquared(Origin, Slot->GetActorLocation()) <= RadiusSq)
			OutSlots.Add(Slot);
	}
}

AAICoverSlot* UCoverRegistrySubsystem::FindBestCoverFor(const FVector& QuerierLoc, AActor* Target, float MaxRadius) const
{
	if (!IsValid(Target))
		return nullptr;

	const FVector TargetLoc = Target->GetActorLocation();
	const float MaxRadiusSq = MaxRadius * MaxRadius;

	AAICoverSlot* BestSlot = nullptr;
	float BestScore = -1.f;
	float BestDistSq = FLT_MAX;

	int32 SlotCount = 0;

	for (const TWeakObjectPtr<AAICoverSlot>& WeakSlot : RegisteredSlots)
	{
		if (!WeakSlot.IsValid())
			continue;

		AAICoverSlot* Slot = WeakSlot.Get();
		if (!IsValid(Slot))
			continue;

		++SlotCount;
		const FString SlotName = Slot->GetName();
		const float DistToQuerierSq = FVector::DistSquared(QuerierLoc, Slot->GetActorLocation());
		const float DistToQuerier = FMath::Sqrt(DistToQuerierSq);
		const bool bClaimed = Slot->IsClaimed();

		// Must be unclaimed (or claimed by the querier's actor — handled via TryClaim being idempotent)
		if (bClaimed)
		{
			UE_LOG(LogCompanionAI, Log, TEXT("CoverRegistry: slot=%s dist=%.0f claimed=1 inArc=0 -> REJECT-claimed"), *SlotName, DistToQuerier);
			continue;
		}

		// Must be within search radius
		if (DistToQuerierSq > MaxRadiusSq)
		{
			UE_LOG(LogCompanionAI, Log, TEXT("CoverRegistry: slot=%s dist=%.0f claimed=0 inArc=0 -> REJECT-radius"), *SlotName, DistToQuerier);
			continue;
		}

		// Target must be within the slot's fire arc
		const bool bInArc = Slot->IsTargetInFireArc(TargetLoc);
		if (!bInArc)
		{
			UE_LOG(LogCompanionAI, Log, TEXT("CoverRegistry: slot=%s dist=%.0f claimed=0 inArc=0 -> REJECT-arc"), *SlotName, DistToQuerier);
			continue;
		}

		// Score the slot
		const FVector StandPos = Slot->GetStandPosition();
		const float ProximityScore = 1.f - FMath::Clamp(DistToQuerier / MaxRadius, 0.f, 1.f);

		const float DistToTarget = FVector::Dist(StandPos, TargetLoc);
		const float DistanceScore = FMath::Clamp((DistToTarget - IdealDistMin) / IdealDistRange, 0.f, 1.f);

		const float StandScore = Slot->CanStandFireFrom() ? 1.f : 0.5f;

		const float TotalScore = Weight_Proximity * ProximityScore
			+ Weight_Distance * DistanceScore
			+ Weight_StandFire * StandScore;

		UE_LOG(LogCompanionAI, Log, TEXT("CoverRegistry: slot=%s dist=%.0f claimed=0 inArc=1 -> SCORE=%.2f"), *SlotName, DistToQuerier, TotalScore);

		const bool bBetter = TotalScore > BestScore;
		const bool bTie = FMath::IsNearlyEqual(TotalScore, BestScore) && DistToQuerierSq < BestDistSq;
		if (bBetter || bTie)
		{
			BestScore = TotalScore;
			BestDistSq = DistToQuerierSq;
			BestSlot = Slot;
		}
	}

	UE_LOG(LogCompanionAI, Log, TEXT("CoverRegistry: queried %d slots, best=%s score=%.2f"), SlotCount, *GetNameSafe(BestSlot), BestScore);

	return BestSlot;
}

void UCoverRegistrySubsystem::PruneStaleSlots()
{
	RegisteredSlots.RemoveAll([](const TWeakObjectPtr<AAICoverSlot>& W) { return !W.IsValid(); });
}
