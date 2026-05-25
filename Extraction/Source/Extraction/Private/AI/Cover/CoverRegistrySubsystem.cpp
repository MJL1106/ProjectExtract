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
		const float DistToQuerierSq = FVector::DistSquared(QuerierLoc, Slot->GetActorLocation());
		const float DistToQuerier = FMath::Sqrt(DistToQuerierSq);
		const bool bClaimed = Slot->IsClaimed();

		// Stand cover with no peekable corners = hide-only; useless for combat.
		if (Slot->Height == ECoverHeight::Stand && !Slot->bIsPeekableCornerStart && !Slot->bIsPeekableCornerEnd)
		{
			UE_LOG(LogCompanionAI, Log, TEXT("CoverRegistry: slot=%s -> REJECT-hide-only"), *Slot->GetName());
			continue;
		}

		// Must be unclaimed (or claimed by the querier's actor — handled via TryClaim being idempotent)
		if (bClaimed)
		{
			UE_LOG(LogCompanionAI, Log, TEXT("CoverRegistry: slot=%s dist=%.0f claimed=1 inArc=0 -> REJECT-claimed"), *Slot->GetName(), DistToQuerier);
			continue;
		}

		// Must be within search radius
		if (DistToQuerierSq > MaxRadiusSq)
		{
			UE_LOG(LogCompanionAI, Log, TEXT("CoverRegistry: slot=%s dist=%.0f claimed=0 inArc=0 -> REJECT-radius"), *Slot->GetName(), DistToQuerier);
			continue;
		}

		// Target must be within the slot's fire arc
		const bool bInArc = Slot->IsTargetInFireArc(TargetLoc);
		if (!bInArc)
		{
			UE_LOG(LogCompanionAI, Log, TEXT("CoverRegistry: slot=%s dist=%.0f claimed=0 inArc=0 -> REJECT-arc"), *Slot->GetName(), DistToQuerier);
			continue;
		}

		// New filter: slot must offer at least one position with LoS to target.
		{
			UWorld* World = GetWorld();
			auto HasLosFrom = [World, Target, &TargetLoc](const FVector& EyePos) -> bool
			{
				if (!World) return false;
				FHitResult Hit;
				FCollisionQueryParams Params;
				Params.AddIgnoredActor(Target);
				const bool bBlocked = World->LineTraceSingleByChannel(Hit, EyePos, TargetLoc, ECC_Visibility, Params);
				return !bBlocked || Hit.GetActor() == Target;
			};

			constexpr float EyeHeight = 150.f;
			constexpr float ApexProbeDist = 100.f;
			bool bHasAnyLos = HasLosFrom(Slot->GetActorLocation() + FVector(0.f, 0.f, EyeHeight));

			if (!bHasAnyLos && Slot->Height == ECoverHeight::Stand)
			{
				const FVector LineDir = Slot->GetLineDirection();
				if (Slot->bIsPeekableCornerStart)
				{
					const FVector StartApex = Slot->GetLeftEdge() + (-LineDir) * ApexProbeDist;
					if (HasLosFrom(StartApex + FVector(0.f, 0.f, EyeHeight))) bHasAnyLos = true;
				}
				if (!bHasAnyLos && Slot->bIsPeekableCornerEnd)
				{
					const FVector EndApex = Slot->GetRightEdge() + LineDir * ApexProbeDist;
					if (HasLosFrom(EndApex + FVector(0.f, 0.f, EyeHeight))) bHasAnyLos = true;
				}
			}

			if (!bHasAnyLos)
			{
				UE_LOG(LogCompanionAI, Log, TEXT("CoverRegistry: slot=%s dist=%.0f -> REJECT-no-los"), *Slot->GetName(), DistToQuerier);
				continue;
			}
		}

		// Score the slot
		const FVector StandPos = Slot->GetStandPosition();
		const float ProximityScore = 1.f - FMath::Clamp(DistToQuerier / MaxRadius, 0.f, 1.f);

		const float DistToTarget = FVector::Dist(StandPos, TargetLoc);
		const float DistanceScore = FMath::Clamp((DistToTarget - IdealDistMin) / IdealDistRange, 0.f, 1.f);

		const float StandScore = Slot->CanStandFireOver() ? 1.f : 0.5f;

		const float TotalScore = Weight_Proximity * ProximityScore
			+ Weight_Distance * DistanceScore
			+ Weight_StandFire * StandScore;

		UE_LOG(LogCompanionAI, Log, TEXT("CoverRegistry: slot=%s dist=%.0f claimed=0 inArc=1 -> SCORE=%.2f"), *Slot->GetName(), DistToQuerier, TotalScore);

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
