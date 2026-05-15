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

	// Returns true if a stand-height trace from cover to target's eye is BLOCKED — meaning the
	// cover is too tall for the companion to fire over (would shoot the wall instead of the enemy).
	static bool IsCoverTooTallToFireOver(UWorld* World, const FVector& CoverLoc, AActor* Target,
		ACompanionCharacter* Companion, float StandFireHeightOffset, float TargetEyeHeightOffset)
	{
		if (!World || !IsValid(Target) || !IsValid(Companion)) return false;

		const FVector StandOrigin = CoverLoc + FVector(0.f, 0.f, StandFireHeightOffset);
		const FVector TargetEye = Target->GetActorLocation() + FVector(0.f, 0.f, TargetEyeHeightOffset);

		FCollisionQueryParams Params;
		Params.AddIgnoredActor(Companion);
		Params.AddIgnoredActor(Companion->GetCurrentWeapon());
		Params.AddIgnoredActor(Target);

		FHitResult Hit;
		const bool bBlocked = World->LineTraceSingleByChannel(Hit, StandOrigin, TargetEye, ECC_Visibility, Params);
		return bBlocked && Hit.GetActor() != Target;
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
	LosBlockedAccum = 0.f;
	LastTickBranch = -1;
	bLastLosBlocked = false;
	LastLosBlocker = nullptr;

	// Reset peek-resolve cache so first tick always resolves.
	LastPeekResolveCoverLoc = FVector::ZeroVector;
	LastPeekResolveTargetLoc = FVector::ZeroVector;

	if (!Companion->GetCurrentWeapon())
		UE_LOG(LogCompanionAI, Warning, TEXT("%s: combat task started but CurrentWeapon is null"), *Companion->GetName());

	// Choose initial state based on cover availability.
	FVector CoverLoc;
	if (ReadCoverState(BB, HasCoverPositionKey, CoverLocationKey, CoverLoc))
	{
		// Fast-fail: cover too tall to fire over -> invalidate now, drop into EngageFromOpen.
		if (IsCoverTooTallToFireOver(Companion->GetWorld(), CoverLoc, Target, Companion,
			StandFireHeightOffset, TargetEyeHeightOffset))
		{
			UE_LOG(LogCompanionAI, Log, TEXT("%s: cover too tall to fire over — falling back to open"),
				*Companion->GetName());
			BB->SetValueAsBool(HasCoverPositionKey.SelectedKeyName, false);
		}
		else
		{
			ResolvedPeekSide = ResolvePeekSide(CoverLoc, Target->GetActorLocation(), Companion->GetActorLocation());
			if (UCompanionAnimInstance* Anim = Cast<UCompanionAnimInstance>(Companion->GetMesh() ? Companion->GetMesh()->GetAnimInstance() : nullptr))
				Anim->EnterCoverPose(ResolvedPeekSide);
		}
	}

	if (bDebugLogging)
	{
		FVector CoverLocForLog;
		const bool bHasCoverForLog = ReadCoverState(BB, HasCoverPositionKey, CoverLocationKey, CoverLocForLog);
		const float Distance = FVector::Dist(Companion->GetActorLocation(), Target->GetActorLocation());
		UE_LOG(LogCompanionAI, Log, TEXT("%s: TASK ENTER target=%s dist=%.0f hasCover=%d"),
			*Companion->GetName(), *Target->GetName(), Distance, (int32)bHasCoverForLog);
	}

	return EBTNodeResult::InProgress;
}

namespace
{
	static const TCHAR* BranchName(int8 Index)
	{
		switch (Index)
		{
		case 0:  return TEXT("CoverIdle");
		case 1:  return TEXT("StandUpFireBurst");
		case 2:  return TEXT("OpenEngage");
		default: return TEXT("None");
		}
	}

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
}

void UBTTask_CompanionCombat::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	FCombatContext Ctx;
	if (!ResolveCombatContext(OwnerComp, CombatTargetKey, Ctx))
	{
		// Target invalid / dead / controller missing — clean up and exit.
		if (Ctx.Companion)
		{
			if (bDebugLogging)
				UE_LOG(LogCompanionAI, Log, TEXT("%s: FIRE STOP reason=target-invalid"), *Ctx.Companion->GetName());
			Ctx.Companion->StopWeaponFire();
		}
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

	// Read cover state early so the per-tick log can derive the branch string; reused below.
	FVector CoverLoc;
	const bool bHasCover = ReadCoverState(Ctx.Blackboard, HasCoverPositionKey, CoverLocationKey, CoverLoc);

	const int8 PrevBranch = LastTickBranch;
	{
		const int8 CurrentBranch = (bHasCover && !bIsFiringBurst) ? 0
			: (bIsFiringBurst && bHasCover) ? 1
			: 2;
		if (CurrentBranch != LastTickBranch)
		{
			if (bDebugLogging)
			{
				UE_LOG(LogCompanionAI, Log, TEXT("%s: BRANCH %s -> %s (cover=%d firing=%d dist=%.0f)"),
					*Ctx.Companion->GetName(), BranchName(LastTickBranch), BranchName(CurrentBranch),
					(int32)bHasCover, (int32)bIsFiringBurst, Distance);
			}
			LastTickBranch = CurrentBranch;
		}
	}

	// Range check (applies to all states — out of range aborts the engagement).
	if (Distance > Ctx.Companion->MaxEngageRange)
	{
		if (bDebugLogging)
			UE_LOG(LogCompanionAI, Log, TEXT("%s: FIRE STOP reason=out-of-range dist=%.0f > MaxRange=%.0f"),
				*Ctx.Companion->GetName(), Distance, Ctx.Companion->MaxEngageRange);
		Ctx.Companion->StopWeaponFire();
		if (bDebugLogging)
			UE_LOG(LogCompanionAI, Log, TEXT("%s: OUT OF RANGE dist=%.0f > MaxRange=%.0f -> Failed"),
				*Ctx.Companion->GetName(), Distance, Ctx.Companion->MaxEngageRange);
#if ENABLE_DRAW_DEBUG
		if (bDebugLogging)
			DrawDebugLine(Ctx.Companion->GetWorld(), MyLocation, TargetLocation, FColor::Yellow, false, 0.5f, 0, 2.0f);
#endif
		return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
	}

	UCompanionAnimInstance* Anim = GetCompanionAnim(Ctx.Companion);

	// --- ENGAGE FROM COVER (cover-idle, no firing) ---
	if (bHasCover && !bIsFiringBurst)
	{
		LosBlockedAccum = 0.f;
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

		// Periodic cover-validity recheck: two failure modes — target has LOS on cover (too short),
		// or cover is too tall for companion to fire over. Both invalidate the slot.
		CoverValidityCheckTimer += DeltaSeconds;
		if (CoverValidityCheckTimer >= CoverValidityCheckInterval)
		{
			CoverValidityCheckTimer = 0.f;

			const FVector TargetEye = TargetLocation + FVector(0.f, 0.f, TargetEyeHeightOffset);
			FCollisionQueryParams ValidityParams;
			ValidityParams.AddIgnoredActor(Ctx.Companion);
			ValidityParams.AddIgnoredActor(Ctx.Companion->GetCurrentWeapon());
			ValidityParams.AddIgnoredActor(Ctx.Target);

			// Crouch trace: if target's eye sees the cover slot at crouch height, our cover is too short.
			const FVector CrouchOrigin = CoverLoc + FVector(0.f, 0.f, CrouchHideHeightOffset);
			FHitResult CrouchHit;
			const bool bCrouchBlocked = Ctx.Companion->GetWorld()->LineTraceSingleByChannel(
				CrouchHit, CrouchOrigin, TargetEye, ECC_Visibility, ValidityParams);

			// Stand trace: if companion at stand height can't see target, cover is too tall to fire over.
			const FVector StandOrigin = CoverLoc + FVector(0.f, 0.f, StandFireHeightOffset);
			FHitResult StandHit;
			const bool bStandBlocked = Ctx.Companion->GetWorld()->LineTraceSingleByChannel(
				StandHit, StandOrigin, TargetEye, ECC_Visibility, ValidityParams);
			const bool bTooTall = bStandBlocked && StandHit.GetActor() != Ctx.Target;

			if (TimeAtCurrentCover >= MinCoverDwellBeforeReEval)
			{
				if (bTooTall)
				{
					UE_LOG(LogCompanionAI, Log, TEXT("%s: cover too tall to fire over — falling back to open"),
						*Ctx.Companion->GetName());
					Ctx.Blackboard->SetValueAsBool(HasCoverPositionKey.SelectedKeyName, false);
					return FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
				}
				if (!bCrouchBlocked)
				{
					if (bDebugLogging)
						UE_LOG(LogCompanionAI, Log, TEXT("%s: cover INVALID (target has LOS on cover) — re-evaluating"),
							*Ctx.Companion->GetName());
					Ctx.Blackboard->SetValueAsBool(HasCoverPositionKey.SelectedKeyName, false);
					return FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
				}
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
		if (bDebugLogging)
		{
			const bool bNowBlocked = !bLosFromCover;
			const bool bStateChanged = (bNowBlocked != bLastLosBlocked) || (BlockedBy != LastLosBlocker.Get());
			if (bStateChanged)
			{
				if (bNowBlocked)
				{
					UE_LOG(LogCompanionAI, Log, TEXT("%s: cover-LoS BLOCKED by %s"), *Ctx.Companion->GetName(), *GetNameSafe(BlockedBy));
				}
				else
				{
					UE_LOG(LogCompanionAI, Log, TEXT("%s: cover-LoS CLEAR"), *Ctx.Companion->GetName());
				}
				bLastLosBlocked = bNowBlocked;
				LastLosBlocker = BlockedBy;
			}
		}
		if (!bLosFromCover) return;

		// Begin stand-up-fire. Reload check first — don't stand up with an empty mag.
		if (Ctx.Companion->NeedsReload() && !Ctx.Companion->IsReloading())
		{
			Ctx.Companion->ReloadWeapon();
			if (bDebugLogging)
				UE_LOG(LogCompanionAI, Log, TEXT("%s: reloading before stand-up"), *Ctx.Companion->GetName());
			return;
		}
		if (Ctx.Companion->IsReloading()) return;

		// Exit cover pose -> standing locomotion; fire montage layers via UpperBody slot in AnimGraph.
		if (Anim) Anim->ExitCoverPose();
		if (bDebugLogging)
			UE_LOG(LogCompanionAI, Log, TEXT("%s: FIRE START branch=CoverIdle->StandUp dist=%.0f"),
				*Ctx.Companion->GetName(), Distance);
		Ctx.Companion->StartWeaponFire();
		bIsFiringBurst = true;
		BurstTimer = FireBurstDuration;
		TimeInCoverIdle = 0.f;
		TimeAtCurrentCover = 0.f;
		CoverValidityCheckTimer = 0.f;

		if (bDebugLogging)
			UE_LOG(LogCompanionAI, Log, TEXT("%s: STAND-UP-FIRE start side=%s"),
				*Ctx.Companion->GetName(), ResolvedPeekSide == EPeekSide::Right ? TEXT("Right") : TEXT("Left"));
		return;
	}

	// --- STAND-UP FIRE (mid-burst, cover still tracked) ---
	if (bIsFiringBurst && bHasCover)
	{
		LosBlockedAccum = 0.f;
		BurstTimer -= DeltaSeconds;

		// Diagnostic LoS trace — state-change only, does NOT gate fire.
		if (bDebugLogging)
		{
			AActor* BurstBlocker = nullptr;
			const bool bBurstLos = HasLineOfSight(Ctx.Companion->GetWorld(), MyLocation, Ctx.Target, Ctx.Companion, BurstBlocker);
			const bool bNowBlocked = !bBurstLos;
			const bool bBlockStateChanged = (bNowBlocked != bLastLosBlocked) || (BurstBlocker != LastLosBlocker.Get());
			if (bBlockStateChanged)
			{
				if (bNowBlocked)
				{
					UE_LOG(LogCompanionAI, Log, TEXT("%s: stand-burst LoS BLOCKED by %s"), *Ctx.Companion->GetName(), *GetNameSafe(BurstBlocker));
				}
				else
				{
					UE_LOG(LogCompanionAI, Log, TEXT("%s: stand-burst LoS CLEAR"), *Ctx.Companion->GetName());
				}
				bLastLosBlocked = bNowBlocked;
				LastLosBlocker = BurstBlocker;
			}
		}

		// Rotate toward target while standing (firing from standing locomotion).
		const FRotator LookAtRot = (TargetLocation - MyLocation).Rotation();
		const FRotator DesiredRot = FRotator(0.0f, LookAtRot.Yaw, 0.0f);
		Ctx.Companion->SetActorRotation(
			FMath::RInterpTo(Ctx.Companion->GetActorRotation(), DesiredRot, DeltaSeconds, Ctx.Companion->RotationInterpSpeed));

		// Reload mid-burst still applies — but stop fire first, then re-enter cover.
		if (Ctx.Companion->NeedsReload() && !Ctx.Companion->IsReloading())
		{
			if (bDebugLogging)
				UE_LOG(LogCompanionAI, Log, TEXT("%s: FIRE STOP reason=reload"), *Ctx.Companion->GetName());
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
			if (bDebugLogging)
				UE_LOG(LogCompanionAI, Log, TEXT("%s: FIRE STOP reason=burst-end-cover"), *Ctx.Companion->GetName());
			Ctx.Companion->StopWeaponFire();
			bIsFiringBurst = false;
			ResolvedPeekSide = ResolvePeekSide(CoverLoc, TargetLocation, MyLocation);
			LastPeekResolveCoverLoc = CoverLoc;
			LastPeekResolveTargetLoc = TargetLocation;
			if (Anim) Anim->EnterCoverPose(ResolvedPeekSide);
			PeekCooldown = FMath::RandRange(MinPeekCooldown, MaxPeekCooldown);
			TimeInCoverIdle = 0.f;
			if (bDebugLogging)
				UE_LOG(LogCompanionAI, Log, TEXT("%s: STAND-UP-FIRE end -> cover idle (next cooldown=%.2fs)"),
					*Ctx.Companion->GetName(), PeekCooldown);
		}
		return;
	}

	// --- ENGAGE FROM OPEN (no cover available — preserved naive behaviour) ---
	// Clean up a burst that was in progress when cover was lost this tick.
	// Guard with PrevBranch so this only fires on the actual cover->open transition,
	// not every tick we're already in open-engage (which caused the 60Hz FIRE STOP/START loop).
	const bool bWasInCover = (PrevBranch == 0 || PrevBranch == 1); // CoverIdle, StandUpFireBurst
	if (bIsFiringBurst && bWasInCover)
	{
		if (bDebugLogging)
		{
			UE_LOG(LogCompanionAI, Log, TEXT("%s: FIRE STOP reason=cover-flipped-mid-burst"), *Ctx.Companion->GetName());
		}
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

	// Line-of-sight check — moved before rotation so we can gate aim-tracking on LoS.
	FHitResult LosHit;
	{
		FCollisionQueryParams QueryParams;
		QueryParams.AddIgnoredActor(Ctx.Companion);
		QueryParams.AddIgnoredActor(Ctx.Companion->GetCurrentWeapon());
		Ctx.Companion->GetWorld()->LineTraceSingleByChannel(LosHit, MyLocation, TargetLocation, ECC_Visibility, QueryParams);
	}
	const bool bLineOfSight = (!LosHit.bBlockingHit) || (LosHit.GetActor() == Ctx.Target);
	if (bDebugLogging)
	{
		AActor* OpenBlocker = bLineOfSight ? nullptr : LosHit.GetActor();
		const bool bNowBlocked = !bLineOfSight;
		const bool bBlockStateChanged = (bNowBlocked != bLastLosBlocked) || (OpenBlocker != LastLosBlocker.Get());
		if (bBlockStateChanged)
		{
			if (bNowBlocked)
			{
				UE_LOG(LogCompanionAI, Log, TEXT("%s: open-LoS BLOCKED by %s"), *Ctx.Companion->GetName(), *GetNameSafe(OpenBlocker));
			}
			else
			{
				UE_LOG(LogCompanionAI, Log, TEXT("%s: open-LoS CLEAR"), *Ctx.Companion->GetName());
			}
			bLastLosBlocked = bNowBlocked;
			LastLosBlocker = OpenBlocker;
		}
	}

	if (!bLineOfSight)
	{
		AActor* BlockedBy = LosHit.GetActor();
		if (bIsFiringBurst)
		{
			if (bDebugLogging)
				UE_LOG(LogCompanionAI, Log, TEXT("%s: FIRE STOP reason=los-blocked-open"), *Ctx.Companion->GetName());
			Ctx.Companion->StopWeaponFire();
			bIsFiringBurst = false;
		}
#if ENABLE_DRAW_DEBUG
		if (bDebugLogging)
			DrawDebugLine(Ctx.Companion->GetWorld(), MyLocation, LosHit.ImpactPoint, FColor::Red, false, 0.25f, 0, 2.0f);
#endif
		LosBlockedAccum += DeltaSeconds;
		if (LosBlockedAccum > AimDropOnLosBlockedSeconds)
			Ctx.Companion->SetAimTarget(nullptr);
		else
		{
			// Grace period — still rotate toward target so brief blips don't snap aim.
			const FRotator LookAtRot = (TargetLocation - MyLocation).Rotation();
			const FRotator DesiredRot = FRotator(0.0f, LookAtRot.Yaw, 0.0f);
			Ctx.Companion->SetActorRotation(
				FMath::RInterpTo(Ctx.Companion->GetActorRotation(), DesiredRot, DeltaSeconds, Ctx.Companion->RotationInterpSpeed));
		}
		if (LosBlockedAbandonSeconds > 0.f && LosBlockedAccum >= LosBlockedAbandonSeconds)
		{
			if (bDebugLogging)
				UE_LOG(LogCompanionAI, Log, TEXT("%s: FIRE STOP reason=los-block-abandon"), *Ctx.Companion->GetName());
			Ctx.Companion->StopWeaponFire();
			// Return Succeeded (not Failed) so the parent Sequence succeeds and the root Selector
			// restarts the cycle, letting the Combat decorator re-evaluate. Returning Failed would
			// leave the BB-LowerPriority decorator dormant because the BB CombatTarget value is
			// unchanged — UE BB suppresses notifications on equal-value writes, so no re-entry
			// signal would ever fire and Combat would stay locked out until perception loses target.
			return FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
		}
		return;
	}

	// LoS clear — reset accumulator, rotate toward target, set aim.
	LosBlockedAccum = 0.f;
	Ctx.Companion->SetAimTarget(Ctx.Target);
	const FRotator LookAtRot = (TargetLocation - MyLocation).Rotation();
	const FRotator DesiredRot = FRotator(0.0f, LookAtRot.Yaw, 0.0f);
	Ctx.Companion->SetActorRotation(
		FMath::RInterpTo(Ctx.Companion->GetActorRotation(), DesiredRot, DeltaSeconds, Ctx.Companion->RotationInterpSpeed));

	// Reload if needed
	if (Ctx.Companion->NeedsReload() && !Ctx.Companion->IsReloading())
	{
		if (bDebugLogging)
			UE_LOG(LogCompanionAI, Log, TEXT("%s: FIRE STOP reason=reload"), *Ctx.Companion->GetName());
		Ctx.Companion->StopWeaponFire();
		Ctx.Companion->ReloadWeapon();
		bIsFiringBurst = false;
		return;
	}

	// Don't fire while reloading
	if (Ctx.Companion->IsReloading()) return;

	// Burst fire logic (preserved)
	BurstTimer -= DeltaSeconds;

	if (bIsFiringBurst && BurstTimer <= 0.0f)
	{
		if (bDebugLogging)
			UE_LOG(LogCompanionAI, Log, TEXT("%s: FIRE STOP reason=burst-end-open"), *Ctx.Companion->GetName());
		Ctx.Companion->StopWeaponFire();
		bIsFiringBurst = false;
		BurstTimer = FirePauseDuration;
	}
	else if (!bIsFiringBurst && BurstTimer <= 0.0f)
	{
		if (bDebugLogging)
			UE_LOG(LogCompanionAI, Log, TEXT("%s: FIRE START branch=OpenEngage dist=%.0f (inaccuracy=%.1f deg)"),
				*Ctx.Companion->GetName(), Distance, Ctx.Companion->GetCurrentInaccuracy());
		Ctx.Companion->StartWeaponFire();
		bIsFiringBurst = true;
		BurstTimer = FireBurstDuration;
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
		if (bDebugLogging)
		{
			const TCHAR* ResultStr = (TaskResult == EBTNodeResult::Succeeded) ? TEXT("Succeeded")
				: (TaskResult == EBTNodeResult::Failed) ? TEXT("Failed") : TEXT("Aborted");

			UBlackboardComponent* ExitBB = OwnerComp.GetBlackboardComponent();
			if (!ExitBB)
			{
				UE_LOG(LogCompanionAI, Log, TEXT("%s: TASK EXIT result=%s | BB unavailable"), *Companion->GetName(), ResultStr);
			}
			else
			{
				AActor* BBTarget = Cast<AActor>(ExitBB->GetValueAsObject(CombatTargetKey.SelectedKeyName));
				const bool bExitHasCover = ExitBB->GetValueAsBool(HasCoverPositionKey.SelectedKeyName);
				auto PostureStr = [](ECompanionPosture P) -> const TCHAR*
				{
					switch (P)
					{
					case ECompanionPosture::Combat:      return TEXT("Combat");
					case ECompanionPosture::Stealth:     return TEXT("Stealth");
					default:                             return TEXT("Exploration");
					}
				};
				UE_LOG(LogCompanionAI, Log, TEXT("%s: TASK EXIT result=%s | BB combatTarget=%s hasCover=%d posture=%s"),
					*Companion->GetName(), ResultStr,
					*GetNameSafe(BBTarget), (int32)bExitHasCover,
					PostureStr(Companion->GetPosture()));
			}
		}

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
	LosBlockedAccum = 0.f;
	LastTickBranch = -1;
	bLastLosBlocked = false;
	LastLosBlocker = nullptr;
}

FString UBTTask_CompanionCombat::GetStaticDescription() const
{
	return FString::Printf(TEXT("Cover-aware Combat (burst: %.1fs fire, %.1fs pause; peek cd %.1f-%.1fs)"),
		FireBurstDuration, FirePauseDuration, MinPeekCooldown, MaxPeekCooldown);
}
