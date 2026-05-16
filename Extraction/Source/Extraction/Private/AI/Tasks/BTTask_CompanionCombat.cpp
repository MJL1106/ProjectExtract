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
#include "Components/SkeletalMeshComponent.h"

namespace
{
	struct FCombatContext
	{
		ACompanionCharacter* Companion = nullptr;
		AActor* Target = nullptr;
		UBlackboardComponent* Blackboard = nullptr;
	};

	static bool HasLineOfSight(UWorld* World, const FVector& FromLoc, AActor* ToTarget, ACompanionCharacter* Companion, AActor*& OutBlockedBy, const TArray<AActor*>& IgnoredAttached)
	{
		OutBlockedBy = nullptr;
		if (!World || !IsValid(ToTarget) || !IsValid(Companion)) return false;

		FHitResult Hit;
		FCollisionQueryParams QueryParams;
		QueryParams.AddIgnoredActor(Companion);
		QueryParams.AddIgnoredActor(Companion->GetCurrentWeapon());
		QueryParams.AddIgnoredActors(IgnoredAttached);

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

// --- Static helpers ---

UBTTask_CompanionCombat::EPeekAction UBTTask_CompanionCombat::RollPeekActionMulti(TArrayView<const TPair<EPeekAction, float>> Weighted)
{
	float Total = 0.f;
	for (const TPair<EPeekAction, float>& Entry : Weighted)
	{
		if (Entry.Value > 0.f) Total += Entry.Value;
	}
	if (Total <= 0.f) return EPeekAction::Hold;

	float Roll = FMath::FRand() * Total;
	for (const TPair<EPeekAction, float>& Entry : Weighted)
	{
		if (Entry.Value <= 0.f) continue;
		Roll -= Entry.Value;
		if (Roll <= 0.f) return Entry.Key;
	}
	return EPeekAction::Hold;
}

void UBTTask_CompanionCombat::ReturnToCover(ACompanionCharacter* Companion, UCompanionAnimInstance* Anim,
	AAICoverSlot* Slot, bool bSuppressed, bool bLowHealth)
{
	if (IsValid(Companion)) Companion->StopWeaponFire();

	bIsFiringBurst = false;

	if (IsValid(Slot))
	{
		const ECoverPeekSide Pref = Slot->PeekPreference;
		ResolvedPeekSide = (Pref == ECoverPeekSide::Left) ? EPeekSide::Left : EPeekSide::Right;
		LastPeekResolveCoverLoc = FVector::ZeroVector;
		LastPeekResolveTargetLoc = FVector::ZeroVector;

		if (AAIController* AIC = IsValid(Companion) ? Cast<AAIController>(Companion->GetController()) : nullptr)
			AIC->StopMovement();
		if (UCharacterMovementComponent* CMC = IsValid(Companion) ? Companion->GetCharacterMovement() : nullptr)
			CMC->StopMovementImmediately();

		if (IsValid(Companion))
		{
			const FVector SubSlotLoc = Slot->GetSubSlotLocation(CurrentSubSlotIndex);
			const FRotator SlotYawRot(0.f, Slot->GetActorRotation().Yaw, 0.f);
			const FVector SnapLoc(SubSlotLoc.X, SubSlotLoc.Y, Companion->GetActorLocation().Z);
			Companion->TeleportTo(SnapLoc, SlotYawRot, false, false);

			if (Slot->Height == ECoverHeight::Crouch)
				Companion->Crouch();
			else
				Companion->UnCrouch();
		}
	}

	if (UAnimMontage* M = ActivePeekMontage.Get())
	{
		if (Anim) Anim->Montage_Stop(0.15f, M);
	}
	ActivePeekMontage.Reset();

	if (Anim) Anim->EnterCoverPose(ResolvedPeekSide);

	float CooldownMult = 1.f;
	if (bSuppressed) CooldownMult *= SuppressionCooldownMultiplier;
	if (bLowHealth)  CooldownMult *= LowHealthCooldownMultiplier;
	PeekCooldown = FMath::RandRange(MinPeekCooldown, MaxPeekCooldown) * CooldownMult;
	TimeInCoverIdle = 0.f;
}

void UBTTask_CompanionCombat::TickRepositionAction(ACompanionCharacter* Companion, UCompanionAnimInstance* Anim,
	AAICoverSlot* Slot, bool bSuppressed, bool bLowHp, float DeltaSeconds)
{
	if (!IsValid(Companion) || !IsValid(Slot)) return;

	if (bSuppressed)
	{
		// Abort: snap to whichever sub-slot is closer.
		const FVector Current = Companion->GetActorLocation();
		const FVector TargetLoc = Slot->GetSubSlotLocation(RepositionTargetSubSlotIndex);
		const bool bCloserToTarget = FVector::DistSquared(Current, TargetLoc)
			< FVector::DistSquared(Current, Slot->GetSubSlotLocation(CurrentSubSlotIndex));
		if (bCloserToTarget) CurrentSubSlotIndex = RepositionTargetSubSlotIndex;
		RepositionTargetSubSlotIndex = INDEX_NONE;
		CurrentBurstAction = EPeekAction::Hold;
		ReturnToCover(Companion, Anim, Slot, true, bLowHp);
		return;
	}

	const FVector Current = Companion->GetActorLocation();
	const FVector RawTarget = Slot->GetSubSlotLocation(RepositionTargetSubSlotIndex);
	const FVector Target(RawTarget.X, RawTarget.Y, Current.Z);

	const FVector Next = FMath::VInterpConstantTo(Current, Target, DeltaSeconds, RepositionWalkSpeed);
	Companion->TeleportTo(Next, Companion->GetActorRotation(), false, false);

	if (FVector::Dist(Next, Target) <= RepositionArrivalTolerance)
	{
		CurrentSubSlotIndex = RepositionTargetSubSlotIndex;
		RepositionTargetSubSlotIndex = INDEX_NONE;
		CurrentBurstAction = EPeekAction::Hold;
		PeekCooldown = FMath::RandRange(MinPeekCooldown, MaxPeekCooldown);
		TimeInCoverIdle = 0.f;
		if (Anim) Anim->EnterCoverPose(ResolvedPeekSide);
	}
}

void UBTTask_CompanionCombat::TickStandUpAndRepositionAction(ACompanionCharacter* Companion, UCompanionAnimInstance* Anim,
	AAICoverSlot* Slot, bool bSuppressed, bool bLowHp, float DeltaSeconds)
{
	if (!IsValid(Companion) || !IsValid(Slot)) return;

	if (bSuppressed)
	{
		ReturnToCover(Companion, Anim, Slot, true, bLowHp);
		bRepositionStandPhase = false;
		RepositionTargetSubSlotIndex = INDEX_NONE;
		CurrentBurstAction = EPeekAction::Hold;
		return;
	}

	const FVector Current = Companion->GetActorLocation();
	const FVector RawTarget = Slot->GetSubSlotLocation(RepositionTargetSubSlotIndex);
	const FVector Target(RawTarget.X, RawTarget.Y, Current.Z);
	const FVector Next = FMath::VInterpConstantTo(Current, Target, DeltaSeconds, RepositionWalkSpeed);
	Companion->TeleportTo(Next, Companion->GetActorRotation(), false, false);

	if (bRepositionStandPhase)
	{
		BurstTimer -= DeltaSeconds;
		if (BurstTimer <= 0.f)
		{
			// Fire phase ended — stop shooting, keep walking silently.
			Companion->StopWeaponFire();
			bRepositionStandPhase = false;
		}
	}

	if (FVector::Dist(Next, Target) <= RepositionArrivalTolerance)
	{
		CurrentSubSlotIndex = RepositionTargetSubSlotIndex;
		RepositionTargetSubSlotIndex = INDEX_NONE;
		bRepositionStandPhase = false;
		Companion->Crouch();
		ReturnToCover(Companion, Anim, Slot, false, bLowHp);
	}
}

void UBTTask_CompanionCombat::TickCornerPeekAction(ACompanionCharacter* Companion, UCompanionAnimInstance* Anim,
	AAICoverSlot* Slot, AActor* Target, bool bSuppressed, bool bLowHp,
	const TArray<AActor*>& IgnoredAttached, float DeltaSeconds)
{
	if (!IsValid(Companion) || !IsValid(Slot) || !IsValid(Target)) return;

	if (bSuppressed && !bCornerPeekReturning)
	{
		bCornerPeekReturning = true;
		if (bCornerPeekFiring)
		{
			Companion->StopWeaponFire();
			bCornerPeekFiring = false;
		}
	}

	const FVector Current = Companion->GetActorLocation();
	UWorld* World = Companion->GetWorld();
	AActor* Blocker = nullptr;

	CornerPeekLosCheckTimer -= DeltaSeconds;
	const bool bRunLosThisTick = CornerPeekLosCheckTimer <= 0.f;
	if (bRunLosThisTick) CornerPeekLosCheckTimer = 0.1f;

	if (!bCornerPeekReturning)
	{
		BurstTimer -= DeltaSeconds;
		const FVector ApexTarget(CornerPeekApexLocation.X, CornerPeekApexLocation.Y, Current.Z);
		const FVector Next = FMath::VInterpConstantTo(Current, ApexTarget, DeltaSeconds, RepositionWalkSpeed);
		Companion->TeleportTo(Next, Companion->GetActorRotation(), false, false);

		if (bRunLosThisTick)
		{
			const FVector Eye = Next + FVector(0.f, 0.f, StandFireEyeHeight);
			const bool bLos = HasLineOfSight(World, Eye, Target, Companion, Blocker, IgnoredAttached);
			if (bLos && !bCornerPeekFiring)
			{
				Companion->StartWeaponFire();
				bCornerPeekFiring = true;
			}
		}

		const bool bAtApex = FVector::Dist(Next, ApexTarget) <= RepositionArrivalTolerance;
		if (bAtApex || BurstTimer <= 0.f)
			bCornerPeekReturning = true;
	}
	else
	{
		const FVector HomeTarget(CornerPeekHomeLocation.X, CornerPeekHomeLocation.Y, Current.Z);
		const FVector Next = FMath::VInterpConstantTo(Current, HomeTarget, DeltaSeconds, RepositionWalkSpeed);
		Companion->TeleportTo(Next, Companion->GetActorRotation(), false, false);

		if (bRunLosThisTick)
		{
			const FVector Eye = Next + FVector(0.f, 0.f, StandFireEyeHeight);
			const bool bLos = HasLineOfSight(World, Eye, Target, Companion, Blocker, IgnoredAttached);
			if (!bLos && bCornerPeekFiring)
			{
				Companion->StopWeaponFire();
				bCornerPeekFiring = false;
			}
		}

		if (FVector::Dist(Next, HomeTarget) <= RepositionArrivalTolerance)
		{
			if (bCornerPeekFiring)
			{
				Companion->StopWeaponFire();
				bCornerPeekFiring = false;
			}
			bIsFiringBurst = false;
			bCornerPeekReturning = false;
			ReturnToCover(Companion, Anim, Slot, false, bLowHp);
		}
	}
}

int32 UBTTask_CompanionCombat::PickBestSubSlotByLos(AAICoverSlot* Slot, AActor* Target, ACompanionCharacter* Companion, int32 ExcludeIndex, const TArray<AActor*>& IgnoredAttached) const
{
	if (!IsValid(Slot) || !IsValid(Target) || !IsValid(Companion)) return INDEX_NONE;

	UWorld* World = Companion->GetWorld();
	const int32 Count = Slot->GetSubSlotCount();
	for (int32 i = 0; i < Count; ++i)
	{
		if (i == ExcludeIndex) continue;
		const FVector Eye = Slot->GetSubSlotLocation(i) + FVector(0.f, 0.f, StandFireEyeHeight);
		AActor* Blocker = nullptr;
		if (HasLineOfSight(World, Eye, Target, Companion, Blocker, IgnoredAttached))
			return i;
	}
	return INDEX_NONE;
}

// --- Constructor ---

UBTTask_CompanionCombat::UBTTask_CompanionCombat()
{
	NodeName = TEXT("Companion Combat");
	bNotifyTick = true;
	bNotifyTaskFinished = true;
	bCreateNodeInstance = true;
}

// --- ExecuteTask ---

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
	ConsecutiveHolds = 0;
	CurrentBurstAction = EPeekAction::Stand;
	ActivePeekMontage.Reset();
	DebugBurstLosCheckTimer = 0.f;
	CurrentSubSlotIndex = 0;
	SubSlotLosRecheckTimer = 0.f;
	LastTickSlot = nullptr;
	BlockedRecheckHits = 0;
	SubSlotLosRecheckInterval = FMath::Max(0.1f, SubSlotLosRecheckInterval);
	RepositionTargetSubSlotIndex = INDEX_NONE;
	bRepositionStandPhase = false;
	CornerPeekHomeLocation = FVector::ZeroVector;
	CornerPeekApexLocation = FVector::ZeroVector;
	bCornerPeekReturning = false;
	bCornerPeekFiring = false;
	bWaitingForFinalApproach = false;
	FinalApproachTarget = FVector::ZeroVector;
	FinalApproachElapsed = 0.f;

	if (!Companion->GetCurrentWeapon())
		UE_LOG(LogCompanionAI, Warning, TEXT("%s: combat task started but CurrentWeapon is null"), *Companion->GetName());

	AAICoverSlot* Slot = Cast<AAICoverSlot>(BB->GetValueAsObject(CoverSlotKey.SelectedKeyName));
	const bool bHasCover = IsValid(Slot) && BB->GetValueAsBool(HasCoverPositionKey.SelectedKeyName);

	if (bHasCover)
	{
		const ECoverPeekSide Pref = Slot->PeekPreference;
		ResolvedPeekSide = (Pref == ECoverPeekSide::Left) ? EPeekSide::Left : EPeekSide::Right;

		// Pick the nearest sub-slot to arrival position.
		const int32 NumSubSlots = Slot->GetSubSlotCount();
		const FVector ArrivalLoc = Companion->GetActorLocation();
		float BestDistSq = FLT_MAX;
		for (int32 i = 0; i < NumSubSlots; ++i)
		{
			const float DistSq = FVector::DistSquared(ArrivalLoc, Slot->GetSubSlotLocation(i));
			if (DistSq < BestDistSq)
			{
				BestDistSq = DistSq;
				CurrentSubSlotIndex = i;
			}
		}

		const FVector SubSlotLoc = Slot->GetSubSlotLocation(CurrentSubSlotIndex);
		const float DistToSubSlot = FVector::Dist(ArrivalLoc, SubSlotLoc);

		if (DistToSubSlot <= FinalApproachAcceptRadius)
		{
			// Already at the slot — snap and enter cover immediately.
			if (AAIController* AIC = Cast<AAIController>(Companion->GetController()))
				AIC->StopMovement();
			if (UCharacterMovementComponent* CMC = Companion->GetCharacterMovement())
				CMC->StopMovementImmediately();

			const FRotator SlotYawRot(0.f, Slot->GetActorRotation().Yaw, 0.f);
			const FVector SnapLoc(SubSlotLoc.X, SubSlotLoc.Y, ArrivalLoc.Z);
			Companion->TeleportTo(SnapLoc, SlotYawRot, false, false);

			if (UCompanionAnimInstance* Anim = GetCompanionAnim(Companion))
				Anim->EnterCoverPose(ResolvedPeekSide);
		}
		else
		{
			// Walk the last bit physically; TickTask commits to cover on arrival.
			bWaitingForFinalApproach = true;
			FinalApproachTarget = SubSlotLoc;
			FinalApproachElapsed = 0.f;

			if (AAIController* AIC = Cast<AAIController>(Companion->GetController()))
				AIC->MoveToLocation(SubSlotLoc, FinalApproachAcceptRadius, false, true, false, true);
		}
	}

	if (bDebugLogging)
	{
		const float Distance = FVector::Dist(Companion->GetActorLocation(), Target->GetActorLocation());
		UE_LOG(LogCompanionAI, Log, TEXT("%s: TASK ENTER target=%s dist=%.0f hasCover=%d slot=%s"),
			*Companion->GetName(), *Target->GetName(), Distance, (int32)bHasCover, *GetNameSafe(Slot));
	}

	return EBTNodeResult::InProgress;
}

// --- TickTask ---

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

	if (bWaitingForFinalApproach)
	{
		FinalApproachElapsed += DeltaSeconds;
		const float Dist = FVector::Dist(Ctx.Companion->GetActorLocation(), FinalApproachTarget);
		const bool bArrived = Dist <= FinalApproachAcceptRadius;
		const bool bTimedOut = FinalApproachElapsed >= FinalApproachTimeout;

		if (!bArrived && !bTimedOut)
			return;

		if (AAIController* AIC = Cast<AAIController>(Ctx.Companion->GetController()))
			AIC->StopMovement();
		if (UCharacterMovementComponent* CMC = Ctx.Companion->GetCharacterMovement())
			CMC->StopMovementImmediately();

		AAICoverSlot* ApproachSlot = Cast<AAICoverSlot>(Ctx.Blackboard->GetValueAsObject(CoverSlotKey.SelectedKeyName));
		if (IsValid(ApproachSlot))
		{
			const FRotator SlotYawRot(0.f, ApproachSlot->GetActorRotation().Yaw, 0.f);
			const FVector SnapLoc(FinalApproachTarget.X, FinalApproachTarget.Y, Ctx.Companion->GetActorLocation().Z);
			Ctx.Companion->TeleportTo(SnapLoc, SlotYawRot, false, false);
			if (UCompanionAnimInstance* Anim = GetCompanionAnim(Ctx.Companion))
				Anim->EnterCoverPose(ResolvedPeekSide);
		}

		bWaitingForFinalApproach = false;
		FinalApproachElapsed = 0.f;
		// Fall through to normal branch logic this tick.
	}

	const FVector MyLocation = Ctx.Companion->GetActorLocation();
	const FVector TargetLocation = Ctx.Target->GetActorLocation();
	const float Distance = FVector::Dist(MyLocation, TargetLocation);

	AAICoverSlot* Slot = Cast<AAICoverSlot>(Ctx.Blackboard->GetValueAsObject(CoverSlotKey.SelectedKeyName));
	const bool bHasCover = IsValid(Slot) && Ctx.Blackboard->GetValueAsBool(HasCoverPositionKey.SelectedKeyName);

	// Build ignored-actors list once per tick — passed to all HasLineOfSight calls below.
	TArray<AActor*> TickIgnoredAttached;
	Ctx.Companion->GetAttachedActors(TickIgnoredAttached);

	// Detect slot swap without ExecuteTask re-running; reset sub-slot index to stay in bounds.
	if (IsValid(Slot) && Slot != LastTickSlot.Get())
	{
		CurrentSubSlotIndex = 0;
		BlockedRecheckHits = 0;
	}
	LastTickSlot = Slot;

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

	const bool bSuppressed = Ctx.Companion->IsSuppressed(SuppressionWindowSeconds);
	const bool bLowHp = Ctx.Companion->GetHealthFraction() < LowHealthFraction;

	// =========================================================================
	// BRANCH 0: COVER IDLE
	// =========================================================================
	if (bHasCover && !bIsFiringBurst)
	{
		LosBlockedAccum = 0.f;
		TimeAtCurrentCover += DeltaSeconds;

		// Idempotent posture sync — applies on first CoverIdle tick after entry, no-op afterward.
		const bool bShouldCrouch = (Slot->Height == ECoverHeight::Crouch);
		if (bShouldCrouch && !Ctx.Companion->bIsCrouched) Ctx.Companion->Crouch();
		else if (!bShouldCrouch && Ctx.Companion->bIsCrouched) Ctx.Companion->UnCrouch();

		CurrentSubSlotIndex = FMath::Clamp(CurrentSubSlotIndex, 0, Slot->GetSubSlotCount() - 1);
		const FVector SubSlotLoc = Slot->GetSubSlotLocation(CurrentSubSlotIndex);

		// Refresh peek side only when geometry has changed beyond threshold.
		const FVector CoverLoc = SubSlotLoc;
		const float CoverDeltaSq = FVector::DistSquared(CoverLoc, LastPeekResolveCoverLoc);
		const float TargetDeltaSq = FVector::DistSquared(TargetLocation, LastPeekResolveTargetLoc);
		if (CoverDeltaSq > PeekResolveDistThresholdSq || TargetDeltaSq > PeekResolveDistThresholdSq)
		{
			const ECoverPeekSide Pref = Slot->PeekPreference;
			if (Pref == ECoverPeekSide::Either)
			{
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

		// Periodic slot-validity check.
		CoverValidityCheckTimer += DeltaSeconds;
		if (CoverValidityCheckTimer >= CoverValidityCheckInterval && TimeAtCurrentCover >= MinCoverDwellBeforeReEval)
		{
			CoverValidityCheckTimer = 0.f;

			if (!Slot->IsClaimed())
			{
				UE_LOG(LogCompanionAI, Log, TEXT("%s: Cover INVALIDATE reason=slot-released slot=%s"), *Ctx.Companion->GetName(), *Slot->GetName());
				if (Slot->IsClaimedBy(Ctx.Companion)) Slot->Release(Ctx.Companion);
				Ctx.Blackboard->SetValueAsObject(CoverSlotKey.SelectedKeyName, nullptr);
				Ctx.Blackboard->SetValueAsBool(HasCoverPositionKey.SelectedKeyName, false);
				return FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
			}

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

		// Gate 1: suppression.
		if (bSuppressed)
		{
			float CooldownMult = SuppressionCooldownMultiplier;
			if (bLowHp) CooldownMult *= LowHealthCooldownMultiplier;
			PeekCooldown = FMath::RandRange(MinPeekCooldown, MaxPeekCooldown) * CooldownMult;
			TimeInCoverIdle = 0.f;
			if (bDebugLogging) UE_LOG(LogCompanionAI, Log, TEXT("%s: STAY DOWN reason=suppressed"), *Ctx.Companion->GetName());
			return;
		}

		// Gate 2: LoS from stand eye.
		AActor* BlockedBy = nullptr;
		const FVector StandEye = SubSlotLoc + FVector(0.f, 0.f, StandFireEyeHeight);
		const bool bLosFromCover = HasLineOfSight(Ctx.Companion->GetWorld(), StandEye, Ctx.Target, Ctx.Companion, BlockedBy, TickIgnoredAttached);
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
		if (!bLosFromCover)
		{
			// Skip re-pick while a movement action owns its own positioning.
			if (CurrentBurstAction == EPeekAction::Reposition
				|| CurrentBurstAction == EPeekAction::StandUpAndReposition
				|| CurrentBurstAction == EPeekAction::CornerPeek)
				return;

			// Throttled sub-slot re-pick: only when unsuppressed and multiple sub-slots exist.
			SubSlotLosRecheckTimer -= DeltaSeconds;
			if (SubSlotLosRecheckTimer <= 0.f && !bSuppressed && Slot->GetSubSlotCount() > 1)
			{
				SubSlotLosRecheckTimer = SubSlotLosRecheckInterval;
				BlockedRecheckHits = FMath::Min<uint8>(BlockedRecheckHits + 1, 255);

				// Require 2 consecutive gated blocked checks before teleporting (anti-thrash).
				if (BlockedRecheckHits >= 2)
				{
					const int32 BestIndex = PickBestSubSlotByLos(Slot, Ctx.Target, Ctx.Companion, CurrentSubSlotIndex, TickIgnoredAttached);
					if (BestIndex != INDEX_NONE)
					{
						CurrentSubSlotIndex = BestIndex;
						const FVector NewSubSlotLoc = Slot->GetSubSlotLocation(CurrentSubSlotIndex);
						const FRotator SlotYawRot(0.f, Slot->GetActorRotation().Yaw, 0.f);
						Ctx.Companion->TeleportTo(FVector(NewSubSlotLoc.X, NewSubSlotLoc.Y, Ctx.Companion->GetActorLocation().Z), SlotYawRot, false, false);
						LastPeekResolveCoverLoc = FVector::ZeroVector;
						LastPeekResolveTargetLoc = FVector::ZeroVector;
						BlockedRecheckHits = 0;
						if (Anim) Anim->EnterCoverPose(ResolvedPeekSide);
						return;
					}
				}
			}
			return;
		}
		BlockedRecheckHits = 0;

		// Gate 3: reload.
		if (Ctx.Companion->NeedsReload() && !Ctx.Companion->IsReloading())
		{
			Ctx.Companion->ReloadWeapon();
			if (bDebugLogging)
				UE_LOG(LogCompanionAI, Log, TEXT("%s: reloading before stand-up"), *Ctx.Companion->GetName());
			return;
		}
		if (Ctx.Companion->IsReloading()) return;

		// Handle ongoing silent Reposition action (doesn't enter burst — processed here).
		if (CurrentBurstAction == EPeekAction::Reposition && RepositionTargetSubSlotIndex != INDEX_NONE)
		{
			TickRepositionAction(Ctx.Companion, Anim, Slot, bSuppressed, bLowHp, DeltaSeconds);
			return;
		}

		// Handle StandUpAndReposition silent-walk phase (fire ended, walking to target).
		if (CurrentBurstAction == EPeekAction::StandUpAndReposition && !bRepositionStandPhase && RepositionTargetSubSlotIndex != INDEX_NONE)
		{
			TickStandUpAndRepositionAction(Ctx.Companion, Anim, Slot, bSuppressed, bLowHp, DeltaSeconds);
			return;
		}

		// Gate 4: slot supports standing.
		const bool bIsCrouchCover = (Slot->Height == ECoverHeight::Crouch);
		if (!bIsCrouchCover && !Slot->CanStandFireOver())
		{
			if (bDebugLogging)
				UE_LOG(LogCompanionAI, Log, TEXT("%s: Cover REJECT stand-fire blocked by slot height slot=%s"), *Ctx.Companion->GetName(), *Slot->GetName());
			return;
		}

		// Gate 5: roll peek action.
		const bool bAtPeekableEndpoint = Slot->IsSubSlotPeekableCorner(CurrentSubSlotIndex);
		const int32 SubSlotCount = Slot->GetSubSlotCount();

		EPeekAction Action = EPeekAction::Hold;
		if (bIsCrouchCover)
		{
			// Disable Reposition/StandUpAndReposition when only one sub-slot exists.
			const float RepoW        = (SubSlotCount > 1) ? (bLowHp ? LowHpRepositionWeight            : RepositionWeight)           : 0.f;
			const float StandUpRepoW = (SubSlotCount > 1) ? (bLowHp ? LowHpStandUpAndRepositionWeight  : StandUpAndRepositionWeight)  : 0.f;
			const TPair<EPeekAction, float> CrouchWeights[] = {
				{ EPeekAction::Stand,                bLowHp ? LowHpStandWeight  : StandWeight },
				{ EPeekAction::Quick,                bLowHp ? LowHpQuickWeight  : QuickWeight },
				{ EPeekAction::Hold,                 bLowHp ? LowHpHoldWeight   : HoldWeight  },
				{ EPeekAction::Reposition,           RepoW },
				{ EPeekAction::StandUpAndReposition, StandUpRepoW },
			};
			Action = RollPeekActionMulti(MakeArrayView(CrouchWeights));
		}
		else if (bAtPeekableEndpoint)
		{
			const TPair<EPeekAction, float> EndpointWeights[] = {
				{ EPeekAction::CornerPeek,  bLowHp ? LowHpCornerPeekWeight  : CornerPeekWeight },
				{ EPeekAction::Reposition,  (SubSlotCount > 1) ? (bLowHp ? LowHpRepositionWeight : RepositionWeight) : 0.f },
				{ EPeekAction::Hold,        bLowHp ? LowHpHoldWeight        : HoldWeight },
			};
			Action = RollPeekActionMulti(MakeArrayView(EndpointWeights));
		}
		else
		{
			// Stand cover, midpoint sub-slot — no fire option.
			const TPair<EPeekAction, float> MidpointWeights[] = {
				{ EPeekAction::Reposition,  (SubSlotCount > 1) ? (bLowHp ? LowHpRepositionWeight : RepositionWeight) : 0.f },
				{ EPeekAction::Hold,        bLowHp ? LowHpHoldWeight : HoldWeight },
			};
			Action = RollPeekActionMulti(MakeArrayView(MidpointWeights));
		}

		if (Action == EPeekAction::Hold)
		{
			if (ConsecutiveHolds < MaxConsecutiveHolds)
			{
				++ConsecutiveHolds;
				PeekCooldown = FMath::RandRange(MinPeekCooldown, MaxPeekCooldown);
				TimeInCoverIdle = 0.f;
				if (bDebugLogging) UE_LOG(LogCompanionAI, Log, TEXT("%s: HOLD this cycle (%d/%d)"), *Ctx.Companion->GetName(), ConsecutiveHolds, MaxConsecutiveHolds);
				return;
			}
			// Hold cap reached — promote to Stand (crouch) or Reposition (stand cover) if possible.
			Action = bIsCrouchCover ? EPeekAction::Stand : EPeekAction::Hold;
		}
		else
		{
			ConsecutiveHolds = 0;
		}

		// --- Commit to action ---

		if (Action == EPeekAction::Reposition || Action == EPeekAction::StandUpAndReposition)
		{
			// Pick adjacent sub-slot. Random direction, clamp to valid range.
			const int32 Direction = (FMath::RandBool() ? 1 : -1);
			int32 Candidate = CurrentSubSlotIndex + Direction;
			if (Candidate < 0) Candidate = CurrentSubSlotIndex + 1;
			if (Candidate >= SubSlotCount) Candidate = CurrentSubSlotIndex - 1;
			Candidate = FMath::Clamp(Candidate, 0, SubSlotCount - 1);

			if (Candidate == CurrentSubSlotIndex)
			{
				// Edge case: single sub-slot or endpoint with no valid neighbour — treat as Hold.
				CurrentBurstAction = EPeekAction::Hold;
				++ConsecutiveHolds;
				PeekCooldown = FMath::RandRange(MinPeekCooldown, MaxPeekCooldown);
				TimeInCoverIdle = 0.f;
				return;
			}
			RepositionTargetSubSlotIndex = Candidate;

			if (Action == EPeekAction::Reposition)
			{
				Ctx.Companion->StopWeaponFire();
				CurrentBurstAction = EPeekAction::Reposition;
				TimeInCoverIdle = 0.f;
				if (bDebugLogging)
					UE_LOG(LogCompanionAI, Log, TEXT("%s: REPOSITION sub-slot %d -> %d"), *Ctx.Companion->GetName(), CurrentSubSlotIndex, RepositionTargetSubSlotIndex);
				return;
			}

			// StandUpAndReposition: start fire burst while walking.
			CurrentBurstAction = EPeekAction::StandUpAndReposition;
			bRepositionStandPhase = true;
			BurstTimer = FMath::RandRange(MinFireBurst, MaxFireBurst);
			bIsFiringBurst = true;
			Ctx.Companion->UnCrouch();
			Ctx.Companion->StartWeaponFire();
			DebugBurstLosCheckTimer = 0.f;
			TimeInCoverIdle = 0.f;
			TimeAtCurrentCover = 0.f;
			CoverValidityCheckTimer = 0.f;
			if (bDebugLogging)
				UE_LOG(LogCompanionAI, Log, TEXT("%s: STANDUP-REPOSITION sub-slot %d -> %d burst=%.2fs"), *Ctx.Companion->GetName(), CurrentSubSlotIndex, RepositionTargetSubSlotIndex, BurstTimer);
			return;
		}

		if (Action == EPeekAction::CornerPeek)
		{
			const int32 LastIndex = SubSlotCount - 1;
			const FVector StrafeDir = (CurrentSubSlotIndex == 0)
				? -Slot->GetActorRightVector()
				: Slot->GetActorRightVector();
			CornerPeekHomeLocation = Slot->GetSubSlotLocation(CurrentSubSlotIndex);
			// CornerPeek apex captured at commit — assumes slot doesn't move mid-action.
			CornerPeekApexLocation = CornerPeekHomeLocation + StrafeDir * CornerPeekStepDistance;
			CurrentBurstAction = EPeekAction::CornerPeek;
			bCornerPeekReturning = false;
			bIsFiringBurst = true;
			BurstTimer = FMath::RandRange(MinFireBurst, MaxFireBurst);
			DebugBurstLosCheckTimer = 0.f;
			CornerPeekLosCheckTimer = 0.f;
			TimeInCoverIdle = 0.f;
			TimeAtCurrentCover = 0.f;
			CoverValidityCheckTimer = 0.f;
			if (bDebugLogging)
				UE_LOG(LogCompanionAI, Log, TEXT("%s: CORNER-PEEK sub-slot %d apex=(%.0f,%.0f,%.0f) burst=%.2fs"), *Ctx.Companion->GetName(), CurrentSubSlotIndex, CornerPeekApexLocation.X, CornerPeekApexLocation.Y, CornerPeekApexLocation.Z, BurstTimer);
			return;
		}

		// Stand or Quick — existing fire flow.
		CurrentBurstAction = Action;
		BurstTimer = (CurrentBurstAction == EPeekAction::Quick)
			? FMath::RandRange(MinQuickPeekBurst, MaxQuickPeekBurst)
			: FMath::RandRange(MinFireBurst, MaxFireBurst);

		if (Anim)
		{
			Anim->ExitCoverPose();
			UAnimMontage* PeekMontage = Anim->PlayPeekFire(ResolvedPeekSide);
			ActivePeekMontage = PeekMontage;
		}

		Ctx.Companion->StartWeaponFire();
		bIsFiringBurst = true;
		DebugBurstLosCheckTimer = 0.f;
		TimeInCoverIdle = 0.f;
		TimeAtCurrentCover = 0.f;
		CoverValidityCheckTimer = 0.f;

		if (bDebugLogging)
			UE_LOG(LogCompanionAI, Log, TEXT("%s: STAND-UP-FIRE start action=%s burst=%.2fs side=%s slot=%s"),
				*Ctx.Companion->GetName(),
				(CurrentBurstAction == EPeekAction::Quick) ? TEXT("Quick") : TEXT("Stand"),
				BurstTimer,
				ResolvedPeekSide == EPeekSide::Right ? TEXT("Right") : TEXT("Left"),
				*Slot->GetName());
		return;
	}

	// =========================================================================
	// BRANCH 1: STAND-UP FIRE BURST
	// =========================================================================
	if (bIsFiringBurst && bHasCover)
	{
		LosBlockedAccum = 0.f;
		BurstTimer -= DeltaSeconds;

		if (bDebugLogging)
		{
			DebugBurstLosCheckTimer -= DeltaSeconds;
			if (DebugBurstLosCheckTimer <= 0.f)
			{
				DebugBurstLosCheckTimer = 0.2f;
				AActor* BurstBlocker = nullptr;
				const bool bBurstLos = HasLineOfSight(Ctx.Companion->GetWorld(), MyLocation, Ctx.Target, Ctx.Companion, BurstBlocker, TickIgnoredAttached);
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
		}

		const FRotator LookAtRot = (TargetLocation - MyLocation).Rotation();
		Ctx.Companion->SetActorRotation(FMath::RInterpTo(Ctx.Companion->GetActorRotation(),
			FRotator(0.f, LookAtRot.Yaw, 0.f), DeltaSeconds, Ctx.Companion->RotationInterpSpeed));

		// Dispatch new multi-phase actions before the shared burst logic.
		if (CurrentBurstAction == EPeekAction::StandUpAndReposition)
		{
			TickStandUpAndRepositionAction(Ctx.Companion, Anim, Slot, bSuppressed, bLowHp, DeltaSeconds);
			return;
		}
		if (CurrentBurstAction == EPeekAction::CornerPeek)
		{
			TickCornerPeekAction(Ctx.Companion, Anim, Slot, Ctx.Target, bSuppressed, bLowHp, TickIgnoredAttached, DeltaSeconds);
			return;
		}

		// Suppression abort — duck back down immediately.
		if (bSuppressed)
		{
			if (bDebugLogging) UE_LOG(LogCompanionAI, Log, TEXT("%s: FIRE STOP reason=suppressed-mid-burst"), *Ctx.Companion->GetName());
			ReturnToCover(Ctx.Companion, Anim, Slot, true, bLowHp);
			return;
		}

		// Reload mid-burst.
		if (Ctx.Companion->NeedsReload() && !Ctx.Companion->IsReloading())
		{
			if (bDebugLogging) UE_LOG(LogCompanionAI, Log, TEXT("%s: FIRE STOP reason=reload"), *Ctx.Companion->GetName());
			Ctx.Companion->ReloadWeapon();
			ReturnToCover(Ctx.Companion, Anim, Slot, false, bLowHp);
			return;
		}

		// Burst elapsed — return to cover.
		if (BurstTimer <= 0.f)
		{
			if (bDebugLogging) UE_LOG(LogCompanionAI, Log, TEXT("%s: FIRE STOP reason=burst-end-cover (next cd=%.2fs)"), *Ctx.Companion->GetName(), PeekCooldown);
			ReturnToCover(Ctx.Companion, Anim, Slot, false, bLowHp);
		}
		return;
	}

	// =========================================================================
	// BRANCH 2: OPEN ENGAGE
	// =========================================================================
	const bool bWasInCover = (PrevBranch == 0 || PrevBranch == 1);
	if (bIsFiringBurst && bWasInCover)
	{
		if (bDebugLogging)
			UE_LOG(LogCompanionAI, Log, TEXT("%s: FIRE STOP reason=cover-flipped-mid-burst"), *Ctx.Companion->GetName());
		Ctx.Companion->StopWeaponFire();
		bIsFiringBurst = false;
		BurstTimer = 0.f;
		Ctx.Companion->UnCrouch();
		bCornerPeekFiring = false;
		bCornerPeekReturning = false;
		CornerPeekHomeLocation = FVector::ZeroVector;
		CornerPeekApexLocation = FVector::ZeroVector;
		CurrentBurstAction = EPeekAction::Hold;
		RepositionTargetSubSlotIndex = INDEX_NONE;
		bRepositionStandPhase = false;
	}

	// Stop any lingering peek montage.
	if (UAnimMontage* M = ActivePeekMontage.Get())
	{
		if (Anim) Anim->Montage_Stop(0.15f, M);
		ActivePeekMontage.Reset();
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
		BurstTimer = FMath::RandRange(MinFireBurst, MaxFireBurst);
	}

#if ENABLE_DRAW_DEBUG
	if (bDebugLogging && bIsFiringBurst)
		DrawDebugLine(Ctx.Companion->GetWorld(), MyLocation, TargetLocation, FColor::Green, false, 0.1f, 0, 1.5f);
#endif
}

// --- OnTaskFinished ---

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
		Companion->UnCrouch();

		UCompanionAnimInstance* Anim = GetCompanionAnim(Companion);
		if (UAnimMontage* M = ActivePeekMontage.Get())
		{
			if (Anim) Anim->Montage_Stop(0.15f, M);
		}
		ActivePeekMontage.Reset();

		if (Anim) Anim->ExitCoverPose();
	}

	// Release cover slot claim on any non-success exit.
	if (BB && TaskResult != EBTNodeResult::Succeeded)
	{
		AAICoverSlot* Slot = Cast<AAICoverSlot>(BB->GetValueAsObject(CoverSlotKey.SelectedKeyName));
		if (IsValid(Slot))
		{
			if (IsValid(Companion) && Slot->IsClaimedBy(Companion))
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
	ConsecutiveHolds = 0;
	CurrentSubSlotIndex = 0;
	SubSlotLosRecheckTimer = 0.f;
	LastTickSlot = nullptr;
	BlockedRecheckHits = 0;
	RepositionTargetSubSlotIndex = INDEX_NONE;
	bRepositionStandPhase = false;
	CornerPeekHomeLocation = FVector::ZeroVector;
	CornerPeekApexLocation = FVector::ZeroVector;
	bCornerPeekReturning = false;
	bCornerPeekFiring = false;
	bWaitingForFinalApproach = false;
	FinalApproachTarget = FVector::ZeroVector;
	FinalApproachElapsed = 0.f;
}

FString UBTTask_CompanionCombat::GetStaticDescription() const
{
	return FString::Printf(TEXT("Cover-aware Combat (burst: %.1f-%.1fs / quick: %.1f-%.1fs; peek cd %.1f-%.1fs)"),
		MinFireBurst, MaxFireBurst, MinQuickPeekBurst, MaxQuickPeekBurst, MinPeekCooldown, MaxPeekCooldown);
}
