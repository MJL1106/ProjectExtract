// BTTask_EnemyCombatFire — latent peek-fire loop driven by a NodeMemory state machine.
// Bugs 1+2 fix: while AwarenessState==Combat, never return Failed to the Selector.
// Instead: pursue (no LOS/range) or re-seek cover (suppressed in the open).

#include "BTTask_EnemyCombatFire.h"
#include "EnemyAIController.h"
#include "EnemyArchetypeData.h"
#include "EnemyCharacter.h"
#include "SuppressionComponent.h"
#include "WeaponBase.h"
#include "AICoverSlot.h"
#include "CoverRegistrySubsystem.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "AIController.h"
#include "Navigation/PathFollowingComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Character.h"
#include "CoverSlotTypes.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"

static constexpr float ExposePhaseDuration  = 0.2f;  // settle before first shot
static constexpr float RecoverPhaseDuration = 0.15f; // re-crouch settle after burst
static constexpr float DefaultCapsuleRadius = 34.f;
static constexpr float SeekCoverArrivalTickRadius = 50.f;   // mirrors BTTask_EnemyMoveToCover
static constexpr float SeekCoverArrivalIdleRadius = 200.f;  // path-failed tolerance

UBTTask_EnemyCombatFire::UBTTask_EnemyCombatFire()
{
	NodeName = TEXT("Enemy Combat Fire");
	bNotifyTick = true;
}

uint16 UBTTask_EnemyCombatFire::GetInstanceMemorySize() const
{
	return sizeof(FFireMemory);
}

EBTNodeResult::Type UBTTask_EnemyCombatFire::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	FFireMemory* Mem = reinterpret_cast<FFireMemory*>(NodeMemory);
	new (Mem) FFireMemory();

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	AAIController* Controller = OwnerComp.GetAIOwner();
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	if (!BB || !Controller || !Pawn) return EBTNodeResult::Failed;

	AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Pawn);
	if (!IsValid(Enemy)) return EBTNodeResult::Failed;

	const UEnemyArchetypeData* DA = Enemy->GetArchetypeData();
	if (!IsValid(DA)) return EBTNodeResult::Failed;

	// Only allow Failed from ExecuteTask when NOT in combat — Bug 2 fix.
	const EEnemyAwarenessState Awareness = static_cast<EEnemyAwarenessState>(
		BB->GetValueAsEnum(AEnemyAIController::BB_AwarenessState));

	AActor* Target = Cast<AActor>(BB->GetValueAsObject(AEnemyAIController::BB_CombatTarget));
	if (!IsValid(Target))
	{
		if (Awareness == EEnemyAwarenessState::Combat)
		{
			// No target yet but combat aware — pursue last known location.
			Mem->Phase = EFireTaskPhase::Pursuing;
			Enemy->SetMoveSpeedMode(EEnemyMoveSpeedMode::Combat);
			const FVector LastKnown = BB->GetValueAsVector(AEnemyAIController::BB_LastKnownLocation);
			if (!LastKnown.IsNearlyZero())
				Controller->MoveToLocation(LastKnown, 100.f, false, true, false, true);
			return EBTNodeResult::InProgress;
		}
		return EBTNodeResult::Failed;
	}

	const bool bHasLOS = BB->GetValueAsBool(AEnemyAIController::BB_HasLineOfSight);
	const bool bInRange = BB->GetValueAsBool(AEnemyAIController::BB_TargetInRange);
	if (!bInRange && !bHasLOS)
	{
		if (Awareness == EEnemyAwarenessState::Combat)
		{
			// Out of range/LOS but combat — pursue.
			Mem->Phase = EFireTaskPhase::Pursuing;
			Enemy->SetMoveSpeedMode(EEnemyMoveSpeedMode::Combat);
			const FVector LastKnown = BB->GetValueAsVector(AEnemyAIController::BB_LastKnownLocation);
			const FVector PursueTarget = LastKnown.IsNearlyZero() ? Target->GetActorLocation() : LastKnown;
			Controller->MoveToLocation(PursueTarget, 100.f, false, true, false, true);
			return EBTNodeResult::InProgress;
		}
		return EBTNodeResult::Failed;
	}

	Enemy->SetMoveSpeedMode(EEnemyMoveSpeedMode::Combat);

	// Start in Acquire phase
	Enemy->SetAimTarget(Target);
	Controller->SetFocus(Target);
	Mem->Phase = EFireTaskPhase::Acquire;
	Mem->PhaseTimer = DA->ReactionDelay;

	return EBTNodeResult::InProgress;
}

void UBTTask_EnemyCombatFire::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	FFireMemory* Mem = reinterpret_cast<FFireMemory*>(NodeMemory);

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	AAIController* Controller = OwnerComp.GetAIOwner();
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	if (!BB || !Pawn) return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);

	AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Pawn);
	if (!IsValid(Enemy)) return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);

	const UEnemyArchetypeData* DA = Enemy->GetArchetypeData();
	if (!IsValid(DA)) return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);

	const EEnemyAwarenessState Awareness = static_cast<EEnemyAwarenessState>(
		BB->GetValueAsEnum(AEnemyAIController::BB_AwarenessState));

	AActor* Target = Cast<AActor>(BB->GetValueAsObject(AEnemyAIController::BB_CombatTarget));

	// Bug 2 fix: only allow Failed when NOT in Combat.
	if (!IsValid(Target))
	{
		StopFireAndCleanUp(OwnerComp, Mem);
		if (Awareness != EEnemyAwarenessState::Combat)
			return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);

		// Still in Combat with no target — pursue last known.
		Mem->Phase = EFireTaskPhase::Pursuing;
		const FVector LastKnown = BB->GetValueAsVector(AEnemyAIController::BB_LastKnownLocation);
		if (!LastKnown.IsNearlyZero() && Controller->GetMoveStatus() != EPathFollowingStatus::Moving)
			Controller->MoveToLocation(LastKnown, 100.f, false, true, false, true);
		return;
	}

	const bool bHasLOS = BB->GetValueAsBool(AEnemyAIController::BB_HasLineOfSight);
	const bool bInRange = BB->GetValueAsBool(AEnemyAIController::BB_TargetInRange);

	// Bug 2 fix: no LOS + out of range while still in Combat — pursue instead of failing.
	// Skip this guard for phases that are already handling movement (avoids path churn / slot release).
	if (!bInRange && !bHasLOS
		&& Mem->Phase != EFireTaskPhase::Fire
		&& Mem->Phase != EFireTaskPhase::Pursuing
		&& Mem->Phase != EFireTaskPhase::SeekingCover)
	{
		if (Awareness != EEnemyAwarenessState::Combat)
		{
			StopFireAndCleanUp(OwnerComp, Mem);
			return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		}

		// Transition to pursue.
		StopFireAndCleanUp(OwnerComp, Mem);
		Mem->Phase = EFireTaskPhase::Pursuing;
		const FVector LastKnown = BB->GetValueAsVector(AEnemyAIController::BB_LastKnownLocation);
		const FVector PursueTarget = LastKnown.IsNearlyZero() ? Target->GetActorLocation() : LastKnown;
		Controller->MoveToLocation(PursueTarget, 100.f, false, true, false, true);
		return;
	}

	// --- Handle Pursuing phase ---
	if (Mem->Phase == EFireTaskPhase::Pursuing)
	{
		if (Awareness != EEnemyAwarenessState::Combat)
		{
			Controller->StopMovement();
			return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		}

		// Check if we regained LOS + range — resume firing.
		if (bHasLOS && bInRange)
		{
			Controller->StopMovement();
			Enemy->SetAimTarget(Target);
			Controller->SetFocus(Target);
			Mem->Phase = EFireTaskPhase::Acquire;
			Mem->PhaseTimer = DA->ReactionDelay * 0.5f; // halved reaction on re-engage
			return;
		}

		// Keep pursuing — update the move target if target has moved.
		const FVector LastKnown = BB->GetValueAsVector(AEnemyAIController::BB_LastKnownLocation);
		const FVector PursueTarget = LastKnown.IsNearlyZero() ? Target->GetActorLocation() : LastKnown;
		if (Controller->GetMoveStatus() != EPathFollowingStatus::Moving)
			Controller->MoveToLocation(PursueTarget, 100.f, false, true, false, true);
		return;
	}

	// --- Handle SeekingCover phase (Bug 1 fix) ---
	if (Mem->Phase == EFireTaskPhase::SeekingCover)
	{
		if (Awareness != EEnemyAwarenessState::Combat)
		{
			Controller->StopMovement();
			StopFireAndCleanUp(OwnerComp, Mem);
			return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		}

		// Distance-gated arrival check (mirrors BTTask_EnemyMoveToCover).
		// Arrival relies SOLELY on this task's authoritative distance gate — a parallel
		// writer setting BB_HasCover=true while we're still in transit must not short-circuit us.
		const EPathFollowingStatus::Type SeekStatus = Controller->GetMoveStatus();
		const float DistToSlot = FVector::Dist(Pawn->GetActorLocation(), Mem->ReseekArrivalPos);
		const bool bArrived = (DistToSlot <= SeekCoverArrivalTickRadius)
			|| (SeekStatus == EPathFollowingStatus::Idle && DistToSlot <= SeekCoverArrivalIdleRadius);

		if (bArrived)
		{
			BB->SetValueAsBool(AEnemyAIController::BB_HasCover, true);

			AAICoverSlot* Slot = Cast<AAICoverSlot>(BB->GetValueAsObject(AEnemyAIController::BB_CoverSlot));
			if (IsValid(Slot) && Slot->Height == ECoverHeight::Crouch)
				if (ACharacter* Char = Cast<ACharacter>(Pawn)) Char->Crouch();

			AWeaponBase* Weapon = Enemy->GetCurrentWeapon();
			if (IsValid(Weapon)) Weapon->StopFiring();

			if (Mem->bSuppressCrouchedNoCover)
			{
				if (ACharacter* Char = Cast<ACharacter>(Pawn)) Char->UnCrouch();
				Mem->bSuppressCrouchedNoCover = false;
			}

			Enemy->SetAimTarget(Target);
			Controller->SetFocus(Target);
			Mem->Phase = EFireTaskPhase::Acquire;
			Mem->PhaseTimer = DA->ReactionDelay * 0.5f;
			return;
		}

		// Path failed (idle but still far from slot) — slot unreachable.
		if (SeekStatus == EPathFollowingStatus::Idle)
		{
			// Release the unreachable slot and fall back to crouch-in-place.
			AAICoverSlot* BadSlot = Mem->ReseekSlot.Get();
			if (IsValid(BadSlot) && IsValid(Pawn))
			{
				BadSlot->Release(Pawn);
				Mem->ReseekSlot = nullptr;
			}
			BB->SetValueAsObject(AEnemyAIController::BB_CoverSlot, nullptr);
			BB->SetValueAsBool(AEnemyAIController::BB_HasCover, false);

			if (ACharacter* Char = Cast<ACharacter>(Pawn)) Char->Crouch();
			Mem->bSuppressCrouchedNoCover = true;

			AWeaponBase* Weapon = Enemy->GetCurrentWeapon();
			if (IsValid(Weapon)) Weapon->StopFiring();

			Enemy->SetAimTarget(Target);
			Controller->SetFocus(Target);
			Mem->Phase = EFireTaskPhase::Pause;
			Mem->PauseDuration = FMath::RandRange(DA->BurstPauseMin, DA->BurstPauseMax);
			Mem->PhaseTimer = Mem->PauseDuration;
			return;
		}

		// Continue moving toward cover. Fire while moving (move-and-shoot).
		Enemy->SetAimTarget(Target);
		// Re-assert firing if the weapon auto-stopped mid-transit (e.g. suppression spike).
		if (bHasLOS && bInRange)
		{
			AWeaponBase* W = Enemy->GetCurrentWeapon();
			if (IsValid(W) && W->CanFire() && !W->IsFiring()) W->StartFiring();
		}
		return;
	}

	Mem->PhaseTimer -= DeltaSeconds;

	// Phase 4: fetch suppression state once per tick
	USuppressionComponent* SupprComp = Enemy->GetSuppressionComponent();
	const bool bSuppressed = IsValid(SupprComp) && SupprComp->IsSuppressed();

	switch (Mem->Phase)
	{
	case EFireTaskPhase::Acquire:
		if (Mem->PhaseTimer <= 0.f)
		{
			// Suppression gate: stay in cover, extend into Pause instead of exposing
			if (bSuppressed)
			{
				const bool bHasCoverAcq = BB->GetValueAsBool(AEnemyAIController::BB_HasCover);
				if (!bHasCoverAcq)
				{
					// Bug 1 fix: suppressed + no cover -> try to find cover instead of looping forever.
					const UWorld* World = Pawn->GetWorld();
					const float Now = World ? World->GetTimeSeconds() : 0.f;
					if ((Now - Mem->LastReseekCoverTime) >= SuppressedReseekCooldown)
					{
						Mem->LastReseekCoverTime = Now;
						UCoverRegistrySubsystem* Registry = World ? World->GetSubsystem<UCoverRegistrySubsystem>() : nullptr;
						if (Registry)
						{
							// Release any slot we still hold before reclaiming — otherwise the old
							// slot is stranded (claims are server-only and never auto-cleared).
							if (AAICoverSlot* Prev = Mem->ReseekSlot.Get())
							{
								if (IsValid(Prev)) Prev->Release(Pawn);
								Mem->ReseekSlot = nullptr;
							}

							AAICoverSlot* FoundSlot = Registry->FindBestCoverFor(
								Pawn->GetActorLocation(), Target, DA->CoverSearchRadius, nullptr, Pawn);
							if (IsValid(FoundSlot) && FoundSlot->TryClaim(Pawn))
							{
								// Claim succeeded — write BB and move toward it.
								BB->SetValueAsObject(AEnemyAIController::BB_CoverSlot, FoundSlot);
								Mem->ReseekSlot = FoundSlot;

								const UCapsuleComponent* Capsule = Enemy->GetCapsuleComponent();
								const float Standoff = (Capsule ? Capsule->GetScaledCapsuleRadius() : DefaultCapsuleRadius) + DA->CoverStandoffPadding;
								const FVector ArrivalPos = FoundSlot->GetBehindCoverPosition(
									FoundSlot->GetAlphaFromLocation(Pawn->GetActorLocation()), Standoff);

								Mem->ReseekArrivalPos = ArrivalPos;
								Controller->MoveToLocation(ArrivalPos, 50.f, false, true, false, true);

								// Start firing while moving (move-and-shoot).
								AWeaponBase* Weapon = Enemy->GetCurrentWeapon();
								if (IsValid(Weapon)) Weapon->StartFiring();

								Mem->Phase = EFireTaskPhase::SeekingCover;
								break;
							}
						}
					}

					// No cover found or cooldown active — fall back to crouch-in-place (safe).
					if (ACharacter* Char = Cast<ACharacter>(Pawn)) Char->Crouch();
					Mem->bSuppressCrouchedNoCover = true;
				}
				Mem->Phase = EFireTaskPhase::Pause;
				Mem->PauseDuration = FMath::RandRange(DA->BurstPauseMin, DA->BurstPauseMax);
				Mem->PhaseTimer = Mem->PauseDuration;
				break;
			}

			// Un-crouch if we previously crouched without cover due to suppression
			if (Mem->bSuppressCrouchedNoCover)
			{
				if (ACharacter* Char = Cast<ACharacter>(Pawn)) Char->UnCrouch();
				Mem->bSuppressCrouchedNoCover = false;
			}

			// Expose: un-crouch if in crouch cover; step sideways otherwise
			AAICoverSlot* Slot = Cast<AAICoverSlot>(BB->GetValueAsObject(AEnemyAIController::BB_CoverSlot));
			const bool bHasCover = BB->GetValueAsBool(AEnemyAIController::BB_HasCover);

			if (bHasCover && IsValid(Slot) && Slot->Height == ECoverHeight::Crouch)
				if (ACharacter* Char = Cast<ACharacter>(Pawn)) Char->UnCrouch();

			Mem->Phase = EFireTaskPhase::Expose;
			Mem->PhaseTimer = ExposePhaseDuration;
		}
		break;

	case EFireTaskPhase::Expose:
		// Suppression interrupt: duck back to Recover immediately
		if (bSuppressed)
		{
			AAICoverSlot* SuppSlot = Cast<AAICoverSlot>(BB->GetValueAsObject(AEnemyAIController::BB_CoverSlot));
			const bool bSuppCover = BB->GetValueAsBool(AEnemyAIController::BB_HasCover);
			if (bSuppCover && IsValid(SuppSlot) && SuppSlot->Height == ECoverHeight::Crouch)
				if (ACharacter* Char = Cast<ACharacter>(Pawn)) Char->Crouch();
			Mem->Phase = EFireTaskPhase::Recover;
			Mem->PhaseTimer = RecoverPhaseDuration;
			break;
		}
		if (Mem->PhaseTimer <= 0.f)
		{
			// For stand-height slots, step sideways toward the peekable corner.
			// Prefer the peekable end; if both ends are peekable pick the nearer one.
			AAICoverSlot* ExposeSlot = Cast<AAICoverSlot>(BB->GetValueAsObject(AEnemyAIController::BB_CoverSlot));
			const bool bHasCoverForExpose = BB->GetValueAsBool(AEnemyAIController::BB_HasCover);
			if (bHasCoverForExpose && IsValid(ExposeSlot) && ExposeSlot->Height != ECoverHeight::Crouch)
			{
				const float PeekAlpha = ExposeSlot->GetAlphaFromLocation(Pawn->GetActorLocation());
				const float LineLen   = FMath::Max(ExposeSlot->GetLineLength(), 1.f);

				// Determine target alpha: bias toward the peekable corner(s).
				float TargetAlpha;
				if (ExposeSlot->bIsPeekableCornerStart && ExposeSlot->bIsPeekableCornerEnd)
				{
					// Both peekable — step toward the nearer end
					TargetAlpha = (PeekAlpha <= 0.5f) ? 0.f : 1.f;
				}
				else if (ExposeSlot->bIsPeekableCornerStart)
				{
					TargetAlpha = 0.f;
				}
				else
				{
					// bIsPeekableCornerEnd (registry guarantees at least one)
					TargetAlpha = 1.f;
				}

				// Move PeekLateralOffset cm toward that corner, clamped to [0,1]
				const float StepDir   = (TargetAlpha < PeekAlpha) ? -1.f : 1.f;
				const float StepAlpha = FMath::Clamp(PeekAlpha + StepDir * PeekLateralOffset / LineLen, 0.f, 1.f);
				const FVector PeekPos = ExposeSlot->GetLocationAtAlpha(StepAlpha);
				Controller->MoveToLocation(PeekPos, 30.f, false, true, false, true);
			}

			AWeaponBase* Weapon = Enemy->GetCurrentWeapon();
			if (IsValid(Weapon))
				Weapon->StartFiring();

			Mem->BurstDuration = FMath::RandRange(DA->BurstDurationMin, DA->BurstDurationMax);
			Mem->Phase = EFireTaskPhase::Fire;
			Mem->PhaseTimer = Mem->BurstDuration;
		}
		break;

	case EFireTaskPhase::Fire:
		// Suppression interrupt: stop firing and duck back
		if (bSuppressed)
		{
			AWeaponBase* SuppWeapon = Enemy->GetCurrentWeapon();
			if (IsValid(SuppWeapon))
				SuppWeapon->StopFiring();

			AAICoverSlot* SuppSlot = Cast<AAICoverSlot>(BB->GetValueAsObject(AEnemyAIController::BB_CoverSlot));
			const bool bSuppCover = BB->GetValueAsBool(AEnemyAIController::BB_HasCover);
			if (bSuppCover && IsValid(SuppSlot) && SuppSlot->Height == ECoverHeight::Crouch)
				if (ACharacter* Char = Cast<ACharacter>(Pawn)) Char->Crouch();

			Mem->Phase = EFireTaskPhase::Recover;
			Mem->PhaseTimer = RecoverPhaseDuration;
			break;
		}
		if (Mem->PhaseTimer <= 0.f)
		{
			AWeaponBase* Weapon = Enemy->GetCurrentWeapon();
			if (IsValid(Weapon))
				Weapon->StopFiring();

			// Recover: re-crouch if slot is crouch height
			AAICoverSlot* Slot = Cast<AAICoverSlot>(BB->GetValueAsObject(AEnemyAIController::BB_CoverSlot));
			const bool bHasCover = BB->GetValueAsBool(AEnemyAIController::BB_HasCover);
			if (bHasCover && IsValid(Slot) && Slot->Height == ECoverHeight::Crouch)
				if (ACharacter* Char = Cast<ACharacter>(Pawn)) Char->Crouch();

			Mem->Phase = EFireTaskPhase::Recover;
			Mem->PhaseTimer = RecoverPhaseDuration;
		}
		break;

	case EFireTaskPhase::Recover:
		if (Mem->PhaseTimer <= 0.f)
		{
			Mem->PauseDuration = FMath::RandRange(DA->BurstPauseMin, DA->BurstPauseMax);
			Mem->Phase = EFireTaskPhase::Pause;
			Mem->PhaseTimer = Mem->PauseDuration;
		}
		break;

	case EFireTaskPhase::Pause:
		if (Mem->PhaseTimer <= 0.f)
		{
			// Loop back to acquire (re-check target change for settle reset)
			Enemy->SetAimTarget(Target);
			Controller->SetFocus(Target);
			Mem->Phase = EFireTaskPhase::Acquire;
			Mem->PhaseTimer = 0.f; // no reaction delay on subsequent loops
		}
		break;

	default:
		break;
	}
}

EBTNodeResult::Type UBTTask_EnemyCombatFire::AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	FFireMemory* Mem = reinterpret_cast<FFireMemory*>(NodeMemory);
	AAIController* Controller = OwnerComp.GetAIOwner();
	if (Controller) Controller->StopMovement();
	StopFireAndCleanUp(OwnerComp, Mem);
	return EBTNodeResult::Aborted;
}

void UBTTask_EnemyCombatFire::StopFireAndCleanUp(UBehaviorTreeComponent& OwnerComp, FFireMemory* Mem) const
{
	AAIController* Controller = OwnerComp.GetAIOwner();
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Pawn);
	if (!IsValid(Enemy)) return;

	AWeaponBase* Weapon = Enemy->GetCurrentWeapon();
	if (IsValid(Weapon))
		Weapon->StopFiring();

	Enemy->SetAimTarget(nullptr);

	if (Mem)
	{
		if (Mem->bSuppressCrouchedNoCover)
		{
			if (ACharacter* Char = Cast<ACharacter>(Pawn)) Char->UnCrouch();
			Mem->bSuppressCrouchedNoCover = false;
		}

		// Release any cover slot we claimed during SeekingCover.
		AAICoverSlot* ReseekSlotPtr = Mem->ReseekSlot.Get();
		if (IsValid(ReseekSlotPtr) && IsValid(Pawn))
		{
			ReseekSlotPtr->Release(Pawn);
			Mem->ReseekSlot = nullptr;

			// Clear the BB keys THIS task wrote (only when we owned the slot).
			UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
			if (BB)
			{
				BB->SetValueAsObject(AEnemyAIController::BB_CoverSlot, nullptr);
				BB->SetValueAsBool(AEnemyAIController::BB_HasCover, false);
			}
		}
	}

	if (Controller)
		Controller->ClearFocus(EAIFocusPriority::Gameplay);
}

FString UBTTask_EnemyCombatFire::GetStaticDescription() const
{
	return TEXT("Peek-fire loop: Acquire → Expose → Fire → Recover → Pause (stays InProgress while Combat)");
}
