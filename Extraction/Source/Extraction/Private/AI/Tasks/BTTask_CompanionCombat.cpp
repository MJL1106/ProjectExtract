// BT task — cover-aware companion combat. State machine drives EngageFromOpen, EngageFromCover, StandUpFire.

#include "BTTask_CompanionCombat.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "AIController.h"
#include "CompanionAIController.h" // for LogCompanionAI
#include "CompanionCharacter.h"
#include "Animation/CompanionAnimInstance.h"
#include "WeaponBase.h"
#include "HealthComponent.h"
#include "DrawDebugHelpers.h"

namespace
{
	struct FCombatContext
	{
		ACompanionCharacter* Companion = nullptr;
		AActor* Target = nullptr;
		UBlackboardComponent* Blackboard = nullptr;
	};

	static EPeekSide ResolvePeekSide(const FVector& CoverLoc, const FVector& TargetLoc, const FVector& CompanionLoc)
	{
		FVector ApproachDir = (CoverLoc - CompanionLoc);
		ApproachDir.Z = 0.f;
		if (!ApproachDir.Normalize()) return EPeekSide::Right;

		const FVector WallRight = FVector::CrossProduct(FVector::UpVector, ApproachDir).GetSafeNormal();
		FVector TargetOffset = (TargetLoc - CoverLoc);
		TargetOffset.Z = 0.f;
		TargetOffset = TargetOffset.GetSafeNormal();

		const float Dot = FVector::DotProduct(TargetOffset, WallRight);
		return (Dot > 0.f) ? EPeekSide::Right : EPeekSide::Left;
	}

	static bool ReadCoverState(const UBlackboardComponent* BB,
		const FBlackboardKeySelector& HasCoverKey,
		const FBlackboardKeySelector& CoverLocKey,
		FVector& OutCoverLoc)
	{
		if (!BB) return false;
		if (!BB->GetValueAsBool(HasCoverKey.SelectedKeyName)) return false;
		OutCoverLoc = BB->GetValueAsVector(CoverLocKey.SelectedKeyName);
		return !OutCoverLoc.IsNearlyZero();
	}
}

UBTTask_CompanionCombat::UBTTask_CompanionCombat()
{
	NodeName = TEXT("Companion Combat");
	bNotifyTick = true;
	bNotifyTaskFinished = true;
	bCreateNodeInstance = true;
}

EBTNodeResult::Type UBTTask_CompanionCombat::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!BB) return EBTNodeResult::Failed;

	AActor* Target = Cast<AActor>(BB->GetValueAsObject(CombatTargetKey.SelectedKeyName));
	if (!IsValid(Target)) return EBTNodeResult::Failed;

	AAIController* AICtl = OwnerComp.GetAIOwner();
	ACompanionCharacter* Companion = AICtl ? Cast<ACompanionCharacter>(AICtl->GetPawn()) : nullptr;
	if (!Companion) return EBTNodeResult::Failed;

	Companion->SetAimTarget(Target);
	BurstTimer = 0.0f;
	bIsFiringBurst = false;
	TimeInCoverIdle = 0.f;
	PeekCooldown = FMath::RandRange(MinPeekCooldown, MaxPeekCooldown);
	CoverValidityCheckTimer = 0.f;
	TimeAtCurrentCover = 0.f;

	// Reset peek-resolve cache so first tick always resolves.
	LastPeekResolveCoverLoc = FVector::ZeroVector;
	LastPeekResolveTargetLoc = FVector::ZeroVector;

	if (!Companion->GetCurrentWeapon())
		UE_LOG(LogCompanionAI, Warning, TEXT("%s: combat task started but CurrentWeapon is null"), *Companion->GetName());

	// Choose initial state based on cover availability.
	FVector CoverLoc;
	if (ReadCoverState(BB, HasCoverPositionKey, CoverLocationKey, CoverLoc))
	{
		ResolvedPeekSide = ResolvePeekSide(CoverLoc, Target->GetActorLocation(), Companion->GetActorLocation());
		if (UCompanionAnimInstance* Anim = Cast<UCompanionAnimInstance>(Companion->GetMesh() ? Companion->GetMesh()->GetAnimInstance() : nullptr))
			Anim->EnterCoverPose(ResolvedPeekSide);
	}

	if (bDebugLogging)
	{
		const float Distance = FVector::Dist(Companion->GetActorLocation(), Target->GetActorLocation());
		UE_LOG(LogCompanionAI, Log, TEXT("%s: combat ENTER target=%s dist=%.0f MaxRange=%.0f"),
			*Companion->GetName(), *Target->GetName(), Distance, Companion->MaxEngageRange);
	}

	return EBTNodeResult::InProgress;
}

namespace
{
	// Helper — read companion + target from owner/BB. Returns false if either is invalid.
	static bool ResolveCombatContext(UBehaviorTreeComponent& OwnerComp,
		const FBlackboardKeySelector& CombatTargetKey,
		FCombatContext& Out)
	{
		AAIController* Controller = OwnerComp.GetAIOwner();
		if (!Controller) return false;

		Out.Companion = Cast<ACompanionCharacter>(Controller->GetPawn());
		if (!Out.Companion) return false;

		Out.Blackboard = OwnerComp.GetBlackboardComponent();
		if (!Out.Blackboard) return false;

		Out.Target = Cast<AActor>(Out.Blackboard->GetValueAsObject(CombatTargetKey.SelectedKeyName));
		if (!IsValid(Out.Target)) return false;

		UHealthComponent* TargetHealth = Out.Target->FindComponentByClass<UHealthComponent>();
		if (TargetHealth && TargetHealth->IsDead()) return false;

		return true;
	}

	static UCompanionAnimInstance* GetCompanionAnim(ACompanionCharacter* Companion)
	{
		if (!IsValid(Companion)) return nullptr;
		USkeletalMeshComponent* Mesh = Companion->GetMesh();
		if (!IsValid(Mesh)) return nullptr;
		return Cast<UCompanionAnimInstance>(Mesh->GetAnimInstance());
	}

	// Returns true if LOS from FromLoc to ToTarget is clear (ignoring Companion + its weapon).
	static bool HasLineOfSight(UWorld* World, const FVector& FromLoc, AActor* ToTarget, ACompanionCharacter* Companion, AActor*& OutBlockedBy)
	{
		OutBlockedBy = nullptr;
		if (!World || !IsValid(ToTarget) || !IsValid(Companion)) return false;

		FHitResult Hit;
		FCollisionQueryParams QueryParams;
		QueryParams.AddIgnoredActor(Companion);
		QueryParams.AddIgnoredActor(Companion->GetCurrentWeapon());

		const FVector ToLoc = ToTarget->GetActorLocation();
		const bool bHit = World->LineTraceSingleByChannel(Hit, FromLoc, ToLoc, ECC_Visibility, QueryParams);
		if (bHit && Hit.GetActor() != ToTarget)
		{
			OutBlockedBy = Hit.GetActor();
			return false;
		}
		return true;
	}
}

void UBTTask_CompanionCombat::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	FCombatContext Ctx;
	if (!ResolveCombatContext(OwnerComp, CombatTargetKey, Ctx))
	{
		// Target invalid / dead / controller missing — clean up and exit.
		if (Ctx.Companion) Ctx.Companion->StopWeaponFire();
		// Succeed if the target died; fail otherwise. Match prior behaviour: if no controller/pawn -> Failed.
		AAIController* Controller = OwnerComp.GetAIOwner();
		ACompanionCharacter* Companion = Controller ? Cast<ACompanionCharacter>(Controller->GetPawn()) : nullptr;
		if (!Companion) return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);

		AActor* Target = Cast<AActor>(OwnerComp.GetBlackboardComponent() ? OwnerComp.GetBlackboardComponent()->GetValueAsObject(CombatTargetKey.SelectedKeyName) : nullptr);
		if (!IsValid(Target)) return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);

		UHealthComponent* TargetHealth = Target->FindComponentByClass<UHealthComponent>();
		const EBTNodeResult::Type Result = (TargetHealth && TargetHealth->IsDead())
			? EBTNodeResult::Succeeded : EBTNodeResult::Failed;
		return FinishLatentTask(OwnerComp, Result);
	}

	Ctx.Companion->SetAimTarget(Ctx.Target);

	const FVector MyLocation = Ctx.Companion->GetActorLocation();
	const FVector TargetLocation = Ctx.Target->GetActorLocation();
	const float Distance = FVector::Dist(MyLocation, TargetLocation);

	// Range check (applies to all states — out of range aborts the engagement).
	if (Distance > Ctx.Companion->MaxEngageRange)
	{
		Ctx.Companion->StopWeaponFire();
		if (bDebugLogging)
			UE_LOG(LogCompanionAI, Verbose, TEXT("%s: OUT OF RANGE dist=%.0f > MaxRange=%.0f -> Failed"),
				*Ctx.Companion->GetName(), Distance, Ctx.Companion->MaxEngageRange);
#if ENABLE_DRAW_DEBUG
		if (bDebugLogging)
			DrawDebugLine(Ctx.Companion->GetWorld(), MyLocation, TargetLocation, FColor::Yellow, false, 0.5f, 0, 2.0f);
#endif
		return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
	}

	// Decide state for this tick by reading the cover BB keys live each frame.
	FVector CoverLoc;
	const bool bHasCover = ReadCoverState(Ctx.Blackboard, HasCoverPositionKey, CoverLocationKey, CoverLoc);

	UCompanionAnimInstance* Anim = GetCompanionAnim(Ctx.Companion);

	// --- ENGAGE FROM COVER (cover-idle, no firing) ---
	if (bHasCover && !bIsFiringBurst)
	{
		// Accumulate dwell at this cover slot.
		TimeAtCurrentCover += DeltaSeconds;

		// Resolve / refresh peek side only when geometry has changed beyond threshold.
		const float CoverDeltaSq = FVector::DistSquared(CoverLoc, LastPeekResolveCoverLoc);
		const float TargetDeltaSq = FVector::DistSquared(TargetLocation, LastPeekResolveTargetLoc);
		if (CoverDeltaSq > PeekResolveDistThresholdSq || TargetDeltaSq > PeekResolveDistThresholdSq)
		{
			ResolvedPeekSide = ResolvePeekSide(CoverLoc, TargetLocation, MyLocation);
			LastPeekResolveCoverLoc = CoverLoc;
			LastPeekResolveTargetLoc = TargetLocation;
		}

		// Rotate to face the cover line of approach (looks toward the target's general direction).
		const FRotator LookAtRot = (TargetLocation - MyLocation).Rotation();
		const FRotator DesiredRot = FRotator(0.0f, LookAtRot.Yaw, 0.0f);
		Ctx.Companion->SetActorRotation(
			FMath::RInterpTo(Ctx.Companion->GetActorRotation(), DesiredRot, DeltaSeconds, Ctx.Companion->RotationInterpSpeed));

		// Periodic cover-validity recheck: if target now has clear LOS on our cover slot, re-eval.
		CoverValidityCheckTimer += DeltaSeconds;
		if (CoverValidityCheckTimer >= CoverValidityCheckInterval)
		{
			CoverValidityCheckTimer = 0.f;
			const FVector TargetEye = TargetLocation + FVector(0.f, 0.f, TargetEyeHeightOffset);
			FCollisionQueryParams ValidityParams;
			ValidityParams.AddIgnoredActor(Ctx.Companion);
			ValidityParams.AddIgnoredActor(Ctx.Companion->GetCurrentWeapon());
			ValidityParams.AddIgnoredActor(Ctx.Target);
			FHitResult ValidityHit;
			const bool bBlocked = Ctx.Companion->GetWorld()->LineTraceSingleByChannel(
				ValidityHit, CoverLoc, TargetEye, ECC_Visibility, ValidityParams);

			if (!bBlocked && TimeAtCurrentCover >= MinCoverDwellBeforeReEval)
			{
				if (bDebugLogging)
					UE_LOG(LogCompanionAI, Verbose, TEXT("%s: cover INVALID (target has LOS on cover) — re-evaluating"),
						*Ctx.Companion->GetName());
				Ctx.Blackboard->SetValueAsBool(HasCoverPositionKey.SelectedKeyName, false);
				return FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
			}
		}

		// Idle dwell + peek cooldown gating.
		TimeInCoverIdle += DeltaSeconds;
		const bool bDwellReady = TimeInCoverIdle >= MinCoverIdleDwell;
		const bool bCooldownReady = TimeInCoverIdle >= MinCoverIdleDwell + PeekCooldown;

		if (!bDwellReady || !bCooldownReady) return;

		// LOS-from-cover check: only stand up if we can actually see the target from the cover slot.
		AActor* BlockedBy = nullptr;
		const bool bLosFromCover = HasLineOfSight(Ctx.Companion->GetWorld(), CoverLoc, Ctx.Target, Ctx.Companion, BlockedBy);
		if (!bLosFromCover)
		{
			if (bDebugLogging)
				UE_LOG(LogCompanionAI, Verbose, TEXT("%s: cover-LOS blocked by %s — holding idle"),
					*Ctx.Companion->GetName(), *GetNameSafe(BlockedBy));
			return;
		}

		// Begin stand-up-fire. Reload check first — don't stand up with an empty mag.
		if (Ctx.Companion->NeedsReload() && !Ctx.Companion->IsReloading())
		{
			Ctx.Companion->ReloadWeapon();
			if (bDebugLogging)
				UE_LOG(LogCompanionAI, Verbose, TEXT("%s: reloading before stand-up"), *Ctx.Companion->GetName());
			return;
		}
		if (Ctx.Companion->IsReloading()) return;

		// Exit cover pose -> standing locomotion; fire montage layers via UpperBody slot in AnimGraph.
		if (Anim) Anim->ExitCoverPose();
		Ctx.Companion->StartWeaponFire();
		bIsFiringBurst = true;
		BurstTimer = FireBurstDuration;
		TimeInCoverIdle = 0.f;
		TimeAtCurrentCover = 0.f;
		CoverValidityCheckTimer = 0.f;

		if (bDebugLogging)
			UE_LOG(LogCompanionAI, Verbose, TEXT("%s: STAND-UP-FIRE start side=%s"),
				*Ctx.Companion->GetName(), ResolvedPeekSide == EPeekSide::Right ? TEXT("Right") : TEXT("Left"));
		return;
	}

	// --- STAND-UP FIRE (mid-burst, cover still tracked) ---
	if (bIsFiringBurst && bHasCover)
	{
		BurstTimer -= DeltaSeconds;

		// Rotate toward target while standing (firing from standing locomotion).
		const FRotator LookAtRot = (TargetLocation - MyLocation).Rotation();
		const FRotator DesiredRot = FRotator(0.0f, LookAtRot.Yaw, 0.0f);
		Ctx.Companion->SetActorRotation(
			FMath::RInterpTo(Ctx.Companion->GetActorRotation(), DesiredRot, DeltaSeconds, Ctx.Companion->RotationInterpSpeed));

		// Reload mid-burst still applies — but stop fire first, then re-enter cover.
		if (Ctx.Companion->NeedsReload() && !Ctx.Companion->IsReloading())
		{
			Ctx.Companion->StopWeaponFire();
			Ctx.Companion->ReloadWeapon();
			bIsFiringBurst = false;
			ResolvedPeekSide = ResolvePeekSide(CoverLoc, TargetLocation, MyLocation);
			LastPeekResolveCoverLoc = FVector::ZeroVector;
			LastPeekResolveTargetLoc = FVector::ZeroVector;
			if (Anim) Anim->EnterCoverPose(ResolvedPeekSide);
			PeekCooldown = FMath::RandRange(MinPeekCooldown, MaxPeekCooldown);
			TimeInCoverIdle = 0.f;
			return;
		}

		if (BurstTimer <= 0.f)
		{
			// Burst ended — return to cover-idle pose. Refresh peek side in case target circled during burst.
			Ctx.Companion->StopWeaponFire();
			bIsFiringBurst = false;
			ResolvedPeekSide = ResolvePeekSide(CoverLoc, TargetLocation, MyLocation);
			LastPeekResolveCoverLoc = CoverLoc;
			LastPeekResolveTargetLoc = TargetLocation;
			if (Anim) Anim->EnterCoverPose(ResolvedPeekSide);
			PeekCooldown = FMath::RandRange(MinPeekCooldown, MaxPeekCooldown);
			TimeInCoverIdle = 0.f;
			if (bDebugLogging)
				UE_LOG(LogCompanionAI, Verbose, TEXT("%s: STAND-UP-FIRE end -> cover idle (next cooldown=%.2fs)"),
					*Ctx.Companion->GetName(), PeekCooldown);
		}
		return;
	}

	// --- ENGAGE FROM OPEN (no cover available — preserved naive behaviour) ---
	// Clean up any in-progress burst from cover/stand-up before falling through.
	// (Cover invalidated mid-burst would otherwise leak bIsFiringBurst + BurstTimer.)
	if (bIsFiringBurst)
	{
		Ctx.Companion->StopWeaponFire();
		bIsFiringBurst = false;
		BurstTimer = 0.f;
	}

	// Cover-tracking only applies in EngageFromCover; reset when out of cover.
	TimeAtCurrentCover = 0.f;
	CoverValidityCheckTimer = 0.f;

	// If we were in cover this frame but bHasCover flipped false, leave cover pose first.
	if (Anim && Anim->IsInCover() && !bHasCover)
		Anim->ExitCoverPose();

	// Rotate toward target
	const FRotator LookAtRot = (TargetLocation - MyLocation).Rotation();
	const FRotator DesiredRot = FRotator(0.0f, LookAtRot.Yaw, 0.0f);
	Ctx.Companion->SetActorRotation(
		FMath::RInterpTo(Ctx.Companion->GetActorRotation(), DesiredRot, DeltaSeconds, Ctx.Companion->RotationInterpSpeed));

	// Reload if needed
	if (Ctx.Companion->NeedsReload() && !Ctx.Companion->IsReloading())
	{
		Ctx.Companion->StopWeaponFire();
		Ctx.Companion->ReloadWeapon();
		bIsFiringBurst = false;
		if (bDebugLogging)
			UE_LOG(LogCompanionAI, Verbose, TEXT("%s: reloading"), *Ctx.Companion->GetName());
		return;
	}

	// Don't fire while reloading
	if (Ctx.Companion->IsReloading()) return;

	// Line-of-sight check — inlined here so the debug draw can reuse the same FHitResult.
	FHitResult LosHit;
	{
		FCollisionQueryParams QueryParams;
		QueryParams.AddIgnoredActor(Ctx.Companion);
		QueryParams.AddIgnoredActor(Ctx.Companion->GetCurrentWeapon());
		Ctx.Companion->GetWorld()->LineTraceSingleByChannel(LosHit, MyLocation, TargetLocation, ECC_Visibility, QueryParams);
	}
	const bool bLineOfSight = (!LosHit.bBlockingHit) || (LosHit.GetActor() == Ctx.Target);
	if (!bLineOfSight)
	{
		AActor* BlockedBy = LosHit.GetActor();
		if (bIsFiringBurst)
		{
			Ctx.Companion->StopWeaponFire();
			bIsFiringBurst = false;
		}
		if (bDebugLogging)
			UE_LOG(LogCompanionAI, Verbose, TEXT("%s: LOS BLOCKED by %s"),
				*Ctx.Companion->GetName(), *GetNameSafe(BlockedBy));
#if ENABLE_DRAW_DEBUG
		if (bDebugLogging)
			DrawDebugLine(Ctx.Companion->GetWorld(), MyLocation, LosHit.ImpactPoint, FColor::Red, false, 0.25f, 0, 2.0f);
#endif
		return;
	}

	// Burst fire logic (preserved)
	BurstTimer -= DeltaSeconds;

	if (bIsFiringBurst && BurstTimer <= 0.0f)
	{
		Ctx.Companion->StopWeaponFire();
		bIsFiringBurst = false;
		BurstTimer = FirePauseDuration;
		if (bDebugLogging)
			UE_LOG(LogCompanionAI, Verbose, TEXT("%s: burst END"), *Ctx.Companion->GetName());
	}
	else if (!bIsFiringBurst && BurstTimer <= 0.0f)
	{
		Ctx.Companion->StartWeaponFire();
		bIsFiringBurst = true;
		BurstTimer = FireBurstDuration;
		if (bDebugLogging)
			UE_LOG(LogCompanionAI, Verbose, TEXT("%s: burst START (inaccuracy=%.1f deg)"),
				*Ctx.Companion->GetName(), Ctx.Companion->GetCurrentInaccuracy());
	}

#if ENABLE_DRAW_DEBUG
	if (bDebugLogging && bIsFiringBurst)
		DrawDebugLine(Ctx.Companion->GetWorld(), MyLocation, TargetLocation, FColor::Green, false, 0.1f, 0, 1.5f);
#endif
}

void UBTTask_CompanionCombat::OnTaskFinished(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTNodeResult::Type TaskResult)
{
	AAIController* Controller = OwnerComp.GetAIOwner();
	ACompanionCharacter* Companion = Controller ? Cast<ACompanionCharacter>(Controller->GetPawn()) : nullptr;
	if (Companion)
	{
		Companion->StopWeaponFire();
		Companion->SetAimTarget(nullptr);

		if (UCompanionAnimInstance* Anim = GetCompanionAnim(Companion))
			Anim->ExitCoverPose();
	}

	bIsFiringBurst = false;
	BurstTimer = 0.f;
	TimeInCoverIdle = 0.f;
	CoverValidityCheckTimer = 0.f;
	TimeAtCurrentCover = 0.f;
}

FString UBTTask_CompanionCombat::GetStaticDescription() const
{
	return FString::Printf(TEXT("Cover-aware Combat (burst: %.1fs fire, %.1fs pause; peek cd %.1f-%.1fs)"),
		FireBurstDuration, FirePauseDuration, MinPeekCooldown, MaxPeekCooldown);
}
