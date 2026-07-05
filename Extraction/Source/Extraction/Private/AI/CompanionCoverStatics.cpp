// Companion-only cover helpers — see header for the sharing contract.

#include "AI/CompanionCoverStatics.h"
#include "AI/CompanionAIController.h"
#include "AI/CompanionTuningDataAsset.h"
#include "Companion/CompanionCharacter.h"
#include "Components/CapsuleComponent.h"
#include "SuppressionComponent.h"
#include "HealthComponent.h"
#include "CoverSystem.h"
#include "CoverGeometryStatics.h"
#include "CoverReservationSubsystem.h"
#include "ExtractionTypes.h"
#include "AIController.h"
#include "GameplayTagAssetInterface.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISense_Sight.h"

namespace CompanionCover
{

FCoverTriggers EvaluateTriggers(const ACompanionCharacter& Companion,
	const UCompanionTuningDataAsset& Tuning, int32 KnownThreatCount, bool bForRelease)
{
	FCoverTriggers Out;

	// Under fire: near-miss suppression, or damage within the window. Release doubles the window —
	// without it a lull between bursts pops the companion out, the next burst re-commits it to a
	// DIFFERENT point (old one on post-vacate cooldown) and it cover-hops every ~8s.
	const float FireWindow = bForRelease
		? Tuning.CoverCommitUnderFireWindow * 2.f
		: Tuning.CoverCommitUnderFireWindow;
	const USuppressionComponent* Supp = Companion.GetSuppressionComponent();
	Out.bUnderFire = (Supp && Supp->IsSuppressed()) || Companion.IsSuppressed(FireWindow);

	const float HealthThresh = bForRelease
		? FMath::Max(Tuning.CoverTriggerHealthReleaseFrac, Tuning.CoverTriggerHealthFrac)
		: Tuning.CoverTriggerHealthFrac;
	Out.bLowHealth = Companion.GetHealthFraction() < HealthThresh;

	const float AmmoThresh = bForRelease
		? FMath::Max(Tuning.CoverTriggerAmmoReleaseFrac, Tuning.CoverTriggerLowAmmoFrac)
		: Tuning.CoverTriggerLowAmmoFrac;
	Out.bLowAmmoOrReloading = Companion.IsReloading() || Companion.GetAmmoFraction() < AmmoThresh;

	// Release keeps the trigger "active" one threat below the commit count, so the companion
	// doesn't pop out the moment a third attacker dies and duck back when it re-perceives one.
	const int32 CountThresh = bForRelease
		? FMath::Max(2, Tuning.CoverTriggerOutnumberedCount - 1)
		: Tuning.CoverTriggerOutnumberedCount;
	Out.bOutnumbered = KnownThreatCount >= CountThresh;

	return Out;
}

int32 CountKnownThreats(AAIController* Controller, int32 Cap)
{
	if (Cap <= 0) return 0;
	UAIPerceptionComponent* Perception = Controller ? Controller->GetPerceptionComponent() : nullptr;
	if (!Perception) return 0;

	TArray<AActor*> Perceived;
	Perception->GetCurrentlyPerceivedActors(UAISense_Sight::StaticClass(), Perceived);

	int32 Count = 0;
	for (const AActor* Actor : Perceived)
	{
		if (!IsValid(Actor)) continue;

		const IGameplayTagAssetInterface* TagIface = Cast<IGameplayTagAssetInterface>(Actor);
		if (!TagIface) continue;
		FGameplayTagContainer Tags;
		TagIface->GetOwnedGameplayTags(Tags);
		if (!Tags.HasTag(TAG_Character_Enemy)) continue;

		const UHealthComponent* Health = Actor->FindComponentByClass<UHealthComponent>();
		if (Health && Health->IsDead()) continue;

		if (++Count >= Cap) break;
	}
	return Count;
}

void GatherExtraThreatActors(AAIController* Controller, const APawn* Pawn, const AActor* FocusTarget,
	int32 MaxExtra, TArray<AActor*, TInlineAllocator<8>>& OutActors)
{
	OutActors.Reset();
	if (MaxExtra <= 0 || !IsValid(Pawn)) return;

	UAIPerceptionComponent* Perception = Controller ? Controller->GetPerceptionComponent() : nullptr;
	if (!Perception) return;

	TArray<AActor*> Perceived;
	Perception->GetCurrentlyPerceivedActors(UAISense_Sight::StaticClass(), Perceived);

	// Perception can hand back null/pending-kill entries; the Sort lambda derefs by const-ref.
	Perceived.RemoveAll([](const AActor* A) { return !IsValid(A); });

	const FVector PawnLoc = Pawn->GetActorLocation();
	Perceived.Sort([PawnLoc](const AActor& A, const AActor& B)
	{
		return FVector::DistSquared(PawnLoc, A.GetActorLocation()) < FVector::DistSquared(PawnLoc, B.GetActorLocation());
	});

	for (AActor* Actor : Perceived)
	{
		if (!IsValid(Actor) || Actor == FocusTarget) continue;

		const IGameplayTagAssetInterface* TagIface = Cast<IGameplayTagAssetInterface>(Actor);
		if (!TagIface) continue;
		FGameplayTagContainer Tags;
		TagIface->GetOwnedGameplayTags(Tags);
		if (!Tags.HasTag(TAG_Character_Enemy)) continue;

		const UHealthComponent* Health = Actor->FindComponentByClass<UHealthComponent>();
		if (Health && Health->IsDead()) continue;

		OutActors.Add(Actor);
		if (OutActors.Num() >= MaxExtra) break;
	}
}

int32 CountUncoveredThreats(UWorld* World, const FCoverData& Cover, float Standoff,
	float ChestHeight, const APawn* Pawn, TArrayView<AActor* const> ExtraThreatActors)
{
	int32 Uncovered = 0;
	for (AActor* const ThreatActor : ExtraThreatActors)
	{
		if (!IsValid(ThreatActor)) continue;
		if (!UCoverGeometryStatics::IsThreatCovered(World, Cover, ThreatActor->GetActorLocation(),
			Standoff, ChestHeight, ThreatActor, Pawn))
			++Uncovered;
	}
	return Uncovered;
}

float DistToNearestCover(UWorld* World, const FVector& Point, float Radius, const AController* Querier)
{
	FVector Unused;
	if (!NearestCoverLocation(World, Point, Radius, Querier, Unused)) return -1.f;
	return FVector::Dist2D(Point, Unused);
}

bool NearestCoverLocation(UWorld* World, const FVector& Point, float Radius,
	const AController* Querier, FVector& OutLocation)
{
	if (!World || Radius <= 0.f) return false;
	ACoverSystem* CoverSys = ACoverSystem::GetCoverSystem(World);
	if (!CoverSys) return false;
	const UCoverReservationSubsystem* ResSub = World->GetSubsystem<UCoverReservationSubsystem>();

	TArray<FCover> Nearby;
	Nearby.Reserve(16);
	const FBoxSphereBounds Bounds(Point, FVector(Radius), Radius);
	CoverSys->GetCoverDataWithinBounds(Bounds, Nearby);

	float BestSq = -1.f;
	for (const FCover& Candidate : Nearby)
	{
		if (!Candidate.IsValid()) continue;
		const float DistSq = FVector::DistSquared2D(Point, Candidate.Data.Location);
		if (DistSq > FMath::Square(Radius)) continue; // bounds query is a box — enforce the sphere

		// A taken duck spot is no duck spot — skip points occupied/intended by anyone else.
		AController* Occupant = CoverSys->GetOccupyingController(Candidate.Handle);
		if (Occupant && Occupant != Querier) continue;
		if (IsValid(ResSub) && ResSub->IsCoverIntendedByOther(Candidate.Handle, Querier)) continue;

		if (BestSq < 0.f || DistSq < BestSq)
		{
			BestSq = DistSq;
			OutLocation = Candidate.Data.Location;
		}
	}
	return BestSq >= 0.f;
}

FVector CompanionHunkerPosition(const ACompanionCharacter& Companion, const FCoverData& Data, float Standoff)
{
	const UWorld* World = Companion.GetWorld();
	const ACompanionAIController* AIC = Cast<ACompanionAIController>(Companion.GetController());
	const UCompanionTuningDataAsset* Tuning = AIC ? AIC->GetTuning() : nullptr;
	if (World && Tuning)
	{
		const UCapsuleComponent* Cap = Companion.GetCapsuleComponent();
		const float CapRadius = Cap ? Cap->GetScaledCapsuleRadius() : 34.f;
		return UCoverGeometryStatics::GetEdgeAlignedHunkerPosition(
			World, Data, Standoff, CapRadius, Tuning->CoverCornerGap, &Companion);
	}
	return UCoverGeometryStatics::GetHunkerPosition(Data, Standoff);
}

} // namespace CompanionCover
