// BTTask_EnemyCombatFire — latent peek-fire loop driven by a NodeMemory state machine.

#include "BTTask_EnemyCombatFire.h"
#include "EnemyAIController.h"
#include "EnemyArchetypeData.h"
#include "EnemyCharacter.h"
#include "WeaponBase.h"
#include "AICoverSlot.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "AIController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Character.h"
#include "CoverSlotTypes.h"
#include "Engine/World.h"

static constexpr float ExposePhaseDuration  = 0.2f;  // settle before first shot
static constexpr float RecoverPhaseDuration = 0.15f; // re-crouch settle after burst

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

	AActor* Target = Cast<AActor>(BB->GetValueAsObject(AEnemyAIController::BB_CombatTarget));
	if (!IsValid(Target)) return EBTNodeResult::Failed;

	AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Pawn);
	if (!IsValid(Enemy)) return EBTNodeResult::Failed;

	const UEnemyArchetypeData* DA = Enemy->GetArchetypeData();
	if (!IsValid(DA)) return EBTNodeResult::Failed;

	// Check target still reachable
	const bool bHasLOS = BB->GetValueAsBool(AEnemyAIController::BB_HasLineOfSight);
	const bool bInRange = BB->GetValueAsBool(AEnemyAIController::BB_TargetInRange);
	if (!bInRange && !bHasLOS) return EBTNodeResult::Failed;

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

	AActor* Target = Cast<AActor>(BB->GetValueAsObject(AEnemyAIController::BB_CombatTarget));

	// Abort if target lost or out of range with no LOS
	if (!IsValid(Target))
	{
		StopFireAndCleanUp(OwnerComp);
		return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
	}

	const bool bHasLOS = BB->GetValueAsBool(AEnemyAIController::BB_HasLineOfSight);
	const bool bInRange = BB->GetValueAsBool(AEnemyAIController::BB_TargetInRange);

	// Fail out to let the selector re-seek cover if far and no LOS
	if (!bInRange && !bHasLOS && Mem->Phase != EFireTaskPhase::Fire)
	{
		StopFireAndCleanUp(OwnerComp);
		return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
	}

	Mem->PhaseTimer -= DeltaSeconds;

	switch (Mem->Phase)
	{
	case EFireTaskPhase::Acquire:
		if (Mem->PhaseTimer <= 0.f)
		{
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
	}
}

EBTNodeResult::Type UBTTask_EnemyCombatFire::AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	StopFireAndCleanUp(OwnerComp);
	return EBTNodeResult::Aborted;
}

void UBTTask_EnemyCombatFire::StopFireAndCleanUp(UBehaviorTreeComponent& OwnerComp) const
{
	AAIController* Controller = OwnerComp.GetAIOwner();
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Pawn);
	if (!IsValid(Enemy)) return;

	AWeaponBase* Weapon = Enemy->GetCurrentWeapon();
	if (IsValid(Weapon))
		Weapon->StopFiring();

	Enemy->SetAimTarget(nullptr);

	if (Controller)
		Controller->ClearFocus(EAIFocusPriority::Gameplay);
}

FString UBTTask_EnemyCombatFire::GetStaticDescription() const
{
	return TEXT("Peek-fire loop: Acquire → Expose → Fire → Recover → Pause");
}
