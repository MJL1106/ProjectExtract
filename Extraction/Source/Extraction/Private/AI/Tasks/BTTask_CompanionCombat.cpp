// BT task — cover-aware companion combat. State machine drives EngageFromOpen, EngageFromCover, StandUpFire.
// P3 AICS migration: cover source changed from AAICoverSlot line-segment slots to FCoverHandle/FCoverData points.

#include "BTTask_CompanionCombat.h"
#include "AI/AITargetingStatics.h"
#include "AI/CompanionDiag.h"
#include "WeaponDataAsset.h"
#include "Character/ExtractionPlayerInterface.h"
#include "WeaponComponent.h"
#include "AI/BlackboardKeyType_Cover.h"
#include "CoverSystem.h"
#include "CoverGeometryStatics.h"
#include "CoverReservationSubsystem.h"
#include "CoverPoseComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "AIController.h"
#include "CompanionAIController.h"
#include "CompanionTuningDataAsset.h"
#include "CompanionCharacter.h"
#include "Animation/CompanionAnimInstance.h"
#include "WeaponBase.h"
#include "HealthComponent.h"
#include "SuppressionComponent.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "NavigationSystem.h"
#include "Navigation/PathFollowingComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "TimerManager.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISense_Sight.h"
#include "GameplayTagAssetInterface.h"
#include "ExtractionTypes.h"

namespace
{
	/** Chest height (cm) for the body-protection trace from a candidate's hunker position — matches the enemy's BodyProtectChestHeight. */
	constexpr float ShuffleBodyProtectChestHeight = 60.f;
}

namespace
{
	struct FCombatContext
	{
		ACompanionCharacter* Companion = nullptr;
		AActor* Target = nullptr;
		UBlackboardComponent* Blackboard = nullptr;
	};

	// Point overload — caller pre-resolves the destination (avoids recomputing GetSightLocation in tight loops).
	static bool HasLineOfSight(UWorld* World, const FVector& FromLoc, const FVector& ToLoc, AActor* ToTarget, ACompanionCharacter* Companion, AActor*& OutBlockedBy, TArrayView<AActor* const> IgnoredAttached)
	{
		OutBlockedBy = nullptr;
		if (!World || !IsValid(ToTarget) || !IsValid(Companion)) return false;

		FHitResult Hit;
		FCollisionQueryParams QueryParams;
		QueryParams.AddIgnoredActor(Companion);
		QueryParams.AddIgnoredActor(Companion->GetCurrentWeapon());
		for (AActor* const A : IgnoredAttached) QueryParams.AddIgnoredActor(A);

		const bool bHit = World->LineTraceSingleByChannel(Hit, FromLoc, ToLoc, ECC_Visibility, QueryParams);
		if (bHit && Hit.GetActor() != ToTarget)
		{
			OutBlockedBy = Hit.GetActor();
			return false;
		}
		return true;
	}

	// Actor overload — resolves the sight point once and forwards to the point overload.
	static bool HasLineOfSight(UWorld* World, const FVector& FromLoc, AActor* ToTarget, ACompanionCharacter* Companion, AActor*& OutBlockedBy, TArrayView<AActor* const> IgnoredAttached)
	{
		if (!IsValid(ToTarget)) { OutBlockedBy = nullptr; return false; }
		return HasLineOfSight(World, FromLoc, AITargeting::GetSightLocation(ToTarget), ToTarget, Companion, OutBlockedBy, IgnoredAttached);
	}

	// Gathers the companion's known threats (sight-perceived, enemy-tagged, alive), sorted nearest-first
	// and capped to MaxThreats. FocusTarget (the current CombatTarget) is guaranteed to be element 0 when
	// valid, so callers can treat [1..] as the "other threats" set. Uses the perception component the
	// combat target selection already relies on — no new perception cost, one sort per call.
	static void GatherKnownThreats(ACompanionCharacter* Companion, AActor* FocusTarget, int32 MaxThreats,
		TArray<AActor*, TInlineAllocator<8>>& OutThreats)
	{
		OutThreats.Reset();
		if (!IsValid(Companion) || MaxThreats <= 0) return;

		AAIController* AIC = Cast<AAIController>(Companion->GetController());
		UAIPerceptionComponent* Perception = AIC ? AIC->GetPerceptionComponent() : nullptr;
		if (!Perception)
		{
			if (IsValid(FocusTarget)) OutThreats.Add(FocusTarget);
			return;
		}

		TArray<AActor*> Perceived;
		Perception->GetCurrentlyPerceivedActors(UAISense_Sight::StaticClass(), Perceived);

		const FVector MyLoc = Companion->GetActorLocation();
		for (AActor* Actor : Perceived)
		{
			if (!IsValid(Actor) || Actor == FocusTarget) continue;

			const IGameplayTagAssetInterface* TagIface = Cast<IGameplayTagAssetInterface>(Actor);
			if (!TagIface) continue;
			FGameplayTagContainer Tags;
			TagIface->GetOwnedGameplayTags(Tags);
			if (!Tags.HasTag(TAG_Character_Enemy)) continue;

			const UHealthComponent* Health = Actor->FindComponentByClass<UHealthComponent>();
			if (Health && Health->IsDead()) continue;

			OutThreats.Add(Actor);
		}

		OutThreats.Sort([MyLoc](const AActor& A, const AActor& B)
		{
			return FVector::DistSquared(MyLoc, A.GetActorLocation()) < FVector::DistSquared(MyLoc, B.GetActorLocation());
		});

		// Focus target always leads the set; cap the total to MaxThreats.
		if (IsValid(FocusTarget)) OutThreats.Insert(FocusTarget, 0);
		if (OutThreats.Num() > MaxThreats) OutThreats.SetNum(MaxThreats, EAllowShrinking::No);
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
				UE_LOG(LogCompanionDiag, Verbose, TEXT("%s: [CrouchCall] t=%.3f site=DeferCoverPosture action=Crouch"),
					*GetNameSafe(C), C->GetWorld() ? C->GetWorld()->GetTimeSeconds() : 0.f);
				C->Crouch();
			}
			else if (!bShouldCrouch && C->bIsCrouched)
			{
				UE_LOG(LogCompanionDiag, Verbose, TEXT("%s: [CrouchCall] t=%.3f site=DeferCoverPosture action=UnCrouch"),
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

	// Returns true when the player is ADS, sets OutAimDir2D to the horizontal aim direction.
	// Accepts an already-resolved player pawn to avoid redundant controller→player walks on hot paths.
	// Returns false if the player is absent, doesn't implement the interface, has no weapon component,
	// is not aiming, or the aim direction is degenerate.
	static bool ResolvePlayerADSAim(APawn* Player, FVector& OutAimDir2D)
	{
		if (!IsValid(Player)) return false;

		IExtractionPlayerInterface* PlayerIface = Cast<IExtractionPlayerInterface>(Player);
		if (!PlayerIface) return false;

		UWeaponComponent* WeaponComp = PlayerIface->GetWeaponComponent();
		if (!WeaponComp || !WeaponComp->IsAiming()) return false;

		OutAimDir2D = Player->GetBaseAimRotation().Vector().GetSafeNormal2D();
		return !OutAimDir2D.IsNearlyZero();
	}

	// Pushes Point horizontally out of the player's ADS cone (preserves Z, preserves distance from PlayerLoc).
	// Returns Point unchanged if: the point is outside Range, the horizontal distance is degenerate, or
	// the point is already outside the cone (angle >= HalfAngleRad).
	static FVector ClampOutsidePlayerADSCone(const FVector& Point, const FVector& PlayerLoc,
		const FVector& AimDir2D, float HalfAngleRad, float MarginRad, float Range)
	{
		FVector ToPoint = Point - PlayerLoc;
		ToPoint.Z = 0.f;
		const float Dist = ToPoint.Size();
		if (Dist < KINDA_SMALL_NUMBER || Dist > Range) return Point;

		const FVector Dir = ToPoint / Dist;
		const float AngleRad = FMath::Acos(FMath::Clamp(FVector::DotProduct(Dir, AimDir2D), -1.f, 1.f));
		if (AngleRad >= HalfAngleRad) return Point;

		// Determine which side of the aim direction the point is on, default to left (+1) if on-axis.
		const float SideSign = FMath::Sign(FVector::DotProduct(FVector::CrossProduct(AimDir2D, Dir), FVector::UpVector));
		const float ChosenSign = (SideSign != 0.f) ? SideSign : 1.f;

		constexpr float MaxConeTargetAngleRad = 85.f * PI / 180.f;
		const float TargetAngle = FMath::Min(HalfAngleRad + MarginRad, MaxConeTargetAngleRad);
		const FVector Rotated = AimDir2D.RotateAngleAxis(FMath::RadiansToDegrees(TargetAngle) * ChosenSign, FVector::UpVector);
		const FVector NewPos = PlayerLoc + Rotated * Dist;
		return FVector(NewPos.X, NewPos.Y, Point.Z);
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
	const FCoverData& CoverData, bool bSuppressed, bool bLowHealth)
{
	if (IsValid(Companion)) Companion->StopWeaponFire();

	bIsFiringBurst = false;
	// Fix 5: the burst is over — clear the muzzle-withhold latch so the next burst starts un-held.
	bStandBurstFireHeld = false;
	StandBurstMuzzleCheckTimer = 0.f;
	LastStandBurstResumeFireTime = 0.f;

	const ECoverHeight Height = CoverData.bCrouchedCover ? ECoverHeight::Crouch : ECoverHeight::Stand;

	if (IsValid(Companion))
	{
		ResolvedPeekSide = (CurrentLean == ECoverLean::Left) ? EPeekSide::Left : EPeekSide::Right;
		LastPeekResolveCoverLoc = FVector::ZeroVector;
		LastPeekResolveTargetLoc = FVector::ZeroVector;

		if (AAIController* AIC = Cast<AAIController>(Companion->GetController()))
			AIC->StopMovement();
		if (UCharacterMovementComponent* CMC = Companion->GetCharacterMovement())
			CMC->StopMovementImmediately();

		const UCapsuleComponent* Cap = Companion->GetCapsuleComponent();
		const float Standoff = (Cap ? Cap->GetScaledCapsuleRadius() : 34.f) + 10.f;
		const FVector HunkerLoc = UCoverGeometryStatics::GetHunkerPosition(CoverData, Standoff);
		const FRotator SlotYawRot(0.f, UCoverGeometryStatics::GetFireArcForward(CoverData).Rotation().Yaw, 0.f);
		// Ground-snap: nav-mesh-arrival Z is biased above the floor; trace down to find the real floor.
		FVector SnapLoc(HunkerLoc.X, HunkerLoc.Y, Companion->GetActorLocation().Z);
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
		BeginSmoothSnap(Companion, SnapLoc, SlotYawRot, Height == ECoverHeight::Crouch, TEXT("ReturnToCover"));
	}

	if (UAnimMontage* M = ActivePeekMontage.Get())
	{
		if (Anim) Anim->Montage_Stop(0.15f, M);
	}
	ActivePeekMontage.Reset();

	CurrentLean = ECoverLean::None;
	if (Anim) Anim->EnterCoverPose(ResolvedPeekSide, Height);

	float CooldownMult = 1.f;
	if (bSuppressed) CooldownMult *= SuppressionCooldownMultiplier;
	if (bLowHealth)  CooldownMult *= LowHealthCooldownMultiplier;
	PeekCooldown = FMath::RandRange(MinPeekCooldown, MaxPeekCooldown) * CooldownMult;
	TimeInCoverIdle = 0.f;
}

void UBTTask_CompanionCombat::TickRepositionAction(ACompanionCharacter* Companion, UCompanionAnimInstance* Anim, UBlackboardComponent* BB,
	const FCoverData& CoverData, bool bSuppressed, bool bLowHp, float DeltaSeconds)
{
	if (!IsValid(Companion) || !RepositionTargetCover.IsValid()) return;

	if (bSuppressed)
	{
		// Abort: snap to whichever position is closer — current hunker or the reposition target.
		const FVector Current = Companion->GetActorLocation();
		const UCapsuleComponent* AbortCap = Companion->GetCapsuleComponent();
		const float AbortStandoff = (AbortCap ? AbortCap->GetScaledCapsuleRadius() : 34.f) + 10.f;
		const FVector CurrentLoc = UCoverGeometryStatics::GetHunkerPosition(CoverData, AbortStandoff);
		bool bCloserToTarget = FVector::DistSquared(Current, RepositionTargetWorldLoc)
			< FVector::DistSquared(Current, CurrentLoc);

		// Occupancy re-check before committing to the shuffle target (mirrors genuine arrival paths).
		if (bCloserToTarget)
		{
			bool bTakenByOther = false;
			if (UWorld* SuppWorld = Companion->GetWorld())
			{
				ACoverSystem* SuppCoverSys = ACoverSystem::GetCoverSystem(SuppWorld);
				if (SuppCoverSys)
				{
					AController* SuppOccupant = SuppCoverSys->GetOccupyingController(RepositionTargetCover.Handle);
					bTakenByOther = SuppOccupant && SuppOccupant != Companion->GetController();
				}
			}
			if (bTakenByOther)
			{
				// Target was claimed mid-walk — fall back to current cover and clear intent.
				UE_LOG(LogCompanionAI, Log, TEXT("%s: REPOSITION-SUPPRESS-ABORT reason=cover-taken-mid-walk"), *GetNameSafe(Companion));
				if (UWorld* IntentWorld = Companion->GetWorld())
				{
					if (UCoverReservationSubsystem* IntentSub = IntentWorld->GetSubsystem<UCoverReservationSubsystem>())
					{
						if (AController* IntentCtrl = Companion->GetController())
							IntentSub->ClearIntendedCover(IntentCtrl);
					}
				}
				bCloserToTarget = false;
			}
		}

		const FCoverData& ReturnData = bCloserToTarget ? RepositionTargetCover.Data : CoverData;
		if (bCloserToTarget && BB)
		{
			CommitCoverSwitch(BB, RepositionTargetCover, Companion->GetController());
		}
		else
		{
			// Not committing to the target — clear the reposition intent stamp so it doesn't
			// leak for the rest of the engagement (mirrors the arrival-abort ClearIntendedCover).
			if (UWorld* IntentWorld = Companion->GetWorld())
			{
				if (UCoverReservationSubsystem* IntentSub = IntentWorld->GetSubsystem<UCoverReservationSubsystem>())
				{
					if (AController* IntentCtrl = Companion->GetController())
						IntentSub->ClearIntendedCover(IntentCtrl);
				}
			}
		}
		if (bDebugLogging && bRepositionStartLogged)
			UE_LOG(LogCompanionDiag, Verbose, TEXT("%s: REPOSITION-DONE result=aborted dist=%.0f elapsed=%.2f"),
				*Companion->GetName(), FVector::Dist(Current, RepositionTargetWorldLoc), RepositionElapsed);
		Companion->SetLowReadyAim(false);
		Companion->SetAimTarget(nullptr);
		RepositionTargetCover = FCover();
		RepositionStalledTime = 0.f;
		bRepositionStartLogged = false;
		CurrentBurstAction = EPeekAction::Hold;
		bJustRepositioned = true;
		if (UCompanionAnimInstance* CAI = Cast<UCompanionAnimInstance>(
			Companion->GetMesh() ? Companion->GetMesh()->GetAnimInstance() : nullptr))
			CAI->ClearCoverStrafeVelocity();
		ReturnToCover(Companion, Anim, ReturnData, true, bLowHp);
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
			UE_LOG(LogCompanionDiag, Verbose, TEXT("%s: REPOSITION-DONE result=arrived dist=%.0f elapsed=%.2f"),
				*Companion->GetName(), Dist, RepositionElapsed);
		Companion->SetLowReadyAim(false);
		Companion->SetAimTarget(nullptr);
		const FCover ArrivedCover = RepositionTargetCover;

		// Re-check occupancy on arrival — another agent may have claimed the point mid-walk.
		bool bTakenByOther = false;
		if (UWorld* RepoWorld = Companion->GetWorld())
		{
			ACoverSystem* RepoCoverSys = ACoverSystem::GetCoverSystem(RepoWorld);
			if (RepoCoverSys)
			{
				AController* RepoOccupant = RepoCoverSys->GetOccupyingController(ArrivedCover.Handle);
				bTakenByOther = RepoOccupant && RepoOccupant != Companion->GetController();
			}
		}

		if (bTakenByOther)
		{
			// Cover was taken while walking — abort gracefully, return to current cover.
			UE_LOG(LogCompanionAI, Log, TEXT("%s: REPOSITION-ABORT reason=cover-taken-mid-walk"), *GetNameSafe(Companion));
			if (UWorld* IntentWorld = Companion->GetWorld())
			{
				if (UCoverReservationSubsystem* IntentSub = IntentWorld->GetSubsystem<UCoverReservationSubsystem>())
				{
					if (AController* IntentCtrl = Companion->GetController())
						IntentSub->ClearIntendedCover(IntentCtrl);
				}
			}
			RepositionTargetCover = FCover();
			RepositionStalledTime = 0.f;
			bRepositionStartLogged = false;
			CurrentBurstAction = EPeekAction::Hold;
			bJustRepositioned = true;
			if (UCompanionAnimInstance* CAI = GetCompanionAnim(Companion))
				CAI->ClearCoverStrafeVelocity();
			ReturnToCover(Companion, Anim, CoverData, false, bLowHp);
			return;
		}

		if (BB) CommitCoverSwitch(BB, ArrivedCover, Companion->GetController());
		RepositionTargetCover = FCover();
		RepositionStalledTime = 0.f;
		LastRepositionDist = 0.f;
		RepositionElapsed = 0.f;
		bRepositionStartLogged = false;
		CurrentBurstAction = EPeekAction::Hold;
		bJustRepositioned = true;
		PeekCooldown = FMath::RandRange(MinPeekCooldown, MaxPeekCooldown);
		TimeInCoverIdle = 0.f;
		if (!ArrivedCover.Data.bCrouchedCover)
		{
			if (UCompanionAnimInstance* CAI = Cast<UCompanionAnimInstance>(
				Companion->GetMesh() ? Companion->GetMesh()->GetAnimInstance() : nullptr))
				CAI->ClearCoverStrafeVelocity();
		}
		// Silent reposition: companion never left cover pose — skip the enter montage to avoid the bobbing animation.
		const ECoverHeight ArrivedHeight = ArrivedCover.Data.bCrouchedCover ? ECoverHeight::Crouch : ECoverHeight::Stand;
		if (Anim) Anim->EnterCoverPose(ResolvedPeekSide, ArrivedHeight, false);
	}
}

void UBTTask_CompanionCombat::TickStandUpAndRepositionAction(ACompanionCharacter* Companion, UCompanionAnimInstance* Anim, UBlackboardComponent* BB,
	const FCoverData& CoverData, bool bSuppressed, bool bLowHp, float DeltaSeconds)
{
	if (!IsValid(Companion) || !RepositionTargetCover.IsValid()) return;

	if (bSuppressed)
	{
		if (bDebugLogging && bRepositionStartLogged)
		{
			UE_LOG(LogCompanionDiag, Verbose, TEXT("%s: REPOSITION-DONE result=aborted dist=%.0f elapsed=%.2f"),
				*Companion->GetName(), FVector::Dist(Companion->GetActorLocation(), RepositionTargetWorldLoc), RepositionElapsed);
		}
		// Clear the reposition intent stamp — walk abandoned, target not committed.
		if (UWorld* IntentWorld = Companion->GetWorld())
		{
			if (UCoverReservationSubsystem* IntentSub = IntentWorld->GetSubsystem<UCoverReservationSubsystem>())
			{
				if (AController* IntentCtrl = Companion->GetController())
					IntentSub->ClearIntendedCover(IntentCtrl);
			}
		}
		ReturnToCover(Companion, Anim, CoverData, true, bLowHp);
		bRepositionStandPhase = false;
		bStandUpRepositionWalking = false;
		RepositionTargetCover = FCover();
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
			RepositionTargetCover = FCover();
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
				UE_LOG(LogCompanionDiag, Verbose, TEXT("%s: STANDUP-REPOSITION-DRY-ABORT"), *Companion->GetName());

			// Defer reload until after the snap completes so the reload montage doesn't start
			// while the actor is still gliding to the slot location (visible reload-while-gliding).
			// The crouch-vs-stand distinction doesn't matter for reload timing -- what matters is
			// being at the slot location before the reload anim plays.
			bPendingReloadAfterSnap = true;

			ReturnToCover(Companion, Anim, CoverData, false, bLowHp);
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
			UE_LOG(LogCompanionDiag, Verbose, TEXT("%s: REPOSITION-DONE result=arrived dist=%.0f elapsed=%.2f"),
				*Companion->GetName(), Dist, RepositionElapsed);
		const FCover ArrivedCover = RepositionTargetCover;

		// Re-check occupancy on arrival — another agent may have claimed the point mid-walk.
		bool bTakenByOther = false;
		if (UWorld* RepoWorld = Companion->GetWorld())
		{
			ACoverSystem* RepoCoverSys = ACoverSystem::GetCoverSystem(RepoWorld);
			if (RepoCoverSys)
			{
				AController* RepoOccupant = RepoCoverSys->GetOccupyingController(ArrivedCover.Handle);
				bTakenByOther = RepoOccupant && RepoOccupant != Companion->GetController();
			}
		}

		if (bTakenByOther)
		{
			UE_LOG(LogCompanionAI, Log, TEXT("%s: STANDUP-REPOSITION-ABORT reason=cover-taken-mid-walk"), *GetNameSafe(Companion));
			if (UWorld* IntentWorld = Companion->GetWorld())
			{
				if (UCoverReservationSubsystem* IntentSub = IntentWorld->GetSubsystem<UCoverReservationSubsystem>())
				{
					if (AController* IntentCtrl = Companion->GetController())
						IntentSub->ClearIntendedCover(IntentCtrl);
				}
			}
			RepositionTargetCover = FCover();
			bRepositionStandPhase = false;
			bStandUpRepositionWalking = false;
			RepositionStalledTime = 0.f;
			bRepositionStartLogged = false;
			bJustRepositioned = true;
			if (UCompanionAnimInstance* CAI = GetCompanionAnim(Companion))
				CAI->ClearCoverStrafeVelocity();
			ReturnToCover(Companion, Anim, CoverData, false, bLowHp);
			return;
		}

		if (BB) CommitCoverSwitch(BB, ArrivedCover, Companion->GetController());
		RepositionTargetCover = FCover();
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
		ReturnToCover(Companion, Anim, ArrivedCover.Data, false, bLowHp);
	}
}

void UBTTask_CompanionCombat::TickCornerPeekAction(ACompanionCharacter* Companion, UCompanionAnimInstance* Anim,
	const FCoverData& CoverData, AActor* Target, bool bSuppressed, bool bLowHp,
	TArrayView<AActor* const> IgnoredAttached, float DeltaSeconds)
{
	if (!IsValid(Companion) || !IsValid(Target)) return;

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
			ReturnToCover(Companion, Anim, CoverData, false, bLowHp);
		}
	}
}

FCover UBTTask_CompanionCombat::FindShuffleCover(ACompanionCharacter* Companion, const FCover& CurrentCover, const FVector& ThreatLocation) const
{
	if (!IsValid(Companion) || !CurrentCover.IsValid()) return FCover();

	UWorld* World = Companion->GetWorld();
	if (!World) return FCover();

	ACoverSystem* CoverSys = ACoverSystem::GetCoverSystem(World);
	if (!CoverSys) return FCover();

	const AController* Controller = Companion->GetController();
	const FVector PawnLoc = Companion->GetActorLocation();

	// Resolve tuning for body-protection gate (Finding 4).
	const ACompanionAIController* CompAIC = Cast<ACompanionAIController>(Companion->GetController());
	const UCompanionTuningDataAsset* Tuning = CompAIC ? CompAIC->GetTuning() : nullptr;
	const bool bRequireBodyProtection = Tuning && Tuning->bCoverRequiresBodyProtection;
	const UCapsuleComponent* Cap = Companion->GetCapsuleComponent();
	const float Standoff = (Cap ? Cap->GetScaledCapsuleRadius() : 34.f) + 10.f;

	// Resolve the threat actor for CanPeekShoot ignore-list (passed to CanPeekShoot so it ignores the target in traces).
	AActor* ThreatActor = nullptr;
	if (AAIController* AIC = Cast<AAIController>(Companion->GetController()))
	{
		if (UBlackboardComponent* BB = AIC->GetBlackboardComponent())
			ThreatActor = Cast<AActor>(BB->GetValueAsObject(CombatTargetKey.SelectedKeyName));
	}

	TArray<FCover> Candidates;
	Candidates.Reserve(32);
	const FBoxSphereBounds SearchBounds(PawnLoc, FVector(ShuffleDistanceMax), ShuffleDistanceMax);
	CoverSys->GetCoverDataWithinBounds(SearchBounds, Candidates);

	FCover BestCover;
	float BestDistSq = FLT_MAX;

	for (const FCover& Candidate : Candidates)
	{
		if (!Candidate.IsValid()) continue;
		if (Candidate.Handle == CurrentCover.Handle) continue;
		if (!UCoverGeometryStatics::IsSameWall(CurrentCover.Data, Candidate.Data)) continue;

		AController* Occupant = CoverSys->GetOccupyingController(Candidate.Handle);
		if (Occupant && Occupant != Controller) continue;

		const float DistSq = FVector::DistSquared(PawnLoc, Candidate.Data.Location);
		const float Dist = FMath::Sqrt(DistSq);
		if (Dist < ShuffleDistanceMin || Dist > ShuffleDistanceMax) continue;

		// Reject candidates that can't peek-shoot toward the threat (anti ping-pong).
		if (!UCoverGeometryStatics::CanPeekShoot(World, Candidate.Data, Candidate.Data.bCrouchedCover,
			ThreatLocation, StandFireEyeHeight, ThreatActor, Companion))
			continue;

		// Body-protection gate (Finding 4): reject candidates that don't protect the body.
		if (bRequireBodyProtection)
		{
			if (!UCoverGeometryStatics::IsThreatCovered(World, Candidate.Data, ThreatLocation,
				Standoff, ShuffleBodyProtectChestHeight, ThreatActor, Companion))
				continue;
		}

		if (DistSq >= BestDistSq) continue;
		BestDistSq = DistSq;
		BestCover = Candidate;
	}

	return BestCover;
}

void UBTTask_CompanionCombat::CommitCoverSwitch(UBlackboardComponent* BB, const FCover& NewCover, AController* Controller)
{
	if (!BB || !NewCover.IsValid()) return;

	UWorld* World = BB->GetWorld();
	UCoverReservationSubsystem* ResSub = World ? World->GetSubsystem<UCoverReservationSubsystem>() : nullptr;

	if (LastTickCoverHandle.IsValid() && LastTickCoverHandle != NewCover.Handle && IsValid(ResSub) && IsValid(Controller))
		ResSub->MarkVacated(LastTickCoverHandle, Controller);

	BB->SetValue<UBlackboardKeyType_Cover>(CoverTargetKey.GetSelectedKeyID(), NewCover);
	if (CoverLocationKey.SelectedKeyName != NAME_None)
		BB->SetValueAsVector(CoverLocationKey.SelectedKeyName, NewCover.Data.Location);

	LastTickCoverHandle = NewCover.Handle;

	// Fresh point = fresh validity state. Task-internal commits (shuffle/reposition) pre-update
	// LastTickCoverHandle, so the tick's swap-detect block never fires for them — reset here instead,
	// or the new point inherits starvation/dwell accrued against the old one.
	ArcStarvationCount = 0;
	CoverCompromiseConsecutiveCount = 0;
	TimeAtCurrentCover = 0.f;
	CoverValidityCheckTimer = 0.f;
}

bool UBTTask_CompanionCombat::TryPrePeekReloadGate(ACompanionCharacter* Companion, const FCoverData& CoverData)
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
				TEXT("%s: PRE-PEEK-RELOAD-GATE ammo=%d burstCost=%d hasCover=1"),
				*Companion->GetName(),
				Companion->GetCurrentAmmo(), BurstCost);
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
	if (CoverData.bCrouchedCover && !Companion->bIsCrouched)
	{
		UE_LOG(LogCompanionDiag, Verbose, TEXT("%s: [CrouchCall] t=%.3f site=PrePeekReloadGate action=Crouch"),
			*GetNameSafe(Companion), World ? World->GetTimeSeconds() : 0.f);
		Companion->Crouch();
	}

	const float ReloadCooldownCap = MaxPeekCooldown * 2.f;
	const float ReloadBump = FMath::Clamp(Companion->GetWeaponReloadTime(), 0.f, ReloadCooldownCap);
	PeekCooldown = FMath::Max(PeekCooldown, ReloadBump);
	return true;
}

void UBTTask_CompanionCombat::TickStandBurstMuzzleWithhold(ACompanionCharacter* Companion, AActor* Target,
	TArrayView<AActor* const> IgnoredAttached, float DeltaSeconds)
{
	if (!IsValid(Companion) || !IsValid(Target)) return;
	UWorld* World = Companion->GetWorld();
	if (!World) return;

	// 10 Hz throttle — burst timing is unaffected (BurstTimer keeps counting in the caller).
	StandBurstMuzzleCheckTimer -= DeltaSeconds;
	if (StandBurstMuzzleCheckTimer > 0.f) return;
	StandBurstMuzzleCheckTimer = 0.1f;

	AWeaponBase* BurstWeapon = Companion->GetCurrentWeapon();
	if (!IsValid(BurstWeapon)) return;

	const FVector MuzzleLoc = BurstWeapon->GetMuzzleLocation();
	FHitResult MuzzleHit;
	FCollisionQueryParams MuzzleParams(SCENE_QUERY_STAT(CompanionStandBurstMuzzle), true);
	MuzzleParams.AddIgnoredActor(Companion);
	MuzzleParams.AddIgnoredActor(BurstWeapon);
	for (AActor* Attached : IgnoredAttached)
		MuzzleParams.AddIgnoredActor(Attached);
	const bool bMuzzleBlocked = World->LineTraceSingleByChannel(
		MuzzleHit, MuzzleLoc, AITargeting::GetSightLocation(Target), ECC_Visibility, MuzzleParams);
	const bool bBlocked = bMuzzleBlocked && MuzzleHit.GetActor() != Target;

	if (bBlocked && !bStandBurstFireHeld)
	{
		Companion->StopWeaponFire();
		bStandBurstFireHeld = true;
		if (bDebugLogging)
			UE_LOG(LogCompanionAI, Log, TEXT("%s: stand-burst FIRE HELD — muzzle blocked by %s"),
				*Companion->GetName(), *GetNameSafe(MuzzleHit.GetActor()));
		return;
	}

	if (!bBlocked && bStandBurstFireHeld)
	{
		// Fix 6: StartFiring() fires an instant shot on every call with no internal refire guard. A rapid
		// blocked/clear flicker at this 10 Hz check would resume-fire faster than the weapon cadence.
		// Gate the resume on the weapon's fire interval so back-to-back resumes can't out-pace it.
		const UWeaponDataAsset* WData = BurstWeapon->GetWeaponData();
		const float FireRate = (WData && WData->FireRate > 0.f) ? WData->FireRate : 0.f;
		const float FireInterval = (FireRate > 0.f) ? (1.0f / FireRate) : 0.f;
		const float Now = World->GetTimeSeconds();
		if (LastStandBurstResumeFireTime > 0.f && FireInterval > 0.f
			&& (Now - LastStandBurstResumeFireTime) < FireInterval)
		{
			// Too soon since the last resume-fire — leave the withhold latched this check; it will
			// resume on a later tick once the interval has elapsed (still muzzle-clear).
			return;
		}
		Companion->StartWeaponFire();
		bStandBurstFireHeld = false;
		LastStandBurstResumeFireTime = Now;
		if (bDebugLogging)
			UE_LOG(LogCompanionAI, Log, TEXT("%s: stand-burst FIRE RESUMED — muzzle clear"),
				*Companion->GetName());
	}
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
	// Single source for the MaxWalkSpeed + MaxAcceleration override; EndOpenAreaMoveShoot is the matching restore.
	// Focus/facing is owned by UpdateMoveShootFacing, called each tick from the LoS-clear and LoS-blocked branches.
	CachedDefaultWalkSpeed = CMC->MaxWalkSpeed;
	CMC->MaxWalkSpeed = CombatMoveSpeed;
	CachedDefaultAcceleration = CMC->MaxAcceleration;
	bMoveShootMoveActive = true;
	MoveShootRepositionTimer = 0.f;
	if (bDebugLogging)
		UE_LOG(LogCompanionAI, Log, TEXT("%s: MOVESHOOT enter %s speed=%.0f"), *Companion->GetName(), Reason, CombatMoveSpeed);
}

void UBTTask_CompanionCombat::UpdateMoveShootFacing(AAIController* AIC, AActor* Target, bool bLosClear)
{
	if (!IsValid(AIC)) return;
	if (bLosClear)
	{
		if (IsValid(Target))
			AIC->SetFocus(Target, EAIFocusPriority::Gameplay);
	}
	else if (bHasLastKnownTargetLocation)
	{
		AIC->SetFocalPoint(LastKnownTargetLocation, EAIFocusPriority::Gameplay);
	}
	// If no last-known location yet, hold current facing — do not snap to live position.
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

		ACompanionAIController* DriftCompAIC = Cast<ACompanionAIController>(Companion->GetController());
		const UCompanionTuningDataAsset* DriftTuning = DriftCompAIC ? DriftCompAIC->GetTuning() : nullptr;
		FVector AimDir2D;
		if (DriftTuning && DriftTuning->bAvoidPlayerADSCone && ResolvePlayerADSAim(KOPlayer, AimDir2D))
		{
			const float HalfRad = FMath::DegreesToRadians(DriftTuning->ADSConeHalfAngleDegrees);
			const float MarginRad = FMath::DegreesToRadians(DriftTuning->ADSConeClearanceMarginDegrees);
			const FVector Clamped = ClampOutsidePlayerADSCone(Projected, KOPlayer->GetActorLocation(), AimDir2D, HalfRad, MarginRad, DriftTuning->ADSConeRange);
			if (!Clamped.Equals(Projected))
			{
				FVector Reproj;
				if (ProjectToNav(Companion->GetWorld(), Clamped, MoveShootNavProjectExtent, Reproj))
					Projected = Reproj;
			}
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

	// Hoist controller + tuning + player pawn once so the anchor block and the per-tick steering block
	// both reuse them — avoids the duplicate controller→tuning→player walk on the hot per-frame path.
	ACompanionAIController* CompAIC = Cast<ACompanionAIController>(AIC);
	const UCompanionTuningDataAsset* Tuning = CompAIC ? CompAIC->GetTuning() : nullptr;
	float KeepOut = 0.f;
	APawn* KOPlayer = ResolvePlayerKeepOut(Companion, KeepOut);

	// (Re)anchor on activation — also fires when resuming after a LoS-blocked stretch (bJiggleActive cleared there),
	// so it jiggles around wherever it ended up. StopMovement() once cancels the regain fan's MoveToLocation path
	// so it doesn't fight AddMovementInput.
	if (!bJiggleActive)
	{
		AIC->StopMovement();
		JiggleHome = Companion->GetActorLocation();
		if (IsValid(KOPlayer))
		{
			JiggleHome = ClampOutsidePlayerCircle(JiggleHome, KOPlayer->GetActorLocation(), KeepOut);

			FVector AimDir2D;
			if (Tuning && Tuning->bAvoidPlayerADSCone && ResolvePlayerADSAim(KOPlayer, AimDir2D))
			{
				const float HalfRad = FMath::DegreesToRadians(Tuning->ADSConeHalfAngleDegrees);
				const float MarginRad = FMath::DegreesToRadians(Tuning->ADSConeClearanceMarginDegrees);
				const FVector Clamped = ClampOutsidePlayerADSCone(JiggleHome, KOPlayer->GetActorLocation(), AimDir2D, HalfRad, MarginRad, Tuning->ADSConeRange);
				if (!Clamped.Equals(JiggleHome))
				{
					FVector Reproj;
					if (ProjectToNav(Companion->GetWorld(), Clamped, MoveShootNavProjectExtent, Reproj))
						JiggleHome = Reproj;
					else
						JiggleHome = Clamped;
				}
			}
		}
		JiggleDriftTimer = JiggleDriftInterval;
		InConeContinuousTime = 0.f;
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
	if (IsValid(KOPlayer))
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

		// ADS cone steering bias: if companion's current position is inside the player's firing cone,
		// blend a tangential push out of the cone into SmoothedMoveDir.
		// bAvoidPlayerADSCone is checked first so the feature-off path does zero casts.
		FVector AimDir2D;
		if (Tuning && Tuning->bAvoidPlayerADSCone && ResolvePlayerADSAim(KOPlayer, AimDir2D))
		{
			const float HalfRad = FMath::DegreesToRadians(Tuning->ADSConeHalfAngleDegrees);
			FVector ToCompanion = CompanionLoc - KOPlayer->GetActorLocation();
			ToCompanion.Z = 0.f;
			const float DistFromPlayer = ToCompanion.Size();
			if (DistFromPlayer > KINDA_SMALL_NUMBER && DistFromPlayer <= Tuning->ADSConeRange)
			{
				const FVector CompDir = ToCompanion / DistFromPlayer;
				const float AngleRad = FMath::Acos(FMath::Clamp(FVector::DotProduct(CompDir, AimDir2D), -1.f, 1.f));
				if (AngleRad < HalfRad)
				{
					// Tangent perpendicular to AimDir2D, toward companion's side.
					const float SideSign = FMath::Sign(FVector::DotProduct(FVector::CrossProduct(AimDir2D, CompDir), FVector::UpVector));
					const float ChosenSign = (SideSign != 0.f) ? SideSign : 1.f;
					FVector Tangent = FVector::CrossProduct(FVector::UpVector, AimDir2D) * ChosenSign;
					SmoothedMoveDir = (SmoothedMoveDir + Tangent).GetSafeNormal();
					InConeContinuousTime += DeltaSeconds;
				}
				else
				{
					InConeContinuousTime = 0.f;
				}
			}
			else
			{
				InConeContinuousTime = 0.f;
			}
		}
		else
		{
			InConeContinuousTime = 0.f;
		}
	}

	// Wall-grind escape: if steering bias hasn't cleared the cone after a bounded interval
	// (companion pinned against a wall on the exit side), force a fresh re-anchor so it picks
	// a new JiggleHome outside the cone rather than grinding forever.
	constexpr float ConePinEscapeSeconds = 1.5f;
	if (InConeContinuousTime >= ConePinEscapeSeconds)
	{
		bJiggleActive = false;
		InConeContinuousTime = 0.f;
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

		ACompanionAIController* FanCompAIC = Cast<ACompanionAIController>(Companion->GetController());
		const UCompanionTuningDataAsset* FanTuning = FanCompAIC ? FanCompAIC->GetTuning() : nullptr;
		FVector AimDir2D;
		if (FanTuning && FanTuning->bAvoidPlayerADSCone && ResolvePlayerADSAim(KOPlayer, AimDir2D))
		{
			const float HalfRad = FMath::DegreesToRadians(FanTuning->ADSConeHalfAngleDegrees);
			FVector ToCandidate = OutDest - KOPlayer->GetActorLocation();
			ToCandidate.Z = 0.f;
			const float DistToCandidate = ToCandidate.Size();
			if (DistToCandidate > KINDA_SMALL_NUMBER && DistToCandidate <= FanTuning->ADSConeRange)
			{
				const float AngleRad = FMath::Acos(FMath::Clamp(FVector::DotProduct(ToCandidate / DistToCandidate, AimDir2D), -1.f, 1.f));
				if (AngleRad < HalfRad) return false;
			}
		}
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
	InConeContinuousTime = 0.f;

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

	CoverTargetKey.SelectedKeyName = TEXT("CoverTarget");

	// Add Cover type filter for the CoverTargetKey selector (same pattern as BTTask_EnemyCombatFire).
	if (GIsEditor && !HasAnyFlags(RF_ClassDefaultObject))
	{
		CoverTargetKey.AllowedTypes.Add(NewObject<UBlackboardKeyType_Cover>(this, TEXT("CoverTargetKey_Cover")));
	}
}

// --- ResetTaskState ---

void UBTTask_CompanionCombat::ResetTaskState(ACompanionCharacter* Companion, UBlackboardComponent* BB, const FCoverHandle& CoverHandle, bool bReleaseSlot, bool bResetPosture)
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
			UE_LOG(LogCompanionDiag, Verbose, TEXT("%s: [SnapAborted] t=%.3f elapsed=%.3f reason=%s"),
				*GetNameSafe(Companion),
				Companion->GetWorld() ? Companion->GetWorld()->GetTimeSeconds() : 0.f,
				SmoothSnapElapsed,
				SmoothSnapReason);
		}
		if (bResetPosture)
		{
			if (bSmoothSnapping && bPendingCrouchAfterSnap)
			{
				UE_LOG(LogCompanionDiag, Verbose, TEXT("%s: [CrouchCall] t=%.3f site=ResetTaskState_SkippedForPendingCrouch action=NoOp"),
					*GetNameSafe(Companion),
					Companion->GetWorld() ? Companion->GetWorld()->GetTimeSeconds() : 0.f);
			}
			else
			{
				UE_LOG(LogCompanionDiag, Verbose, TEXT("%s: [CrouchCall] t=%.3f site=ResetTaskState action=UnCrouch"),
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
		if (CoverHandle.IsValid())
		{
			// Guard: only clear the BB key if it still holds the cover this task instance was using.
			// The monitor may have already written a replacement target — wiping it would lose the switch.
			const FCover CurrentBBCover = BB->GetValue<UBlackboardKeyType_Cover>(CoverTargetKey.GetSelectedKeyID());
			if (!CurrentBBCover.IsValid() || CurrentBBCover.Handle == CoverHandle)
			{
				BB->ClearValue(CoverTargetKey.GetSelectedKeyID());
				BB->SetValueAsBool(HasCoverPositionKey.SelectedKeyName, false);
			}
		}
	}

	// Clear intended-cover tracking — but only when the BB's current target is the same cover
	// this task instance was actually using (LastTickCoverHandle). If the monitor replaced the
	// target mid-tick, its intent stamp belongs to the new target and must not be wiped.
	if (UWorld* World = IsValid(Companion) ? Companion->GetWorld() : nullptr)
	{
		if (UCoverReservationSubsystem* ResSub = World->GetSubsystem<UCoverReservationSubsystem>())
		{
			if (AController* Controller = Companion->GetController())
			{
				const bool bNoTracking = !LastTickCoverHandle.IsValid();
				const FCover CurrentBBCoverForIntent = BB ? BB->GetValue<UBlackboardKeyType_Cover>(CoverTargetKey.GetSelectedKeyID()) : FCover();
				const bool bBBIsOurCover = !CurrentBBCoverForIntent.IsValid()
					|| CurrentBBCoverForIntent.Handle == LastTickCoverHandle;
				if (bNoTracking || bBBIsOurCover)
					ResSub->ClearIntendedCover(Controller);
			}
		}
	}
	LastTickCoverHandle = FCoverHandle();

	bIsFiringBurst = false;
	BurstTimer = 0.f;
	// Fix 5: clear the muzzle-withhold latch so a stale held state can't leak into the next engagement.
	bStandBurstFireHeld = false;
	StandBurstMuzzleCheckTimer = 0.f;
	LastStandBurstResumeFireTime = 0.f;
	TimeInCoverIdle = 0.f;
	PeekCooldown = 0.f;
	CoverValidityCheckTimer = 0.f;
	TimeAtCurrentCover = 0.f;
	CoverCompromiseConsecutiveCount = 0;
	ArcStarvationCount = 0;
	LastValidatedTarget = nullptr;
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
	CurrentLean = ECoverLean::None;
	CachedSlotForwardYaw = 0.f;
	SubSlotLosRecheckTimer = 0.f;
	BlockedRecheckHits = 0;
	RepositionTargetCover = FCover();
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
	bHasLastKnownTargetLocation = false;
	LastKnownTargetLocation = FVector::ZeroVector;
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

			UE_LOG(LogCompanionDiag, Verbose, TEXT("%s: [CrouchCall] t=%.3f site=ImmediateOnShortSnap action=Crouch"),
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

			UE_LOG(LogCompanionDiag, Verbose, TEXT("%s: [CrouchCall] t=%.3f site=ImmediateOnShortSnap action=UnCrouch"),
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

	UE_LOG(LogCompanionDiag, Verbose, TEXT("%s: [BeginSmoothSnap] t=%.3f initialDist=%.1f effectiveDur=%.3f yawDelta=%.1f crouchAfter=%d reason=%s"),
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
		UE_LOG(LogCompanionDiag, Verbose, TEXT("%s: [SnapComplete] t=%.3f elapsed=%.3f effectiveDur=%.3f initialDist=%.1f linear=%d crouchPending=%d wasCrouched=%d willCrouch=%d willUncrouch=%d reason=%s"),
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
			UE_LOG(LogCompanionDiag, Verbose, TEXT("%s: [CrouchCall] t=%.3f site=PostSnapApply action=Crouch"),
				*GetNameSafe(Companion),
				Companion->GetWorld() ? Companion->GetWorld()->GetTimeSeconds() : 0.f);
			Companion->Crouch();
		}
		else if (bWillUncrouch)
		{
			UE_LOG(LogCompanionDiag, Verbose, TEXT("%s: [CrouchCall] t=%.3f site=PostSnapApply action=UnCrouch"),
				*GetNameSafe(Companion),
				Companion->GetWorld() ? Companion->GetWorld()->GetTimeSeconds() : 0.f);
			Companion->UnCrouch();
		}
		bPendingCrouchAfterSnap = false;
		if (bPendingReloadAfterSnap && IsValid(Companion))
		{
			UE_LOG(LogCompanionDiag, Verbose, TEXT("%s: [ReloadAfterSnap] t=%.3f"),
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
	// Pass the task's own tracked handle — NOT the BB value — so ResetTaskState's wipe-guard
	// can genuinely discriminate "our cover" vs "monitor's newly-written cover".
	const FCoverHandle TrackedHandle = LastTickCoverHandle;
	ResetTaskState(Companion, BB, TrackedHandle, true);
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

	// Full state reset without releasing the cover we're about to occupy.
	// Don't reset posture — preserves anticipatory crouch from MoveToCoverPoint.
	ResetTaskState(Companion, BB, FCoverHandle(), false, false);

	Companion->SetAimTarget(Target);
	PeekCooldown = FMath::RandRange(MinPeekCooldown, MaxPeekCooldown);
	DebugBurstLosCheckTimer = 0.f;
	SubSlotLosRecheckInterval = FMath::Max(0.1f, SubSlotLosRecheckInterval);

	if (!Companion->GetCurrentWeapon())
		UE_LOG(LogCompanionAI, Warning, TEXT("%s: combat task started but CurrentWeapon is null"), *Companion->GetName());

	const FCover Cover = BB->GetValue<UBlackboardKeyType_Cover>(CoverTargetKey.GetSelectedKeyID());
	const bool bHasCover = Cover.IsValid() && BB->GetValueAsBool(HasCoverPositionKey.SelectedKeyName);

	if (bHasCover)
	{
		LastTickCoverHandle = Cover.Handle;

		const FVector ArrivalLoc = Companion->GetActorLocation();
		CurrentLean = UCoverGeometryStatics::ResolveLeanSide(Cover.Data, Cover.Data.bCrouchedCover, Target->GetActorLocation());
		ResolvedPeekSide = (CurrentLean == ECoverLean::Left) ? EPeekSide::Left : EPeekSide::Right;

		const UCapsuleComponent* Cap = Companion->GetCapsuleComponent();
		const float Standoff = (Cap ? Cap->GetScaledCapsuleRadius() : 34.f) + 10.f;
		const FVector HunkerLoc = UCoverGeometryStatics::GetHunkerPosition(Cover.Data, Standoff);
		const float DistToHunker = FVector::Dist(ArrivalLoc, HunkerLoc);

		if (DistToHunker <= FinalApproachAcceptRadius)
		{
			// Already at the cover point — snap and enter cover immediately.
			if (AAIController* AIC = Cast<AAIController>(Companion->GetController()))
				AIC->StopMovement();
			if (UCharacterMovementComponent* CMC = Companion->GetCharacterMovement())
				CMC->StopMovementImmediately();

			const FRotator SlotYawRot(0.f, UCoverGeometryStatics::GetFireArcForward(Cover.Data).Rotation().Yaw, 0.f);
			if (bDebugLogging) UE_LOG(LogCompanionDiag, Log, TEXT("%s: ALREADY-CLOSE-SNAP distToHunker=%.1f acceptRadius=%.1f arrival=%s hunker=%s"),
				*Companion->GetName(), DistToHunker, FinalApproachAcceptRadius, *ArrivalLoc.ToString(), *HunkerLoc.ToString());
			// Ground-snap: nav-mesh-arrival Z is biased above the floor; trace down to find the real floor.
			FVector SnapLoc(HunkerLoc.X, HunkerLoc.Y, ArrivalLoc.Z);
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
			const ECoverHeight ArrivalHeight = Cover.Data.bCrouchedCover ? ECoverHeight::Crouch : ECoverHeight::Stand;
			BeginSmoothSnap(Companion, SnapLoc, SlotYawRot, ArrivalHeight == ECoverHeight::Crouch, TEXT("AlreadyClose"));
			if (UCompanionAnimInstance* Anim = GetCompanionAnim(Companion))
				Anim->EnterCoverPose(ResolvedPeekSide, ArrivalHeight);
		}
		else
		{
			// Walk the last bit physically; TickTask commits to cover on arrival.
			bWaitingForFinalApproach = true;
			FinalApproachTarget = HunkerLoc;
			FinalApproachElapsed = 0.f;
			LastFinalApproachDist = DistToHunker;

			if (AAIController* AIC = Cast<AAIController>(Companion->GetController()))
			{
				AIC->MoveToLocation(HunkerLoc, FinalApproachAcceptRadius, false, true, true, true);
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
					UE_LOG(LogCompanionDiag, Verbose, TEXT("%s: FINALAPPROACH-KICK target=%s current=%s dist=%.0f pathStatus=%s"),
						*Companion->GetName(), *HunkerLoc.ToString(), *ArrivalLoc.ToString(), DistToHunker, PFStatus);
				}
			}
		}

		UE_LOG(LogCompanionAI, Log,
			TEXT("%s: COVER-CLAIMED height=%s lean=%d canPeek=%d"),
			*Companion->GetName(),
			Cover.Data.bCrouchedCover ? TEXT("Crouch") : TEXT("Stand"),
			(int32)CurrentLean, (int32)(CurrentLean != ECoverLean::None));
		if (CurrentLean == ECoverLean::None)
		{
			UE_LOG(LogCompanionAI, Warning,
				TEXT("%s: COVER-CONFIG-WARN cover point has no valid lean side toward the target — companion cannot fire from this point."),
				*Companion->GetName());
		}
	}

	if (bDebugLogging)
	{
		const float Distance = FVector::Dist(Companion->GetActorLocation(), Target->GetActorLocation());
		UE_LOG(LogCompanionAI, Log, TEXT("%s: TASK ENTER target=%s dist=%.0f hasCover=%d"),
			*Companion->GetName(), *Target->GetName(), Distance, (int32)bHasCover);

		if (bHasCover)
		{
			const float DistToCover = FVector::Dist(Companion->GetActorLocation(), Cover.Data.Location);
			UE_LOG(LogCompanionDiag, Verbose, TEXT("%s: TASK-ENTER target=%s dist=%.0f hasCover=%d lean=%d distToCover=%.1f"),
				*Companion->GetName(), *Target->GetName(), Distance, 1, (int32)CurrentLean, DistToCover);
		}
		else
		{
			UE_LOG(LogCompanionDiag, Verbose, TEXT("%s: TASK-ENTER target=%s dist=%.0f hasCover=0 lean=-1 distToCover=-1"),
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

		const FCover ApproachCover = Ctx.Blackboard->GetValue<UBlackboardKeyType_Cover>(CoverTargetKey.GetSelectedKeyID());
		if (ApproachCover.IsValid())
		{
			const FRotator SlotYawRot(0.f, UCoverGeometryStatics::GetFireArcForward(ApproachCover.Data).Rotation().Yaw, 0.f);
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
				UE_LOG(LogCompanionDiag, Verbose, TEXT("%s: FINALAPPROACH-SNAP arrived=%d timedOut=%d stalled=%d elapsed=%.2f dist=%.0f warped=%.0f"),
					*Ctx.Companion->GetName(), (int32)bArrived, (int32)bTimedOut, (int32)bStalled, FinalApproachElapsed, Dist, WarpDist);
			}
			const ECoverHeight ApproachHeight = ApproachCover.Data.bCrouchedCover ? ECoverHeight::Crouch : ECoverHeight::Stand;
			BeginSmoothSnap(Ctx.Companion, SnapLoc, SlotYawRot, ApproachHeight == ECoverHeight::Crouch, TEXT("FinalApproach"));
			if (UCompanionAnimInstance* Anim = GetCompanionAnim(Ctx.Companion))
				Anim->EnterCoverPose(ResolvedPeekSide, ApproachHeight);
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
		const FCover PreSnapCover = Ctx.Blackboard->GetValue<UBlackboardKeyType_Cover>(CoverTargetKey.GetSelectedKeyID());
		if (!PreSnapCover.IsValid())
		{
			UE_LOG(LogCompanionAI, Warning, TEXT("%s: [SLOT-LOST-MID-SNAP] Slot invalidated during warp — aborting cleanly"), *GetNameSafe(Ctx.Companion));
			// If a reload was deferred for this snap, fire it now so the dry companion
			// isn't stranded clicking-on-empty until a new slot is acquired.
			if (bPendingReloadAfterSnap && IsValid(Ctx.Companion))
			{
				UE_LOG(LogCompanionDiag, Verbose, TEXT("%s: [ReloadAfterSlotLoss] t=%.3f"),
					*GetNameSafe(Ctx.Companion),
					Ctx.Companion->GetWorld() ? Ctx.Companion->GetWorld()->GetTimeSeconds() : 0.f);
				Ctx.Companion->ReloadWeapon();
			}
			ResetTaskState(Ctx.Companion, Ctx.Blackboard, FCoverHandle(), false, false);
			Ctx.Blackboard->ClearValue(CombatTargetKey.SelectedKeyName);
			return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		}
	}

	if (bSmoothSnapping)
	{
		if (!TickSmoothSnap(Ctx.Companion, DeltaSeconds))
			return; // snap not complete — pause branch logic this tick
		// Snap just completed — fall through to normal tick.
	}

	// Hoist controller cast + tuning fetch once per tick — reused by the range gate, cover-validity
	// check, and arc gate below instead of three separate Cast+GetTuning calls.
	const ACompanionAIController* TickController = Cast<ACompanionAIController>(Ctx.Companion->GetController());
	const UCompanionTuningDataAsset* TickTuning = TickController ? TickController->GetTuning() : nullptr;

	const FVector MyLocation = Ctx.Companion->GetActorLocation();
	const FVector TargetLocation = Ctx.Target->GetActorLocation();
	const float Distance = FVector::Dist(MyLocation, TargetLocation);

	const FCover Cover = Ctx.Blackboard->GetValue<UBlackboardKeyType_Cover>(CoverTargetKey.GetSelectedKeyID());
	const bool bHasCover = Cover.IsValid() && Ctx.Blackboard->GetValueAsBool(HasCoverPositionKey.SelectedKeyName);

	// Build ignored-actors list once per tick — passed to all HasLineOfSight calls below.
	// TInlineAllocator keeps up to 4 entries on the stack (weapons + accessories — no heap alloc in the common case).
	TArray<AActor*, TInlineAllocator<4>> TickIgnoredAttached;
	Ctx.Companion->ForEachAttachedActors([&TickIgnoredAttached](AActor* A) { TickIgnoredAttached.Add(A); return true; });

	// Snapshot the handle we tracked BEFORE it can be overwritten — the slot-loss guard
	// needs the previous-tick value to detect a monitor-commit vs genuine loss.
	const FCoverHandle PrevTickCoverHandle = LastTickCoverHandle;

	// Detect cover swap without ExecuteTask re-running; re-resolve lean side for the new point.
	// Reset dwell timers so the monitor-swapped point gets its dwell protection before the first validity eval.
	if (Cover.IsValid() && Cover.Handle != LastTickCoverHandle)
	{
		CurrentLean = UCoverGeometryStatics::ResolveLeanSide(Cover.Data, Cover.Data.bCrouchedCover, TargetLocation);
		BlockedRecheckHits = 0;
		RepositionTargetCover = FCover();
		RepositionTargetWorldLoc = FVector::ZeroVector;
		bRepositionStandPhase = false;
		bStandUpRepositionWalking = false;
		bRepositionStartLogged = false;
		CurrentBurstAction = EPeekAction::Hold;
		CoverCompromiseConsecutiveCount = 0;
		ArcStarvationCount = 0;
		TimeAtCurrentCover = 0.f;
		CoverValidityCheckTimer = 0.f;
	}
	if (Cover.IsValid()) LastTickCoverHandle = Cover.Handle;

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

	{
		const float RetentionMult = TickTuning ? TickTuning->EngageRangeRetentionMultiplier : 1.15f;
		const float RetainRange = Ctx.Companion->MaxEngageRange * RetentionMult;
		if (Distance > RetainRange)
		{
			if (bDebugLogging)
				UE_LOG(LogCompanionAI, Log, TEXT("%s: FIRE STOP reason=out-of-range dist=%.0f > RetainRange=%.0f"),
					*Ctx.Companion->GetName(), Distance, RetainRange);
			Ctx.Companion->StopWeaponFire();
			Ctx.Blackboard->ClearValue(CombatTargetKey.SelectedKeyName);
			return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		}
	}

	UCompanionAnimInstance* Anim = GetCompanionAnim(Ctx.Companion);

	const bool bSuppressed = Ctx.Companion->IsSuppressed(SuppressionWindowSeconds);
	const bool bLowHp = Ctx.Companion->GetHealthFraction() < LowHealthFraction;

	// Slot-loss guard: cover dropped mid-task while companion was in cover branch.
	// Prevents falling through to open-engage with stale crouch / firing state.
	// If the monitor REPLACED the target (CoverTarget is still valid but different from what we tracked),
	// finish Succeeded so OnTaskFinished's teardown preserves the new target rather than wiping it.
	// Uses PrevBranch (pre-sync snapshot) — LastTickBranch is already synced to CurrentBranch above.
	if (PrevBranch == 0 && !bHasCover)
	{
		const FCover MonitorCover = Ctx.Blackboard->GetValue<UBlackboardKeyType_Cover>(CoverTargetKey.GetSelectedKeyID());
		const bool bMonitorReplaced = MonitorCover.IsValid() && MonitorCover.Handle != PrevTickCoverHandle;
		if (Ctx.Companion->bIsCrouched)
		{
			UE_LOG(LogCompanionDiag, Verbose, TEXT("%s: [CrouchCall] t=%.3f site=SlotLossGuard action=UnCrouch"),
				*GetNameSafe(Ctx.Companion), Ctx.Companion->GetWorld() ? Ctx.Companion->GetWorld()->GetTimeSeconds() : 0.f);
			Ctx.Companion->UnCrouch();
		}
		bIsFiringBurst = false;
		if (bMonitorReplaced)
		{
			UE_LOG(LogCompanionAI, Log, TEXT("Slot replaced by monitor on companion %s — finishing Succeeded to preserve new target"), *GetNameSafe(Ctx.Companion));
			return FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
		}
		UE_LOG(LogCompanionAI, Warning, TEXT("Slot lost mid-task on companion %s — aborting cleanly"), *GetNameSafe(Ctx.Companion));
		Ctx.Blackboard->ClearValue(CombatTargetKey.SelectedKeyName);
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
		const bool bShouldCrouch = Cover.Data.bCrouchedCover;
		if (bShouldCrouch && !Ctx.Companion->bIsCrouched)
		{
			UE_LOG(LogCompanionDiag, Verbose, TEXT("%s: [CrouchCall] t=%.3f site=CoverIdlePostureSync action=Crouch"),
				*GetNameSafe(Ctx.Companion), Ctx.Companion->GetWorld() ? Ctx.Companion->GetWorld()->GetTimeSeconds() : 0.f);
			Ctx.Companion->Crouch();
		}
		else if (!bShouldCrouch && Ctx.Companion->bIsCrouched)
		{
			UE_LOG(LogCompanionDiag, Verbose, TEXT("%s: [CrouchCall] t=%.3f site=CoverIdlePostureSync action=UnCrouch"),
				*GetNameSafe(Ctx.Companion), Ctx.Companion->GetWorld() ? Ctx.Companion->GetWorld()->GetTimeSeconds() : 0.f);
			Ctx.Companion->UnCrouch();
		}

		const bool bRepoActive = (CurrentBurstAction == EPeekAction::Reposition && RepositionTargetCover.IsValid());

		// Early Reposition dispatch — a committed Reposition must not wait on the action-roll cooldown gate.
		if (bRepoActive)
		{
			TickRepositionAction(Ctx.Companion, Anim, Ctx.Blackboard, Cover.Data, bSuppressed, bLowHp, DeltaSeconds);
			return;
		}

		const UCapsuleComponent* IdleCap = Ctx.Companion->GetCapsuleComponent();
		const float IdleStandoff = (IdleCap ? IdleCap->GetScaledCapsuleRadius() : 34.f) + 10.f;
		const FVector HunkerLoc = UCoverGeometryStatics::GetHunkerPosition(Cover.Data, IdleStandoff);

#if ENABLE_DRAW_DEBUG
		if (bDebugLogging)
		{
			const FVector HunkerPt = HunkerLoc + FVector(0.f, 0.f, 10.f);
			DrawDebugSphere(Ctx.Companion->GetWorld(), HunkerPt, 22.f, 8, FColor::Red, false, 0.f, 0, 1.5f);
			if (CurrentLean != ECoverLean::None)
			{
				const FVector PeekPt = UCoverGeometryStatics::GetLeanPeekPosition(Cover.Data, CurrentLean) + FVector(0.f, 0.f, 20.f);
				DrawDebugSphere(Ctx.Companion->GetWorld(), PeekPt, 14.f, 12, FColor::Magenta, false, 0.f, 0, 1.5f);
				DrawDebugLine(Ctx.Companion->GetWorld(), HunkerPt, PeekPt, FColor::Magenta, false, 0.f, 0, 2.5f);
			}
			// Eye-height marker showing where stand-fire LoS trace originates.
			const FVector EyeMarker = HunkerLoc + FVector(0.f, 0.f, StandFireEyeHeight);
			DrawDebugSphere(Ctx.Companion->GetWorld(), EyeMarker, 6.f, 6, FColor::Cyan, false, 0.f, 0, 1.f);
		}
#endif

		const FVector SubSlotLoc = HunkerLoc;

		// Refresh peek side only when geometry has changed beyond threshold.
		const FVector CoverLoc = SubSlotLoc;
		const float CoverDeltaSq = FVector::DistSquared(CoverLoc, LastPeekResolveCoverLoc);
		const float TargetDeltaSq = FVector::DistSquared(TargetLocation, LastPeekResolveTargetLoc);
		if (CoverDeltaSq > PeekResolveDistThresholdSq || TargetDeltaSq > PeekResolveDistThresholdSq)
		{
			CurrentLean = UCoverGeometryStatics::ResolveLeanSide(Cover.Data, Cover.Data.bCrouchedCover, TargetLocation);
			ResolvedPeekSide = (CurrentLean == ECoverLean::Left) ? EPeekSide::Left : EPeekSide::Right;
			LastPeekResolveCoverLoc = CoverLoc;
			LastPeekResolveTargetLoc = TargetLocation;
		}

		// Periodic slot-validity check.
		CoverValidityCheckTimer += DeltaSeconds;
		if (CoverValidityCheckTimer >= CoverValidityCheckInterval && TimeAtCurrentCover >= MinCoverDwellBeforeReEval)
		{
			CoverValidityCheckTimer = 0.f;

			ACoverSystem* CoverSysCheck = ACoverSystem::GetCoverSystem(Ctx.Companion->GetWorld());
			AController* CoverController = Ctx.Companion->GetController();
			AController* CoverOccupant = (CoverSysCheck && CoverController) ? CoverSysCheck->GetOccupyingController(Cover.Handle) : nullptr;
			const bool bOccupiedByOther = CoverOccupant && CoverOccupant != CoverController;
			if (bOccupiedByOther)
			{
				UE_LOG(LogCompanionAI, Log, TEXT("%s: Cover INVALIDATE reason=slot-released"), *Ctx.Companion->GetName());
				Ctx.Blackboard->ClearValue(CoverTargetKey.GetSelectedKeyID());
				Ctx.Blackboard->SetValueAsBool(HasCoverPositionKey.SelectedKeyName, false);
				return FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
			}

			const float ArcHalfAngleDeg   = TickTuning ? TickTuning->CoverFlankArcHalfAngleDeg : 60.f;
			const float ArcSlackDeg       = TickTuning ? TickTuning->CoverCompromiseArcSlackDeg : 0.f;
			const int32 DebounceRequired  = TickTuning ? TickTuning->CoverCompromiseDebounce : 2;
			const float ChestH            = TickTuning ? TickTuning->CoverProtectionChestHeight : 60.f;
			const bool  bProtectRequired  = TickTuning ? TickTuning->bCoverRequiresBodyProtection : false;
			const int32 MaxThreats        = TickTuning ? TickTuning->MaxThreatsForCoverScoring : 3;
			const int32 StarvationThreshold = TickTuning ? TickTuning->CoverArcStarvationEvals : 4;

			// Reset the compromise debounce when the combat target changes — a fresh target must not
			// inherit violation counts accrued against the old one, or retargeting among surrounding
			// enemies rage-quits the cover before the switch-monitor gets its debounced relocate chance.
			if (LastValidatedTarget.Get() != Ctx.Target)
			{
				CoverCompromiseConsecutiveCount = 0;
				LastValidatedTarget = Ctx.Target;
			}

			const FVector ToTarget2D = (TargetLocation - Cover.Data.Location).GetSafeNormal2D();
			const FVector FireFwd    = UCoverGeometryStatics::GetFireArcForward(Cover.Data);
			const float   ArcDot     = FVector::DotProduct(FireFwd, ToTarget2D);

			// At least one lean side must be able to see the target (mirrors the old "peekable corner" gate).
			// This is a genuine unusability of the point (not a transient flank) — instant invalidate is fine.
			// Stamp MarkVacated so the EQS PostVacate filter blocks an immediate re-pick of the same blind point.
			if (!UCoverGeometryStatics::CanPeekShoot(Ctx.Companion->GetWorld(), Cover.Data, Cover.Data.bCrouchedCover,
				TargetLocation, StandFireEyeHeight, Ctx.Target, Ctx.Companion))
			{
				UE_LOG(LogCompanionAI, Log, TEXT("%s: Cover INVALIDATE reason=no-peek-los"), *Ctx.Companion->GetName());
				if (UWorld* VacateWorld = Ctx.Companion->GetWorld())
				{
					if (UCoverReservationSubsystem* VacateSub = VacateWorld->GetSubsystem<UCoverReservationSubsystem>())
					{
						if (AController* VacateCtrl = Ctx.Companion->GetController())
							VacateSub->MarkVacated(Cover.Handle, VacateCtrl);
					}
				}
				Ctx.Blackboard->ClearValue(CoverTargetKey.GetSelectedKeyID());
				Ctx.Blackboard->SetValueAsBool(HasCoverPositionKey.SelectedKeyName, false);
				return FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
			}

			// Flank-compromise check: is the enemy getting an angle on the companion in cover?
			// Mirrors the enemy's IsCoverCompromised + CompromiseDebounceRequired pattern. Fire-arc
			// violation is folded in here (no longer an instant hard-fail) so a single retarget among
			// surrounding enemies can't dump the cover before the debounce elapses.
			// Compromised = focused target outside the arc widened by ArcSlackDeg, OR
			//               ANY of the closest N known threats has a clear angle on the hunkered body.
			{
				const float WidenedHalfArcDeg = ArcHalfAngleDeg + ArcSlackDeg;
				const bool bOutsideArc = ArcDot < FMath::Cos(FMath::DegreesToRadians(WidenedHalfArcDeg));

				// Body-shield test: if the hunker position can be seen by any considered threat, the wall
				// is no longer interposing — flanked / body exposed. Only run if protection was requested
				// (default on). Consider the closest N threats so cover exposed to a non-focused attacker
				// is treated as compromised (multi-threat surround case).
				bool bBodyExposed = false;
				if (bProtectRequired)
				{
					const UCapsuleComponent* CompCap = Ctx.Companion->GetCapsuleComponent();
					const float CapsuleR = CompCap ? CompCap->GetScaledCapsuleRadius() : 34.f;
					const float Standoff = CapsuleR + 10.f;

					TArray<AActor*, TInlineAllocator<8>> Threats;
					GatherKnownThreats(Ctx.Companion, Ctx.Target, MaxThreats, Threats);
					for (AActor* Threat : Threats)
					{
						if (!IsValid(Threat)) continue;
						if (!UCoverGeometryStatics::IsThreatCovered(Ctx.Companion->GetWorld(), Cover.Data,
							Threat->GetActorLocation(), Standoff, ChestH, Threat, Ctx.Companion))
						{
							bBodyExposed = true;
							break;
						}
					}
				}

				const bool bCompromisedNow = bOutsideArc || bBodyExposed;
				if (bCompromisedNow)
					++CoverCompromiseConsecutiveCount;
				else
					CoverCompromiseConsecutiveCount = 0;

				// Target-agnostic starvation backstop: accrues across ALL targets whenever the
				// current target is outside the widened arc. Prevents fast-flip deadlocks where
				// the per-target debounce never accumulates because the target keeps changing.
				if (bOutsideArc)
					++ArcStarvationCount;
				else
					ArcStarvationCount = 0;

				const bool bPerTargetTripped = CoverCompromiseConsecutiveCount >= DebounceRequired;
				const bool bStarvationTripped = StarvationThreshold > 0 && ArcStarvationCount >= StarvationThreshold;

				if (bPerTargetTripped || bStarvationTripped)
				{
					CoverCompromiseConsecutiveCount = 0;
					ArcStarvationCount = 0;
					UE_LOG(LogCompanionAI, Log,
						TEXT("%s: Cover INVALIDATE reason=%s outsideArc=%d bodyExposed=%d"),
						*Ctx.Companion->GetName(),
						bStarvationTripped ? TEXT("arc-starvation") : TEXT("flanked-compromised"),
						(int32)bOutsideArc, (int32)bBodyExposed);
					if (UWorld* InvalidateWorld = Ctx.Companion->GetWorld())
					{
						if (UCoverReservationSubsystem* ResSub = InvalidateWorld->GetSubsystem<UCoverReservationSubsystem>())
						{
							if (AController* Controller = Ctx.Companion->GetController())
								ResSub->MarkVacated(Cover.Handle, Controller);
						}
					}
					Ctx.Blackboard->ClearValue(CoverTargetKey.GetSelectedKeyID());
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
			RepositionTargetCover = FCover();
			bRepositionStandPhase = false;
			ConsecutiveHolds = 0;
			PeekCooldown = 0.f;
			TimeInCoverIdle = 0.f;
			bReloadGateActive = false;
			ReloadGateStartTime = 0.f;
			// Fix 3b: clear the just-repositioned latch — otherwise a stray set (e.g. an aborted move) could
			// keep the reposition/corner-peek family zeroed across the force re-roll and re-starve the roll.
			bJustRepositioned = false;
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

		// Gate 2: LoS from the resolved lean-peek position (collapses the old stand-eye + corner-apex fallback —
		// a cover point either has a valid lean toward the target or it doesn't; CanPeekShoot already
		// resolves the best lean side and traces from its peek position).
		AActor* BlockedBy = nullptr;
		bool bLosFromCover = UCoverGeometryStatics::CanPeekShoot(Ctx.Companion->GetWorld(), Cover.Data,
			Cover.Data.bCrouchedCover, TargetLocation, StandFireEyeHeight, Ctx.Target, Ctx.Companion);
		if (bDebugLogging)
		{
			const bool bNowBlocked = !bLosFromCover;
			const bool bStateChanged = (bNowBlocked != bLastLosBlocked);
			if (bStateChanged)
			{
				if (bNowBlocked)
				{
					UE_LOG(LogCompanionAI, Log, TEXT("%s: cover-LoS BLOCKED"), *Ctx.Companion->GetName());
				}
				else
				{
					UE_LOG(LogCompanionAI, Log, TEXT("%s: cover-LoS CLEAR"), *Ctx.Companion->GetName());
				}
				bLastLosBlocked = bNowBlocked;
			}
		}
		if (!bLosFromCover)
		{
			// Skip re-pick while a movement action owns its own positioning.
			if (CurrentBurstAction == EPeekAction::Reposition
				|| CurrentBurstAction == EPeekAction::StandUpAndReposition
				|| CurrentBurstAction == EPeekAction::CornerPeek)
				return;

			// Throttled shuffle re-pick: only when unsuppressed.
			SubSlotLosRecheckTimer -= DeltaSeconds;
			if (SubSlotLosRecheckTimer <= 0.f && !bSuppressed)
			{
				SubSlotLosRecheckTimer = SubSlotLosRecheckInterval;
				BlockedRecheckHits = FMath::Min<uint8>(BlockedRecheckHits + 1, 255);

				// Require 2 consecutive gated blocked checks before teleporting (anti-thrash).
				if (BlockedRecheckHits >= 2)
				{
					const FCover BestCover = FindShuffleCover(Ctx.Companion, Cover, TargetLocation);
					if (BestCover.IsValid())
					{
						CommitCoverSwitch(Ctx.Blackboard, BestCover, Ctx.Companion->GetController());
						CurrentLean = UCoverGeometryStatics::ResolveLeanSide(BestCover.Data, BestCover.Data.bCrouchedCover, TargetLocation);
						const UCapsuleComponent* TpCap = Ctx.Companion->GetCapsuleComponent();
						const float TpStandoff = (TpCap ? TpCap->GetScaledCapsuleRadius() : 34.f) + 10.f;
						const FVector NewHunkerLoc = UCoverGeometryStatics::GetHunkerPosition(BestCover.Data, TpStandoff);
						const FRotator SlotYawRot(0.f, UCoverGeometryStatics::GetFireArcForward(BestCover.Data).Rotation().Yaw, 0.f);
						const FVector TeleportDest(NewHunkerLoc.X, NewHunkerLoc.Y, Ctx.Companion->GetActorLocation().Z);
						if (bDebugLogging) UE_LOG(LogCompanionDiag, Verbose, TEXT("%s: MID-COMBAT-SHUFFLE-TELEPORT dist=%.0f isReloading=%d"),
							*Ctx.Companion->GetName(), FVector::Dist(Ctx.Companion->GetActorLocation(), TeleportDest), (int32)Ctx.Companion->IsReloading());
						Ctx.Companion->TeleportTo(TeleportDest, SlotYawRot, false, false);
						LastPeekResolveCoverLoc = FVector::ZeroVector;
						LastPeekResolveTargetLoc = FVector::ZeroVector;
						BlockedRecheckHits = 0;
						if (BestCover.Data.bCrouchedCover)
						{
							UE_LOG(LogCompanionDiag, Verbose, TEXT("%s: [CrouchCall] t=%.3f site=SubSlotTeleport action=Crouch"),
								*GetNameSafe(Ctx.Companion), Ctx.Companion->GetWorld() ? Ctx.Companion->GetWorld()->GetTimeSeconds() : 0.f);
							Ctx.Companion->Crouch();
						}
						else
						{
							UE_LOG(LogCompanionDiag, Verbose, TEXT("%s: [CrouchCall] t=%.3f site=SubSlotTeleport action=UnCrouch"),
								*GetNameSafe(Ctx.Companion), Ctx.Companion->GetWorld() ? Ctx.Companion->GetWorld()->GetTimeSeconds() : 0.f);
							Ctx.Companion->UnCrouch();
						}
						const ECoverHeight NewHeight = BestCover.Data.bCrouchedCover ? ECoverHeight::Crouch : ECoverHeight::Stand;
						if (Anim) Anim->EnterCoverPose(ResolvedPeekSide, NewHeight);
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
				UE_LOG(LogCompanionDiag, Verbose, TEXT("%s: RELOAD-START gate=697 ammo=%d/%d reserve=%d vel=%.1f hasCover=%d reloadTime=%.2f"),
					*Ctx.Companion->GetName(),
					W ? W->GetCurrentAmmo() : -1,
					W && W->GetWeaponData() ? W->GetWeaponData()->MagazineSize : -1,
					W ? W->GetReserveAmmo() : -1,
					Ctx.Companion->GetVelocity().Size(),
					(int32)bHasCover,
					ReloadTime);
				UE_LOG(LogCompanionAI, Log, TEXT("%s: reloading before stand-up"), *Ctx.Companion->GetName());
			}
			Ctx.Companion->ReloadWeapon();
			return;
		}
		if (Ctx.Companion->IsReloading()) return;

		// StandUpAndReposition always runs in BRANCH 1 (bIsFiringBurst=true throughout).
		// No silent-walk fallback needed here.

		// Gate 4: cover point supports a peek (CurrentLean resolved non-None). Points with no
		// valid lean toward the target are unusable for combat (mirrors the old "no peekable corner" gate).
		const bool bIsCrouchCover = Cover.Data.bCrouchedCover;
		if (CurrentLean == ECoverLean::None)
		{
			if (bDebugLogging)
				UE_LOG(LogCompanionAI, Log, TEXT("%s: Cover REJECT cover point has no valid lean side — unusable for combat"),
					*Ctx.Companion->GetName());
			return;
		}

		// Pre-peek ammo gate: never expose without enough ammo for a useful burst.
		if (TryPrePeekReloadGate(Ctx.Companion, Cover.Data))
		{
			TimeInCoverIdle = 0.f;
			return;
		}

		// Arc gate: CanPeekShoot (Gate 2) can pass on raw LoS even when the focused target is outside
		// this point's fire arc - peeking then would fire through/around the wall at an off-arc enemy.
		// Stay hunkered this cycle instead; the debounced compromise check above is already accruing
		// against the arc violation and will relocate the companion deliberately if it persists.
		// Uses the SAME widened arc (base + slack) the compromise counter uses — zero dead zone.
		{
			const float ArcGateHalfAngleDeg = (TickTuning ? TickTuning->CoverFlankArcHalfAngleDeg : 60.f)
				+ (TickTuning ? TickTuning->CoverCompromiseArcSlackDeg : 0.f);
			const FVector ArcToTarget2D = (TargetLocation - Cover.Data.Location).GetSafeNormal2D();
			const FVector ArcFireFwd    = UCoverGeometryStatics::GetFireArcForward(Cover.Data);
			const float   ArcGateDot    = FVector::DotProduct(ArcFireFwd, ArcToTarget2D);
			if (ArcGateDot < FMath::Cos(FMath::DegreesToRadians(ArcGateHalfAngleDeg)))
			{
				PeekCooldown = FMath::RandRange(MinPeekCooldown, MaxPeekCooldown);
				TimeInCoverIdle = 0.f;
				if (bDebugLogging)
					UE_LOG(LogCompanionAI, Log, TEXT("%s: STAY DOWN reason=target-out-of-arc"), *Ctx.Companion->GetName());
				return;
			}
		}

		// Stand-eye gate (restores the old "stand-eye" eligibility branch dropped in the P3 port).
		// Gate 2 (CanPeekShoot) validates LoS from the LEAN-PEEK position (65uu sideways), but the in-place
		// fire actions (Stand/Quick) fire from the HUNKER position — so a crouch point whose lean has LoS
		// can still have its own wall blocking the stand-up-in-place shot, burning the magazine into the wall.
		// Trace from the actual hunker position at stand-fire eye height toward the target: if BLOCKED, exclude
		// the in-place fire family from the roll (leaving lean/corner-peek, Reposition, StandUpAndReposition,
		// Hold). If CLEAR, the weight table is unchanged.
		AActor* StandEyeBlocker = nullptr;
		const FVector StandEye = SubSlotLoc + FVector(0.f, 0.f, StandFireEyeHeight);
		const bool bStandEyeClear = HasLineOfSight(Ctx.Companion->GetWorld(), StandEye, Ctx.Target,
			Ctx.Companion, StandEyeBlocker, TickIgnoredAttached);
		if (!bStandEyeClear && bDebugLogging)
			UE_LOG(LogCompanionAI, Log, TEXT("%s: stand-eye BLOCKED by %s — excluding in-place fire from roll"),
				*Ctx.Companion->GetName(), *GetNameSafe(StandEyeBlocker));

		// Gate 5: roll peek action. A single cover point has one lean side — "endpoint" always true,
		// and shuffle (FindShuffleCover) replaces the old line-length gate for Reposition eligibility.
		const bool bAtPeekableEndpoint = true;
		const bool bLineLongEnough = true;

		UE_LOG(LogCompanionAI, Log,
			TEXT("%s: PEEK-DECISION height=%s lean=%d -> weightsPath=%s"),
			*Ctx.Companion->GetName(),
			bIsCrouchCover ? TEXT("Crouch") : TEXT("Stand"),
			(int32)CurrentLean,
			bIsCrouchCover ? TEXT("Crouch") : TEXT("StandEndpoint"));

		EPeekAction Action = EPeekAction::Hold;
		if (bIsCrouchCover)
		{
			// Disable Reposition/StandUpAndReposition when line is too short, or when we just repositioned.
			const float RepoW        = (bLineLongEnough && !bJustRepositioned) ? (bLowHp ? LowHpRepositionWeight            : RepositionWeight)           : 0.f;
			// Fix 2a: StandUpAndReposition Phase A stands up and fires IN PLACE at the hunker position before
			// walking. If the stand-eye is blocked (own wall in front of the stand-up shot) Phase A burns the
			// magazine into the wall — so gate its weight on bStandEyeClear too, same as the in-place Stand/Quick.
			const float StandUpRepoW = (bLineLongEnough && !bJustRepositioned && bStandEyeClear) ? (bLowHp ? LowHpStandUpAndRepositionWeight  : StandUpAndRepositionWeight)  : 0.f;
			// In-place fire (Stand/Quick) fires from the hunker position — zero its weight when the
			// stand-eye trace is blocked so the roll can't pick a shot that would hit our own wall.
			const float StandW = bStandEyeClear ? (bLowHp ? LowHpStandWeight : StandWeight) : 0.f;
			const float QuickW = bStandEyeClear ? (bLowHp ? LowHpQuickWeight : QuickWeight) : 0.f;
			// Fix 3c: when the stand-eye is blocked, the in-place fire family is zeroed and (in tight crouch
			// geometry with no same-wall neighbour) reposition can be zero too, leaving only Hold — the
			// companion holds forever despite lean-peek having CONFIRMED LoS (Gate 2). Offer CornerPeek as the
			// crouch lean-fire option: it fires from GetLeanPeekPosition (exactly the position Gate 2/CanPeekShoot
			// validated), is height-agnostic, and stays crouched. Only surface it when the in-place shot is
			// blocked — when the stand-eye is clear the existing Stand/Quick family already covers firing.
			const float CornerPeekW = (!bStandEyeClear && CurrentLean != ECoverLean::None)
				? (bLowHp ? LowHpCornerPeekWeight : CornerPeekWeight) : 0.f;
			const TPair<EPeekAction, float> CrouchWeights[] = {
				{ EPeekAction::Stand,                StandW },
				{ EPeekAction::Quick,                QuickW },
				{ EPeekAction::Hold,                 bLowHp ? LowHpHoldWeight   : HoldWeight  },
				{ EPeekAction::Reposition,           RepoW },
				{ EPeekAction::StandUpAndReposition, StandUpRepoW },
				{ EPeekAction::CornerPeek,           CornerPeekW },
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
				// Never promote to any in-place stand-fire family when the stand-eye is blocked (fires into
				// our own wall). Fix 2b: StandUpAndReposition ALSO fires in place (Phase A) before walking,
				// so the old blocked-stand-eye promotion to StandUpAndReposition still burned the magazine
				// into the wall.
				if (bStandEyeClear)
				{
					Action = EPeekAction::Stand;
				}
				// Fix 3c: stand-eye blocked but Gate 2 confirmed a lean has LoS — promote to CornerPeek, which
				// fires from the validated lean-peek position instead of the blocked hunker. This is the fire
				// path out of the blocked-stand-eye crouch deadlock.
				else if (CurrentLean != ECoverLean::None)
				{
					Action = EPeekAction::CornerPeek;
				}
				// No usable lean-fire — fall back to a SILENT Reposition (walks to a new same-wall point, no
				// in-place fire), or extend the hold if no reposition is available.
				else if (bLineLongEnough && !bJustRepositioned)
				{
					Action = EPeekAction::Reposition;
				}
				else
				{
					++ConsecutiveHolds;
					PeekCooldown = FMath::RandRange(MinPeekCooldown, MaxPeekCooldown);
					TimeInCoverIdle = 0.f;
					if (bDebugLogging)
						UE_LOG(LogCompanionAI, Log, TEXT("%s: HOLD-CAP promote suppressed — stand-eye blocked, no lean-fire, no reposition"),
							*Ctx.Companion->GetName());
					return;
				}
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
			// Block reposition if the player is standing at this cover point.
			AActor* Player = Cast<AActor>(Ctx.Blackboard->GetValueAsObject(ACompanionAIController::BB_PlayerActor));
			if (IsValid(Player) && FVector::Dist(Player->GetActorLocation(), Cover.Data.Location) <= FinalApproachAcceptRadius)
			{
				CurrentBurstAction = EPeekAction::Hold;
				++ConsecutiveHolds;
				PeekCooldown = FMath::RandRange(MinPeekCooldown, MaxPeekCooldown);
				TimeInCoverIdle = 0.f;
				UE_LOG(LogCompanionAI, Log, TEXT("%s: PEEK-ACTION=Hold (player-overlap-cover)"), *GetNameSafe(Ctx.Companion));
				return;
			}

			const FCover ShuffleTarget = FindShuffleCover(Ctx.Companion, Cover, TargetLocation);
			if (!ShuffleTarget.IsValid())
			{
				// No neighbouring cover point in range — no reposition possible from here. Treat as Hold.
				// Fix 3a: do NOT set bJustRepositioned here — nothing actually moved. Setting it starved the
				// next roll of Reposition/StandUpAndReposition/CornerPeek weights, and in blocked-stand-eye
				// crouch geometry (Stand/Quick zeroed) that left ONLY Hold rollable, so nothing ever reset
				// bJustRepositioned again → permanent hold despite lean-peek having confirmed LoS.
				CurrentBurstAction = EPeekAction::Hold;
				++ConsecutiveHolds;
				PeekCooldown = FMath::RandRange(MinPeekCooldown, MaxPeekCooldown);
				TimeInCoverIdle = 0.f;
				return;
			}

			RepositionTargetCover = ShuffleTarget;
			RepositionTargetWorldLoc = ShuffleTarget.Data.Location;
			CachedSlotForwardYaw = UCoverGeometryStatics::GetFireArcForward(ShuffleTarget.Data).Rotation().Yaw;

			// Stamp intended cover so other agents see the reservation during the walk.
			if (UWorld* IntentStampWorld = Ctx.Companion->GetWorld())
			{
				if (UCoverReservationSubsystem* IntentStampSub = IntentStampWorld->GetSubsystem<UCoverReservationSubsystem>())
				{
					if (AController* IntentStampCtrl = Ctx.Companion->GetController())
						IntentStampSub->SetIntendedCover(IntentStampCtrl, ShuffleTarget.Handle);
				}
			}

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
					UE_LOG(LogCompanionDiag, Verbose, TEXT("%s: REPOSITION-START kind=silent toLoc=%s dist=%.0f isReloading=%d"),
						*Ctx.Companion->GetName(), *RepositionTargetWorldLoc.ToString(),
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
			UE_LOG(LogCompanionDiag, Verbose, TEXT("%s: [CrouchCall] t=%.3f site=StandUpRepoCommit action=UnCrouch"),
				*GetNameSafe(Ctx.Companion), Ctx.Companion->GetWorld() ? Ctx.Companion->GetWorld()->GetTimeSeconds() : 0.f);
			Ctx.Companion->UnCrouch();
			if (Anim)
			{
				Anim->ExitCoverPose();
				ActivePeekMontage = Anim->PlayPeekFire(ResolvedPeekSide);
			}
			Ctx.Companion->StartWeaponFire();
			DebugBurstLosCheckTimer = 0.f;
			// Fix 2c: arm the Phase A muzzle-withhold state fresh (mirrors the Stand/Quick commit).
			StandBurstMuzzleCheckTimer = 0.f;
			bStandBurstFireHeld = false;
			TimeInCoverIdle = 0.f;
			TimeAtCurrentCover = 0.f;
			CoverValidityCheckTimer = 0.f;
			LastRepositionDist = 0.f;
			RepositionElapsed = 0.f;
			RepositionStalledTime = 0.f;
			bRepositionStartLogged = true;
			if (bDebugLogging)
			{
				UE_LOG(LogCompanionAI, Log, TEXT("%s: STAND-UP-FIRE start action=StandReposition burst=%.2fs side=%s"),
					*Ctx.Companion->GetName(), BurstTimer,
					ResolvedPeekSide == EPeekSide::Right ? TEXT("Right") : TEXT("Left"));
				UE_LOG(LogCompanionDiag, Verbose, TEXT("%s: REPOSITION-START kind=standup toLoc=%s dist=%.0f isReloading=%d"),
					*Ctx.Companion->GetName(), *RepositionTargetWorldLoc.ToString(),
					LastRepositionDist, (int32)Ctx.Companion->IsReloading());
			}
			return;
		}

		if (Action == EPeekAction::CornerPeek)
		{
			CornerPeekHomeLocation = SubSlotLoc;
			// CornerPeek apex = the resolved lean-peek position. Captured at commit — assumes the
			// cover point's lean geometry doesn't change mid-action.
			CornerPeekApexLocation = (CurrentLean != ECoverLean::None)
				? UCoverGeometryStatics::GetLeanPeekPosition(Cover.Data, CurrentLean)
				: CornerPeekHomeLocation;
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
				TEXT("%s: PEEK-ACTION=CornerPeek home=%s apex=%s burst=%.2fs ammo=%d"),
				*GetNameSafe(Ctx.Companion),
				*CornerPeekHomeLocation.ToString(), *CornerPeekApexLocation.ToString(),
				BurstTimer, Ctx.Companion->GetCurrentAmmo());
#if ENABLE_DRAW_DEBUG
			if (bDebugLogging)
			{
				DrawDebugLine(Ctx.Companion->GetWorld(), CornerPeekHomeLocation + FVector(0, 0, 20.f),
					CornerPeekApexLocation + FVector(0, 0, 20.f), FColor::Magenta, false, 3.f, 0, 4.f);
				DrawDebugSphere(Ctx.Companion->GetWorld(), CornerPeekApexLocation + FVector(0, 0, 20.f),
					18.f, 12, FColor::Magenta, false, 3.f, 0, 2.f);
			}
#endif
			DebugBurstLosCheckTimer = 0.f;
			CornerPeekLosCheckTimer = 0.f;
			TimeInCoverIdle = 0.f;
			TimeAtCurrentCover = 0.f;
			CoverValidityCheckTimer = 0.f;
			if (bDebugLogging)
				UE_LOG(LogCompanionAI, Log, TEXT("%s: CORNER-PEEK apex=(%.0f,%.0f,%.0f) burst=%.2fs"), *Ctx.Companion->GetName(), CornerPeekApexLocation.X, CornerPeekApexLocation.Y, CornerPeekApexLocation.Z, BurstTimer);
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

		UE_LOG(LogCompanionDiag, Verbose, TEXT("%s: [CrouchCall] t=%.3f site=StandQuickPeekCommit action=UnCrouch"),
			*GetNameSafe(Ctx.Companion), Ctx.Companion->GetWorld() ? Ctx.Companion->GetWorld()->GetTimeSeconds() : 0.f);
		Ctx.Companion->UnCrouch();
		Ctx.Companion->StartWeaponFire();
		bIsFiringBurst = true;
		DebugBurstLosCheckTimer = 0.f;
		StandBurstMuzzleCheckTimer = 0.f;
		bStandBurstFireHeld = false;
		TimeInCoverIdle = 0.f;
		TimeAtCurrentCover = 0.f;
		CoverValidityCheckTimer = 0.f;

		if (bDebugLogging)
			UE_LOG(LogCompanionAI, Log, TEXT("%s: STAND-UP-FIRE start action=%s burst=%.2fs side=%s"),
				*Ctx.Companion->GetName(),
				(CurrentBurstAction == EPeekAction::Quick) ? TEXT("Quick") : TEXT("Stand"),
				BurstTimer,
				ResolvedPeekSide == EPeekSide::Right ? TEXT("Right") : TEXT("Left"));
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
			&& bStandUpRepositionWalking && Cover.IsValid());
		const FRotator LookAtRot = (TargetLocation - MyLocation).Rotation();
		const FRotator DesiredRot = bUseSlotForward
			? FRotator(0.f, CachedSlotForwardYaw, 0.f)
			: FRotator(0.f, LookAtRot.Yaw, 0.f);
		Ctx.Companion->SetActorRotation(FMath::RInterpTo(Ctx.Companion->GetActorRotation(),
			DesiredRot, DeltaSeconds, Ctx.Companion->RotationInterpSpeed));

		// Dispatch new multi-phase actions before the shared burst logic.
		if (CurrentBurstAction == EPeekAction::StandUpAndReposition && RepositionTargetCover.IsValid())
		{
			// Fix 2c: Phase A is stand-up fire IN PLACE at the hunker — same wall-burn exposure as the plain
			// Stand/Quick burst, but the dispatch returns before the muzzle-withhold ran below. Run it here as
			// a backstop so a mid-Phase-A occlusion pauses the trigger (no shots into our own wall). Phase B
			// walks and owns its own fire cadence, so only guard Phase A (bRepositionStandPhase).
			if (bRepositionStandPhase && !bSuppressed && !Ctx.Companion->IsReloading())
				TickStandBurstMuzzleWithhold(Ctx.Companion, Ctx.Target, TickIgnoredAttached, DeltaSeconds);
			TickStandUpAndRepositionAction(Ctx.Companion, Anim, Ctx.Blackboard, Cover.Data, bSuppressed, bLowHp, DeltaSeconds);
			return;
		}
		if (CurrentBurstAction == EPeekAction::CornerPeek)
		{
			TickCornerPeekAction(Ctx.Companion, Anim, Cover.Data, Ctx.Target, bSuppressed, bLowHp, TickIgnoredAttached, DeltaSeconds);
			return;
		}

		// Suppression abort — duck back down immediately.
		if (bSuppressed)
		{
			if (bDebugLogging) UE_LOG(LogCompanionAI, Log, TEXT("%s: FIRE STOP reason=suppressed-mid-burst"), *Ctx.Companion->GetName());
			ReturnToCover(Ctx.Companion, Anim, Cover.Data, true, bLowHp);
			return;
		}

		// Reload mid-burst.
		if (Ctx.Companion->NeedsReload() && !Ctx.Companion->IsReloading())
		{
			if (bDebugLogging)
			{
				AWeaponBase* W = Ctx.Companion->GetCurrentWeapon();
				const float ReloadTime = (W && W->GetWeaponData()) ? W->GetWeaponData()->ReloadTime : -1.f;
				UE_LOG(LogCompanionDiag, Verbose, TEXT("%s: RELOAD-START gate=945 ammo=%d/%d reserve=%d vel=%.1f hasCover=%d reloadTime=%.2f"),
					*Ctx.Companion->GetName(),
					W ? W->GetCurrentAmmo() : -1,
					W && W->GetWeaponData() ? W->GetWeaponData()->MagazineSize : -1,
					W ? W->GetReserveAmmo() : -1,
					Ctx.Companion->GetVelocity().Size(),
					(int32)bHasCover,
					ReloadTime);
				const FVector B1ReloadLoc = Cover.IsValid() ? Cover.Data.Location : FVector::ZeroVector;
				UE_LOG(LogCompanionDiag, Verbose, TEXT("%s: RETURN-TO-COVER reason=reload moveTarget=%s dist=%.0f isReloading=%d"),
					*Ctx.Companion->GetName(), *B1ReloadLoc.ToString(),
					FVector::Dist(Ctx.Companion->GetActorLocation(), B1ReloadLoc),
					(int32)Ctx.Companion->IsReloading());
				UE_LOG(LogCompanionAI, Log, TEXT("%s: FIRE STOP reason=reload"), *Ctx.Companion->GetName());
			}
			// Only defer the reload if we're actually returning to crouch cover (where the
			// capsule resize would otherwise pop mid-reload-anim). Stand cover has no resize.
			if (Cover.IsValid() && Cover.Data.bCrouchedCover)
			{
				bPendingReloadAfterSnap = true;
			}
			else
			{
				Ctx.Companion->ReloadWeapon();
			}
			ReturnToCover(Ctx.Companion, Anim, Cover.Data, false, bLowHp);
			return;
		}

		// Muzzle-LoS withhold (plain Stand/Quick in-place burst only — StandUpAndReposition/CornerPeek
		// already returned above; StandUpAndReposition Phase A runs its own withhold backstop inside the
		// dispatch below). The pre-commit stand-eye gate stops a bad-position burst from starting, but the
		// target can duck behind cover mid-burst; withhold the trigger while the muzzle→target line is
		// blocked (no shots into the wall) and resume when clear. BurstTimer keeps counting, so FSM flow is
		// unchanged. Throttled to 10 Hz; uses the muzzle (where rounds originate), not the head eye.
		TickStandBurstMuzzleWithhold(Ctx.Companion, Ctx.Target, TickIgnoredAttached, DeltaSeconds);

		// Burst elapsed — return to cover.
		if (BurstTimer <= 0.f)
		{
			if (bDebugLogging)
			{
				const FVector B1BurstEndLoc = Cover.IsValid() ? Cover.Data.Location : FVector::ZeroVector;
				UE_LOG(LogCompanionDiag, Verbose, TEXT("%s: RETURN-TO-COVER reason=burst-end moveTarget=%s dist=%.0f isReloading=%d"),
					*Ctx.Companion->GetName(), *B1BurstEndLoc.ToString(),
					FVector::Dist(Ctx.Companion->GetActorLocation(), B1BurstEndLoc),
					(int32)Ctx.Companion->IsReloading());
				UE_LOG(LogCompanionAI, Log, TEXT("%s: FIRE STOP reason=burst-end-cover (next cd=%.2fs)"), *Ctx.Companion->GetName(), PeekCooldown);
			}
			ReturnToCover(Ctx.Companion, Anim, Cover.Data, false, bLowHp);
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
		UE_LOG(LogCompanionDiag, Verbose, TEXT("%s: [CrouchCall] t=%.3f site=CoverFlippedMidBurst action=UnCrouch"),
			*GetNameSafe(Ctx.Companion), Ctx.Companion->GetWorld() ? Ctx.Companion->GetWorld()->GetTimeSeconds() : 0.f);
		Ctx.Companion->UnCrouch();
		bCornerPeekFiring = false;
		bCornerPeekReturning = false;
		CornerPeekHomeLocation = FVector::ZeroVector;
		CornerPeekApexLocation = FVector::ZeroVector;
		CurrentBurstAction = EPeekAction::Hold;
		RepositionTargetCover = FCover();
		bRepositionStandPhase = false;
		bStandUpRepositionWalking = false;
		bRepositionStartLogged = false;
	}

	// Open-engage: defensively UnCrouch in case MoveToCover anticipated a crouch slot but ended
	// without arrival, leaving the companion crouched while shooting in the open.
	// Idempotent — UnCrouch is a no-op if not crouched.
	if (Ctx.Companion->bIsCrouched)
	{
		UE_LOG(LogCompanionDiag, Verbose, TEXT("%s: [CrouchCall] t=%.3f site=OpenEngageDefensive action=UnCrouch"),
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
		Ctx.Companion->GetWorld()->LineTraceSingleByChannel(LosHit, AimOrigin, AITargeting::GetSightLocation(Ctx.Target), ECC_Visibility, QueryParams);
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

		// Cover-commit gate (mirrors BTTask_MoveToCoverPoint): only leave open-engage for cover that is
		// worth the trip AND needed. Without this the reseek yanks the companion out of a stand-fight
		// toward any baked point in CoverSearchRadius.
		bool bCommitAllowed = true;
		if (CoverTuning && CoverTuning->bCoverCommitRequiresUnderFire)
		{
			const USuppressionComponent* CommitSupp = Ctx.Companion->GetSuppressionComponent();
			const bool bUnderFire = (CommitSupp && CommitSupp->IsSuppressed())
				|| Ctx.Companion->IsSuppressed(CoverTuning->CoverCommitUnderFireWindow);
			bCommitAllowed = bUnderFire;
		}

		if (CoverTuning && CoverWorld && bCommitAllowed)
		{
			ACoverSystem* CoverSys = ACoverSystem::GetCoverSystem(CoverWorld);
			UCoverReservationSubsystem* ResSub = CoverWorld->GetSubsystem<UCoverReservationSubsystem>();
			AController* CoverController = Ctx.Companion->GetController();
			if (CoverSys)
			{
				const float CommitRadius = FMath::Min(CoverTuning->CoverSearchRadius, CoverTuning->CoverCommitMaxDistance);
				TArray<FCover> OpenEngageCandidates;
				OpenEngageCandidates.Reserve(32);
				const FBoxSphereBounds SearchBounds(MyLocation, FVector(CommitRadius), CommitRadius);
				CoverSys->GetCoverDataWithinBounds(SearchBounds, OpenEngageCandidates);

				const UCapsuleComponent* OpenCap = Ctx.Companion->GetCapsuleComponent();
				const float OpenStandoff = (OpenCap ? OpenCap->GetScaledCapsuleRadius() : 34.f) + 10.f;
				const float ReseekArcCos = FMath::Cos(FMath::DegreesToRadians(CoverTuning->CoverFlankArcHalfAngleDeg));

				bool bFoundReachable = false;
				for (const FCover& Candidate : OpenEngageCandidates)
				{
					if (!Candidate.IsValid()) continue;
					// Bounds query is a box — enforce the commit radius as a true distance.
					if (FVector::DistSquared(MyLocation, Candidate.Data.Location) > FMath::Square(CommitRadius)) continue;
					AController* Occupant = CoverSys->GetOccupyingController(Candidate.Handle);
					if (Occupant && Occupant != CoverController) continue;
					if (IsValid(ResSub) && ResSub->IsOnPostVacateCooldown(Candidate.Handle, CoverController, CoverTuning->CoverSwitchPostVacateCooldown))
						continue;

					// Fire-arc gate: target must be within the cover's engagement arc.
					const FVector ReseekToTarget2D = (TargetLocation - Candidate.Data.Location).GetSafeNormal2D();
					const FVector ReseekFireFwd    = UCoverGeometryStatics::GetFireArcForward(Candidate.Data);
					const float   ReseekArcDot     = FVector::DotProduct(ReseekFireFwd, ReseekToTarget2D);
					if (ReseekArcDot < ReseekArcCos) continue;

					// Peek-LoS gate: must be able to actually fire from this cover toward the threat.
					if (!UCoverGeometryStatics::CanPeekShoot(CoverWorld, Candidate.Data, Candidate.Data.bCrouchedCover,
						TargetLocation, StandFireEyeHeight, Ctx.Target, Ctx.Companion))
						continue;

					if (CoverTuning->bCoverRequiresBodyProtection)
					{
						if (!UCoverGeometryStatics::IsThreatCovered(CoverWorld, Candidate.Data, TargetLocation,
							OpenStandoff, CoverTuning->CoverProtectionChestHeight, Ctx.Target, Ctx.Companion))
							continue;
					}
					bFoundReachable = true;
					break;
				}

				if (bFoundReachable)
				{
					if (bDebugLogging)
						UE_LOG(LogCompanionAI, Log, TEXT("%s: open-engage cover re-seek -> reachable cover found, finishing to MoveToCoverPoint"),
							*Ctx.Companion->GetName());
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
		LosBlockedAccum += DeltaSeconds;

		if (bEnableOpenAreaMoveAndShoot)
		{
			// Drop the jiggle latch so the clear-LoS path re-anchors JiggleHome when LoS returns.
			bJiggleActive = false;
			// Weapon down while LoS is blocked — no precise aim offset through the wall.
			Ctx.Companion->SetAimTarget(nullptr);
			Ctx.Companion->SetLowReadyAim(true);
			if (AAIController* RegainAIC = Cast<AAIController>(Ctx.Companion->GetController()))
			{
				if (bPlayerTooFar)
					TickMoveShootTowardPlayer(Ctx.Companion, RegainAIC, Ctx.Target, DeltaSeconds);
				else
					TickRegainLosReposition(Ctx.Companion, RegainAIC, Ctx.Target, TickIgnoredAttached, DeltaSeconds);
				// Face the frozen last-seen position (not the live actor) while repositioning.
				UpdateMoveShootFacing(RegainAIC, Ctx.Target, false);
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

	// Snapshot the last confirmed LoS position so the blocked path has a frozen facing anchor.
	LastKnownTargetLocation = TargetLocation;
	bHasLastKnownTargetLocation = true;

	// Restore full aim — low-ready raised during a blocked stretch is cleared here.
	Ctx.Companion->SetLowReadyAim(false);
	Ctx.Companion->SetAimTarget(Ctx.Target);

	// Facing: when move-and-shoot is enabled, UpdateMoveShootFacing drives body yaw via AIController focus
	// toward the live target — so skip the manual rotation here, which would fight bUseControllerDesiredRotation.
	// When disabled, fall back to today's manual face-the-target.
	if (bEnableOpenAreaMoveAndShoot)
	{
		if (AAIController* ClearAIC = Cast<AAIController>(Ctx.Companion->GetController()))
			UpdateMoveShootFacing(ClearAIC, Ctx.Target, true);
	}
	else
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
			UE_LOG(LogCompanionDiag, Verbose, TEXT("%s: RELOAD-START gate=1063 ammo=%d/%d reserve=%d vel=%.1f hasCover=%d reloadTime=%.2f"),
				*Ctx.Companion->GetName(),
				W ? W->GetCurrentAmmo() : -1,
				W && W->GetWeaponData() ? W->GetWeaponData()->MagazineSize : -1,
				W ? W->GetReserveAmmo() : -1,
				Ctx.Companion->GetVelocity().Size(),
				(int32)bHasCover,
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
		// Pass the task's own tracked handle — NOT the BB value — so ResetTaskState's wipe-guard
		// can genuinely discriminate "our cover" vs "monitor's newly-written cover".
		const FCoverHandle TrackedHandle = LastTickCoverHandle;
		ResetTaskState(Companion, BB, TrackedHandle, bReleaseSlot);
	}
}

void UBTTask_CompanionCombat::InitializeFromAsset(UBehaviorTree& Asset)
{
	Super::InitializeFromAsset(Asset);

	if (UBlackboardData* BBAsset = GetBlackboardAsset())
	{
		CombatTargetKey.ResolveSelectedKey(*BBAsset);
		HasCoverPositionKey.ResolveSelectedKey(*BBAsset);
		CoverLocationKey.ResolveSelectedKey(*BBAsset);
		CoverTargetKey.ResolveSelectedKey(*BBAsset);

		ensureMsgf(CoverTargetKey.SelectedKeyType != nullptr,
			TEXT("BTTask_CompanionCombat: CoverTargetKey '%s' failed to resolve against BB asset '%s' — cover will never be used"),
			*CoverTargetKey.SelectedKeyName.ToString(), *GetNameSafe(BBAsset));
	}
}

FString UBTTask_CompanionCombat::GetStaticDescription() const
{
	return FString::Printf(TEXT("Cover-aware Combat (burst: %.1f-%.1fs / quick: %.1f-%.1fs; peek cd %.1f-%.1fs)"),
		MinFireBurst, MaxFireBurst, MinQuickPeekBurst, MaxQuickPeekBurst, MinPeekCooldown, MaxPeekCooldown);
}
