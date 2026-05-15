// BT task — cover-aware companion combat. State machine drives EngageFromOpen, EngageFromCover, StandUpFire.

#include "BTTask_CompanionCombat.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "AIController.h"
#include "CompanionAIController.h"
#include "CompanionCharacter.h"
#include "Animation/CompanionAnimInstance.h"
#include "WeaponBase.h"
#include "HealthComponent.h"
#include "AICoverSlot.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/CharacterMovementComponent.h"

namespace
{
	struct FCombatContext
	{
		ACompanionCharacter* Companion = nullptr;
		AActor* Target = nullptr;
		UBlackboardComponent* Blackboard = nullptr;
	};

	// Returns true if LOS from FromLoc to ToTarget is clear (ignoring Companion + its weapon).
	static bool HasLineOfSight(UWorld* World, const FVector& FromLoc, AActor* ToTarget, ACompanionCharacter* Companion, AActor*& OutBlockedBy)
	{
		OutBlockedBy = nullptr;
		if (!World || !IsValid(ToTarget) || !IsValid(Companion)) return false;

		FHitResult Hit;
		FCollisionQueryParams QueryParams;
		QueryParams.AddIgnoredActor(Companion);
		QueryParams.AddIgnoredActor(Companion->GetCurrentWeapon());

		const bool bHit = World->LineTraceSingleByChannel(Hit, FromLoc, ToTarget->GetActorLocation(), ECC_Visibility, QueryParams);
		if (bHit && Hit.GetActor() != ToTarget)
		{
			OutBlockedBy = Hit.GetActor();
			return false;
		}
		return true;
	}

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
	LastPeekResolveCoverLoc = FVector::ZeroVector;
	LastPeekResolveTargetLoc = FVector::ZeroVector;

	if (!Companion->GetCurrentWeapon())
		UE_LOG(LogCompanionAI, Warning, TEXT("%s: combat task started but CurrentWeapon is null"), *Companion->GetName());

	AAICoverSlot* Slot = Cast<AAICoverSlot>(BB->GetValueAsObject(CoverSlotKey.SelectedKeyName));
	const bool bHasCover = IsValid(Slot) && BB->GetValueAsBool(HasCoverPositionKey.SelectedKeyName);

	if (bHasCover)
	{
		// Use the slot's authored peek preference; default Either -> Right.
		const ECoverPeekSide Pref = Slot->PeekPreference;
		ResolvedPeekSide = (Pref == ECoverPeekSide::Left) ? EPeekSide::Left : EPeekSide::Right;

		if (AAIController* AIC = Cast<AAIController>(Companion->GetController()))
			AIC->StopMovement();
		if (UCharacterMovementComponent* CMC = Companion->GetCharacterMovement())
			CMC->StopMovementImmediately();
		if (Slot)
		{
			const FRotator SlotYawRot(0.f, Slot->GetActorRotation().Yaw, 0.f);
			Companion->SetActorRotation(SlotYawRot);
			const FVector SlotLoc = Slot->GetActorLocation();
			Companion->SetActorLocation(FVector(SlotLoc.X, SlotLoc.Y, Companion->GetActorLocation().Z));
		}
		if (UCompanionAnimInstance* Anim = GetCompanionAnim(Companion))
			Anim->EnterCoverPose(ResolvedPeekSide);
	}

	if (bDebugLogging)
	{
		const float Distance = FVector::Dist(Companion->GetActorLocation(), Target->GetActorLocation());
		UE_LOG(LogCompanionAI, Log, TEXT("%s: TASK ENTER target=%s dist=%.0f hasCover=%d slot=%s"),
			*Companion->GetName(), *Target->GetName(), Distance, (int32)bHasCover, *GetNameSafe(Slot));
	}

	return EBTNodeResult::InProgress;
}

void UBTTask_CompanionCombat::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	FCombatContext Ctx;
	if (!ResolveCombatContext(OwnerComp, CombatTargetKey, Ctx))
	{
		if (Ctx.Companion)
		{
			if (bDebugLogging)
				UE_LOG(LogCompanionAI, Log, TEXT("%s: FIRE STOP reason=target-invalid"), *Ctx.Companion->GetName());
			Ctx.Companion->StopWeaponFire();
		}
		AAIController* Controller = OwnerComp.GetAIOwner();
		ACompanionCharacter* Companion = Controller ? Cast<ACompanionCharacter>(Controller->GetPawn()) : nullptr;
		if (!Companion) return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);

		AActor* Target = Cast<AActor>(OwnerComp.GetBlackboardComponent()
			? OwnerComp.GetBlackboardComponent()->GetValueAsObject(CombatTargetKey.SelectedKeyName) : nullptr);
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

	AAICoverSlot* Slot = Cast<AAICoverSlot>(Ctx.Blackboard->GetValueAsObject(CoverSlotKey.SelectedKeyName));
	const bool bHasCover = IsValid(Slot) && Ctx.Blackboard->GetValueAsBool(HasCoverPositionKey.SelectedKeyName);

	const int8 PrevBranch = LastTickBranch;
	{
		const int8 CurrentBranch = (bHasCover && !bIsFiringBurst) ? 0
			: (bIsFiringBurst && bHasCover) ? 1
			: 2;
		if (CurrentBranch != LastTickBranch)
		{
			if (bDebugLogging)
				UE_LOG(LogCompanionAI, Log, TEXT("%s: BRANCH %s -> %s (cover=%d firing=%d dist=%.0f)"),
					*Ctx.Companion->GetName(), BranchName(LastTickBranch), BranchName(CurrentBranch),
					(int32)bHasCover, (int32)bIsFiringBurst, Distance);
			LastTickBranch = CurrentBranch;
		}
	}

	if (Distance > Ctx.Companion->MaxEngageRange)
	{
		if (bDebugLogging)
			UE_LOG(LogCompanionAI, Log, TEXT("%s: FIRE STOP reason=out-of-range dist=%.0f > MaxRange=%.0f"),
				*Ctx.Companion->GetName(), Distance, Ctx.Companion->MaxEngageRange);
		Ctx.Companion->StopWeaponFire();
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
		TimeAtCurrentCover += DeltaSeconds;

		// Refresh peek side only when geometry has changed beyond threshold.
		const FVector CoverLoc = Slot->GetStandPosition();
		const float CoverDeltaSq = FVector::DistSquared(CoverLoc, LastPeekResolveCoverLoc);
		const float TargetDeltaSq = FVector::DistSquared(TargetLocation, LastPeekResolveTargetLoc);
		if (CoverDeltaSq > PeekResolveDistThresholdSq || TargetDeltaSq > PeekResolveDistThresholdSq)
		{
			const ECoverPeekSide Pref = Slot->PeekPreference;
			if (Pref == ECoverPeekSide::Either)
			{
				// Derive from companion->target vector relative to slot forward.
				FVector SlotFwd = Slot->GetActorForwardVector(); SlotFwd.Z = 0.f; SlotFwd = SlotFwd.GetSafeNormal();
				const FVector WallRight = FVector::CrossProduct(FVector::UpVector, SlotFwd).GetSafeNormal();
				FVector TargetOffset = (TargetLocation - CoverLoc); TargetOffset.Z = 0.f; TargetOffset = TargetOffset.GetSafeNormal();
				ResolvedPeekSide = (FVector::DotProduct(TargetOffset, WallRight) > 0.f) ? EPeekSide::Right : EPeekSide::Left;
			}
			else
			{
				ResolvedPeekSide = (Pref == ECoverPeekSide::Left) ? EPeekSide::Left : EPeekSide::Right;
			}
			LastPeekResolveCoverLoc = CoverLoc;
			LastPeekResolveTargetLoc = TargetLocation;
		}

		const FRotator LookAtRot = (TargetLocation - MyLocation).Rotation();
		Ctx.Companion->SetActorRotation(FMath::RInterpTo(Ctx.Companion->GetActorRotation(),
			FRotator(0.f, LookAtRot.Yaw, 0.f), DeltaSeconds, Ctx.Companion->RotationInterpSpeed));

		// Periodic slot-validity check — no traces, reads slot data only.
		CoverValidityCheckTimer += DeltaSeconds;
		if (CoverValidityCheckTimer >= CoverValidityCheckInterval && TimeAtCurrentCover >= MinCoverDwellBeforeReEval)
		{
			CoverValidityCheckTimer = 0.f;

			// Check: slot still claimed by us.
			if (!Slot->IsClaimed())
			{
				UE_LOG(LogCompanionAI, Log, TEXT("%s: Cover INVALIDATE reason=slot-released slot=%s"), *Ctx.Companion->GetName(), *Slot->GetName());
				if (Slot->IsClaimedBy(Ctx.Companion)) Slot->Release(Ctx.Companion);
				Ctx.Blackboard->SetValueAsObject(CoverSlotKey.SelectedKeyName, nullptr);
				Ctx.Blackboard->SetValueAsBool(HasCoverPositionKey.SelectedKeyName, false);
				return FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
			}

			// Check: target still in fire arc.
			if (!Slot->IsTargetInFireArc(TargetLocation))
			{
				UE_LOG(LogCompanionAI, Log, TEXT("%s: Cover INVALIDATE reason=fire-arc-violated slot=%s"), *Ctx.Companion->GetName(), *Slot->GetName());
				if (Slot->IsClaimedBy(Ctx.Companion)) Slot->Release(Ctx.Companion);
				Ctx.Blackboard->SetValueAsObject(CoverSlotKey.SelectedKeyName, nullptr);
				Ctx.Blackboard->SetValueAsBool(HasCoverPositionKey.SelectedKeyName, false);
				return FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
			}
		}

		TimeInCoverIdle += DeltaSeconds;
		if (TimeInCoverIdle < MinCoverIdleDwell + PeekCooldown) return;

		// LOS-from-cover check: only stand up if we can see the target from the slot.
		// Trace from a stand-eye position (slot base + eye height) — not crouch level — so the
		// wall doesn't block the check before the companion has even risen.
		AActor* BlockedBy = nullptr;
		const FVector StandEye = Slot->GetActorLocation() + FVector(0.f, 0.f, StandFireEyeHeight);
		const bool bLosFromCover = HasLineOfSight(Ctx.Companion->GetWorld(), StandEye, Ctx.Target, Ctx.Companion, BlockedBy);
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

		if (Ctx.Companion->NeedsReload() && !Ctx.Companion->IsReloading())
		{
			Ctx.Companion->ReloadWeapon();
			if (bDebugLogging)
				UE_LOG(LogCompanionAI, Log, TEXT("%s: reloading before stand-up"), *Ctx.Companion->GetName());
			return;
		}
		if (Ctx.Companion->IsReloading()) return;

		// Only stand up if the slot supports it.
		if (!Slot->CanStandFireFrom())
		{
			if (bDebugLogging)
				UE_LOG(LogCompanionAI, Log, TEXT("%s: Cover REJECT stand-fire blocked by slot height slot=%s"), *Ctx.Companion->GetName(), *Slot->GetName());
			return;
		}

		if (Anim) Anim->ExitCoverPose();
		if (bDebugLogging)
			UE_LOG(LogCompanionAI, Log, TEXT("%s: FIRE START branch=CoverIdle->StandUp dist=%.0f"), *Ctx.Companion->GetName(), Distance);
		Ctx.Companion->StartWeaponFire();
		bIsFiringBurst = true;
		BurstTimer = FireBurstDuration;
		TimeInCoverIdle = 0.f;
		TimeAtCurrentCover = 0.f;
		CoverValidityCheckTimer = 0.f;
		if (bDebugLogging)
			UE_LOG(LogCompanionAI, Log, TEXT("%s: STAND-UP-FIRE start side=%s slot=%s"),
				*Ctx.Companion->GetName(), ResolvedPeekSide == EPeekSide::Right ? TEXT("Right") : TEXT("Left"), *Slot->GetName());
		return;
	}

	// --- STAND-UP FIRE (mid-burst, cover still tracked) ---
	if (bIsFiringBurst && bHasCover)
	{
		LosBlockedAccum = 0.f;
		BurstTimer -= DeltaSeconds;

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

		const FRotator LookAtRot = (TargetLocation - MyLocation).Rotation();
		Ctx.Companion->SetActorRotation(FMath::RInterpTo(Ctx.Companion->GetActorRotation(),
			FRotator(0.f, LookAtRot.Yaw, 0.f), DeltaSeconds, Ctx.Companion->RotationInterpSpeed));

		if (Ctx.Companion->NeedsReload() && !Ctx.Companion->IsReloading())
		{
			if (bDebugLogging)
				UE_LOG(LogCompanionAI, Log, TEXT("%s: FIRE STOP reason=reload"), *Ctx.Companion->GetName());
			Ctx.Companion->StopWeaponFire();
			Ctx.Companion->ReloadWeapon();
			bIsFiringBurst = false;
			const ECoverPeekSide Pref = Slot->PeekPreference;
			ResolvedPeekSide = (Pref == ECoverPeekSide::Left) ? EPeekSide::Left : EPeekSide::Right;
			LastPeekResolveCoverLoc = FVector::ZeroVector;
			LastPeekResolveTargetLoc = FVector::ZeroVector;
			if (AAIController* AIC = Cast<AAIController>(Ctx.Companion->GetController()))
				AIC->StopMovement();
			if (UCharacterMovementComponent* CMC = Ctx.Companion->GetCharacterMovement())
				CMC->StopMovementImmediately();
			if (Slot)
			{
				const FRotator SlotYawRot(0.f, Slot->GetActorRotation().Yaw, 0.f);
				Ctx.Companion->SetActorRotation(SlotYawRot);
				const FVector SlotLoc = Slot->GetActorLocation();
				Ctx.Companion->SetActorLocation(FVector(SlotLoc.X, SlotLoc.Y, Ctx.Companion->GetActorLocation().Z));
			}
			if (Anim) Anim->EnterCoverPose(ResolvedPeekSide);
			PeekCooldown = FMath::RandRange(MinPeekCooldown, MaxPeekCooldown);
			TimeInCoverIdle = 0.f;
			return;
		}

		if (BurstTimer <= 0.f)
		{
			if (bDebugLogging)
				UE_LOG(LogCompanionAI, Log, TEXT("%s: FIRE STOP reason=burst-end-cover"), *Ctx.Companion->GetName());
			Ctx.Companion->StopWeaponFire();
			bIsFiringBurst = false;
			const ECoverPeekSide Pref = Slot->PeekPreference;
			ResolvedPeekSide = (Pref == ECoverPeekSide::Left) ? EPeekSide::Left : EPeekSide::Right;
			LastPeekResolveCoverLoc = FVector::ZeroVector;
			LastPeekResolveTargetLoc = FVector::ZeroVector;
			if (AAIController* AIC = Cast<AAIController>(Ctx.Companion->GetController()))
				AIC->StopMovement();
			if (UCharacterMovementComponent* CMC = Ctx.Companion->GetCharacterMovement())
				CMC->StopMovementImmediately();
			if (Slot)
			{
				const FRotator SlotYawRot(0.f, Slot->GetActorRotation().Yaw, 0.f);
				Ctx.Companion->SetActorRotation(SlotYawRot);
				const FVector SlotLoc = Slot->GetActorLocation();
				Ctx.Companion->SetActorLocation(FVector(SlotLoc.X, SlotLoc.Y, Ctx.Companion->GetActorLocation().Z));
			}
			if (Anim) Anim->EnterCoverPose(ResolvedPeekSide);
			PeekCooldown = FMath::RandRange(MinPeekCooldown, MaxPeekCooldown);
			TimeInCoverIdle = 0.f;
			if (bDebugLogging)
				UE_LOG(LogCompanionAI, Log, TEXT("%s: STAND-UP-FIRE end -> cover idle (next cooldown=%.2fs)"),
					*Ctx.Companion->GetName(), PeekCooldown);
		}
		return;
	}

	// --- ENGAGE FROM OPEN (no cover available) ---
	const bool bWasInCover = (PrevBranch == 0 || PrevBranch == 1);
	if (bIsFiringBurst && bWasInCover)
	{
		if (bDebugLogging)
			UE_LOG(LogCompanionAI, Log, TEXT("%s: FIRE STOP reason=cover-flipped-mid-burst"), *Ctx.Companion->GetName());
		Ctx.Companion->StopWeaponFire();
		bIsFiringBurst = false;
		BurstTimer = 0.f;
	}

	TimeAtCurrentCover = 0.f;
	CoverValidityCheckTimer = 0.f;

	if (Anim && Anim->IsInCover() && !bHasCover)
		Anim->ExitCoverPose();

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
			const FRotator LookAtRot = (TargetLocation - MyLocation).Rotation();
			Ctx.Companion->SetActorRotation(FMath::RInterpTo(Ctx.Companion->GetActorRotation(),
				FRotator(0.f, LookAtRot.Yaw, 0.f), DeltaSeconds, Ctx.Companion->RotationInterpSpeed));
		}
		if (LosBlockedAbandonSeconds > 0.f && LosBlockedAccum >= LosBlockedAbandonSeconds)
		{
			if (bDebugLogging)
				UE_LOG(LogCompanionAI, Log, TEXT("%s: FIRE STOP reason=los-block-abandon"), *Ctx.Companion->GetName());
			Ctx.Companion->StopWeaponFire();
			return FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
		}
		return;
	}

	LosBlockedAccum = 0.f;
	Ctx.Companion->SetAimTarget(Ctx.Target);
	const FRotator LookAtRot = (TargetLocation - MyLocation).Rotation();
	Ctx.Companion->SetActorRotation(FMath::RInterpTo(Ctx.Companion->GetActorRotation(),
		FRotator(0.f, LookAtRot.Yaw, 0.f), DeltaSeconds, Ctx.Companion->RotationInterpSpeed));

	if (Ctx.Companion->NeedsReload() && !Ctx.Companion->IsReloading())
	{
		if (bDebugLogging)
			UE_LOG(LogCompanionAI, Log, TEXT("%s: FIRE STOP reason=reload"), *Ctx.Companion->GetName());
		Ctx.Companion->StopWeaponFire();
		Ctx.Companion->ReloadWeapon();
		bIsFiringBurst = false;
		return;
	}
	if (Ctx.Companion->IsReloading()) return;

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
	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();

	if (Companion)
	{
		if (bDebugLogging)
		{
			const TCHAR* ResultStr = (TaskResult == EBTNodeResult::Succeeded) ? TEXT("Succeeded")
				: (TaskResult == EBTNodeResult::Failed) ? TEXT("Failed") : TEXT("Aborted");
			if (!BB)
			{
				UE_LOG(LogCompanionAI, Log, TEXT("%s: TASK EXIT result=%s | BB unavailable"), *Companion->GetName(), ResultStr);
			}
			else
			{
				AActor* BBTarget = Cast<AActor>(BB->GetValueAsObject(CombatTargetKey.SelectedKeyName));
				const bool bExitHasCover = BB->GetValueAsBool(HasCoverPositionKey.SelectedKeyName);
				auto PostureStr = [](ECompanionPosture P) -> const TCHAR*
				{
					switch (P)
					{
					case ECompanionPosture::Combat:  return TEXT("Combat");
					case ECompanionPosture::Stealth: return TEXT("Stealth");
					default:                         return TEXT("Exploration");
					}
				};
				UE_LOG(LogCompanionAI, Log, TEXT("%s: TASK EXIT result=%s | BB combatTarget=%s hasCover=%d posture=%s"),
					*Companion->GetName(), ResultStr, *GetNameSafe(BBTarget), (int32)bExitHasCover,
					PostureStr(Companion->GetPosture()));
			}
		}

		Companion->StopWeaponFire();
		Companion->SetAimTarget(nullptr);

		if (UCompanionAnimInstance* Anim = GetCompanionAnim(Companion))
			Anim->ExitCoverPose();
	}

	// Release cover slot claim on any non-success exit (success means MoveToCover loops back and re-validates the existing claim).
	if (BB && TaskResult != EBTNodeResult::Succeeded)
	{
		AAICoverSlot* Slot = Cast<AAICoverSlot>(BB->GetValueAsObject(CoverSlotKey.SelectedKeyName));
		if (IsValid(Slot))
		{
			Slot->Release(Companion);
			BB->SetValueAsObject(CoverSlotKey.SelectedKeyName, nullptr);
			BB->SetValueAsBool(HasCoverPositionKey.SelectedKeyName, false);
		}
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
