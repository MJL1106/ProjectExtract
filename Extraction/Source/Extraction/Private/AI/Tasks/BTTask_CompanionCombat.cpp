// BT task — cover-aware companion combat. State machine drives EngageFromOpen, EngageFromCover, StandUpFire.

#include "BTTask_CompanionCombat.h"
#include "AI/CompanionDiag.h"
#include "WeaponDataAsset.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "AIController.h"
#include "CompanionAIController.h"
#include "CompanionTuningDataAsset.h"
#include "CompanionCharacter.h"
#include "Animation/CompanionAnimInstance.h"
#include "WeaponBase.h"
#include "HealthComponent.h"
#include "AICoverSlot.h"
#include "CoverRegistrySubsystem.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "NavigationSystem.h"
#include "Navigation/PathFollowingComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "TimerManager.h"

namespace
{
	struct FCombatContext
	{
		ACompanionCharacter* Companion = nullptr;
		AActor* Target = nullptr;
		UBlackboardComponent* Blackboard = nullptr;
	};

	static bool HasLineOfSight(UWorld* World, const FVector& FromLoc, AActor* ToTarget, ACompanionCharacter* Companion, AActor*& OutBlockedBy, TArrayView<AActor* const> IgnoredAttached)
	{
		OutBlockedBy = nullptr;
		if (!World || !IsValid(ToTarget) || !IsValid(Companion)) return false;

		FHitResult Hit;
		FCollisionQueryParams QueryParams;
		QueryParams.AddIgnoredActor(Companion);
		QueryParams.AddIgnoredActor(Companion->GetCurrentWeapon());
		for (AActor* const A : IgnoredAttached) QueryParams.AddIgnoredActor(A);

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

	// Returns the minimum ammo required before starting a peek action.
	static int32 ComputeMinBurstAmmoCost(ACompanionCharacter* Companion)
	{
		if (!IsValid(Companion)) return 1;
		AWeaponBase* W = Companion->GetCurrentWeapon();
		if (!IsValid(W)) return 1;
		const UWeaponDataAsset* Data = W->GetWeaponData();
		if (!Data) return 1;
		return FMath::Clamp(Data->BurstCount, 1, FMath::Max(1, Data->MagazineSize));
	}

	static void DeferCoverPosture(ACompanionCharacter* Companion, bool bShouldCrouch)
	{
		if (!IsValid(Companion)) return;
		UWorld* World = Companion->GetWorld();
		if (!World) return;
		TWeakObjectPtr<ACompanionCharacter> WeakC(Companion);
		World->GetTimerManager().SetTimerForNextTick([WeakC, bShouldCrouch]()
		{
			ACompanionCharacter* C = WeakC.Get();
			if (!IsValid(C)) return;
			if (bShouldCrouch && !C->bIsCrouched)
			{
				UE_LOG(LogCompanionDiag, Log, TEXT("%s: [CrouchCall] t=%.3f site=DeferCoverPosture action=Crouch"),
					*GetNameSafe(C), C->GetWorld() ? C->GetWorld()->GetTimeSeconds() : 0.f);
				C->Crouch();
			}
			else if (!bShouldCrouch && C->bIsCrouched)
			{
				UE_LOG(LogCompanionDiag, Log, TEXT("%s: [CrouchCall] t=%.3f site=DeferCoverPosture action=UnCrouch"),
					*GetNameSafe(C), C->GetWorld() ? C->GetWorld()->GetTimeSeconds() : 0.f);
				C->UnCrouch();
			}
		});
	}

	// Fallback leash distance (cm) when no tuning asset is assigned — mirrors SprintDistanceThreshold default.
	static constexpr float DefaultCombatPlayerLeash = 1000.f;

	// Fallback stop distance (cm) for the player-pull dead-band — mirrors AcceptableRadius default.
	static constexpr float DefaultPlayerPullStopDist = 250.f;

	// Projects a candidate onto the navmesh. Returns true + fills OutLoc on success.
	static bool ProjectToNav(UWorld* World, const FVector& Candidate, float Extent, FVector& OutLoc)
	{
		UNavigationSystemV1* NavSys = World ? UNavigationSystemV1::GetCurrent(World) : nullptr;
		if (!NavSys) return false;
		FNavLocation NavLoc;
		if (!NavSys->ProjectPointToNavigation(Candidate, NavLoc, FVector(Extent))) return false;
		OutLoc = NavLoc.Location;
		return true;
	}

	// Resolves the player pawn + keep-out radius (AcceptableRadius) from the companion's controller.
	// OutMinDist defaults to the existing DefaultPlayerPullStopDist fallback when tuning is absent.
	static APawn* ResolvePlayerKeepOut(ACompanionCharacter* Companion, float& OutMinDist)
	{
		OutMinDist = DefaultPlayerPullStopDist;
		ACompanionAIController* CompAIC = Companion ? Cast<ACompanionAIController>(Companion->GetController()) : nullptr;
		if (!CompAIC) return nullptr;
		if (const UCompanionTuningDataAsset* T = CompAIC->GetTuning()) OutMinDist = FMath::Min(T->AcceptableRadius, T->SprintDistanceThreshold * 0.5f);
		return CompAIC->GetPlayerCharacter();
	}

	// Pushes a horizontal point out to the player keep-out circle edge if inside MinDist (preserves Z).
	static FVector ClampOutsidePlayerCircle(const FVector& Point, const FVector& PlayerLoc, float MinDist)
	{
		FVector ToPoint = Point - PlayerLoc;
		ToPoint.Z = 0.f;
		const float Dist = ToPoint.Size();
		if (Dist >= MinDist) return Point;
		const FVector Dir = (Dist > KINDA_SMALL_NUMBER) ? (ToPoint / Dist) : FVector(1.f, 0.f, 0.f);
		const FVector Out = PlayerLoc + Dir * MinDist;
		return FVector(Out.X, Out.Y, Point.Z);
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
			const FVector SubSlotLoc = Slot->GetLocationAtAlpha(CurrentAlpha);
			const FRotator SlotYawRot(0.f, Slot->GetForwardDirection().Rotation().Yaw, 0.f);
			// Ground-snap: nav-mesh-arrival Z is biased above the floor; trace down to find the real floor.
			FVector SnapLoc(SubSlotLoc.X, SubSlotLoc.Y, Companion->GetActorLocation().Z);
			if (UWorld* SnapWorld = Companion->GetWorld())
			{
				const FVector TraceStart = SnapLoc + FVector(0.f, 0.f, 80.f);
				const FVector TraceEnd = SnapLoc - FVector(0.f, 0.f, 200.f);
				FHitResult GroundHit;
				FCollisionQueryParams Params;
				Params.AddIgnoredActor(Companion);
				if (SnapWorld->LineTraceSingleByChannel(GroundHit, TraceStart, TraceEnd, ECC_Visibility, Params))
				{
					const float CapsuleHalfHeight = Companion->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
					SnapLoc.Z = GroundHit.ImpactPoint.Z + CapsuleHalfHeight;
				}
			}
			BeginSmoothSnap(Companion, SnapLoc, SlotYawRot, Slot->Height == ECoverHeight::Crouch, TEXT("ReturnToCover"));
		}
	}

	if (UAnimMontage* M = ActivePeekMontage.Get())
	{
		if (Anim) Anim->Montage_Stop(0.15f, M);
	}
	ActivePeekMontage.Reset();

	if (Anim) Anim->EnterCoverPose(ResolvedPeekSide, IsValid(Slot) ? Slot->Height : ECoverHeight::Crouch);

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
		// Abort: snap to whichever alpha position is closer.
		const FVector Current = Companion->GetActorLocation();
		const FVector CurrentLoc = Slot->GetLocationAtAlpha(CurrentAlpha);
		const bool bCloserToTarget = FVector::DistSquared(Current, RepositionTargetWorldLoc)
			< FVector::DistSquared(Current, CurrentLoc);
		if (bCloserToTarget) CurrentAlpha = *RepositionTargetAlpha;
		if (bDebugLogging && bRepositionStartLogged)
			UE_LOG(LogCompanionDiag, Log, TEXT("%s: REPOSITION-DONE result=aborted dist=%.0f elapsed=%.2f"),
				*Companion->GetName(), FVector::Dist(Current, RepositionTargetWorldLoc), RepositionElapsed);
		Companion->SetLowReadyAim(false);
		Companion->SetAimTarget(nullptr);
		RepositionTargetAlpha.Reset();
		RepositionStalledTime = 0.f;
		bRepositionStartLogged = false;
		CurrentBurstAction = EPeekAction::Hold;
		bJustRepositioned = true;
		if (UCompanionAnimInstance* CAI = Cast<UCompanionAnimInstance>(
			Companion->GetMesh() ? Companion->GetMesh()->GetAnimInstance() : nullptr))
			CAI->ClearCoverStrafeVelocity();
		ReturnToCover(Companion, Anim, Slot, true, bLowHp);
		return;
	}

	RepositionElapsed += DeltaSeconds;
	const FVector Current = Companion->GetActorLocation();
	const FVector Target(RepositionTargetWorldLoc.X, RepositionTargetWorldLoc.Y, Current.Z);

	const FVector Next = FMath::VInterpConstantTo(Current, Target, DeltaSeconds, RepositionWalkSpeed);
	Companion->SetActorLocation(Next, false, nullptr, ETeleportType::TeleportPhysics);

	if (UCompanionAnimInstance* CAI = Cast<UCompanionAnimInstance>(
		Companion->GetMesh() ? Companion->GetMesh()->GetAnimInstance() : nullptr))
	{
		const FVector MoveDelta = Next - Current;
		CAI->SetCoverStrafeVelocity((DeltaSeconds > KINDA_SMALL_NUMBER)
			? (MoveDelta / DeltaSeconds)
			: FVector::ZeroVector);
	}

	const float Dist = FVector::Dist(Next, Target);

	// Defensive: silent reposition must never fire — detect and self-heal stray fire.
	AWeaponBase* SilentWeapon = Companion->GetCurrentWeapon();
	if (IsValid(SilentWeapon) && SilentWeapon->IsFiring())
	{
		UE_LOG(LogCompanionDiag, Warning, TEXT("%s: SILENT-REPOSITION-STRAY-FIRE detected — forcing stop"), *GetNameSafe(Companion));
		Companion->StopWeaponFire();
	}

	// Stall detection.
	if (FMath::Abs(Dist - LastRepositionDist) < 0.5f)
		RepositionStalledTime += DeltaSeconds;
	else
		RepositionStalledTime = 0.f;

	const bool bStalled = RepositionStalledTime >= RepositionStalledGracePeriod;
	const bool bTimedOut = RepositionElapsed >= RepositionTimeoutSeconds;

	if (bDebugLogging && bRepositionStartLogged)
	{
		const float Delta = LastRepositionDist - Dist;
		UE_LOG(LogCompanionDiag, VeryVerbose, TEXT("%s: REPOSITION-TICK status=Moving dist=%.0f delta=%.1f elapsed=%.1f stalled=%.2f isReloading=%d"),
			*Companion->GetName(), Dist, Delta, RepositionElapsed, RepositionStalledTime, (int32)Companion->IsReloading());
	}
	LastRepositionDist = Dist;

	const bool bArrived = Dist <= RepositionArrivalTolerance || bStalled || bTimedOut;
	if (bStalled || bTimedOut)
	{
		if (AAIController* AIC = Companion->GetController() ? Cast<AAIController>(Companion->GetController()) : nullptr)
			AIC->StopMovement();
		if (UCharacterMovementComponent* CMC = Companion->GetCharacterMovement())
			CMC->StopMovementImmediately();
		UE_LOG(LogCompanionDiag, Warning, TEXT("%s: REPOSITION-SNAP arrived=0 timedOut=%d stalled=%d elapsed=%.2f dist=%.0f warped=%.0f"),
			*Companion->GetName(), (int32)bTimedOut, (int32)bStalled, RepositionElapsed, Dist,
			FVector::Dist(Companion->GetActorLocation(), Target));
		Companion->SetActorLocation(Target, false, nullptr, ETeleportType::TeleportPhysics);
	}

	if (bArrived)
	{
		if (bDebugLogging && bRepositionStartLogged)
			UE_LOG(LogCompanionDiag, Log, TEXT("%s: REPOSITION-DONE result=arrived dist=%.0f elapsed=%.2f"),
				*Companion->GetName(), Dist, RepositionElapsed);
		Companion->SetLowReadyAim(false);
		Companion->SetAimTarget(nullptr);
		CurrentAlpha = *RepositionTargetAlpha;
		RepositionTargetAlpha.Reset();
		RepositionStalledTime = 0.f;
		LastRepositionDist = 0.f;
		RepositionElapsed = 0.f;
		bRepositionStartLogged = false;
		CurrentBurstAction = EPeekAction::Hold;
		bJustRepositioned = true;
		PeekCooldown = FMath::RandRange(MinPeekCooldown, MaxPeekCooldown);
		TimeInCoverIdle = 0.f;
		if (Slot->Height == ECoverHeight::Stand)
		{
			if (UCompanionAnimInstance* CAI = Cast<UCompanionAnimInstance>(
				Companion->GetMesh() ? Companion->GetMesh()->GetAnimInstance() : nullptr))
				CAI->ClearCoverStrafeVelocity();
		}
		// Silent reposition: companion never left cover pose — skip the enter montage to avoid the bobbing animation.
		if (Anim) Anim->EnterCoverPose(ResolvedPeekSide, Slot->Height, false);
	}
}

void UBTTask_CompanionCombat::TickStandUpAndRepositionAction(ACompanionCharacter* Companion, UCompanionAnimInstance* Anim,
	AAICoverSlot* Slot, bool bSuppressed, bool bLowHp, float DeltaSeconds)
{
	if (!IsValid(Companion) || !IsValid(Slot)) return;

	if (bSuppressed)
	{
		if (bDebugLogging && bRepositionStartLogged)
		{
			UE_LOG(LogCompanionDiag, Log, TEXT("%s: REPOSITION-DONE result=aborted dist=%.0f elapsed=%.2f"),
				*Companion->GetName(), FVector::Dist(Companion->GetActorLocation(), RepositionTargetWorldLoc), RepositionElapsed);
		}
		ReturnToCover(Companion, Anim, Slot, true, bLowHp);
		bRepositionStandPhase = false;
		bStandUpRepositionWalking = false;
		RepositionTargetAlpha.Reset();
		RepositionStalledTime = 0.f;
		bRepositionStartLogged = false;
		CurrentBurstAction = EPeekAction::Hold;
		bJustRepositioned = true;
		return;
	}

	// Phase A: stand-and-fire burst in place. BurstTimer is decremented by Branch 1; stay put until it expires.
	if (bRepositionStandPhase)
	{
		if (BurstTimer > 0.f) return;
		// Burst elapsed — transition to walk phase. Do NOT stop fire.
		bRepositionStandPhase = false;

		// Dry-abort: Phase A left us empty — skip Phase B walk, crouch in place and reload.
		if (Companion->NeedsReload() && !Companion->IsReloading())
		{
			if (IsValid(Slot))
				CurrentAlpha = Slot->GetAlphaFromLocation(Companion->GetActorLocation());

			RepositionTargetAlpha.Reset();
			bStandUpRepositionWalking = false;
			RepositionStalledTime   = 0.f;
			LastRepositionDist      = 0.f;
			RepositionElapsed       = 0.f;
			bRepositionStartLogged  = false;
			bJustRepositioned       = true;
			CurrentBurstAction      = EPeekAction::Hold;

			if (UCompanionAnimInstance* CAI = GetCompanionAnim(Companion))
				CAI->ClearCoverStrafeVelocity();
			if (UCharacterMovementComponent* CMC = Companion->GetCharacterMovement())
				CMC->StopMovementImmediately();

			if (bDebugLogging)
				UE_LOG(LogCompanionDiag, Log, TEXT("%s: STANDUP-REPOSITION-DRY-ABORT alpha=%.2f"),
					*Companion->GetName(), CurrentAlpha);

			// Defer reload until after the snap completes so the reload montage doesn't start
			// while the actor is still gliding to the slot location (visible reload-while-gliding).
			// The crouch-vs-stand distinction doesn't matter for reload timing -- what matters is
			// being at the slot location before the reload anim plays.
			bPendingReloadAfterSnap = true;

			ReturnToCover(Companion, Anim, Slot, false, bLowHp);
			return;
		}
	}

	RepositionElapsed += DeltaSeconds;

	// Phase B entry: first tick after burst completes.
	if (!bStandUpRepositionWalking)
	{
		bStandUpRepositionWalking = true;
		UE_LOG(LogCompanionDiag, Warning, TEXT("%s: STANDUP-WALK-START burst-elapsed=%.2f dist=%.0f"),
			*Companion->GetName(), FMath::RandRange(MinFireBurst, MaxFireBurst), FVector::Dist(Companion->GetActorLocation(), RepositionTargetWorldLoc));
	}

	const FVector Current = Companion->GetActorLocation();
	const FVector Target(RepositionTargetWorldLoc.X, RepositionTargetWorldLoc.Y, Current.Z);
	const FVector Next = FMath::VInterpConstantTo(Current, Target, DeltaSeconds, RepositionWalkSpeed);
	Companion->SetActorLocation(Next, false, nullptr, ETeleportType::TeleportPhysics);
	if (UCompanionAnimInstance* CAI = GetCompanionAnim(Companion))
	{
		const FVector MoveDelta = Next - Current;
		CAI->SetCoverStrafeVelocity((DeltaSeconds > KINDA_SMALL_NUMBER) ? (MoveDelta / DeltaSeconds) : FVector::ZeroVector);
	}

	// Phase B: walk-and-fire. Keep weapon firing each tick, but don't fire mid-reload — the
	// fire montage would preempt the reload montage on the UpperBody slot.
	if (bStandUpRepositionWalking && !Companion->IsReloading())
	{
		Companion->StartWeaponFire();
		// Note: ammo-empty is handled naturally by the weapon; no auto-reload during walk.
	}

	const float Dist = FVector::Dist(Next, Target);

	// Stall detection.
	if (FMath::Abs(Dist - LastRepositionDist) < 0.5f)
		RepositionStalledTime += DeltaSeconds;
	else
		RepositionStalledTime = 0.f;

	const bool bStalled = RepositionStalledTime >= RepositionStalledGracePeriod;
	const bool bTimedOut = RepositionElapsed >= RepositionTimeoutSeconds;

	if (bDebugLogging && bRepositionStartLogged)
	{
		const float Delta = LastRepositionDist - Dist;
		UE_LOG(LogCompanionDiag, VeryVerbose, TEXT("%s: REPOSITION-TICK status=Moving dist=%.0f delta=%.1f elapsed=%.1f stalled=%.2f isReloading=%d"),
			*Companion->GetName(), Dist, Delta, RepositionElapsed, RepositionStalledTime, (int32)Companion->IsReloading());
	}
	LastRepositionDist = Dist;

	const bool bArrived = Dist <= RepositionArrivalTolerance || bStalled || bTimedOut;
	if (bStalled || bTimedOut)
	{
		if (AAIController* AIC = Companion->GetController() ? Cast<AAIController>(Companion->GetController()) : nullptr)
			AIC->StopMovement();
		if (UCharacterMovementComponent* CMC = Companion->GetCharacterMovement())
			CMC->StopMovementImmediately();
		UE_LOG(LogCompanionDiag, Warning, TEXT("%s: REPOSITION-SNAP arrived=0 timedOut=%d stalled=%d elapsed=%.2f dist=%.0f warped=%.0f"),
			*Companion->GetName(), (int32)bTimedOut, (int32)bStalled, RepositionElapsed, Dist,
			FVector::Dist(Companion->GetActorLocation(), Target));
		Companion->SetActorLocation(Target, false, nullptr, ETeleportType::TeleportPhysics);
	}

	if (bArrived)
	{
		if (bDebugLogging && bRepositionStartLogged)
			UE_LOG(LogCompanionDiag, Log, TEXT("%s: REPOSITION-DONE result=arrived dist=%.0f elapsed=%.2f"),
				*Companion->GetName(), Dist, RepositionElapsed);
		CurrentAlpha = *RepositionTargetAlpha;
		RepositionTargetAlpha.Reset();
		bRepositionStandPhase = false;
		bStandUpRepositionWalking = false;
		RepositionStalledTime = 0.f;
		LastRepositionDist = 0.f;
		RepositionElapsed = 0.f;
		bRepositionStartLogged = false;
		bJustRepositioned = true;
		if (UCompanionAnimInstance* CAI = GetCompanionAnim(Companion))
			CAI->ClearCoverStrafeVelocity();
		// Phase C: stay standing — let next BT decision pick posture.
		ReturnToCover(Companion, Anim, Slot, false, bLowHp);
	}
}

void UBTTask_CompanionCombat::TickCornerPeekAction(ACompanionCharacter* Companion, UCompanionAnimInstance* Anim,
	AAICoverSlot* Slot, AActor* Target, bool bSuppressed, bool bLowHp,
	TArrayView<AActor* const> IgnoredAttached, float DeltaSeconds)
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
		const FVector ApexTarget(CornerPeekApexLocation.X, CornerPeekApexLocation.Y, Current.Z);
		const FVector Next = FMath::VInterpConstantTo(Current, ApexTarget, DeltaSeconds, CornerPeekOutSpeed);
		Companion->TeleportTo(Next, Companion->GetActorRotation(), false, true);
		if (UCompanionAnimInstance* CAI = GetCompanionAnim(Companion))
		{
			const FVector MoveDelta = Next - Current;
			CAI->SetCoverStrafeVelocity((DeltaSeconds > KINDA_SMALL_NUMBER) ? (MoveDelta / DeltaSeconds) : FVector::ZeroVector);
		}

		if (bRunLosThisTick)
		{
			AWeaponBase* W = Companion->GetCurrentWeapon();
			const FVector FireOrigin = IsValid(W) ? W->GetMuzzleLocation() : (Next + FVector(0.f, 0.f, StandFireEyeHeight));
			const bool bLos = HasLineOfSight(World, FireOrigin, Target, Companion, Blocker, IgnoredAttached);
			if (bLos && !bCornerPeekFiring)
			{
				Companion->StartWeaponFire();
				bCornerPeekFiring = true;
				UE_LOG(LogCompanionAI, Log,
					TEXT("%s: CORNER-PEEK-FIRE-START muzzle=%s target=%s pos=%s apex=%s"),
					*GetNameSafe(Companion),
					IsValid(W) ? *W->GetMuzzleLocation().ToString() : TEXT("(no weapon)"),
					IsValid(Target) ? *Target->GetActorLocation().ToString() : TEXT("(null)"),
					*Next.ToString(), *CornerPeekApexLocation.ToString());
			}
		}

		const bool bAtApex = FVector::Dist(Next, ApexTarget) <= RepositionArrivalTolerance;
		if (bAtApex)
		{
			// At apex — burst timer now counts down "time firing at the corner".
			BurstTimer -= DeltaSeconds;
			if (BurstTimer <= 0.f)
				bCornerPeekReturning = true;
		}
	}
	else
	{
		const FVector HomeTarget(CornerPeekHomeLocation.X, CornerPeekHomeLocation.Y, Current.Z);
		const FVector Next = FMath::VInterpConstantTo(Current, HomeTarget, DeltaSeconds, CornerPeekReturnSpeed);
		Companion->TeleportTo(Next, Companion->GetActorRotation(), false, true);
		if (UCompanionAnimInstance* CAI = GetCompanionAnim(Companion))
		{
			const FVector MoveDelta = Next - Current;
			CAI->SetCoverStrafeVelocity((DeltaSeconds > KINDA_SMALL_NUMBER) ? (MoveDelta / DeltaSeconds) : FVector::ZeroVector);
		}

		if (bRunLosThisTick)
		{
			AWeaponBase* W = Companion->GetCurrentWeapon();
			const FVector FireOrigin = IsValid(W) ? W->GetMuzzleLocation() : (Next + FVector(0.f, 0.f, StandFireEyeHeight));
			const bool bLos = HasLineOfSight(World, FireOrigin, Target, Companion, Blocker, IgnoredAttached);
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
			if (UCompanionAnimInstance* CAI = GetCompanionAnim(Companion))
				CAI->ClearCoverStrafeVelocity();
			ReturnToCover(Companion, Anim, Slot, false, bLowHp);
		}
	}
}

TOptional<float> UBTTask_CompanionCombat::PickBestAlphaByLos(AAICoverSlot* Slot, AActor* Target, ACompanionCharacter* Companion, float ExcludeAlpha, TArrayView<AActor* const> IgnoredAttached) const
{
	if (!IsValid(Slot) || !IsValid(Target) || !IsValid(Companion)) return {};
	UWorld* World = Companion->GetWorld();

	if (Slot->Height == ECoverHeight::Stand)
	{
		// Stand cover only fires from peekable corner apexes — test those, not arbitrary eye positions.
		const FVector LineDir = Slot->GetLineDirection();
		struct FCornerProbe { float Alpha; bool bFlag; FVector Dir; };
		const FCornerProbe Probes[] = {
			{ 0.f, Slot->bIsPeekableCornerStart, -LineDir },
			{ 1.f, Slot->bIsPeekableCornerEnd,    LineDir },
		};
		for (const FCornerProbe& P : Probes)
		{
			if (!P.bFlag) continue;
			if (FMath::Abs(P.Alpha - ExcludeAlpha) < 0.1f) continue;
			const FVector CornerLoc = Slot->GetLocationAtAlpha(P.Alpha);
			const FVector ApexLoc = CornerLoc + P.Dir * CornerPeekStepDistance;
			const FVector ApexEye = ApexLoc + FVector(0.f, 0.f, StandFireEyeHeight);
			AActor* Blocker = nullptr;
			if (HasLineOfSight(World, ApexEye, Target, Companion, Blocker, IgnoredAttached))
				return P.Alpha;
		}
		return {};
	}

	// Crouch cover: sweep N alphas evenly across the line, test eye-level LoS.
	const int32 N = FMath::Max(2, LosSweepSampleCount);
	const float ExcludeEpsilon = 0.5f / float(N - 1);
	for (int32 i = 0; i < N; ++i)
	{
		const float Alpha = (float)i / (float)(N - 1);
		if (FMath::Abs(Alpha - ExcludeAlpha) < ExcludeEpsilon) continue;
		const FVector Eye = Slot->GetLocationAtAlpha(Alpha) + FVector(0.f, 0.f, StandFireEyeHeight);
		AActor* Blocker = nullptr;
		if (HasLineOfSight(World, Eye, Target, Companion, Blocker, IgnoredAttached))
			return Alpha;
	}
	return {};
}

bool UBTTask_CompanionCombat::TryPrePeekReloadGate(ACompanionCharacter* Companion, AAICoverSlot* Slot)
{
	if (!IsValid(Companion)) return false;
	const int32 BurstCost = ComputeMinBurstAmmoCost(Companion);
	if (Companion->GetCurrentAmmo() >= BurstCost) return false;
	if (!Companion->CanReload()) return false;

	UWorld* const World = Companion->GetWorld();
	if (!World) return false;
	const float Now = World->GetTimeSeconds();

	// If the weapon is no longer reloading, clear the gate unconditionally.
	if (!Companion->IsReloading())
	{
		bReloadGateActive = false;
		ReloadGateStartTime = 0.f;
	}

	// Timeout escape: gate latched but reload never completed (e.g. montage interrupted).
	if (bReloadGateActive)
	{
		const float ReloadTime = Companion->GetWeaponReloadTime();
		const float TimeoutSeconds = ReloadTime * ReloadGateTimeoutMultiplier + ReloadGateTimeoutGrace;
		if ((Now - ReloadGateStartTime) > TimeoutSeconds)
		{
			UE_LOG(LogCompanionAI, Warning, TEXT("RELOAD-GATE-TIMEOUT — companion %s gate stuck >%.2fs; force-clearing"),
				*GetNameSafe(Companion), TimeoutSeconds);
			bReloadGateActive = false;
			ReloadGateStartTime = 0.f;
			return false;
		}
	}

	// Trigger reload on first entry.
	if (!Companion->IsReloading() && !bReloadGateActive)
	{
		if (UE_LOG_ACTIVE(LogCompanionDiag, Log))
			UE_LOG(LogCompanionDiag, Log,
				TEXT("%s: PRE-PEEK-RELOAD-GATE ammo=%d burstCost=%d hasCover=1 slot=%s"),
				*Companion->GetName(),
				Companion->GetCurrentAmmo(), BurstCost,
				*GetNameSafe(Slot));
		Companion->ReloadWeapon();
		if (Companion->IsReloading())
		{
			bReloadGateActive = true;
			ReloadGateStartTime = Now;
		}
	}

	// Re-assert crouch every tick while the gate holds. The AnimBP bIsReloading flag can
	// drive a standing-pose transition if the graph isn't guarded — Crouch() here counteracts
	// any capsule stand-up that results, keeping the companion behind cover during the reload.
	if (IsValid(Slot) && Slot->Height == ECoverHeight::Crouch && !Companion->bIsCrouched)
	{
		UE_LOG(LogCompanionDiag, Log, TEXT("%s: [CrouchCall] t=%.3f site=PrePeekReloadGate action=Crouch"),
			*GetNameSafe(Companion), World ? World->GetTimeSeconds() : 0.f);
		Companion->Crouch();
	}

	const float ReloadCooldownCap = MaxPeekCooldown * 2.f;
	const float ReloadBump = FMath::Clamp(Companion->GetWeaponReloadTime(), 0.f, ReloadCooldownCap);
	PeekCooldown = FMath::Max(PeekCooldown, ReloadBump);
	return true;
}

// --- Open-area move-and-shoot ---

bool UBTTask_CompanionCombat::PointHasLosToTarget(ACompanionCharacter* Companion, const FVector& Point, AActor* Target, TArrayView<AActor* const> IgnoredAttached) const
{
	if (!IsValid(Companion) || !IsValid(Target)) return false;
	AActor* BlockedBy = nullptr;
	const FVector Eye = Point + FVector(0.f, 0.f, StandFireEyeHeight);
	return HasLineOfSight(Companion->GetWorld(), Eye, Target, Companion, BlockedBy, IgnoredAttached);
}

void UBTTask_CompanionCombat::EnterMoveShootIfNeeded(ACompanionCharacter* Companion, AAIController* AIC, AActor* Target, UCharacterMovementComponent* CMC, const TCHAR* Reason)
{
	if (bMoveShootMoveActive) return;
	// Single source for the MaxWalkSpeed + MaxAcceleration override + focus; EndOpenAreaMoveShoot is the matching restore.
	CachedDefaultWalkSpeed = CMC->MaxWalkSpeed;
	CMC->MaxWalkSpeed = CombatMoveSpeed;
	CachedDefaultAcceleration = CMC->MaxAcceleration;
	AIC->SetFocus(Target, EAIFocusPriority::Gameplay);
	bMoveShootMoveActive = true;
	MoveShootRepositionTimer = 0.f;
	if (bDebugLogging)
		UE_LOG(LogCompanionAI, Log, TEXT("%s: MOVESHOOT enter %s speed=%.0f"), *Companion->GetName(), Reason, CombatMoveSpeed);
}

bool UBTTask_CompanionCombat::ShouldRepickMoveShoot(ACompanionCharacter* Companion, AAIController* AIC, float DeltaSeconds)
{
	// Re-pick on timer expiry or on arrival; otherwise let the current move run. While holding (last
	// pick failed), ignore the bIdle shortcut — StopMovement forced Idle, so honouring it would re-pick
	// every frame and thrash the nav query. Wait the full interval before re-picking.
	MoveShootRepositionTimer -= DeltaSeconds;
	const bool bArrived =
		FVector::DistSquared(Companion->GetActorLocation(), MoveShootDestination) <= FMath::Square(MoveShootAcceptRadius);
	const bool bIdle = AIC->GetMoveStatus() == EPathFollowingStatus::Idle;
	if (bMoveShootHolding)
	{
		if (MoveShootRepositionTimer > 0.f) return false;
	}
	else if (MoveShootRepositionTimer > 0.f && !bArrived && !bIdle)
	{
		return false;
	}
	MoveShootRepositionTimer = MoveShootRepositionInterval;
	return true;
}

void UBTTask_CompanionCombat::RerollJiggleOffset(ACompanionCharacter* Companion, AActor* Target, TArrayView<AActor* const> IgnoredAttached)
{
	// Try up to JiggleLosRetryCount random ground-plane offsets; accept the first whose micro-target has LoS.
	// Falls back to ZeroVector (sit on the already-LoS-safe anchor) if none pass.
	const int32 MaxTries = FMath::Max(1, JiggleLosRetryCount);
	for (int32 i = 0; i < MaxTries; ++i)
	{
		const float Angle = FMath::FRandRange(0.f, 2.f * PI);
		const float Radius = FMath::FRandRange(0.f, JiggleRadius);
		const FVector Candidate(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 0.f);
		if (PointHasLosToTarget(Companion, JiggleHome + Candidate, Target, IgnoredAttached))
		{
			JiggleOffset = Candidate;
			JiggleRetargetTimer = JiggleRetargetInterval;
			return;
		}
	}
	JiggleOffset = FVector::ZeroVector;
	JiggleRetargetTimer = JiggleRetargetInterval;
}

UBTTask_CompanionCombat::EJiggleDrift UBTTask_CompanionCombat::RollJiggleDrift() const
{
	// Roll against the running total — mirrors RollPeekActionMulti. All-zero degrades to Hold.
	const TPair<EJiggleDrift, float> Weighted[] = {
		{ EJiggleDrift::Closer,  JiggleDriftCloserWeight },
		{ EJiggleDrift::Farther, JiggleDriftFartherWeight },
		{ EJiggleDrift::Hold,    JiggleDriftHoldWeight },
	};
	float Total = 0.f;
	for (const TPair<EJiggleDrift, float>& Entry : Weighted)
	{
		if (Entry.Value > 0.f) Total += Entry.Value;
	}
	if (Total <= 0.f) return EJiggleDrift::Hold;

	float Roll = FMath::FRand() * Total;
	for (const TPair<EJiggleDrift, float>& Entry : Weighted)
	{
		if (Entry.Value <= 0.f) continue;
		Roll -= Entry.Value;
		if (Roll <= 0.f) return Entry.Key;
	}
	return EJiggleDrift::Hold;
}

void UBTTask_CompanionCombat::TickJiggleDrift(ACompanionCharacter* Companion, AActor* Target, TArrayView<AActor* const> IgnoredAttached, float DeltaSeconds)
{
	JiggleDriftTimer -= DeltaSeconds;
	if (JiggleDriftTimer > 0.f) return;
	JiggleDriftTimer = JiggleDriftInterval;

	const EJiggleDrift Drift = RollJiggleDrift();
	if (Drift == EJiggleDrift::Hold) return;

	// Horizontal home->target axis; nudge JiggleHome toward (Closer) or away (Farther) along it.
	FVector ToTarget = Target->GetActorLocation() - JiggleHome;
	ToTarget.Z = 0.f;
	if (ToTarget.SizeSquared() <= KINDA_SMALL_NUMBER) return;
	const FVector ToTargetDir = ToTarget.GetSafeNormal();
	const float Sign = (Drift == EJiggleDrift::Closer) ? 1.f : -1.f;

	FVector Nudged = JiggleHome + ToTargetDir * (JiggleDriftStep * Sign);

	// Clamp the nudged anchor's horizontal distance to the target into the ideal range band.
	FVector NudgedToTarget = Target->GetActorLocation() - Nudged;
	NudgedToTarget.Z = 0.f;
	const float NudgedDist = NudgedToTarget.Size();
	if (NudgedDist <= KINDA_SMALL_NUMBER) return;
	const float ClampedDist = FMath::Clamp(NudgedDist, MoveShootIdealRangeMin, MoveShootIdealRangeMax);
	const FVector NudgedTargetLoc(Target->GetActorLocation().X, Target->GetActorLocation().Y, JiggleHome.Z);
	Nudged = NudgedTargetLoc - NudgedToTarget.GetSafeNormal() * ClampedDist;

	// Project onto navmesh; skip the drift this cycle if it fails rather than anchoring off-mesh.
	FVector Projected;
	if (!ProjectToNav(Companion->GetWorld(), Nudged, MoveShootNavProjectExtent, Projected)) return;

	// LoS-gate the Closer drift: don't creep into the occluded zone near an elevated enemy.
	if (Drift == EJiggleDrift::Closer && !PointHasLosToTarget(Companion, Projected, Target, IgnoredAttached)) return;

	float KeepOut = 0.f;
	if (APawn* KOPlayer = ResolvePlayerKeepOut(Companion, KeepOut))
	{
		const FVector ClampedHome = ClampOutsidePlayerCircle(Projected, KOPlayer->GetActorLocation(), KeepOut);
		if (!ClampedHome.Equals(Projected))
		{
			FVector Reproj;
			if (!ProjectToNav(Companion->GetWorld(), ClampedHome, MoveShootNavProjectExtent, Reproj)) return;
			Projected = Reproj;
		}
	}

	JiggleHome = Projected;
}

static FVector SeedMoveDir(const FVector& MicroTarget, const FVector& CompanionLoc, const FVector& CompanionForward)
{
	FVector Dir = MicroTarget - CompanionLoc;
	Dir.Z = 0.f;
	return Dir.SizeSquared() > KINDA_SMALL_NUMBER ? Dir.GetSafeNormal() : CompanionForward;
}

void UBTTask_CompanionCombat::TickCombatJiggle(ACompanionCharacter* Companion, AAIController* AIC, AActor* Target, TArrayView<AActor* const> IgnoredAttached, float DeltaSeconds)
{
	if (!IsValid(Companion) || !IsValid(AIC) || !IsValid(Target)) return;
	UCharacterMovementComponent* CMC = Companion->GetCharacterMovement();
	if (!IsValid(CMC)) return;

	// Jiggle owns movement now — mark player-pull inactive so re-entry resets the pick gate.
	bPlayerPullActive = false;

	// Shares the speed/focus entry with the regain fan (single-source MaxWalkSpeed + SetFocus facing).
	EnterMoveShootIfNeeded(Companion, AIC, Target, CMC, TEXT("(jiggle)"));
	CMC->MaxAcceleration = CombatMoveAcceleration;

	// (Re)anchor on activation — also fires when resuming after a LoS-blocked stretch (bJiggleActive cleared there),
	// so it jiggles around wherever it ended up. StopMovement() once cancels the regain fan's MoveToLocation path
	// so it doesn't fight AddMovementInput.
	if (!bJiggleActive)
	{
		AIC->StopMovement();
		JiggleHome = Companion->GetActorLocation();
		float KeepOut = 0.f;
		if (APawn* KOPlayer = ResolvePlayerKeepOut(Companion, KeepOut))
			JiggleHome = ClampOutsidePlayerCircle(JiggleHome, KOPlayer->GetActorLocation(), KeepOut);
		JiggleDriftTimer = JiggleDriftInterval;
		RerollJiggleOffset(Companion, Target, IgnoredAttached);
		SmoothedMoveDir = SeedMoveDir(JiggleHome + JiggleOffset, JiggleHome, Companion->GetActorForwardVector());
		bJiggleActive = true;
	}

	TickJiggleDrift(Companion, Target, IgnoredAttached, DeltaSeconds);

	// Re-roll the micro-target on the timer or on reach, so the companion never settles.
	const FVector CompanionLoc = Companion->GetActorLocation();
	const FVector MicroTarget = JiggleHome + JiggleOffset;
	JiggleRetargetTimer -= DeltaSeconds;
	const bool bReached = FVector::DistSquared2D(CompanionLoc, MicroTarget) <= FMath::Square(JiggleReachThreshold);
	if (JiggleRetargetTimer <= 0.f || bReached) RerollJiggleOffset(Companion, Target, IgnoredAttached);

	// Bounded-turn-rate input toward the micro-target — smoothly rotates the heading rather than snapping,
	// so legs commit to full deliberate strafe steps instead of half-steps.
	FVector DesiredDir = (JiggleHome + JiggleOffset) - CompanionLoc;
	DesiredDir.Z = 0.f;
	if (DesiredDir.SizeSquared() <= KINDA_SMALL_NUMBER) return;
	const FVector TargetDir = DesiredDir.GetSafeNormal();
	if (SmoothedMoveDir.IsNearlyZero()) SmoothedMoveDir = TargetDir;
	SmoothedMoveDir = FMath::VInterpNormalRotationTo(SmoothedMoveDir, TargetDir, DeltaSeconds, CombatMoveTurnRate);

	// Player keep-out: never drive within the circle — push straight out if inside, slide tangentially if approaching.
	float KeepOut = 0.f;
	if (APawn* KOPlayer = ResolvePlayerKeepOut(Companion, KeepOut))
	{
		FVector ToPlayer = KOPlayer->GetActorLocation() - CompanionLoc;
		ToPlayer.Z = 0.f;
		const float DistToPlayer = ToPlayer.Size();
		if (DistToPlayer > KINDA_SMALL_NUMBER)
		{
			const FVector ToPlayerDir = ToPlayer / DistToPlayer;
			if (DistToPlayer < KeepOut)
			{
				FVector Tangent = FVector::CrossProduct(FVector::UpVector, ToPlayerDir);
				if (FVector::DotProduct(SmoothedMoveDir, Tangent) < 0.f) Tangent = -Tangent;
				SmoothedMoveDir = (-ToPlayerDir + Tangent).GetSafeNormal();
			}
			else if (DistToPlayer < KeepOut + JiggleRadius)
			{
				const float Inward = FVector::DotProduct(SmoothedMoveDir, ToPlayerDir);
				if (Inward > 0.f)
				{
					const FVector Slide = SmoothedMoveDir - ToPlayerDir * Inward;
					if (Slide.SizeSquared() > KINDA_SMALL_NUMBER) SmoothedMoveDir = Slide.GetSafeNormal();
				}
			}
		}
	}

	Companion->AddMovementInput(SmoothedMoveDir, 1.0f);
}

void UBTTask_CompanionCombat::TickRegainLosReposition(ACompanionCharacter* Companion, AAIController* AIC, AActor* Target, TArrayView<AActor* const> IgnoredAttached, float DeltaSeconds)
{
	if (!IsValid(Companion) || !IsValid(AIC) || !IsValid(Target)) return;
	UCharacterMovementComponent* CMC = Companion->GetCharacterMovement();
	if (!IsValid(CMC)) return;

	// Regain-LoS fan owns movement now — mark player-pull inactive so re-entry resets the pick gate.
	bPlayerPullActive = false;

	// Shares entry/speed/focus + re-pick gate with the clear path: MaxWalkSpeed restore stays single-source
	// (EndOpenAreaMoveShoot) and the body keeps facing the enemy (focus) while we sidestep.
	EnterMoveShootIfNeeded(Companion, AIC, Target, CMC, TEXT("(regain-los)"));
	if (CachedDefaultAcceleration > 0.f) CMC->MaxAcceleration = CachedDefaultAcceleration;
	if (!ShouldRepickMoveShoot(Companion, AIC, DeltaSeconds)) return;

	// Fan of offsets from CURRENT location (right/left/back/back-right/back-left), nearest LoS-valid wins.
	// For an enemy above on a ramp, lateral steps rarely clear the lip — backing up lowers the look-up
	// angle past it, so the fan includes the back directions. Try base distance, then 2x, else hold.
	constexpr float WideStepFactor = 2.0f;
	FVector Dest;
	const bool bHaveDest =
		PickFanLosDestination(Companion, Target, IgnoredAttached, MoveShootStrafeDistance, Dest)
		|| PickFanLosDestination(Companion, Target, IgnoredAttached, MoveShootStrafeDistance * WideStepFactor, Dest);

	// No LoS-clear fan point at either distance — hold (respect the reposition timer, don't thrash the nav query).
	if (!bHaveDest)
	{
		bMoveShootHolding = true;
		AIC->StopMovement();
		return;
	}

	bMoveShootHolding = false;
	MoveShootDestination = Dest;
	AIC->MoveToLocation(MoveShootDestination, MoveShootAcceptRadius, false, true, false, true);
}

bool UBTTask_CompanionCombat::PickFanLosDestination(ACompanionCharacter* Companion, AActor* Target, TArrayView<AActor* const> IgnoredAttached, float StepDistance, FVector& OutDest) const
{
	if (!IsValid(Companion) || !IsValid(Target)) return false;

	const FVector MyLoc = Companion->GetActorLocation();
	FVector ToTarget = Target->GetActorLocation() - MyLoc;
	ToTarget.Z = 0.f;
	if (ToTarget.SizeSquared() <= KINDA_SMALL_NUMBER) return false;

	const FVector ToTargetDir = ToTarget.GetSafeNormal();
	const FVector RightOfTarget = FVector::CrossProduct(FVector::UpVector, ToTargetDir).GetSafeNormal();
	const FVector Back = -ToTargetDir; // horizontal target->companion: lowers the look-up angle past a ramp lip
	const FVector Offsets[] = { RightOfTarget, -RightOfTarget, Back, Back + RightOfTarget, Back - RightOfTarget };

	// Nearest valid candidate (smallest displacement from current location that regains LoS) — biases a
	// small back-step over a big lateral swing. Project + LoS-verify each, track the closest hit.
	bool bFound = false;
	float BestDistSq = TNumericLimits<float>::Max();
	for (const FVector& Dir : Offsets)
	{
		FVector Candidate;
		if (!TryLateralLosDestination(Companion, Target, IgnoredAttached, Dir.GetSafeNormal() * StepDistance, Candidate)) continue;
		const float DistSq = FVector::DistSquared(Candidate, MyLoc);
		if (DistSq >= BestDistSq) continue;
		BestDistSq = DistSq;
		OutDest = Candidate;
		bFound = true;
	}
	return bFound;
}

bool UBTTask_CompanionCombat::TryLateralLosDestination(ACompanionCharacter* Companion, AActor* Target, TArrayView<AActor* const> IgnoredAttached, const FVector& LateralOffset, FVector& OutDest) const
{
	const FVector Candidate = Companion->GetActorLocation() + LateralOffset;
	if (!ProjectToNav(Companion->GetWorld(), Candidate, MoveShootNavProjectExtent, OutDest)) return false;
	float KeepOut = 0.f;
	if (APawn* KOPlayer = ResolvePlayerKeepOut(Companion, KeepOut))
	{
		FVector ToPlayer = KOPlayer->GetActorLocation() - OutDest;
		ToPlayer.Z = 0.f;
		if (ToPlayer.SizeSquared() < FMath::Square(KeepOut)) return false;
	}
	return PointHasLosToTarget(Companion, OutDest, Target, IgnoredAttached);
}

void UBTTask_CompanionCombat::EndOpenAreaMoveShoot(ACompanionCharacter* Companion)
{
	if (!bMoveShootMoveActive) return;

	// Restore speed first (so it's attempted even if the controller is gone), then stop/clear focus.
	if (IsValid(Companion))
	{
		if (UCharacterMovementComponent* CMC = Companion->GetCharacterMovement())
		{
			if (CachedDefaultWalkSpeed > 0.f)
				CMC->MaxWalkSpeed = CachedDefaultWalkSpeed;
			if (CachedDefaultAcceleration > 0.f)
				CMC->MaxAcceleration = CachedDefaultAcceleration;
		}
		if (AAIController* AIC = Cast<AAIController>(Companion->GetController()))
		{
			AIC->ClearFocus(EAIFocusPriority::Gameplay);
			AIC->StopMovement();
		}
	}

	// Always clear state — flags can never survive without the restore having been attempted,
	// and a recycled node instance can't carry stale move-shoot state.
	bMoveShootMoveActive = false;
	MoveShootRepositionTimer = 0.f;
	MoveShootDestination = FVector::ZeroVector;
	bMoveShootHolding = false;
	CachedDefaultWalkSpeed = 0.f;
	CachedDefaultAcceleration = 0.f;

	// Jiggle teardown — reset anchor/offset + timers so a resumed engagement re-anchors fresh.
	bJiggleActive = false;
	JiggleHome = FVector::ZeroVector;
	JiggleOffset = FVector::ZeroVector;
	JiggleRetargetTimer = 0.f;
	JiggleDriftTimer = 0.f;
	SmoothedMoveDir = FVector::ZeroVector;

	// Player-pull hysteresis + entry-detection — both start unlatched so a fresh engagement is clean.
	bPlayerPullLatched = false;
	bPlayerPullActive = false;
}

void UBTTask_CompanionCombat::TickMoveShootTowardPlayer(ACompanionCharacter* Companion, AAIController* AIC, AActor* Target, float DeltaSeconds)
{
	if (!IsValid(Companion) || !IsValid(AIC) || !IsValid(Target)) return;
	UCharacterMovementComponent* CMC = Companion->GetCharacterMovement();
	if (!IsValid(CMC)) return;

	APawn* Player = nullptr;
	float StopDist = DefaultPlayerPullStopDist;
	if (ACompanionAIController* CompAIC = Cast<ACompanionAIController>(AIC))
	{
		Player = CompAIC->GetPlayerCharacter();
		if (const UCompanionTuningDataAsset* T = CompAIC->GetTuning())
		{
			const float LeashDist = T->SprintDistanceThreshold > 0.f ? T->SprintDistanceThreshold : DefaultCombatPlayerLeash;
			StopDist = FMath::Min(T->AcceptableRadius, LeashDist * 0.5f);
		}
	}
	if (!IsValid(Player)) return;

	// Edge-detect first entry into player-pull: flush any stale "hold" from the regain fan so the
	// first MoveToLocation fires immediately rather than waiting up to MoveShootRepositionInterval.
	if (!bPlayerPullActive)
	{
		bMoveShootHolding = false;
		MoveShootRepositionTimer = 0.f;
		bPlayerPullActive = true;
	}

	// Keep body facing the enemy via focus while closing the gap; re-anchor jiggle when we switch back.
	EnterMoveShootIfNeeded(Companion, AIC, Target, CMC, TEXT("(player-pull)"));
	if (CachedDefaultAcceleration > 0.f) CMC->MaxAcceleration = CachedDefaultAcceleration;
	bJiggleActive = false;

	if (!ShouldRepickMoveShoot(Companion, AIC, DeltaSeconds)) return;

	// Desired point: StopDist behind the player along the companion->player axis.
	const FVector PlayerLoc = Player->GetActorLocation();
	const FVector CompLoc   = Companion->GetActorLocation();
	FVector FromPlayer = CompLoc - PlayerLoc;
	FromPlayer.Z = 0.f;
	if (FromPlayer.SizeSquared() <= KINDA_SMALL_NUMBER)
	{
		// Companion is essentially on the player — hold rather than picking a degenerate direction.
		bMoveShootHolding = true;
		AIC->StopMovement();
		return;
	}
	const FVector Desired = PlayerLoc + FromPlayer.GetSafeNormal() * StopDist;

	FVector Projected;
	if (!ProjectToNav(Companion->GetWorld(), Desired, MoveShootNavProjectExtent, Projected))
	{
		bMoveShootHolding = true;
		AIC->StopMovement();
		return;
	}
	bMoveShootHolding = false;
	MoveShootDestination = Projected;
	AIC->MoveToLocation(MoveShootDestination, MoveShootAcceptRadius, false, true, false, true);
}

// --- Constructor ---

UBTTask_CompanionCombat::UBTTask_CompanionCombat()
{
	NodeName = TEXT("Companion Combat");
	bNotifyTick = true;
	bNotifyTaskFinished = true;
	bCreateNodeInstance = true;
}

// --- ResetTaskState ---

void UBTTask_CompanionCombat::ResetTaskState(ACompanionCharacter* Companion, UBlackboardComponent* BB, AAICoverSlot* Slot, bool bReleaseSlot, bool bResetPosture)
{
	// Sole move-and-shoot teardown owner: restores walk speed, clears focus, stops movement, and
	// always clears the latch/cache. Tolerates a null/invalid Companion and is idempotent, so the
	// branch-transition / LoS-lost / toggle-off call sites are unaffected.
	EndOpenAreaMoveShoot(Companion);

	if (IsValid(Companion))
	{
		Companion->StopWeaponFire();
		Companion->SetAimTarget(nullptr);
		Companion->SetLowReadyAim(false);
		if (bSmoothSnapping)
		{
			UE_LOG(LogCompanionDiag, Log, TEXT("%s: [SnapAborted] t=%.3f elapsed=%.3f reason=%s"),
				*GetNameSafe(Companion),
				Companion->GetWorld() ? Companion->GetWorld()->GetTimeSeconds() : 0.f,
				SmoothSnapElapsed,
				SmoothSnapReason);
		}
		if (bResetPosture)
		{
			if (bSmoothSnapping && bPendingCrouchAfterSnap)
			{
				UE_LOG(LogCompanionDiag, Log, TEXT("%s: [CrouchCall] t=%.3f site=ResetTaskState_SkippedForPendingCrouch action=NoOp"),
					*GetNameSafe(Companion),
					Companion->GetWorld() ? Companion->GetWorld()->GetTimeSeconds() : 0.f);
			}
			else
			{
				UE_LOG(LogCompanionDiag, Log, TEXT("%s: [CrouchCall] t=%.3f site=ResetTaskState action=UnCrouch"),
					*GetNameSafe(Companion),
					Companion->GetWorld() ? Companion->GetWorld()->GetTimeSeconds() : 0.f);
				Companion->UnCrouch();
			}
		}

		UCompanionAnimInstance* Anim = GetCompanionAnim(Companion);
		if (UAnimMontage* M = ActivePeekMontage.Get())
		{
			if (Anim) Anim->Montage_Stop(0.15f, M);
		}
		ActivePeekMontage.Reset();
		if (Anim) Anim->ExitCoverPose();
	}

	if (bReleaseSlot && BB)
	{
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
	PeekCooldown = 0.f;
	CoverValidityCheckTimer = 0.f;
	TimeAtCurrentCover = 0.f;
	LosBlockedAccum = 0.f;
	TimeInOpenEngageNoCover = 0.f;
	LastTickBranch = -1;
	bLastLosBlocked = false;
	LastLosBlocker = nullptr;
	// Move-and-shoot teardown is owned solely by EndOpenAreaMoveShoot (called above) — do not
	// duplicate the member resets here, or an invalid-Companion path would clear the latch flag
	// and cached speed without restoring MaxWalkSpeed.
	LastPeekResolveCoverLoc = FVector::ZeroVector;
	LastPeekResolveTargetLoc = FVector::ZeroVector;
	ConsecutiveHolds = 0;
	bJustRepositioned = false;
	CurrentBurstAction = EPeekAction::Stand;
	CurrentAlpha = 0.5f;
	CachedSlotForwardYaw = 0.f;
	SubSlotLosRecheckTimer = 0.f;
	LastTickSlot = nullptr;
	BlockedRecheckHits = 0;
	RepositionTargetAlpha.Reset();
	RepositionTargetWorldLoc = FVector::ZeroVector;
	bRepositionStandPhase = false;
	bStandUpRepositionWalking = false;
	LastRepositionDist = 0.f;
	RepositionElapsed = 0.f;
	RepositionStalledTime = 0.f;
	bRepositionStartLogged = false;
	CornerPeekHomeLocation = FVector::ZeroVector;
	CornerPeekApexLocation = FVector::ZeroVector;
	bCornerPeekReturning = false;
	bCornerPeekFiring = false;
	bWaitingForFinalApproach = false;
	FinalApproachTarget = FVector::ZeroVector;
	FinalApproachElapsed = 0.f;
	FinalApproachStalledTime = 0.f;
	bSmoothSnapping = false;
	SmoothSnapElapsed = 0.f;
	bPendingCrouchAfterSnap = false;
	bPendingReloadAfterSnap = false;
	SmoothSnapInitialDist = 0.f;
	SmoothSnapEffectiveDuration = 0.f;
	bReloadGateActive = false;
	ReloadGateStartTime = 0.f;
	LastDecisionTime = 0.f;
}

// --- Smooth-snap helpers ---

void UBTTask_CompanionCombat::BeginSmoothSnap(ACompanionCharacter* Companion, const FVector& TargetLoc, const FRotator& TargetRot, bool bShouldCrouchAfter, const TCHAR* Reason)
{
	if (!IsValid(Companion)) return;
	SmoothSnapStartLoc = Companion->GetActorLocation();
	SmoothSnapStartRot = Companion->GetActorRotation();
	SmoothSnapTargetLoc = TargetLoc;
	SmoothSnapTargetRot = TargetRot;
	SmoothSnapElapsed = 0.f;
	bSmoothSnapping = true;
	SmoothSnapReason = Reason;
	SmoothSnapInitialDist = (SmoothSnapTargetLoc - SmoothSnapStartLoc).Size();
	SmoothSnapEffectiveDuration = FMath::Clamp(SmoothSnapDuration * (SmoothSnapInitialDist / 30.f), SmoothSnapDuration, 1.0f);

	// Short snaps (<=30 cm): fire crouch/uncrouch immediately so the AnimBP pose blend
	// runs in parallel with the position glide — companion crouches WHILE gliding into cover.
	// Long snaps defer to snap completion to avoid visible capsule resize mid-glide.
	const float DistThreshold = 30.f;
	if (SmoothSnapInitialDist <= DistThreshold)
	{
		if (bShouldCrouchAfter && !Companion->bIsCrouched)
		{
			// Adjust target Z for the upcoming capsule shrink so the crouched actor sits on the floor.
			const float StandingHalfH = Companion->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
			float CrouchedHalfH = StandingHalfH;
			if (UCharacterMovementComponent* CMC = Companion->GetCharacterMovement())
				CrouchedHalfH = CMC->GetCrouchedHalfHeight();
			SmoothSnapTargetLoc.Z -= (StandingHalfH - CrouchedHalfH);

			UE_LOG(LogCompanionDiag, Log, TEXT("%s: [CrouchCall] t=%.3f site=ImmediateOnShortSnap action=Crouch"),
				*GetNameSafe(Companion),
				Companion->GetWorld() ? Companion->GetWorld()->GetTimeSeconds() : 0.f);
			Companion->Crouch();
			bPendingCrouchAfterSnap = bShouldCrouchAfter;
		}
		else if (!bShouldCrouchAfter && Companion->bIsCrouched)
		{
			// Adjust target Z for the upcoming capsule grow so the standing actor sits on the floor.
			const float CrouchedHalfH_Now = Companion->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
			float StandingHalfH_Target = CrouchedHalfH_Now;
			if (ACharacter* DefaultChar = Cast<ACharacter>(Companion->GetClass()->GetDefaultObject()))
			{
				if (UCapsuleComponent* DefaultCapsule = DefaultChar->GetCapsuleComponent())
					StandingHalfH_Target = DefaultCapsule->GetUnscaledCapsuleHalfHeight();
			}
			SmoothSnapTargetLoc.Z += (StandingHalfH_Target - CrouchedHalfH_Now);

			UE_LOG(LogCompanionDiag, Log, TEXT("%s: [CrouchCall] t=%.3f site=ImmediateOnShortSnap action=UnCrouch"),
				*GetNameSafe(Companion),
				Companion->GetWorld() ? Companion->GetWorld()->GetTimeSeconds() : 0.f);
			Companion->UnCrouch();
			bPendingCrouchAfterSnap = bShouldCrouchAfter;
		}
		else
		{
			bPendingCrouchAfterSnap = bShouldCrouchAfter;
		}
	}
	else
	{
		bPendingCrouchAfterSnap = bShouldCrouchAfter;
	}

	UE_LOG(LogCompanionDiag, Log, TEXT("%s: [BeginSmoothSnap] t=%.3f initialDist=%.1f effectiveDur=%.3f yawDelta=%.1f crouchAfter=%d reason=%s"),
		*GetNameSafe(Companion),
		Companion->GetWorld() ? Companion->GetWorld()->GetTimeSeconds() : 0.f,
		SmoothSnapInitialDist,
		SmoothSnapEffectiveDuration,
		FMath::FindDeltaAngleDegrees(SmoothSnapStartRot.Yaw, SmoothSnapTargetRot.Yaw),
		(int32)bShouldCrouchAfter,
		Reason);
}

bool UBTTask_CompanionCombat::TickSmoothSnap(ACompanionCharacter* Companion, float DeltaSeconds)
{
	if (!bSmoothSnapping || !IsValid(Companion)) return true;
	SmoothSnapElapsed += DeltaSeconds;
	const float Alpha = FMath::Clamp(SmoothSnapElapsed / FMath::Max(SmoothSnapEffectiveDuration, 0.001f), 0.f, 1.f);
	const float InterpAlpha = (SmoothSnapInitialDist > 30.f) ? Alpha : FMath::SmoothStep(0.f, 1.f, Alpha);
	const FVector NextLoc = FMath::Lerp(SmoothSnapStartLoc, SmoothSnapTargetLoc, InterpAlpha);
	const FRotator NextRot = FMath::Lerp(SmoothSnapStartRot, SmoothSnapTargetRot, InterpAlpha);
	Companion->SetActorLocationAndRotation(NextLoc, NextRot, false, nullptr, ETeleportType::TeleportPhysics);
	if (Alpha >= 1.f)
	{
		bSmoothSnapping = false;
		const bool bWasCrouched = Companion->bIsCrouched;
		const bool bWillCrouch = (bPendingCrouchAfterSnap && !Companion->bIsCrouched);
		const bool bWillUncrouch = (!bPendingCrouchAfterSnap && Companion->bIsCrouched);
		UE_LOG(LogCompanionDiag, Log, TEXT("%s: [SnapComplete] t=%.3f elapsed=%.3f effectiveDur=%.3f initialDist=%.1f linear=%d crouchPending=%d wasCrouched=%d willCrouch=%d willUncrouch=%d reason=%s"),
			*GetNameSafe(Companion),
			Companion->GetWorld() ? Companion->GetWorld()->GetTimeSeconds() : 0.f,
			SmoothSnapElapsed,
			SmoothSnapEffectiveDuration,
			SmoothSnapInitialDist,
			(SmoothSnapInitialDist > 30.f) ? 1 : 0,
			(int32)bPendingCrouchAfterSnap,
			(int32)bWasCrouched,
			(int32)bWillCrouch,
			(int32)bWillUncrouch,
			SmoothSnapReason);
		SmoothSnapElapsed = 0.f;
		// Apply pending posture change on snap completion (avoids crouch pop mid-snap).
		if (bWillCrouch)
		{
			UE_LOG(LogCompanionDiag, Log, TEXT("%s: [CrouchCall] t=%.3f site=PostSnapApply action=Crouch"),
				*GetNameSafe(Companion),
				Companion->GetWorld() ? Companion->GetWorld()->GetTimeSeconds() : 0.f);
			Companion->Crouch();
		}
		else if (bWillUncrouch)
		{
			UE_LOG(LogCompanionDiag, Log, TEXT("%s: [CrouchCall] t=%.3f site=PostSnapApply action=UnCrouch"),
				*GetNameSafe(Companion),
				Companion->GetWorld() ? Companion->GetWorld()->GetTimeSeconds() : 0.f);
			Companion->UnCrouch();
		}
		bPendingCrouchAfterSnap = false;
		if (bPendingReloadAfterSnap && IsValid(Companion))
		{
			UE_LOG(LogCompanionDiag, Log, TEXT("%s: [ReloadAfterSnap] t=%.3f"),
				*GetNameSafe(Companion),
				Companion->GetWorld() ? Companion->GetWorld()->GetTimeSeconds() : 0.f);
			Companion->ReloadWeapon();
			bPendingReloadAfterSnap = false;
		}
		return true;
	}
	return false;
}

// --- AbortTask ---

EBTNodeResult::Type UBTTask_CompanionCombat::AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	AAIController* Controller = OwnerComp.GetAIOwner();
	ACompanionCharacter* Companion = Controller ? Cast<ACompanionCharacter>(Controller->GetPawn()) : nullptr;
	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	AAICoverSlot* Slot = BB ? Cast<AAICoverSlot>(BB->GetValueAsObject(CoverSlotKey.SelectedKeyName)) : nullptr;
	ResetTaskState(Companion, BB, Slot, true);
	return EBTNodeResult::Aborted;
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

	// Full state reset without releasing the slot we're about to claim.
	// Don't reset posture — preserves anticipatory crouch from MoveToCover.
	ResetTaskState(Companion, BB, nullptr, false, false);

	Companion->SetAimTarget(Target);
	PeekCooldown = FMath::RandRange(MinPeekCooldown, MaxPeekCooldown);
	DebugBurstLosCheckTimer = 0.f;
	SubSlotLosRecheckInterval = FMath::Max(0.1f, SubSlotLosRecheckInterval);

	if (!Companion->GetCurrentWeapon())
		UE_LOG(LogCompanionAI, Warning, TEXT("%s: combat task started but CurrentWeapon is null"), *Companion->GetName());

	AAICoverSlot* Slot = Cast<AAICoverSlot>(BB->GetValueAsObject(CoverSlotKey.SelectedKeyName));
	const bool bHasCover = IsValid(Slot) && BB->GetValueAsBool(HasCoverPositionKey.SelectedKeyName);

	if (bHasCover)
	{
		const ECoverPeekSide Pref = Slot->PeekPreference;
		ResolvedPeekSide = (Pref == ECoverPeekSide::Left) ? EPeekSide::Left : EPeekSide::Right;

		const FVector ArrivalLoc = Companion->GetActorLocation();
		float InitialAlpha = Slot->GetAlphaFromLocation(ArrivalLoc);
		// Stand cover: snap to the nearest peekable endpoint; companion must never idle at a midpoint.
		const bool bStandRestrictToCorners = (Slot->Height == ECoverHeight::Stand);
		if (bStandRestrictToCorners)
		{
			const bool bStartPeekable = Slot->bIsPeekableCornerStart;
			const bool bEndPeekable = Slot->bIsPeekableCornerEnd;
			if (bStartPeekable && bEndPeekable)
				InitialAlpha = (InitialAlpha < 0.5f) ? 0.f : 1.f;
			else if (bStartPeekable)
				InitialAlpha = 0.f;
			else if (bEndPeekable)
				InitialAlpha = 1.f;
			else
				InitialAlpha = 0.f; // warn fires below via STAND-COVER-CONFIG-WARN
		}
		CurrentAlpha = InitialAlpha;

		const FVector SubSlotLoc = Slot->GetLocationAtAlpha(CurrentAlpha);
		const float DistToSubSlot = FVector::Dist(ArrivalLoc, SubSlotLoc);

		if (DistToSubSlot <= FinalApproachAcceptRadius)
		{
			// Already at the slot — snap and enter cover immediately.
			if (AAIController* AIC = Cast<AAIController>(Companion->GetController()))
				AIC->StopMovement();
			if (UCharacterMovementComponent* CMC = Companion->GetCharacterMovement())
				CMC->StopMovementImmediately();

			const FRotator SlotYawRot(0.f, Slot->GetForwardDirection().Rotation().Yaw, 0.f);
			if (bDebugLogging) UE_LOG(LogCompanionDiag, Log, TEXT("%s: ALREADY-CLOSE-SNAP distToSubSlot=%.1f acceptRadius=%.1f arrival=%s subSlot=%s"),
				*Companion->GetName(), DistToSubSlot, FinalApproachAcceptRadius, *ArrivalLoc.ToString(), *SubSlotLoc.ToString());
			// Ground-snap: nav-mesh-arrival Z is biased above the floor; trace down to find the real floor.
			FVector SnapLoc(SubSlotLoc.X, SubSlotLoc.Y, ArrivalLoc.Z);
			if (UWorld* SnapWorld = Companion->GetWorld())
			{
				const FVector TraceStart = SnapLoc + FVector(0.f, 0.f, 80.f);
				const FVector TraceEnd = SnapLoc - FVector(0.f, 0.f, 200.f);
				FHitResult GroundHit;
				FCollisionQueryParams Params;
				Params.AddIgnoredActor(Companion);
				if (SnapWorld->LineTraceSingleByChannel(GroundHit, TraceStart, TraceEnd, ECC_Visibility, Params))
				{
					const float CapsuleHalfHeight = Companion->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
					SnapLoc.Z = GroundHit.ImpactPoint.Z + CapsuleHalfHeight;
				}
			}
			BeginSmoothSnap(Companion, SnapLoc, SlotYawRot, Slot->Height == ECoverHeight::Crouch, TEXT("AlreadyClose"));
			if (UCompanionAnimInstance* Anim = GetCompanionAnim(Companion))
				Anim->EnterCoverPose(ResolvedPeekSide, Slot->Height);
		}
		else
		{
			// Walk the last bit physically; TickTask commits to cover on arrival.
			bWaitingForFinalApproach = true;
			FinalApproachTarget = SubSlotLoc;
			FinalApproachElapsed = 0.f;
			LastFinalApproachDist = DistToSubSlot;

			if (AAIController* AIC = Cast<AAIController>(Companion->GetController()))
			{
				AIC->MoveToLocation(SubSlotLoc, FinalApproachAcceptRadius, false, true, true, true);
				if (bDebugLogging)
				{
					const TCHAR* PFStatus = TEXT("Unknown");
					if (UPathFollowingComponent* PF = AIC->GetPathFollowingComponent())
					{
						const EPathFollowingStatus::Type St = PF->GetStatus();
						PFStatus = (St == EPathFollowingStatus::Moving)  ? TEXT("Moving")
							: (St == EPathFollowingStatus::Idle)    ? TEXT("Idle")
							: (St == EPathFollowingStatus::Waiting) ? TEXT("Waiting")
							:                                          TEXT("Paused");
					}
					UE_LOG(LogCompanionDiag, Log, TEXT("%s: FINALAPPROACH-KICK target=%s current=%s dist=%.0f pathStatus=%s"),
						*Companion->GetName(), *SubSlotLoc.ToString(), *ArrivalLoc.ToString(), DistToSubSlot, PFStatus);
				}
			}
		}
	}

	if (bHasCover && IsValid(Slot))
	{
		// One-shot diagnostic so we can see cover-slot config without toggling bDebugLogging.
		const bool bAnyPeekableCorner = Slot->bIsPeekableCornerStart || Slot->bIsPeekableCornerEnd;
		if (Slot->Height == ECoverHeight::Stand && !bAnyPeekableCorner)
		{
			UE_LOG(LogCompanionAI, Warning,
				TEXT("%s: STAND-COVER-CONFIG-WARN slot=%s has Height=Stand but NEITHER bIsPeekableCornerStart NOR bIsPeekableCornerEnd is ticked — companion cannot fire from this slot. Tick at least one in the slot's details panel."),
				*Companion->GetName(), *Slot->GetName());
		}
		UE_LOG(LogCompanionAI, Log,
			TEXT("%s: COVER-CLAIMED slot=%s height=%s alpha=%.2f lineLen=%.0f cornerStart=%d cornerEnd=%d peekPref=%d canStandFireOver=%d"),
			*Companion->GetName(), *Slot->GetName(),
			Slot->Height == ECoverHeight::Crouch ? TEXT("Crouch") : TEXT("Stand"),
			CurrentAlpha, Slot->GetLineLength(),
			(int32)Slot->bIsPeekableCornerStart, (int32)Slot->bIsPeekableCornerEnd,
			(int32)Slot->PeekPreference,
			(int32)Slot->CanStandFireOver());
		if (Slot->GetLineLength() < 10.f)
		{
			UE_LOG(LogCompanionAI, Warning, TEXT("%s: COVER-LINE-DEGENERATE slot=%s lineLen=%.1fcm — endpoint arrows collapsed; forward-direction fix falls back to actor yaw"),
				*Companion->GetName(), *Slot->GetName(), Slot->GetLineLength());
		}
	}

	if (bDebugLogging)
	{
		const float Distance = FVector::Dist(Companion->GetActorLocation(), Target->GetActorLocation());
		UE_LOG(LogCompanionAI, Log, TEXT("%s: TASK ENTER target=%s dist=%.0f hasCover=%d slot=%s"),
			*Companion->GetName(), *Target->GetName(), Distance, (int32)bHasCover, *GetNameSafe(Slot));

		if (bHasCover && IsValid(Slot))
		{
			const FVector SlotLoc = Slot->GetLocationAtAlpha(CurrentAlpha);
			const float DistToSlot = FVector::Dist(Companion->GetActorLocation(), SlotLoc);
			UE_LOG(LogCompanionDiag, Log, TEXT("%s: TASK-ENTER target=%s dist=%.0f hasCover=%d slot=%s alpha=%.2f distToSlot=%.1f"),
				*Companion->GetName(), *Target->GetName(), Distance, 1, *GetNameSafe(Slot), CurrentAlpha, DistToSlot);
		}
		else
		{
			UE_LOG(LogCompanionDiag, Log, TEXT("%s: TASK-ENTER target=%s dist=%.0f hasCover=0 slot=None alpha=-1 distToSlot=-1"),
				*Companion->GetName(), *Target->GetName(), Distance);
		}
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

		// Stall detection: accumulate time spent Idle without having arrived.
		EPathFollowingStatus::Type PFStatusEnum = EPathFollowingStatus::Idle;
		if (AAIController* PFAIC = Cast<AAIController>(Ctx.Companion->GetController()))
		{
			if (UPathFollowingComponent* PF = PFAIC->GetPathFollowingComponent())
				PFStatusEnum = PF->GetStatus();
		}
		if (!bArrived && PFStatusEnum == EPathFollowingStatus::Idle)
			FinalApproachStalledTime += DeltaSeconds;
		else
			FinalApproachStalledTime = 0.f;
		const bool bStalled = FinalApproachStalledTime >= FinalApproachStalledGracePeriod;

		if (bDebugLogging)
		{
			const float Delta = LastFinalApproachDist - Dist;
			LastFinalApproachDist = Dist;
			const float Vel = Ctx.Companion->GetVelocity().Size();
			const TCHAR* PFStatus = (PFStatusEnum == EPathFollowingStatus::Moving)  ? TEXT("Moving")
				: (PFStatusEnum == EPathFollowingStatus::Idle)    ? TEXT("Idle")
				: (PFStatusEnum == EPathFollowingStatus::Waiting) ? TEXT("Waiting")
				:                                                    TEXT("Paused");
			UE_LOG(LogCompanionDiag, VeryVerbose, TEXT("%s: FINALAPPROACH-WATCH elapsed=%.2f dist=%.0f delta=%.1f vel=%.1f pathStatus=%s stalled=%.2f"),
				*Ctx.Companion->GetName(), FinalApproachElapsed, Dist, Delta, Vel, PFStatus, FinalApproachStalledTime);
		}

		if (!bArrived && !bTimedOut && !bStalled)
			return;

		if (AAIController* AIC = Cast<AAIController>(Ctx.Companion->GetController()))
			AIC->StopMovement();
		if (UCharacterMovementComponent* CMC = Ctx.Companion->GetCharacterMovement())
			CMC->StopMovementImmediately();

		AAICoverSlot* ApproachSlot = Cast<AAICoverSlot>(Ctx.Blackboard->GetValueAsObject(CoverSlotKey.SelectedKeyName));
		if (IsValid(ApproachSlot))
		{
			const FRotator SlotYawRot(0.f, ApproachSlot->GetForwardDirection().Rotation().Yaw, 0.f);
			// Ground-snap: nav-mesh-arrival Z is biased above the floor; trace down to find the real floor.
			FVector SnapLoc(FinalApproachTarget.X, FinalApproachTarget.Y, Ctx.Companion->GetActorLocation().Z);
			if (UWorld* SnapWorld = Ctx.Companion->GetWorld())
			{
				const FVector TraceStart = SnapLoc + FVector(0.f, 0.f, 80.f);
				const FVector TraceEnd = SnapLoc - FVector(0.f, 0.f, 200.f);
				FHitResult GroundHit;
				FCollisionQueryParams Params;
				Params.AddIgnoredActor(Ctx.Companion);
				if (SnapWorld->LineTraceSingleByChannel(GroundHit, TraceStart, TraceEnd, ECC_Visibility, Params))
				{
					const float CapsuleHalfHeight = Ctx.Companion->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
					SnapLoc.Z = GroundHit.ImpactPoint.Z + CapsuleHalfHeight;
				}
			}
			if (bDebugLogging)
			{
				const float WarpDist = FVector::Dist(Ctx.Companion->GetActorLocation(), SnapLoc);
				UE_LOG(LogCompanionDiag, Log, TEXT("%s: FINALAPPROACH-SNAP arrived=%d timedOut=%d stalled=%d elapsed=%.2f dist=%.0f warped=%.0f"),
					*Ctx.Companion->GetName(), (int32)bArrived, (int32)bTimedOut, (int32)bStalled, FinalApproachElapsed, Dist, WarpDist);
			}
			BeginSmoothSnap(Ctx.Companion, SnapLoc, SlotYawRot, ApproachSlot->Height == ECoverHeight::Crouch, TEXT("FinalApproach"));
			if (UCompanionAnimInstance* Anim = GetCompanionAnim(Ctx.Companion))
				Anim->EnterCoverPose(ResolvedPeekSide, ApproachSlot->Height);
		}

		bWaitingForFinalApproach = false;
		FinalApproachElapsed = 0.f;
		LastFinalApproachDist = 0.f;
		FinalApproachStalledTime = 0.f;
		// Fall through to normal branch logic this tick.
	}

	// Slot-loss guard: check before the snap early-return so a slot invalidated mid-warp
	// doesn't let the snap complete and post-snap Crouch() fire before the loss is caught.
	if (bSmoothSnapping)
	{
		AAICoverSlot* PreSnapSlot = Cast<AAICoverSlot>(Ctx.Blackboard->GetValueAsObject(CoverSlotKey.SelectedKeyName));
		if (!IsValid(PreSnapSlot))
		{
			UE_LOG(LogCompanionAI, Warning, TEXT("%s: [SLOT-LOST-MID-SNAP] Slot invalidated during warp — aborting cleanly"), *GetNameSafe(Ctx.Companion));
			// If a reload was deferred for this snap, fire it now so the dry companion
			// isn't stranded clicking-on-empty until a new slot is acquired.
			if (bPendingReloadAfterSnap && IsValid(Ctx.Companion))
			{
				UE_LOG(LogCompanionDiag, Log, TEXT("%s: [ReloadAfterSlotLoss] t=%.3f"),
					*GetNameSafe(Ctx.Companion),
					Ctx.Companion->GetWorld() ? Ctx.Companion->GetWorld()->GetTimeSeconds() : 0.f);
				Ctx.Companion->ReloadWeapon();
			}
			ResetTaskState(Ctx.Companion, Ctx.Blackboard, nullptr, false, false);
			return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		}
	}

	if (bSmoothSnapping)
	{
		if (!TickSmoothSnap(Ctx.Companion, DeltaSeconds))
			return; // snap not complete — pause branch logic this tick
		// Snap just completed — fall through to normal tick.
	}

	const FVector MyLocation = Ctx.Companion->GetActorLocation();
	const FVector TargetLocation = Ctx.Target->GetActorLocation();
	const float Distance = FVector::Dist(MyLocation, TargetLocation);

	AAICoverSlot* Slot = Cast<AAICoverSlot>(Ctx.Blackboard->GetValueAsObject(CoverSlotKey.SelectedKeyName));
	const bool bHasCover = IsValid(Slot) && Ctx.Blackboard->GetValueAsBool(HasCoverPositionKey.SelectedKeyName);

	// Build ignored-actors list once per tick — passed to all HasLineOfSight calls below.
	// TInlineAllocator keeps up to 4 entries on the stack (weapons + accessories — no heap alloc in the common case).
	TArray<AActor*, TInlineAllocator<4>> TickIgnoredAttached;
	Ctx.Companion->ForEachAttachedActors([&TickIgnoredAttached](AActor* A) { TickIgnoredAttached.Add(A); return true; });

	// Detect slot swap without ExecuteTask re-running; reset alpha to midpoint of new slot.
	if (IsValid(Slot) && Slot != LastTickSlot.Get())
	{
		CurrentAlpha = 0.5f;
		BlockedRecheckHits = 0;
		RepositionTargetAlpha.Reset();
		RepositionTargetWorldLoc = FVector::ZeroVector;
		bRepositionStandPhase = false;
		bStandUpRepositionWalking = false;
		bRepositionStartLogged = false;
		CurrentBurstAction = EPeekAction::Hold;
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
			// Leaving OpenEngage (e.g. cover gained): tear down move-and-shoot symmetrically.
			if (LastTickBranch == 2 && CurrentBranch != 2)
				EndOpenAreaMoveShoot(Ctx.Companion);
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

	// Slot-loss guard: cover dropped mid-task while companion was in cover branch.
	// Prevents falling through to open-engage with stale crouch / firing state.
	if (LastTickBranch == 0 && (!bHasCover || !IsValid(Slot)))
	{
		if (Ctx.Companion->bIsCrouched)
		{
			UE_LOG(LogCompanionDiag, Log, TEXT("%s: [CrouchCall] t=%.3f site=SlotLossGuard action=UnCrouch"),
				*GetNameSafe(Ctx.Companion), Ctx.Companion->GetWorld() ? Ctx.Companion->GetWorld()->GetTimeSeconds() : 0.f);
			Ctx.Companion->UnCrouch();
		}
		bIsFiringBurst = false;
		UE_LOG(LogCompanionAI, Warning, TEXT("Slot lost mid-task on companion %s — aborting cleanly"), *GetNameSafe(Ctx.Companion));
		return FinishLatentTask(OwnerComp, EBTNodeResult::Aborted);
	}

	// =========================================================================
	// BRANCH 0: COVER IDLE
	// =========================================================================
	if (bHasCover && !bIsFiringBurst)
	{
		LosBlockedAccum = 0.f;
		TimeInOpenEngageNoCover = 0.f;
		TimeAtCurrentCover += DeltaSeconds;

		// Neither height has a cover idle montage to mask the aim offset — drop the aim target so the
		// companion doesn't visibly track the enemy through the wall while hiding. Restored on peek commit
		// by the per-tick SetAimTarget at the top of TickTask once bIsFiringBurst flips true.
		Ctx.Companion->SetAimTarget(nullptr);

		// Idempotent posture sync — applies on first CoverIdle tick after entry, no-op afterward.
		const bool bShouldCrouch = (Slot->Height == ECoverHeight::Crouch);
		if (bShouldCrouch && !Ctx.Companion->bIsCrouched)
		{
			UE_LOG(LogCompanionDiag, Log, TEXT("%s: [CrouchCall] t=%.3f site=CoverIdlePostureSync action=Crouch"),
				*GetNameSafe(Ctx.Companion), Ctx.Companion->GetWorld() ? Ctx.Companion->GetWorld()->GetTimeSeconds() : 0.f);
			Ctx.Companion->Crouch();
		}
		else if (!bShouldCrouch && Ctx.Companion->bIsCrouched)
		{
			UE_LOG(LogCompanionDiag, Log, TEXT("%s: [CrouchCall] t=%.3f site=CoverIdlePostureSync action=UnCrouch"),
				*GetNameSafe(Ctx.Companion), Ctx.Companion->GetWorld() ? Ctx.Companion->GetWorld()->GetTimeSeconds() : 0.f);
			Ctx.Companion->UnCrouch();
		}

		const bool bRepoActive = (CurrentBurstAction == EPeekAction::Reposition && RepositionTargetAlpha.IsSet());

		// Early Reposition dispatch — a committed Reposition must not wait on the action-roll cooldown gate.
		if (bRepoActive)
		{
			TickRepositionAction(Ctx.Companion, Anim, Slot, bSuppressed, bLowHp, DeltaSeconds);
			return;
		}

#if ENABLE_DRAW_DEBUG
		if (bDebugLogging)
		{
			const FVector DrawLineDir = Slot->GetLineDirection();
			const FVector LeftEdgePt  = Slot->GetLeftEdge()  + FVector(0.f, 0.f, 10.f);
			const FVector RightEdgePt = Slot->GetRightEdge() + FVector(0.f, 0.f, 10.f);
			const FVector CurrentPt   = Slot->GetLocationAtAlpha(CurrentAlpha) + FVector(0.f, 0.f, 10.f);
			DrawDebugSphere(Ctx.Companion->GetWorld(), LeftEdgePt,  14.f, 8, FColor::White, false, 0.f, 0, 1.5f);
			DrawDebugSphere(Ctx.Companion->GetWorld(), RightEdgePt, 14.f, 8, FColor::White, false, 0.f, 0, 1.5f);
			DrawDebugSphere(Ctx.Companion->GetWorld(), CurrentPt,   22.f, 8, FColor::Red,   false, 0.f, 0, 1.5f);
			// Magenta apex markers for peekable endpoint corners.
			if (Slot->bIsPeekableCornerStart)
			{
				const FVector ApexStart = Slot->GetLeftEdge() + FVector(0.f, 0.f, 20.f) + (-DrawLineDir) * CornerPeekStepDistance;
				DrawDebugSphere(Ctx.Companion->GetWorld(), ApexStart, 14.f, 12, FColor::Magenta, false, 0.f, 0, 1.5f);
				DrawDebugLine(Ctx.Companion->GetWorld(), Slot->GetLeftEdge() + FVector(0.f, 0.f, 20.f), ApexStart, FColor::Magenta, false, 0.f, 0, 2.5f);
			}
			if (Slot->bIsPeekableCornerEnd)
			{
				const FVector ApexEnd = Slot->GetRightEdge() + FVector(0.f, 0.f, 20.f) + DrawLineDir * CornerPeekStepDistance;
				DrawDebugSphere(Ctx.Companion->GetWorld(), ApexEnd, 14.f, 12, FColor::Magenta, false, 0.f, 0, 1.5f);
				DrawDebugLine(Ctx.Companion->GetWorld(), Slot->GetRightEdge() + FVector(0.f, 0.f, 20.f), ApexEnd, FColor::Magenta, false, 0.f, 0, 2.5f);
			}
			// Eye-height marker showing where stand-fire LoS trace originates.
			const FVector EyeMarker = Slot->GetLocationAtAlpha(CurrentAlpha) + FVector(0.f, 0.f, StandFireEyeHeight);
			DrawDebugSphere(Ctx.Companion->GetWorld(), EyeMarker, 6.f, 6, FColor::Cyan, false, 0.f, 0, 1.f);
		}
#endif

		const FVector SubSlotLoc = Slot->GetLocationAtAlpha(CurrentAlpha);

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

			// Stand cover: at least one peekable corner apex must be able to see the target.
			if (Slot->Height == ECoverHeight::Stand)
			{
				const FVector LineDir = Slot->GetLineDirection();
				const FVector StartApexEye = Slot->GetLocationAtAlpha(0.f) + (-LineDir) * CornerPeekStepDistance + FVector(0.f, 0.f, StandFireEyeHeight);
				const FVector EndApexEye   = Slot->GetLocationAtAlpha(1.f) + ( LineDir) * CornerPeekStepDistance + FVector(0.f, 0.f, StandFireEyeHeight);
				AActor* IgnoredBlocker = nullptr;
				const bool bStartLos = Slot->bIsPeekableCornerStart && HasLineOfSight(Ctx.Companion->GetWorld(), StartApexEye, Ctx.Target, Ctx.Companion, IgnoredBlocker, TickIgnoredAttached);
				const bool bEndLos   = Slot->bIsPeekableCornerEnd   && HasLineOfSight(Ctx.Companion->GetWorld(), EndApexEye,   Ctx.Target, Ctx.Companion, IgnoredBlocker, TickIgnoredAttached);
				if (!bStartLos && !bEndLos)
				{
					UE_LOG(LogCompanionAI, Log, TEXT("%s: Cover INVALIDATE reason=no-corner-apex-los slot=%s"), *Ctx.Companion->GetName(), *Slot->GetName());
					if (Slot->IsClaimedBy(Ctx.Companion)) Slot->Release(Ctx.Companion);
					Ctx.Blackboard->SetValueAsObject(CoverSlotKey.SelectedKeyName, nullptr);
					Ctx.Blackboard->SetValueAsBool(HasCoverPositionKey.SelectedKeyName, false);
					return FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
				}
			}
		}

		TimeInCoverIdle += DeltaSeconds;
		if (TimeInCoverIdle < MinCoverIdleDwell + PeekCooldown) return;

		UWorld* const TickWorld = Ctx.Companion ? Ctx.Companion->GetWorld() : nullptr;
		if (!TickWorld) return;
		const float Now = TickWorld->GetTimeSeconds();

		// Watchdog: companion has been in cover-idle with no peek decision for too long.
		// Clears all latching state and forces a fresh decision roll.
		if (LastDecisionTime > 0.f && (Now - LastDecisionTime) > CoverIdleWatchdogSeconds)
		{
			UE_LOG(LogCompanionAI, Warning, TEXT("COVER-IDLE-WATCHDOG-FIRED on companion %s — force re-roll"), *GetNameSafe(Ctx.Companion));
			bIsFiringBurst = false;
			CurrentBurstAction = EPeekAction::Hold;
			RepositionTargetAlpha.Reset();
			bRepositionStandPhase = false;
			ConsecutiveHolds = 0;
			PeekCooldown = 0.f;
			TimeInCoverIdle = 0.f;
			bReloadGateActive = false;
			ReloadGateStartTime = 0.f;
			LastDecisionTime = Now;
		}

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

		// Gate 2: LoS from stand eye (or apex for stand-cover corners).
		AActor* BlockedBy = nullptr;
		const FVector StandEye = SubSlotLoc + FVector(0.f, 0.f, StandFireEyeHeight);
		bool bLosFromCover = HasLineOfSight(Ctx.Companion->GetWorld(), StandEye, Ctx.Target, Ctx.Companion, BlockedBy, TickIgnoredAttached);

		// Stand cover at a peekable corner can never see over the wall — check the would-be CornerPeek apex instead.
		if (!bLosFromCover && Slot->Height == ECoverHeight::Stand && Slot->IsAlphaAtPeekableCorner(CurrentAlpha))
		{
			const FVector LineDir = Slot->GetLineDirection();
			const FVector StrafeDir = (CurrentAlpha < 0.5f) ? -LineDir : LineDir;
			const FVector ApexLoc = SubSlotLoc + StrafeDir * CornerPeekStepDistance;
			const FVector ApexEye = ApexLoc + FVector(0.f, 0.f, StandFireEyeHeight);
			AActor* ApexBlocker = nullptr;
			bLosFromCover = HasLineOfSight(Ctx.Companion->GetWorld(), ApexEye, Ctx.Target, Ctx.Companion, ApexBlocker, TickIgnoredAttached);
			if (bLosFromCover)
				BlockedBy = nullptr;
		}
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
			if (SubSlotLosRecheckTimer <= 0.f && !bSuppressed && Slot->GetLineLength() >= 50.f)
			{
				SubSlotLosRecheckTimer = SubSlotLosRecheckInterval;
				BlockedRecheckHits = FMath::Min<uint8>(BlockedRecheckHits + 1, 255);

				// Require 2 consecutive gated blocked checks before teleporting (anti-thrash).
				if (BlockedRecheckHits >= 2)
				{
					const TOptional<float> BestAlpha = PickBestAlphaByLos(Slot, Ctx.Target, Ctx.Companion, CurrentAlpha, TickIgnoredAttached);
					if (BestAlpha.IsSet())
					{
						const float PrevAlpha = CurrentAlpha;
						CurrentAlpha = *BestAlpha;
						const FVector NewSubSlotLoc = Slot->GetLocationAtAlpha(CurrentAlpha);
						const FRotator SlotYawRot(0.f, Slot->GetForwardDirection().Rotation().Yaw, 0.f);
						const FVector TeleportDest(NewSubSlotLoc.X, NewSubSlotLoc.Y, Ctx.Companion->GetActorLocation().Z);
						if (bDebugLogging) UE_LOG(LogCompanionDiag, Log, TEXT("%s: MID-COMBAT-ALPHA-TELEPORT fromAlpha=%.2f toAlpha=%.2f dist=%.0f isReloading=%d"),
							*Ctx.Companion->GetName(), PrevAlpha, CurrentAlpha, FVector::Dist(Ctx.Companion->GetActorLocation(), TeleportDest), (int32)Ctx.Companion->IsReloading());
						Ctx.Companion->TeleportTo(TeleportDest, SlotYawRot, false, false);
						LastPeekResolveCoverLoc = FVector::ZeroVector;
						LastPeekResolveTargetLoc = FVector::ZeroVector;
						BlockedRecheckHits = 0;
						if (Slot->Height == ECoverHeight::Crouch)
						{
							UE_LOG(LogCompanionDiag, Log, TEXT("%s: [CrouchCall] t=%.3f site=SubSlotTeleport action=Crouch"),
								*GetNameSafe(Ctx.Companion), Ctx.Companion->GetWorld() ? Ctx.Companion->GetWorld()->GetTimeSeconds() : 0.f);
							Ctx.Companion->Crouch();
						}
						else
						{
							UE_LOG(LogCompanionDiag, Log, TEXT("%s: [CrouchCall] t=%.3f site=SubSlotTeleport action=UnCrouch"),
								*GetNameSafe(Ctx.Companion), Ctx.Companion->GetWorld() ? Ctx.Companion->GetWorld()->GetTimeSeconds() : 0.f);
							Ctx.Companion->UnCrouch();
						}
						if (Anim) Anim->EnterCoverPose(ResolvedPeekSide, Slot->Height);
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
			if (bDebugLogging)
			{
				AWeaponBase* W = Ctx.Companion->GetCurrentWeapon();
				const float ReloadTime = (W && W->GetWeaponData()) ? W->GetWeaponData()->ReloadTime : -1.f;
				UE_LOG(LogCompanionDiag, Log, TEXT("%s: RELOAD-START gate=697 ammo=%d/%d reserve=%d vel=%.1f hasCover=%d slot=%s reloadTime=%.2f"),
					*Ctx.Companion->GetName(),
					W ? W->GetCurrentAmmo() : -1,
					W && W->GetWeaponData() ? W->GetWeaponData()->MagazineSize : -1,
					W ? W->GetReserveAmmo() : -1,
					Ctx.Companion->GetVelocity().Size(),
					(int32)bHasCover,
					*GetNameSafe(Slot),
					ReloadTime);
				UE_LOG(LogCompanionAI, Log, TEXT("%s: reloading before stand-up"), *Ctx.Companion->GetName());
			}
			Ctx.Companion->ReloadWeapon();
			return;
		}
		if (Ctx.Companion->IsReloading()) return;

		// StandUpAndReposition always runs in BRANCH 1 (bIsFiringBurst=true throughout).
		// No silent-walk fallback needed here.

		// Gate 4: slot supports some form of peek (crouch fires over, stand corner-peeks).
		// Stand-cover slots with no peekable corners are unusable for combat.
		const bool bIsCrouchCover = (Slot->Height == ECoverHeight::Crouch);
		const bool bAnyPeekableCorner = Slot->bIsPeekableCornerStart || Slot->bIsPeekableCornerEnd;
		if (!bIsCrouchCover && !Slot->CanStandFireOver() && !bAnyPeekableCorner)
		{
			if (bDebugLogging)
				UE_LOG(LogCompanionAI, Log, TEXT("%s: Cover REJECT slot=%s is Stand height with no peekable corners — slot unusable for combat"),
					*Ctx.Companion->GetName(), *Slot->GetName());
			return;
		}

		// Pre-peek ammo gate: never expose without enough ammo for a useful burst.
		if (TryPrePeekReloadGate(Ctx.Companion, Slot))
		{
			TimeInCoverIdle = 0.f;
			return;
		}

		// Gate 5: roll peek action.
		const bool bAtPeekableEndpoint = Slot->IsAlphaAtPeekableCorner(CurrentAlpha);
		const bool bLineLongEnough = (Slot->GetLineLength() >= 50.f);

		UE_LOG(LogCompanionAI, Log,
			TEXT("%s: PEEK-DECISION slot=%s height=%s alpha=%.2f lineLen=%.0f atPeekableCorner=%d -> weightsPath=%s"),
			*Ctx.Companion->GetName(), *Slot->GetName(),
			bIsCrouchCover ? TEXT("Crouch") : TEXT("Stand"),
			CurrentAlpha, Slot->GetLineLength(),
			(int32)bAtPeekableEndpoint,
			bIsCrouchCover ? TEXT("Crouch") : (bAtPeekableEndpoint ? TEXT("StandEndpoint") : TEXT("StandMidpoint-NoFire")));

		EPeekAction Action = EPeekAction::Hold;
		if (bIsCrouchCover)
		{
			// Disable Reposition/StandUpAndReposition when line is too short, or when we just repositioned.
			const float RepoW        = (bLineLongEnough && !bJustRepositioned) ? (bLowHp ? LowHpRepositionWeight            : RepositionWeight)           : 0.f;
			const float StandUpRepoW = (bLineLongEnough && !bJustRepositioned) ? (bLowHp ? LowHpStandUpAndRepositionWeight  : StandUpAndRepositionWeight)  : 0.f;
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
				{ EPeekAction::CornerPeek,  bLowHp ? LowHpCornerPeekWeight      : CornerPeekWeight },
				{ EPeekAction::Reposition,  (bLineLongEnough && !bJustRepositioned) ? (bLowHp ? LowHpRepositionWeightStand : RepositionWeightStand) : 0.f },
				{ EPeekAction::Hold,        bLowHp ? LowHpHoldWeight            : HoldWeight },
			};
			Action = RollPeekActionMulti(MakeArrayView(EndpointWeights));
		}
		else
		{
			// Stand cover, midpoint alpha — no fire option.
			const TPair<EPeekAction, float> MidpointWeights[] = {
				{ EPeekAction::Reposition,  (bLineLongEnough && !bJustRepositioned) ? (bLowHp ? LowHpRepositionWeightStand : RepositionWeightStand) : 0.f },
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
				UE_LOG(LogCompanionAI, Log, TEXT("%s: PEEK-ACTION=Hold ammo=%d"), *GetNameSafe(Ctx.Companion), Ctx.Companion->GetCurrentAmmo());
				if (bDebugLogging) UE_LOG(LogCompanionAI, Log, TEXT("%s: HOLD this cycle (%d/%d)"), *Ctx.Companion->GetName(), ConsecutiveHolds, MaxConsecutiveHolds);
				return;
			}
			// Hold cap reached — promote based on cover type and position.
			if (bIsCrouchCover)
			{
				Action = EPeekAction::Stand;
			}
			else if (bAtPeekableEndpoint)
			{
				Action = EPeekAction::CornerPeek;
			}
			else
			{
				// Stand cover midpoint has no fire action — stay Hold, but force early return to avoid Stand/Quick fallthrough.
				++ConsecutiveHolds;
				PeekCooldown = FMath::RandRange(MinPeekCooldown, MaxPeekCooldown);
				TimeInCoverIdle = 0.f;
				return;
			}
		}
		else
		{
			ConsecutiveHolds = 0;
			if (Action != EPeekAction::Reposition && Action != EPeekAction::StandUpAndReposition)
				bJustRepositioned = false;
		}

		// --- Commit to action ---

		if (Action == EPeekAction::Reposition || Action == EPeekAction::StandUpAndReposition)
		{
			// Block within-slot reposition if the player is overlapping this cover slot.
			AActor* Player = Cast<AActor>(Ctx.Blackboard->GetValueAsObject(ACompanionAIController::BB_PlayerActor));
			if (IsValid(Player) && Slot->IsLocationOverlappingCoverLine(Player->GetActorLocation()))
			{
				CurrentBurstAction = EPeekAction::Hold;
				++ConsecutiveHolds;
				PeekCooldown = FMath::RandRange(MinPeekCooldown, MaxPeekCooldown);
				TimeInCoverIdle = 0.f;
				UE_LOG(LogCompanionAI, Log, TEXT("%s: PEEK-ACTION=Hold (player-overlap-cover) alpha=%.2f"), *GetNameSafe(Ctx.Companion), CurrentAlpha);
				return;
			}

			float NewAlpha;

			if (Slot->Height == ECoverHeight::Stand)
			{
				// Stand cover: jump to the opposite peekable corner if one exists.
				const bool bStartPeekable = Slot->bIsPeekableCornerStart;
				const bool bEndPeekable = Slot->bIsPeekableCornerEnd;
				const bool bAtStart = (CurrentAlpha < 0.5f);
				const bool bOppositeAvailable = bAtStart ? bEndPeekable : bStartPeekable;
				if (!bOppositeAvailable)
				{
					// Only this corner is peekable — no Stand-cover reposition possible.
					CurrentBurstAction = EPeekAction::Hold;
					++ConsecutiveHolds;
					PeekCooldown = FMath::RandRange(MinPeekCooldown, MaxPeekCooldown);
					TimeInCoverIdle = 0.f;
					return;
				}
				NewAlpha = bAtStart ? 1.f : 0.f;
			}
			else
			{
				// Crouch cover: room-aware picker.
				// Only sides with at least RepositionAlphaMin available are eligible.
				const float RoomRight = 1.f - CurrentAlpha;  // headroom toward Alpha=1
				const float RoomLeft  = CurrentAlpha;        // headroom toward Alpha=0
				const bool bRightOk = RoomRight >= RepositionAlphaMin;
				const bool bLeftOk  = RoomLeft  >= RepositionAlphaMin;

				if (!bRightOk && !bLeftOk)
				{
					// No side has enough room — line too narrow from this Alpha. Treat as Hold.
					CurrentBurstAction = EPeekAction::Hold;
					++ConsecutiveHolds;
					PeekCooldown = FMath::RandRange(MinPeekCooldown, MaxPeekCooldown);
					TimeInCoverIdle = 0.f;
					bJustRepositioned = true;
					return;
				}

				// Pick direction: random if both sides ok, forced otherwise.
				const float PickedSign = (bRightOk && bLeftOk)
					? (FMath::RandBool() ? 1.f : -1.f)
					: (bRightOk ? 1.f : -1.f);

				// Clamp the random magnitude to the available room on the chosen side so the
				// achieved delta is always at least RepositionAlphaMin.
				const float MaxRoom = (PickedSign > 0.f) ? RoomRight : RoomLeft;
				const float DeltaMag = FMath::RandRange(RepositionAlphaMin, FMath::Min(RepositionAlphaMax, MaxRoom));
				NewAlpha = FMath::Clamp(CurrentAlpha + (PickedSign * DeltaMag), 0.f, 1.f);
			}

			RepositionTargetAlpha = NewAlpha;
			RepositionTargetWorldLoc = Slot->GetLocationAtAlpha(NewAlpha);
			CachedSlotForwardYaw = Slot->GetForwardDirection().Rotation().Yaw;

			if (Action == EPeekAction::Reposition)
			{
				// Silent reposition: stay crouched, no montage, no aim, low-ready weapon.
				Ctx.Companion->StopWeaponFire();
				Ctx.Companion->SetAimTarget(nullptr);
				Ctx.Companion->SetLowReadyAim(true);
				if (AAIController* AIC = Cast<AAIController>(Ctx.Companion->GetController()))
					AIC->StopMovement();
				if (UCharacterMovementComponent* CMC = Ctx.Companion->GetCharacterMovement())
					CMC->StopMovementImmediately();
				// Stay crouched — do NOT UnCrouch here.
				UE_LOG(LogCompanionAI, Log, TEXT("%s: PEEK-ACTION=Reposition-Silent ammo=%d"), *GetNameSafe(Ctx.Companion), Ctx.Companion->GetCurrentAmmo());
				CurrentBurstAction = EPeekAction::Reposition;
				LastDecisionTime = Now;
				TimeInCoverIdle = 0.f;
				LastRepositionDist = FVector::Dist(Ctx.Companion->GetActorLocation(), RepositionTargetWorldLoc);
				RepositionElapsed = 0.f;
				RepositionStalledTime = 0.f;
				bRepositionStartLogged = true;
				if (bDebugLogging)
				{
					UE_LOG(LogCompanionAI, Log, TEXT("%s: REPOSITION alpha %.2f -> %.2f"), *Ctx.Companion->GetName(), CurrentAlpha, *RepositionTargetAlpha);
					UE_LOG(LogCompanionDiag, Log, TEXT("%s: REPOSITION-START kind=silent fromAlpha=%.2f toAlpha=%.2f fromLoc=%s toLoc=%s dist=%.0f isReloading=%d"),
						*Ctx.Companion->GetName(), CurrentAlpha, *RepositionTargetAlpha,
						*Ctx.Companion->GetActorLocation().ToString(), *RepositionTargetWorldLoc.ToString(),
						LastRepositionDist, (int32)Ctx.Companion->IsReloading());
				}
				return;
			}

			// StandUpAndReposition: stand up, start burst in place (Phase A), then walk-and-fire (Phase B).
			UE_LOG(LogCompanionAI, Log, TEXT("%s: PEEK-ACTION=StandUpAndReposition ammo=%d"), *GetNameSafe(Ctx.Companion), Ctx.Companion->GetCurrentAmmo());
			CurrentBurstAction = EPeekAction::StandUpAndReposition;
			LastDecisionTime = Now;
			bRepositionStandPhase = true;
			bStandUpRepositionWalking = false;
			BurstTimer = FMath::RandRange(MinFireBurst, MaxFireBurst);
			bIsFiringBurst = true;
			if (AAIController* AIC = Cast<AAIController>(Ctx.Companion->GetController()))
				AIC->StopMovement();
			if (UCharacterMovementComponent* CMC = Ctx.Companion->GetCharacterMovement())
				CMC->StopMovementImmediately();
			UE_LOG(LogCompanionDiag, Log, TEXT("%s: [CrouchCall] t=%.3f site=StandUpRepoCommit action=UnCrouch"),
				*GetNameSafe(Ctx.Companion), Ctx.Companion->GetWorld() ? Ctx.Companion->GetWorld()->GetTimeSeconds() : 0.f);
			Ctx.Companion->UnCrouch();
			if (Anim)
			{
				Anim->ExitCoverPose();
				ActivePeekMontage = Anim->PlayPeekFire(ResolvedPeekSide);
			}
			Ctx.Companion->StartWeaponFire();
			DebugBurstLosCheckTimer = 0.f;
			TimeInCoverIdle = 0.f;
			TimeAtCurrentCover = 0.f;
			CoverValidityCheckTimer = 0.f;
			LastRepositionDist = 0.f;
			RepositionElapsed = 0.f;
			RepositionStalledTime = 0.f;
			bRepositionStartLogged = true;
			if (bDebugLogging)
			{
				UE_LOG(LogCompanionAI, Log, TEXT("%s: STAND-UP-FIRE start action=StandReposition burst=%.2fs side=%s slot=%s"),
					*Ctx.Companion->GetName(), BurstTimer,
					ResolvedPeekSide == EPeekSide::Right ? TEXT("Right") : TEXT("Left"),
					*Slot->GetName());
				UE_LOG(LogCompanionAI, Log, TEXT("%s: STANDUP-REPOSITION alpha %.2f -> %.2f burst=%.2fs"), *Ctx.Companion->GetName(), CurrentAlpha, *RepositionTargetAlpha, BurstTimer);
				UE_LOG(LogCompanionDiag, Log, TEXT("%s: REPOSITION-START kind=standup fromAlpha=%.2f toAlpha=%.2f fromLoc=%s toLoc=%s dist=%.0f isReloading=%d"),
					*Ctx.Companion->GetName(), CurrentAlpha, *RepositionTargetAlpha,
					*Ctx.Companion->GetActorLocation().ToString(), *RepositionTargetWorldLoc.ToString(),
					LastRepositionDist, (int32)Ctx.Companion->IsReloading());
			}
			return;
		}

		if (Action == EPeekAction::CornerPeek)
		{
			const FVector StrafeDir = (CurrentAlpha < 0.5f) ? -Slot->GetLineDirection() : Slot->GetLineDirection();
			CornerPeekHomeLocation = SubSlotLoc;
			// CornerPeek apex captured at commit — assumes slot doesn't move mid-action.
			CornerPeekApexLocation = CornerPeekHomeLocation + StrafeDir * CornerPeekStepDistance;
			CurrentBurstAction = EPeekAction::CornerPeek;
			LastDecisionTime = Now;
			bCornerPeekReturning = false;
			bIsFiringBurst = true;
			BurstTimer = FMath::RandRange(MinFireBurst, MaxFireBurst);
			if (Anim)
			{
				Anim->ExitCoverPose();
				ActivePeekMontage = Anim->PlayPeekFire(ResolvedPeekSide);
			}
			UE_LOG(LogCompanionAI, Warning,
				TEXT("%s: PEEK-ACTION=CornerPeek slot=%s alpha=%.2f home=%s apex=%s stepDist=%.0f burst=%.2fs ammo=%d"),
				*GetNameSafe(Ctx.Companion), *Slot->GetName(),
				CurrentAlpha, *CornerPeekHomeLocation.ToString(), *CornerPeekApexLocation.ToString(),
				CornerPeekStepDistance, BurstTimer, Ctx.Companion->GetCurrentAmmo());
#if ENABLE_DRAW_DEBUG
			DrawDebugLine(Ctx.Companion->GetWorld(), CornerPeekHomeLocation + FVector(0, 0, 20.f),
				CornerPeekApexLocation + FVector(0, 0, 20.f), FColor::Magenta, false, 3.f, 0, 4.f);
			DrawDebugSphere(Ctx.Companion->GetWorld(), CornerPeekApexLocation + FVector(0, 0, 20.f),
				18.f, 12, FColor::Magenta, false, 3.f, 0, 2.f);
#endif
			DebugBurstLosCheckTimer = 0.f;
			CornerPeekLosCheckTimer = 0.f;
			TimeInCoverIdle = 0.f;
			TimeAtCurrentCover = 0.f;
			CoverValidityCheckTimer = 0.f;
			if (bDebugLogging)
				UE_LOG(LogCompanionAI, Log, TEXT("%s: CORNER-PEEK alpha=%.2f apex=(%.0f,%.0f,%.0f) burst=%.2fs"), *Ctx.Companion->GetName(), CurrentAlpha, CornerPeekApexLocation.X, CornerPeekApexLocation.Y, CornerPeekApexLocation.Z, BurstTimer);
			return;
		}

		// Safety net: Hold must never reach the fire path.
		if (Action == EPeekAction::Hold)
		{
			UE_LOG(LogCompanionAI, Warning, TEXT("%s: PEEK-HOLD-FALLTHROUGH-GUARD — action still Hold at fire path, blocking"), *GetNameSafe(Ctx.Companion));
			return;
		}

		// Stand or Quick — existing fire flow.
		UE_LOG(LogCompanionAI, Log, TEXT("%s: PEEK-ACTION=%s ammo=%d"), *GetNameSafe(Ctx.Companion),
			(Action == EPeekAction::Quick) ? TEXT("Quick") : TEXT("Stand"),
			Ctx.Companion->GetCurrentAmmo());
		CurrentBurstAction = Action;
		LastDecisionTime = Now;
		BurstTimer = (CurrentBurstAction == EPeekAction::Quick)
			? FMath::RandRange(MinQuickPeekBurst, MaxQuickPeekBurst)
			: FMath::RandRange(MinFireBurst, MaxFireBurst);

		if (Anim)
		{
			Anim->ExitCoverPose();
			UAnimMontage* PeekMontage = Anim->PlayPeekFire(ResolvedPeekSide);
			ActivePeekMontage = PeekMontage;
		}

		UE_LOG(LogCompanionDiag, Log, TEXT("%s: [CrouchCall] t=%.3f site=StandQuickPeekCommit action=UnCrouch"),
			*GetNameSafe(Ctx.Companion), Ctx.Companion->GetWorld() ? Ctx.Companion->GetWorld()->GetTimeSeconds() : 0.f);
		Ctx.Companion->UnCrouch();
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
		TimeInOpenEngageNoCover = 0.f;
		BurstTimer -= DeltaSeconds;

		if (bDebugLogging)
		{
			DebugBurstLosCheckTimer -= DeltaSeconds;
			if (DebugBurstLosCheckTimer <= 0.f)
			{
				DebugBurstLosCheckTimer = 0.2f;
				AActor* BurstBlocker = nullptr;
				const bool bBurstLos = HasLineOfSight(Ctx.Companion->GetWorld(), Ctx.Companion->GetPawnViewLocation(), Ctx.Target, Ctx.Companion, BurstBlocker, TickIgnoredAttached);
				const bool bNowBlocked = !bBurstLos;
				const bool bBlockStateChanged = (bNowBlocked != bLastLosBlocked) || (BurstBlocker != LastLosBlocker.Get());
				if (bBlockStateChanged)
				{
					if (bNowBlocked)
					{
						UE_LOG(LogCompanionAI, Log, TEXT("%s: stand-burst LoS BLOCKED by %s"), *Ctx.Companion->GetName(), *GetNameSafe(BurstBlocker));
						if (UE_LOG_ACTIVE(LogCompanionDiag, Log))
						{
							if (IsValid(Ctx.Target))
							{
								if (AWeaponBase* W = Ctx.Companion->GetCurrentWeapon())
								{
									const FVector MuzzleLoc = W->GetMuzzleLocation();
									FHitResult MuzzleHit;
									FCollisionQueryParams MuzzleParams;
									MuzzleParams.AddIgnoredActor(Ctx.Companion);
									MuzzleParams.AddIgnoredActor(W);
									Ctx.Companion->GetWorld()->LineTraceSingleByChannel(MuzzleHit, MuzzleLoc, Ctx.Target->GetActorLocation(), ECC_Visibility, MuzzleParams);
									const bool bMuzzleClear = !MuzzleHit.bBlockingHit || (MuzzleHit.GetActor() == Ctx.Target);
									if (bMuzzleClear)
									{
										UE_LOG(LogCompanionDiag, Log,
											TEXT("%s: STAND-UP-FIRE-LOS-MISMATCH peekBlockedBy=%s muzzleLoc=%s muzzleClear=1"),
											*Ctx.Companion->GetName(), *GetNameSafe(BurstBlocker), *MuzzleLoc.ToString());
									}
								}
							}
						}
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

		// Phase B of StandUpAndReposition: face slot forward so lateral strafe is pure ±90° relative
		// to actor. Upper-body aim offset handles the visual tracking of the enemy.
		const bool bUseSlotForward = (CurrentBurstAction == EPeekAction::StandUpAndReposition
			&& bStandUpRepositionWalking && IsValid(Slot));
		const FRotator LookAtRot = (TargetLocation - MyLocation).Rotation();
		const FRotator DesiredRot = bUseSlotForward
			? FRotator(0.f, CachedSlotForwardYaw, 0.f)
			: FRotator(0.f, LookAtRot.Yaw, 0.f);
		Ctx.Companion->SetActorRotation(FMath::RInterpTo(Ctx.Companion->GetActorRotation(),
			DesiredRot, DeltaSeconds, Ctx.Companion->RotationInterpSpeed));

		// Dispatch new multi-phase actions before the shared burst logic.
		if (CurrentBurstAction == EPeekAction::StandUpAndReposition && RepositionTargetAlpha.IsSet())
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
			if (bDebugLogging)
			{
				AWeaponBase* W = Ctx.Companion->GetCurrentWeapon();
				const float ReloadTime = (W && W->GetWeaponData()) ? W->GetWeaponData()->ReloadTime : -1.f;
				UE_LOG(LogCompanionDiag, Log, TEXT("%s: RELOAD-START gate=945 ammo=%d/%d reserve=%d vel=%.1f hasCover=%d slot=%s reloadTime=%.2f"),
					*Ctx.Companion->GetName(),
					W ? W->GetCurrentAmmo() : -1,
					W && W->GetWeaponData() ? W->GetWeaponData()->MagazineSize : -1,
					W ? W->GetReserveAmmo() : -1,
					Ctx.Companion->GetVelocity().Size(),
					(int32)bHasCover,
					*GetNameSafe(Slot),
					ReloadTime);
				const FVector B1ReloadLoc = IsValid(Slot) ? Slot->GetLocationAtAlpha(CurrentAlpha) : FVector::ZeroVector;
				UE_LOG(LogCompanionDiag, Log, TEXT("%s: RETURN-TO-COVER reason=reload moveTarget=%s dist=%.0f isReloading=%d"),
					*Ctx.Companion->GetName(), *B1ReloadLoc.ToString(),
					FVector::Dist(Ctx.Companion->GetActorLocation(), B1ReloadLoc),
					(int32)Ctx.Companion->IsReloading());
				UE_LOG(LogCompanionAI, Log, TEXT("%s: FIRE STOP reason=reload"), *Ctx.Companion->GetName());
			}
			// Only defer the reload if we're actually returning to crouch cover (where the
			// capsule resize would otherwise pop mid-reload-anim). Stand cover has no resize.
			if (IsValid(Slot) && Slot->Height == ECoverHeight::Crouch)
			{
				bPendingReloadAfterSnap = true;
			}
			else
			{
				Ctx.Companion->ReloadWeapon();
			}
			ReturnToCover(Ctx.Companion, Anim, Slot, false, bLowHp);
			return;
		}

		// Burst elapsed — return to cover.
		if (BurstTimer <= 0.f)
		{
			if (bDebugLogging)
			{
				const FVector B1BurstEndLoc = IsValid(Slot) ? Slot->GetLocationAtAlpha(CurrentAlpha) : FVector::ZeroVector;
				UE_LOG(LogCompanionDiag, Log, TEXT("%s: RETURN-TO-COVER reason=burst-end moveTarget=%s dist=%.0f isReloading=%d"),
					*Ctx.Companion->GetName(), *B1BurstEndLoc.ToString(),
					FVector::Dist(Ctx.Companion->GetActorLocation(), B1BurstEndLoc),
					(int32)Ctx.Companion->IsReloading());
				UE_LOG(LogCompanionAI, Log, TEXT("%s: FIRE STOP reason=burst-end-cover (next cd=%.2fs)"), *Ctx.Companion->GetName(), PeekCooldown);
			}
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
		UE_LOG(LogCompanionDiag, Log, TEXT("%s: [CrouchCall] t=%.3f site=CoverFlippedMidBurst action=UnCrouch"),
			*GetNameSafe(Ctx.Companion), Ctx.Companion->GetWorld() ? Ctx.Companion->GetWorld()->GetTimeSeconds() : 0.f);
		Ctx.Companion->UnCrouch();
		bCornerPeekFiring = false;
		bCornerPeekReturning = false;
		CornerPeekHomeLocation = FVector::ZeroVector;
		CornerPeekApexLocation = FVector::ZeroVector;
		CurrentBurstAction = EPeekAction::Hold;
		RepositionTargetAlpha.Reset();
		bRepositionStandPhase = false;
		bStandUpRepositionWalking = false;
		bRepositionStartLogged = false;
	}

	// Open-engage: defensively UnCrouch in case MoveToCover anticipated a crouch slot but ended
	// without arrival, leaving the companion crouched while shooting in the open.
	// Idempotent — UnCrouch is a no-op if not crouched.
	if (Ctx.Companion->bIsCrouched)
	{
		UE_LOG(LogCompanionDiag, Log, TEXT("%s: [CrouchCall] t=%.3f site=OpenEngageDefensive action=UnCrouch"),
			*GetNameSafe(Ctx.Companion), Ctx.Companion->GetWorld() ? Ctx.Companion->GetWorld()->GetTimeSeconds() : 0.f);
		Ctx.Companion->UnCrouch();
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
		// Trace from the eyeline (GetPawnViewLocation ~= head height), not the actor centre — the lowered-barrel
		// height falsely reports blocked LoS against elevated enemies / low geometry.
		const FVector AimOrigin = Ctx.Companion->GetPawnViewLocation();
		Ctx.Companion->GetWorld()->LineTraceSingleByChannel(LosHit, AimOrigin, TargetLocation, ECC_Visibility, QueryParams);
	}
	const bool bLineOfSight = (!LosHit.bBlockingHit) || (LosHit.GetActor() == Ctx.Target);

	bool bPlayerTooFar = false;
	{
		APawn* LeashPlayer = nullptr;
		float LeashDist = DefaultCombatPlayerLeash;
		float StopDist = DefaultPlayerPullStopDist;
		if (ACompanionAIController* CompAIC = Cast<ACompanionAIController>(Ctx.Companion->GetController()))
		{
			LeashPlayer = CompAIC->GetPlayerCharacter();
			if (const UCompanionTuningDataAsset* T = CompAIC->GetTuning())
			{
				LeashDist = T->SprintDistanceThreshold;
				StopDist = T->AcceptableRadius;
			}
		}
		if (IsValid(LeashPlayer))
		{
			const float CurDist = FVector::Dist(MyLocation, LeashPlayer->GetActorLocation());
			// Dead-band: release only once the companion is well back inside the leash radius.
			// Floor to LeashDist * 0.5 guards against a misconfig where AcceptableRadius >= LeashDist.
			const float ReleaseDist = FMath::Max(LeashDist * 0.5f, LeashDist - StopDist);
			if (bPlayerPullLatched)
				bPlayerPullLatched = (CurDist > ReleaseDist);   // release once well back inside
			else
				bPlayerPullLatched = (CurDist > LeashDist);      // trip when crossing the leash
		}
		else
		{
			bPlayerPullLatched = false; // no valid player — never pull
		}
		bPlayerTooFar = bPlayerPullLatched;
	}

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

	// Cover priority: while move-shooting in the open, periodically check for reachable cover and,
	// if any exists, finish the task so the BT re-runs MoveToCover and routes us into cover.
	// Suppressed while catching up to a distant player (player-pull wins per design).
	TimeInOpenEngageNoCover += DeltaSeconds;
	if (OpenEngageCoverReseekInterval > 0.f && !bPlayerTooFar
		&& TimeInOpenEngageNoCover >= OpenEngageCoverReseekInterval)
	{
		TimeInOpenEngageNoCover = 0.f;
		const ACompanionAIController* CoverCtrl = Cast<ACompanionAIController>(Ctx.Companion->GetController());
		const UCompanionTuningDataAsset* CoverTuning = CoverCtrl ? CoverCtrl->GetTuning() : nullptr;
		UWorld* CoverWorld = Ctx.Companion->GetWorld();
		if (CoverTuning && CoverWorld)
		{
			if (UCoverRegistrySubsystem* Reg = CoverWorld->GetSubsystem<UCoverRegistrySubsystem>())
			{
				AAICoverSlot* Avail = Reg->FindBestCoverFor(MyLocation, Ctx.Target,
					CoverTuning->CoverSearchRadius, nullptr, Ctx.Companion, CoverTuning->CoverSwitchPostVacateCooldown);
				if (IsValid(Avail))
				{
					if (bDebugLogging)
						UE_LOG(LogCompanionAI, Log, TEXT("%s: open-engage cover re-seek -> reachable slot %s, finishing to MoveToCover"),
							*Ctx.Companion->GetName(), *Avail->GetName());
					Ctx.Companion->StopWeaponFire();
					EndOpenAreaMoveShoot(Ctx.Companion);
					return FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
				}
			}
		}
	}

	if (!bLineOfSight)
	{
		// Never fire through a wall, regardless of branch.
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

		if (bEnableOpenAreaMoveAndShoot)
		{
			// Drop the jiggle latch so the clear-LoS path re-anchors JiggleHome when LoS returns.
			bJiggleActive = false;
			Ctx.Companion->SetAimTarget(Ctx.Target);
			if (AAIController* RegainAIC = Cast<AAIController>(Ctx.Companion->GetController()))
			{
				if (bPlayerTooFar)
					TickMoveShootTowardPlayer(Ctx.Companion, RegainAIC, Ctx.Target, DeltaSeconds);
				else
					TickRegainLosReposition(Ctx.Companion, RegainAIC, Ctx.Target, TickIgnoredAttached, DeltaSeconds);
			}
		}
		else
		{
			// Toggle off: today's exact behaviour — stop move-and-shoot, drop aim or manually face, abandon.
			EndOpenAreaMoveShoot(Ctx.Companion);
			if (LosBlockedAccum > AimDropOnLosBlockedSeconds)
				Ctx.Companion->SetAimTarget(nullptr);
			else
			{
				const FRotator LookAtRot = (TargetLocation - MyLocation).Rotation();
				Ctx.Companion->SetActorRotation(FMath::RInterpTo(Ctx.Companion->GetActorRotation(),
					FRotator(0.f, LookAtRot.Yaw, 0.f), DeltaSeconds, Ctx.Companion->RotationInterpSpeed));
			}
		}

		if (LosBlockedAbandonSeconds > 0.f && LosBlockedAccum >= LosBlockedAbandonSeconds)
		{
			if (bDebugLogging)
				UE_LOG(LogCompanionAI, Log, TEXT("%s: FIRE STOP reason=los-block-abandon"), *Ctx.Companion->GetName());
			Ctx.Companion->StopWeaponFire();
			EndOpenAreaMoveShoot(Ctx.Companion);
			return FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
		}
		return;
	}

	LosBlockedAccum = 0.f;
	Ctx.Companion->SetAimTarget(Ctx.Target);

	// Facing: when move-and-shoot is enabled, AIController focus (set in EnterMoveShootIfNeeded)
	// drives body yaw toward the target while the jiggle strafes — so skip the manual rotation here,
	// which would fight bUseControllerDesiredRotation. When disabled, fall back to today's manual face-the-target.
	if (!bEnableOpenAreaMoveAndShoot)
	{
		EndOpenAreaMoveShoot(Ctx.Companion);
		const FRotator LookAtRot = (TargetLocation - MyLocation).Rotation();
		Ctx.Companion->SetActorRotation(FMath::RInterpTo(Ctx.Companion->GetActorRotation(),
			FRotator(0.f, LookAtRot.Yaw, 0.f), DeltaSeconds, Ctx.Companion->RotationInterpSpeed));
	}

	if (Ctx.Companion->NeedsReload() && !Ctx.Companion->IsReloading())
	{
		if (bDebugLogging)
		{
			AWeaponBase* W = Ctx.Companion->GetCurrentWeapon();
			const float ReloadTime = (W && W->GetWeaponData()) ? W->GetWeaponData()->ReloadTime : -1.f;
			UE_LOG(LogCompanionDiag, Log, TEXT("%s: RELOAD-START gate=1063 ammo=%d/%d reserve=%d vel=%.1f hasCover=%d slot=%s reloadTime=%.2f"),
				*Ctx.Companion->GetName(),
				W ? W->GetCurrentAmmo() : -1,
				W && W->GetWeaponData() ? W->GetWeaponData()->MagazineSize : -1,
				W ? W->GetReserveAmmo() : -1,
				Ctx.Companion->GetVelocity().Size(),
				(int32)bHasCover,
				*GetNameSafe(Slot),
				ReloadTime);
			UE_LOG(LogCompanionAI, Log, TEXT("%s: FIRE STOP reason=reload"), *Ctx.Companion->GetName());
		}
		Ctx.Companion->StopWeaponFire();
		Ctx.Companion->ReloadWeapon();
		bIsFiringBurst = false;
		BurstTimer = 0.f; // resume firing on the first tick after reload completes — don't wait out a frozen leftover burst timer
		// No return — keep moving (strafe-walk / player-pull) while the reload montage plays.
	}
	const bool bReloadingNow = Ctx.Companion->IsReloading();

	// Combat jiggle (or player-pull): continuous restless micro-motion while firing continues below.
	// bMoveShootMoveActive may already be true (carried over from a regain-LoS sidestep) — the shared speed/focus
	// entry stays active; TickCombatJiggle re-anchors JiggleHome + StopMovement's the fan on the first jiggle tick
	// because the regain branch cleared bJiggleActive. Player-pull wins when staying near the player is urgent;
	// the firing-burst logic below still fires at the enemy because LoS is clear.
	if (bEnableOpenAreaMoveAndShoot)
	{
		if (AAIController* MoveAIC = Cast<AAIController>(Ctx.Companion->GetController()))
		{
			if (bPlayerTooFar)
				TickMoveShootTowardPlayer(Ctx.Companion, MoveAIC, Ctx.Target, DeltaSeconds);
			else
				TickCombatJiggle(Ctx.Companion, MoveAIC, Ctx.Target, TickIgnoredAttached, DeltaSeconds);
		}
	}

	if (!bReloadingNow)
	{
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

		const bool bReleaseSlot = (TaskResult != EBTNodeResult::Succeeded);
		AAICoverSlot* FinishSlot = BB ? Cast<AAICoverSlot>(BB->GetValueAsObject(CoverSlotKey.SelectedKeyName)) : nullptr;
		ResetTaskState(Companion, BB, FinishSlot, bReleaseSlot);
	}
}

FString UBTTask_CompanionCombat::GetStaticDescription() const
{
	return FString::Printf(TEXT("Cover-aware Combat (burst: %.1f-%.1fs / quick: %.1f-%.1fs; peek cd %.1f-%.1fs)"),
		MinFireBurst, MaxFireBurst, MinQuickPeekBurst, MaxQuickPeekBurst, MinPeekCooldown, MaxPeekCooldown);
}
