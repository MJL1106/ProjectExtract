// BTTask_CompanionDownedRetreat — downed crawl to the safest nearby cover, then hold until revived.

#include "BTTask_CompanionDownedRetreat.h"
#include "AI/CompanionCoverStatics.h"
#include "CompanionAIController.h"
#include "CompanionCharacter.h"
#include "CompanionTuningDataAsset.h"
#include "CoverSystem.h"
#include "CoverReservationSubsystem.h"
#include "World/DoorRegistrySubsystem.h"
#include "AIController.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Navigation/PathFollowingComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"

// Low-rate work cadence (clamp re-assert, arrival + revive-hold checks) — the crawl is slow,
// per-frame precision buys nothing.
static constexpr float DownedRetreatSlowTickInterval = 0.25f;
// Chest height for the body-shield scoring trace when no tuning asset is reachable.
static constexpr float DownedRetreatFallbackChestHeight = 120.f;
// Same-floor gate fallback when no tuning asset is reachable (mirrors CoverPickMaxZDelta's default).
static constexpr float DownedRetreatFallbackMaxZDelta = 250.f;
// Give up re-issuing a failing/unreachable move after this many attempts and idle in place.
static constexpr int32 DownedRetreatMaxMoveRetries = 3;
// Pause the crawl while the player stands this close — they are coming to revive, and a body
// crawling out from under them reads broken. Release at the wider radius (hysteresis) so the
// hold can't thrash on the boundary.
static constexpr float DownedRetreatPlayerHoldRadius = 350.f;
static constexpr float DownedRetreatPlayerHoldReleaseRadius = 450.f;

UBTTask_CompanionDownedRetreat::UBTTask_CompanionDownedRetreat()
{
	NodeName = TEXT("Companion Downed Retreat");
	bNotifyTick = true;
	// Per-run member state (hold latch, arrival, retries) — every stateful companion task sets
	// this; without it the state lives on the shared tree-asset node.
	bCreateNodeInstance = true;
}

EBTNodeResult::Type UBTTask_CompanionDownedRetreat::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* /*NodeMemory*/)
{
	AAIController* Controller = OwnerComp.GetAIOwner();
	ACompanionCharacter* Companion = Controller ? Cast<ACompanionCharacter>(Controller->GetPawn()) : nullptr;
	if (!IsValid(Companion)) return EBTNodeResult::Failed;

	CachedCompanion = Companion;
	RetreatDestination = PickRetreatDestination(*Companion);
	bMoveIssued = false;
	bHeldForRevive = false;
	SlowTickAccumulator = DownedRetreatSlowTickInterval; // do the first slow-tick pass immediately
	MoveRetries = 0;

	bArrived = FVector::Dist2D(Companion->GetActorLocation(), RetreatDestination) <= ArrivalRadius;
	UE_LOG(LogCompanionAI, Log, TEXT("%s DOWNED RETREAT start: dest=%s dist=%.0f%s"),
		*Companion->GetName(), *RetreatDestination.ToCompactString(),
		FVector::Dist2D(Companion->GetActorLocation(), RetreatDestination),
		bArrived ? TEXT(" (already in place)") : TEXT(""));

	return EBTNodeResult::InProgress;
}

void UBTTask_CompanionDownedRetreat::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* /*NodeMemory*/, float DeltaSeconds)
{
	// Never FinishLatentTask here — the IsDowned decorator abort is the sole exit. Finishing
	// spontaneously deadlocks the BT (known project pitfall).
	SlowTickAccumulator += DeltaSeconds;
	if (SlowTickAccumulator < DownedRetreatSlowTickInterval) return;
	SlowTickAccumulator = 0.f;

	ACompanionCharacter* Companion = CachedCompanion.Get();
	AAIController* Controller = OwnerComp.GetAIOwner();
	if (!IsValid(Companion) || !Controller) return;

	// Re-assert the crawl clamp: cover/follow tasks that ran before the abort may have queued a
	// speed write, and the companion's own Tick (the usual re-applier) is off while DBNO.
	if (UCharacterMovementComponent* Movement = Companion->GetCharacterMovement())
	{
		Movement->MaxWalkSpeed = Companion->GetDownedCrawlSpeed();
		Movement->MaxWalkSpeedCrouched = Companion->GetDownedCrawlSpeed();
	}

	// Hold: an active revive hold always wins; a player standing close is about to press E, so
	// the crawl pauses for them too (bHeldForRevive doubles as the hold latch for both).
	bool bShouldHold = Companion->IsBeingRevived();
	if (!bShouldHold)
	{
		if (const APawn* Player = UGameplayStatics::GetPlayerPawn(Companion, 0))
		{
			// Same-floor only: a player 3m away in plan distance but a storey up (stairwells)
			// is not approaching to revive and must not freeze the crawl.
			const float HoldRadius = bHeldForRevive
				? DownedRetreatPlayerHoldReleaseRadius : DownedRetreatPlayerHoldRadius;
			bShouldHold = FMath::Abs(Player->GetActorLocation().Z - Companion->GetActorLocation().Z) < 250.f
				&& FVector::Dist2D(Player->GetActorLocation(), Companion->GetActorLocation()) <= HoldRadius;
		}
	}
	if (bShouldHold)
	{
		if (!bHeldForRevive)
		{
			Controller->StopMovement();
			bHeldForRevive = true;
		}
		return;
	}
	if (bHeldForRevive)
	{
		bHeldForRevive = false;
		bMoveIssued = false;
	}

	if (bArrived) return;

	const float DistToDest = FVector::Dist2D(Companion->GetActorLocation(), RetreatDestination);
	if (DistToDest <= ArrivalRadius)
	{
		Controller->StopMovement();
		bArrived = true;
		UE_LOG(LogCompanionAI, Log, TEXT("%s DOWNED RETREAT arrived — holding for revive"), *Companion->GetName());
		return;
	}

	const bool bMoveActive = Controller->GetMoveStatus() == EPathFollowingStatus::Moving;
	if (!bMoveIssued || !bMoveActive)
	{
		// A finished move that got near enough counts as arrived — re-issuing against a
		// partial-path end just shuffles the body back and forth on the spot.
		if (bMoveIssued && !bMoveActive && DistToDest <= ArrivalRadius * 2.f)
		{
			bArrived = true;
			UE_LOG(LogCompanionAI, Log, TEXT("%s DOWNED RETREAT settled %.0f from dest — holding for revive"),
				*Companion->GetName(), DistToDest);
			return;
		}
		// A move that keeps dying (unreachable island, nav edge) retries a few times, then the
		// companion idles where it lies rather than twitching forever.
		if (bMoveIssued && ++MoveRetries > DownedRetreatMaxMoveRetries)
		{
			bArrived = true;
			UE_LOG(LogCompanionAI, Warning, TEXT("%s DOWNED RETREAT gave up after %d move retries — idling in place"),
				*Companion->GetName(), DownedRetreatMaxMoveRetries);
			return;
		}
		Controller->MoveToLocation(RetreatDestination, ArrivalRadius,
			/*bStopOnOverlap*/ true, /*bUsePathfinding*/ true, /*bProjectDestinationToNavigation*/ true);
		bMoveIssued = true;
	}
}

EBTNodeResult::Type UBTTask_CompanionDownedRetreat::AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* /*NodeMemory*/)
{
	if (AAIController* Controller = OwnerComp.GetAIOwner())
		Controller->StopMovement();
	CachedCompanion.Reset();
	return EBTNodeResult::Aborted;
}

FString UBTTask_CompanionDownedRetreat::GetStaticDescription() const
{
	return FString::Printf(TEXT("Crawl to safest cover within %.0fcm, hold until revived"), CoverSearchRadius);
}

FVector UBTTask_CompanionDownedRetreat::PickRetreatDestination(ACompanionCharacter& Companion) const
{
	const FVector PawnLoc = Companion.GetActorLocation();
	UWorld* World = Companion.GetWorld();
	ACoverSystem* CoverSys = World ? ACoverSystem::GetCoverSystem(World) : nullptr;
	if (!CoverSys) return PawnLoc;

	AAIController* Controller = Cast<AAIController>(Companion.GetController());
	const UCoverReservationSubsystem* ResSub = World->GetSubsystem<UCoverReservationSubsystem>();
	const UDoorRegistrySubsystem* DoorRegistry = World->GetSubsystem<UDoorRegistrySubsystem>();

	TArray<AActor*, TInlineAllocator<8>> Threats;
	CompanionCover::GatherExtraThreatActors(Controller, &Companion, /*FocusTarget*/ nullptr, MaxThreatsScored, Threats);

	const ACompanionAIController* CompAIC = Cast<ACompanionAIController>(Controller);
	const UCompanionTuningDataAsset* Tuning = CompAIC ? CompAIC->GetTuning() : nullptr;
	const float ChestHeight = Tuning ? Tuning->CoverProtectionChestHeight : DownedRetreatFallbackChestHeight;
	const UCapsuleComponent* Capsule = Companion.GetCapsuleComponent();
	const float Standoff = (Capsule ? Capsule->GetScaledCapsuleRadius() : 34.f) + 10.f;

	TArray<FCover> Nearby;
	Nearby.Reserve(16);
	const FBoxSphereBounds Bounds(PawnLoc, FVector(CoverSearchRadius), CoverSearchRadius);
	CoverSys->GetCoverDataWithinBounds(Bounds, Nearby);

	// Crawl-range gate stays keyed on the PAWN (the crawl is slow), but the distance PREFERENCE is
	// keyed on the PLAYER: the downed companion should hole up where the player is fighting — near
	// enough to be revived — not crawl off to the best-shielded spot in another room.
	const APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(World, 0);
	const FVector PreferLoc = IsValid(PlayerPawn) ? PlayerPawn->GetActorLocation() : PawnLoc;

	const ACompanionAIController* ZCompCtrl = Cast<ACompanionAIController>(Controller);
	const UCompanionTuningDataAsset* ZTuning = ZCompCtrl ? ZCompCtrl->GetTuning() : nullptr;
	const float MaxZDelta = ZTuning ? ZTuning->CoverPickMaxZDelta : DownedRetreatFallbackMaxZDelta;

	const FCover* Best = nullptr;
	int32 BestUncovered = TNumericLimits<int32>::Max();
	float BestDistSq = TNumericLimits<float>::Max();
	for (const FCover& Candidate : Nearby)
	{
		if (!Candidate.IsValid()) continue;
		const float CrawlDistSq = FVector::DistSquared2D(PawnLoc, Candidate.Data.Location);
		if (CrawlDistSq > FMath::Square(CoverSearchRadius)) continue; // bounds query is a box — enforce the sphere

		// Same-floor gate: 2D distance reads a slot directly overhead as "nearest" (playtest: the
		// downed crawl targeted a cover one storey up and set off toward another room's stairs).
		if (MaxZDelta > 0.f && FMath::Abs(PawnLoc.Z - Candidate.Data.Location.Z) > MaxZDelta) continue;

		// A spot someone else holds or wants is no hiding spot.
		AController* Occupant = CoverSys->GetOccupyingController(Candidate.Handle);
		if (Occupant && Occupant != Controller) continue;
		if (IsValid(ResSub) && ResSub->IsCoverIntendedByOther(Candidate.Handle, Controller)) continue;

		// Never crawl through a closed door — the downed companion can't hole up in another room
		// (same rule as the EQS DoorCrossing filter).
		if (IsValid(DoorRegistry) && DoorRegistry->AnyClosedDoorBlocksSegment(PawnLoc, Candidate.Data.Location)) continue;

		// Shielding first (fewest threats with a clear body-shield line), distance-to-player second
		// — an exposed near spot is worse than a shielded far one, but among equal shielding the
		// spot nearest the player wins so the revive stays practical.
		const int32 Uncovered = Threats.Num() > 0
			? CompanionCover::CountUncoveredThreats(World, Candidate.Data, Standoff, ChestHeight, &Companion, Threats)
			: 0;
		const float PreferDistSq = FVector::DistSquared2D(PreferLoc, Candidate.Data.Location);
		if (Uncovered > BestUncovered) continue;
		if (Uncovered == BestUncovered && PreferDistSq >= BestDistSq) continue;

		Best = &Candidate;
		BestUncovered = Uncovered;
		BestDistSq = PreferDistSq;
	}

	if (!Best)
	{
		UE_LOG(LogCompanionAI, Log, TEXT("%s DOWNED RETREAT: no available cover within %.0f — idling where downed"),
			*Companion.GetName(), CoverSearchRadius);
		return PawnLoc;
	}
	return CompanionCover::CompanionHunkerPosition(Companion, Best->Data, Standoff);
}
