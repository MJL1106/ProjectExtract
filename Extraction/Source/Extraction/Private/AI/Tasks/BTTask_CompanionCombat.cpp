// BT task — companion faces enemy, fires in bursts with settling inaccuracy, reloads.

#include "BTTask_CompanionCombat.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "AIController.h"
#include "CompanionCharacter.h"
#include "HealthComponent.h"

UBTTask_CompanionCombat::UBTTask_CompanionCombat()
{
	NodeName = TEXT("Companion Combat");
	bNotifyTick = true;
	bNotifyTaskFinished = true;
	bCreateNodeInstance = true;
}

EBTNodeResult::Type UBTTask_CompanionCombat::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	AActor* Target = Cast<AActor>(OwnerComp.GetBlackboardComponent()->GetValueAsObject(CombatTargetKey.SelectedKeyName));
	if (!IsValid(Target)) return EBTNodeResult::Failed;

	ACompanionCharacter* Companion = Cast<ACompanionCharacter>(OwnerComp.GetAIOwner()->GetPawn());
	if (!Companion) return EBTNodeResult::Failed;

	Companion->SetAimTarget(Target);
	BurstTimer = 0.0f;
	bIsFiringBurst = false;

	return EBTNodeResult::InProgress;
}

void UBTTask_CompanionCombat::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	AAIController* Controller = OwnerComp.GetAIOwner();
	if (!Controller) return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);

	ACompanionCharacter* Companion = Cast<ACompanionCharacter>(Controller->GetPawn());
	if (!Companion) return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);

	// Validate target
	AActor* Target = Cast<AActor>(OwnerComp.GetBlackboardComponent()->GetValueAsObject(CombatTargetKey.SelectedKeyName));
	if (!IsValid(Target))
	{
		Companion->StopWeaponFire();
		return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
	}

	UHealthComponent* TargetHealth = Target->FindComponentByClass<UHealthComponent>();
	if (TargetHealth && TargetHealth->IsDead())
	{
		Companion->StopWeaponFire();
		return FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
	}

	Companion->SetAimTarget(Target);

	const FVector MyLocation = Companion->GetActorLocation();
	const FVector TargetLocation = Target->GetActorLocation();
	const float Distance = FVector::Dist(MyLocation, TargetLocation);

	// Range check
	if (Distance > Companion->MaxEngageRange)
	{
		Companion->StopWeaponFire();
		return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
	}

	// Rotate toward target
	const FRotator LookAtRot = (TargetLocation - MyLocation).Rotation();
	const FRotator DesiredRot = FRotator(0.0f, LookAtRot.Yaw, 0.0f);
	Companion->SetActorRotation(
		FMath::RInterpTo(Companion->GetActorRotation(), DesiredRot, DeltaSeconds, Companion->RotationInterpSpeed));

	// Reload if needed
	if (Companion->NeedsReload() && !Companion->IsReloading())
	{
		Companion->StopWeaponFire();
		Companion->ReloadWeapon();
		bIsFiringBurst = false;
		return;
	}

	// Don't fire while reloading
	if (Companion->IsReloading()) return;

	// Line-of-sight check
	const UWorld* World = Companion->GetWorld();
	if (World)
	{
		FHitResult Hit;
		FCollisionQueryParams QueryParams;
		QueryParams.AddIgnoredActor(Companion);
		QueryParams.AddIgnoredActor(Companion->GetCurrentWeapon());

		const bool bHit = World->LineTraceSingleByChannel(Hit, MyLocation, TargetLocation, ECC_Visibility, QueryParams);
		if (bHit && Hit.GetActor() != Target)
		{
			// Blocked — stop firing, wait
			if (bIsFiringBurst)
			{
				Companion->StopWeaponFire();
				bIsFiringBurst = false;
			}
			return;
		}
	}

	// Burst fire logic
	BurstTimer -= DeltaSeconds;

	if (bIsFiringBurst && BurstTimer <= 0.0f)
	{
		// End burst
		Companion->StopWeaponFire();
		bIsFiringBurst = false;
		BurstTimer = FirePauseDuration;
	}
	else if (!bIsFiringBurst && BurstTimer <= 0.0f)
	{
		// Start new burst — apply inaccuracy to aim
		const float Inaccuracy = Companion->GetCurrentInaccuracy();
		FRotator AimRot = (TargetLocation - MyLocation).Rotation();
		AimRot.Yaw += FMath::RandRange(-Inaccuracy, Inaccuracy);
		AimRot.Pitch += FMath::RandRange(-Inaccuracy, Inaccuracy);
		Companion->SetActorRotation(FRotator(0.0f, AimRot.Yaw, 0.0f));

		Companion->StartWeaponFire();
		bIsFiringBurst = true;
		BurstTimer = FireBurstDuration;
	}
}

void UBTTask_CompanionCombat::OnTaskFinished(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTNodeResult::Type TaskResult)
{
	ACompanionCharacter* Companion = Cast<ACompanionCharacter>(OwnerComp.GetAIOwner()->GetPawn());
	if (Companion)
	{
		Companion->StopWeaponFire();
		Companion->SetAimTarget(nullptr);
	}
}

FString UBTTask_CompanionCombat::GetStaticDescription() const
{
	return FString::Printf(TEXT("Combat (burst: %.1fs fire, %.1fs pause)"), FireBurstDuration, FirePauseDuration);
}
