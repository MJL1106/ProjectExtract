#include "CoverRegistrySubsystem.h"
#include "AICoverSlot.h"
#include "CompanionAIController.h" // for LogCompanionAI
#include "GameFramework/Character.h"
#include "Components/CapsuleComponent.h"

DEFINE_LOG_CATEGORY(LogCoverRegistry);

// Scoring weights — tune here, not in scoring logic
static constexpr float RegistryWeight_Proximity   = 1.0f;
static constexpr float RegistryWeight_Distance    = 0.7f;
// Part C de-bias: was Weight_StandFire = 0.3 with StandScore (crouch=1.0, stand=0.5),
// giving crouch +0.30 and stand +0.15 — a 0.15 differential disfavouring stand slots.
// Split into two independent weights so the terms are fully neutral between the two heights.
static constexpr float RegistryWeight_CoverBonus  = 0.15f;

// Target distance scoring: sweet spot between MinIdealDist and MaxIdealDist (cm)
static constexpr float RegistryIdealDistMin       = 500.f;
static constexpr float RegistryIdealDistRange     = 700.f;

// Initial reservation size for the slot registry
static constexpr int32 InitialReserveSize = 32;

// Fallback capsule radius (cm) when the querier is not an ACharacter or has no capsule.
// Matches the enemy's DefaultCapsuleRadius in BTTask_EnemyCombatFire.cpp.
static constexpr float DefaultCoverStandoffCapsule = 34.f;

// Small clearance padding (cm) added on top of capsule radius for the body-shield standoff.
// Keeps the trace origin visually inside the wall's protective volume.
static constexpr float CoverStandoffPadding = 10.f;

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

		// Per-instance kill-switch: slots tagged "CoverDisabled" in-editor sit out of all queries (AICS migration aid)
		if (Slot->ActorHasTag(TEXT("CoverDisabled")))
			continue;

		if (FVector::DistSquared(Origin, Slot->GetActorLocation()) <= RadiusSq)
			OutSlots.Add(Slot);
	}
}

AAICoverSlot* UCoverRegistrySubsystem::FindBestCoverFor(const FVector& QuerierLoc, AActor* Target, float MaxRadius, float* OutScore,
	const AActor* QuerierPawn, float PostVacateCooldown,
	bool bRequireBodyProtection, float ChestHeight) const
{
	if (!IsValid(Target))
		return nullptr;

	const FVector TargetLoc = Target->GetActorLocation();
	const float MaxRadiusSq = MaxRadius * MaxRadius;

	AAICoverSlot* BestSlot = nullptr;
	float BestScore = -1.f;
	float BestDistSq = FLT_MAX;

	int32 SlotCount = 0;

	// Build trace params once — identical across all slots, rebuilding per-slot is pure waste.
	UWorld* World = GetWorld();
	FCollisionQueryParams LoSParams(SCENE_QUERY_STAT(CompanionCoverLoS), false);
	LoSParams.AddIgnoredActor(Target);

	auto HasLosFrom = [World, Target, &TargetLoc, &LoSParams](const FVector& EyePos) -> bool
	{
		if (!World) return false;
		FHitResult Hit;
		const bool bBlocked = World->LineTraceSingleByChannel(Hit, EyePos, TargetLoc, ECC_Visibility, LoSParams);
		return !bBlocked || Hit.GetActor() == Target;
	};

	// Hoist loop-invariant standoff: depends only on QuerierPawn, not on individual slots.
	const ACharacter* QuerierChar = Cast<ACharacter>(QuerierPawn);
	const UCapsuleComponent* QuerierCap = QuerierChar ? QuerierChar->GetCapsuleComponent() : nullptr;
	const float QuerierCapsuleR = QuerierCap ? QuerierCap->GetScaledCapsuleRadius() : DefaultCoverStandoffCapsule;
	const float ProtectionStandoff = QuerierCapsuleR + CoverStandoffPadding;

	for (const TWeakObjectPtr<AAICoverSlot>& WeakSlot : RegisteredSlots)
	{
		if (!WeakSlot.IsValid())
			continue;

		AAICoverSlot* Slot = WeakSlot.Get();
		if (!IsValid(Slot))
			continue;

		// Per-instance kill-switch: slots tagged "CoverDisabled" in-editor sit out of all queries (AICS migration aid)
		if (Slot->ActorHasTag(TEXT("CoverDisabled")))
			continue;

		++SlotCount;
		const float DistToQuerierSq = FVector::DistSquared(QuerierLoc, Slot->GetActorLocation());
		const float DistToQuerier = FMath::Sqrt(DistToQuerierSq);
		const bool bClaimed = Slot->IsClaimed();

		// Stand cover with no peekable corners = hide-only; useless for combat.
		if (Slot->Height == ECoverHeight::Stand && !Slot->bIsPeekableCornerStart && !Slot->bIsPeekableCornerEnd)
		{
			if (UE_LOG_ACTIVE(LogCompanionAI, Verbose))
				UE_LOG(LogCompanionAI, Verbose, TEXT("CoverRegistry: slot=%s -> REJECT-hide-only"), *Slot->GetName());
			continue;
		}

		// Must be unclaimed (or claimed by the querier's actor — handled via TryClaim being idempotent)
		if (bClaimed)
		{
			if (UE_LOG_ACTIVE(LogCompanionAI, Verbose))
				UE_LOG(LogCompanionAI, Verbose, TEXT("CoverRegistry: slot=%s dist=%.0f claimed=1 -> REJECT-claimed"), *Slot->GetName(), DistToQuerier);
			continue;
		}

		// P4 — exclude a slot the querier just deliberately vacated, until the cooldown elapses (anti snap-back).
		if (Slot->IsOnPostVacateCooldownFor(QuerierPawn, PostVacateCooldown))
		{
			if (UE_LOG_ACTIVE(LogCompanionAI, Verbose))
				UE_LOG(LogCompanionAI, Verbose, TEXT("CoverRegistry: slot=%s dist=%.0f -> REJECT-post-vacate-cooldown"), *Slot->GetName(), DistToQuerier);
			continue;
		}

		// Must be within search radius
		if (DistToQuerierSq > MaxRadiusSq)
		{
			if (UE_LOG_ACTIVE(LogCompanionAI, Verbose))
				UE_LOG(LogCompanionAI, Verbose, TEXT("CoverRegistry: slot=%s dist=%.0f -> REJECT-radius"), *Slot->GetName(), DistToQuerier);
			continue;
		}

		// Target must be within the slot's fire arc
		const bool bInArc = Slot->IsTargetInFireArc(TargetLoc);
		if (!bInArc)
		{
			if (UE_LOG_ACTIVE(LogCompanionAI, Verbose))
				UE_LOG(LogCompanionAI, Verbose, TEXT("CoverRegistry: slot=%s dist=%.0f -> REJECT-arc"), *Slot->GetName(), DistToQuerier);
			continue;
		}

		// Slot must offer at least one position with LoS to target.
		{
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
				if (UE_LOG_ACTIVE(LogCompanionAI, Verbose))
					UE_LOG(LogCompanionAI, Verbose, TEXT("CoverRegistry: slot=%s dist=%.0f -> REJECT-no-los"), *Slot->GetName(), DistToQuerier);
				continue;
			}
		}

		// Body-protection hard reject: the companion's hunkered body must be shielded from the target
		// by world geometry. Slots whose wall is off to the side or behind the companion relative to
		// the target are discarded. Standoff is loop-invariant and hoisted above the loop.
		if (bRequireBodyProtection)
		{
			// Use the pawn's actual location for the alpha projection when available: the
			// CoverSwitchMonitor passes a player-relative FormationPoint as QuerierLoc, not the
			// pawn's real position, so projecting QuerierLoc would test an alpha the companion
			// won't actually stand at. MoveToCover passes QuerierLoc == PawnLoc, so this is a
			// no-op there. QuerierLoc is preserved for proximity scoring.
			const AActor* PawnActor = QuerierPawn;
			const FVector AlphaOrigin = IsValid(PawnActor) ? PawnActor->GetActorLocation() : QuerierLoc;
			const float Alpha = Slot->GetAlphaFromLocation(AlphaOrigin);

			if (!Slot->IsBodyShieldedFrom(TargetLoc, Alpha, ProtectionStandoff, ChestHeight, Target, QuerierPawn))
			{
				if (UE_LOG_ACTIVE(LogCompanionAI, Verbose))
					UE_LOG(LogCompanionAI, Verbose, TEXT("CoverRegistry: slot=%s dist=%.0f -> REJECT-not-protective"),
						*Slot->GetName(), DistToQuerier);
				continue;
			}
		}

		// Score the slot — pass DistToQuerier so ScoreSlotFor skips the recompute.
		const float TotalScore = UCoverRegistrySubsystem::ScoreSlotFor(Slot, QuerierLoc, Target, MaxRadius, DistToQuerier);

		if (UE_LOG_ACTIVE(LogCompanionAI, Verbose))
			UE_LOG(LogCompanionAI, Verbose, TEXT("CoverRegistry: slot=%s dist=%.0f inArc=1 -> SCORE=%.2f"), *Slot->GetName(), DistToQuerier, TotalScore);

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

	if (OutScore) *OutScore = BestScore;
	return BestSlot;
}

float UCoverRegistrySubsystem::ScoreSlotFor(const AAICoverSlot* Slot, const FVector& QuerierLoc, const AActor* Target, float MaxRadius, float PrecomputedDist)
{
	if (!IsValid(Slot) || !IsValid(Target))
		return -1.f;

	const FVector StandPos = Slot->GetStandPosition();
	const float DistToQuerier = PrecomputedDist >= 0.f ? PrecomputedDist : FVector::Dist(QuerierLoc, Slot->GetActorLocation());
	const float ProximityScore = 1.f - FMath::Clamp(DistToQuerier / MaxRadius, 0.f, 1.f);

	const float DistToTarget = FVector::Dist(StandPos, Target->GetActorLocation());
	const float DistanceScore = FMath::Clamp((DistToTarget - RegistryIdealDistMin) / RegistryIdealDistRange, 0.f, 1.f);

	// Part C: flat cover bonus — applied equally to crouch and flagged-stand slots.
	// Hide-only stand slots are already rejected before scoring reaches here.
	return RegistryWeight_Proximity * ProximityScore
		+ RegistryWeight_Distance * DistanceScore
		+ RegistryWeight_CoverBonus;
}

void UCoverRegistrySubsystem::PruneStaleSlots()
{
	RegisteredSlots.RemoveAll([](const TWeakObjectPtr<AAICoverSlot>& W) { return !W.IsValid(); });
}
