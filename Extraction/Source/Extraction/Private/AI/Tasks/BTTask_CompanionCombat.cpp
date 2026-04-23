// BT task — companion faces enemy, fires in bursts with settling inaccuracy, reloads.

#include "BTTask_CompanionCombat.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "AIController.h"
#include "CompanionAIController.h" // for LogCompanionAI
#include "CompanionCharacter.h"
#include "WeaponBase.h"
#include "HealthComponent.h"
#include "DrawDebugHelpers.h"

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

	// Always warn (not gated) — a missing weapon means the task will silently do nothing forever.
	if (!Companion->GetCurrentWeapon())
		UE_LOG(LogCompanionAI, Warning, TEXT("%s: combat task started but CurrentWeapon is null"), *Companion->GetName());

	if (bDebugLogging)
	{
		const float Distance = FVector::Dist(Companion->GetActorLocation(), Target->GetActorLocation());
		UE_LOG(LogCompanionAI, Log, TEXT("%s: combat ENTER target=%s dist=%.0f MaxRange=%.0f"),
			*Companion->GetName(), *Target->GetName(), Distance, Companion->MaxEngageRange);
	}

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
		if (bDebugLogging)
			UE_LOG(LogCompanionAI, Verbose, TEXT("%s: OUT OF RANGE dist=%.0f > MaxRange=%.0f -> Failed"),
				*Companion->GetName(), Distance, Companion->MaxEngageRange);
#if ENABLE_DRAW_DEBUG
		if (bDebugLogging)
			DrawDebugLine(Companion->GetWorld(), MyLocation, TargetLocation, FColor::Yellow, false, 0.5f, 0, 2.0f);
#endif
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
		if (bDebugLogging)
			UE_LOG(LogCompanionAI, Verbose, TEXT("%s: reloading"), *Companion->GetName());
		return;
	}

	// Don't fire while reloading
	if (Companion->IsReloading()) return;

	// Line-of-sight check
	const UWorld* World = Companion->GetWorld();
	bool bLineOfSight = true;
	AActor* BlockedBy = nullptr;
	if (World)
	{
		FHitResult Hit;
		FCollisionQueryParams QueryParams;
		QueryParams.AddIgnoredActor(Companion);
		QueryParams.AddIgnoredActor(Companion->GetCurrentWeapon());

		const bool bHit = World->LineTraceSingleByChannel(Hit, MyLocation, TargetLocation, ECC_Visibility, QueryParams);
		if (bHit && Hit.GetActor() != Target)
		{
			bLineOfSight = false;
			BlockedBy = Hit.GetActor();

			// Blocked — stop firing, wait
			if (bIsFiringBurst)
			{
				Companion->StopWeaponFire();
				bIsFiringBurst = false;
			}
			if (bDebugLogging)
				UE_LOG(LogCompanionAI, Verbose, TEXT("%s: LOS BLOCKED by %s"),
					*Companion->GetName(), *GetNameSafe(BlockedBy));
#if ENABLE_DRAW_DEBUG
			if (bDebugLogging)
				DrawDebugLine(World, MyLocation, Hit.ImpactPoint, FColor::Red, false, 0.25f, 0, 2.0f);
#endif
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
		if (bDebugLogging)
			UE_LOG(LogCompanionAI, Verbose, TEXT("%s: burst END"), *Companion->GetName());
	}
	else if (!bIsFiringBurst && BurstTimer <= 0.0f)
	{
		// Start new burst. Body stays on the smooth look-at interp above; the weapon applies
		// spread to the fire direction via Companion->GetCurrentInaccuracy() in WeaponBase.
		Companion->StartWeaponFire();
		bIsFiringBurst = true;
		BurstTimer = FireBurstDuration;
		if (bDebugLogging)
			UE_LOG(LogCompanionAI, Verbose, TEXT("%s: burst START (inaccuracy=%.1f deg)"),
				*Companion->GetName(), Companion->GetCurrentInaccuracy());
	}

#if ENABLE_DRAW_DEBUG
	// Green line while actively firing + LOS clear
	if (bDebugLogging && bIsFiringBurst && bLineOfSight)
		DrawDebugLine(World, MyLocation, TargetLocation, FColor::Green, false, 0.1f, 0, 1.5f);
#endif
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
