// BT service — periodically re-evaluates whether the companion's current cover slot
// is still the best available, and clears BB_HasCoverPosition to trigger a switch.

#include "BTService_CoverSwitchMonitor.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "AIController.h"
#include "AI/CompanionAIController.h"
#include "AI/CompanionTuningDataAsset.h"
#include "AI/Cover/AICoverSlot.h"
#include "AI/Cover/CoverRegistrySubsystem.h"
#include "Companion/CompanionCharacter.h"
#include "Weapon/WeaponBase.h"

UBTService_CoverSwitchMonitor::UBTService_CoverSwitchMonitor()
{
	NodeName         = TEXT("Cover Switch Monitor");
	Interval         = 0.1f;
	RandomDeviation  = 0.02f;
	bCreateNodeInstance = false; // state lives in NodeMemory
	INIT_SERVICE_NODE_NOTIFY_FLAGS();
}

void UBTService_CoverSwitchMonitor::TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	Super::TickNode(OwnerComp, NodeMemory, DeltaSeconds);

	FCoverSwitchMonitorMemory& Mem = *reinterpret_cast<FCoverSwitchMonitorMemory*>(NodeMemory);

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!BB) return;

	ACompanionAIController* Controller = Cast<ACompanionAIController>(OwnerComp.GetAIOwner());
	if (!Controller) return;

	APawn* Pawn = Controller->GetPawn();
	if (!Pawn) return;

	const bool bHasCover = BB->GetValueAsBool(HasCoverPositionKey.SelectedKeyName);

	if (!bHasCover)
	{
		Mem = {};
		return;
	}

	AAICoverSlot* CurrentSlot = Cast<AAICoverSlot>(BB->GetValueAsObject(CoverSlotKey.SelectedKeyName));
	if (!IsValid(CurrentSlot))
	{
		BB->SetValueAsBool(HasCoverPositionKey.SelectedKeyName, false);
		return;
	}

	const UCompanionTuningDataAsset* Tuning = Controller->GetTuning();
	if (!Tuning) return;

	// Fresh arrival: previous tick had no cover, now we do. Dwell does not start until physical arrival.
	if (!Mem.bWasInCoverLastTick)
	{
		Mem.TimeSinceArrival    = 0.f;
		Mem.TimeSinceReEval     = 0.f;
		Mem.bWasInCoverLastTick = true;
		Mem.bHasArrived         = false;
		return;
	}

	// Dwell-from-arrival: only treat the slot as occupied (start accruing dwell) once the pawn is physically
	// at its arrival point on the cover line. BTTask_MoveToCover moves the pawn to the slot's projection of
	// its own location onto the cover line, so test against that exact point — not the slot center, which on
	// long cover lines would be far from where the pawn actually stands and would deadlock dwell. (Fixes the
	// claim-time dwell-start TODO; replaces the fragile CoverLocation BB dependency.)
	if (!Mem.bHasArrived)
	{
		const FVector PawnLoc = Pawn->GetActorLocation();
		const float ArrivalAlpha = CurrentSlot->GetAlphaFromLocation(PawnLoc);
		const FVector OnLine = CurrentSlot->GetLocationAtAlpha(ArrivalAlpha);
		const bool bArrivedNow = FVector::Dist2D(PawnLoc, OnLine) <= ArrivalRadius;
		if (!bArrivedNow)
		{
			Mem.TimeSinceArrival = 0.f;
			return;
		}
		Mem.bHasArrived = true;
	}

	Mem.TimeSinceArrival += DeltaSeconds;
	Mem.TimeSinceReEval  += DeltaSeconds;

	if (Mem.TimeSinceArrival < Tuning->CoverSwitchMinDwell) return;
	if (Mem.TimeSinceReEval  < Tuning->CoverSwitchReEvalInterval) return;

	// Bail early on no combat target before paying for Registry lookup.
	AActor* CombatTarget = Cast<AActor>(BB->GetValueAsObject(CombatTargetKey.SelectedKeyName));
	if (!IsValid(CombatTarget)) return;

	UCoverRegistrySubsystem* Registry = Pawn->GetWorld()->GetSubsystem<UCoverRegistrySubsystem>();
	if (!Registry) return;

	Mem.TimeSinceReEval = 0.f;

	// TODO: lift formation-point computation to a shared utility (spec §5.7 open question).
	// FollowPlayer uses a velocity-relative offset; the spec wants a fixed actor-facing offset.
	AActor* Player = Cast<AActor>(BB->GetValueAsObject(PlayerActorKey.SelectedKeyName));
	if (!IsValid(Player)) return;

	const FVector FormationPoint = Player->GetActorLocation()
		+ Player->GetActorRightVector()      * Tuning->FormationOffsetRight
		+ (-Player->GetActorForwardVector()) * Tuning->FormationOffsetBack;

	// FindBestCoverFor skips claimed slots, so it naturally excludes CurrentSlot (P3). It also skips any slot
	// this pawn just vacated for the post-vacate cooldown window (P4) — the direct snap-back guard.
	// OutScore avoids a duplicate ScoreSlotFor call for BestSlot below.
	float BestScore = -1.f;
	AAICoverSlot* BestSlot = Registry->FindBestCoverFor(
		FormationPoint, CombatTarget, SearchRadius, &BestScore, Pawn, Tuning->CoverSwitchPostVacateCooldown);
	if (!IsValid(BestSlot) || BestSlot == CurrentSlot)
	{
		// No better candidate this re-eval — reset the debounce so a future winner must agree fresh. (G2)
		Mem.PendingBestSlot.Reset();
		Mem.ConsecutiveBetterCount = 0;
		return;
	}

	const float CurrentScore = UCoverRegistrySubsystem::ScoreSlotFor(CurrentSlot, FormationPoint, CombatTarget, SearchRadius);

	if (BestScore < CurrentScore * Tuning->CoverSwitchScoreMargin) // P6
	{
		Mem.PendingBestSlot.Reset();
		Mem.ConsecutiveBetterCount = 0;
		return;
	}

	// G4 — hold the switch while firing. Return WITHOUT advancing the debounce so a burst doesn't count
	// as an agreeing re-eval; the switch resumes evaluating once the weapon stops.
	const ACompanionCharacter* Companion = Cast<ACompanionCharacter>(Pawn);
	const AWeaponBase* Weapon = Companion ? Companion->GetCurrentWeapon() : nullptr;
	if (IsValid(Weapon) && Weapon->IsFiring()) return;

	// G2 — debounce: require two consecutive re-evals agreeing on the same candidate before committing.
	if (Mem.PendingBestSlot.Get() == BestSlot)
	{
		++Mem.ConsecutiveBetterCount;
	}
	else
	{
		Mem.PendingBestSlot        = BestSlot;
		Mem.ConsecutiveBetterCount = 1;
	}

	if (Mem.ConsecutiveBetterCount < Tuning->CoverSwitchRequiredAgreeingReEvals) return;

	// Claim the new slot BEFORE releasing the old one.
	// If the claim fails (rare race), abort — don't leave the companion slotless.
	if (!BestSlot->TryClaim(Pawn))
	{
		UE_LOG(LogCompanionAI, Verbose,
			TEXT("CoverSwitch: slot=%s claim race lost — aborting switch"), *BestSlot->GetName());
		return;
	}

	// Write the pre-claimed slot to BB so BTTask_MoveToCover can skip its own picker.
	BB->SetValueAsObject(NextCoverSlotKey.SelectedKeyName, BestSlot);

	// P4 — stamp the old slot as deliberately vacated so neither this re-flow nor MoveToCover's fresh picker
	// can re-select it during the cooldown window. Must precede Release (Release clears nothing cooldown-wise).
	CurrentSlot->MarkVacatedForSwitch(Pawn);
	CurrentSlot->Release(Pawn);
	BB->SetValueAsObject(CoverSlotKey.SelectedKeyName, nullptr);
	BB->SetValueAsBool(HasCoverPositionKey.SelectedKeyName, false);
	Mem = {};

	UE_LOG(LogCompanionAI, Log,
		TEXT("CoverSwitch: %s -> %s (curScore=%.2f, bestScore=%.2f, margin=%.2fx)"),
		*CurrentSlot->GetName(), *BestSlot->GetName(), CurrentScore, BestScore, Tuning->CoverSwitchScoreMargin);
}

void UBTService_CoverSwitchMonitor::OnCeaseRelevant(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	Super::OnCeaseRelevant(OwnerComp, NodeMemory);

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (BB)
	{
		APawn* Pawn = OwnerComp.GetAIOwner() ? OwnerComp.GetAIOwner()->GetPawn() : nullptr;
		AAICoverSlot* PendingSlot = Cast<AAICoverSlot>(BB->GetValueAsObject(NextCoverSlotKey.SelectedKeyName));
		if (IsValid(PendingSlot) && IsValid(Pawn) && PendingSlot->IsClaimedBy(Pawn))
		{
			PendingSlot->Release(Pawn);
			UE_LOG(LogCompanionAI, Log,
				TEXT("CoverSwitchMonitor: released stale NextCoverSlot claim on %s (OnCeaseRelevant)"),
				*PendingSlot->GetName());
		}
		BB->SetValueAsObject(NextCoverSlotKey.SelectedKeyName, nullptr);
	}

	*reinterpret_cast<FCoverSwitchMonitorMemory*>(NodeMemory) = {};
}

FString UBTService_CoverSwitchMonitor::GetStaticDescription() const
{
	return FString::Printf(TEXT("Cover Switch Monitor (radius: %.0f)"), SearchRadius);
}
