// BT task — cover-aware companion combat. State machine drives EngageFromOpen, EngageFromCover, StandUpFire.
// P3 AICS migration: cover source changed from AAICoverSlot line-segment slots to FCoverHandle/FCoverData points.

#include "BTTask_CompanionCombat.h"
#include "AI/AITargetingStatics.h"
#include "AI/CompanionDiag.h"
#include "WeaponDataAsset.h"
#include "Character/ExtractionPlayerInterface.h"
#include "WeaponComponent.h"
#include "AI/BlackboardKeyType_Cover.h"
#include "AI/CompanionCoverStatics.h"
#include "CoverSystem.h"
#include "CoverGeometryStatics.h"
#include "CoverScoringStatics.h"
#include "CoverReservationSubsystem.h"
#include "CoverPoseComponent.h"
#include "World/DoorRegistrySubsystem.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "AIController.h"
#include "CompanionAIController.h"
#include "CompanionTuningDataAsset.h"
#include "CompanionCharacter.h"
#include "EnemyCharacter.h"
#include "EnemyAIController.h"        // angle-seek: who is this enemy targeting
#include "EnemyAwarenessComponent.h"  // angle-seek: GetCombatTarget
#include "EnemyGrenadierComponent.h"  // grenade lob: shared grenadier component on the companion
#include "Engine/OverlapResult.h"     // angle-seek: player-centered attacker scan
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
#include "HAL/IConsoleManager.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISense_Sight.h"
#include "GameplayTagAssetInterface.h"
#include "ExtractionTypes.h"

namespace
{
	/** Chest height (cm) for the body-protection trace from a candidate's hunker position — matches the enemy's BodyProtectChestHeight. */
	constexpr float ShuffleBodyProtectChestHeight = 60.f;

	/** companion.CoverDebug 1 — deep [COVDBG]/[COVMOVE] logging: every cover decision attempt with
	 *  full counter state, and every position change stamped with its cause. */
	TAutoConsoleVariable<int32> CVarCompanionCoverDebug(
		TEXT("companion.CoverDebug"), 0,
		TEXT("1 = verbose companion cover decision/movement logging ([COVDBG] decisions, [COVMOVE] position changes)."));

	bool CovDbg() { return CVarCompanionCoverDebug.GetValueOnGameThread() > 0; }

	// companion.FireDebug is registered once in WeaponBase.cpp — re-query by name to avoid a
	// duplicate CVar registration across translation units.
	bool FireDbg()
	{
		static const IConsoleVariable* CVar =
			IConsoleManager::Get().FindConsoleVariable(TEXT("companion.FireDebug"));
		return CVar && CVar->GetInt() != 0;
	}
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

	// Root-motion peek commit: the peek montages own the step-out/return motion, so the capsule must
	// start from wall-aligned yaw and nothing may rotate it while they play. Snap actor AND control
	// rotation (bUseControllerDesiredRotation eases toward control rotation) and clear both focus
	// priorities — with focus None the AI controller mirrors pawn yaw instead of fighting it.
	static void CompanionSnapToCoverFacing(ACompanionCharacter* Companion, const FCoverData& CoverData)
	{
		if (!IsValid(Companion)) return;
		const FRotator SlotYaw(0.f, UCoverGeometryStatics::GetFireArcForward(CoverData).Rotation().Yaw, 0.f);
		Companion->SetActorRotation(SlotYaw);
		if (AAIController* AIC = Cast<AAIController>(Companion->GetController()))
		{
			AIC->ClearFocus(EAIFocusPriority::Gameplay);
			AIC->ClearFocus(EAIFocusPriority::Move);
			AIC->SetControlRotation(SlotYaw);
		}
	}

	static const UCompanionTuningDataAsset* GetCompanionTuning(const ACompanionCharacter* Companion)
	{
		const ACompanionAIController* AIC = IsValid(Companion) ? Cast<ACompanionAIController>(Companion->GetController()) : nullptr;
		return AIC ? AIC->GetTuning() : nullptr;
	}

	// Peek fire cone: single unbiased 2D cone about the cover's wall-forward, origin at the PAWN
	// (mirrors the enemy's bTargetInPeekCone — fire reach must never exceed what the cover pose
	// can point at). True when the cone is disabled or the bearing is inside it.
	static bool IsTargetInPeekCone(const ACompanionCharacter* Companion, const FCoverData& CoverData,
		const FVector& TargetLocation, float ConeHalfAngleDeg)
	{
		if (!IsValid(Companion) || ConeHalfAngleDeg >= 179.9f) return true;
		const FVector ConeFwd = UCoverGeometryStatics::GetFireArcForward(CoverData);
		FVector ToTarget2D = TargetLocation - Companion->GetActorLocation();
		ToTarget2D.Z = 0.f;
		if (!ToTarget2D.Normalize()) return true;
		return FVector::DotProduct(ConeFwd, ToTarget2D) >= FMath::Cos(FMath::DegreesToRadians(ConeHalfAngleDeg));
	}

	// Muzzle→target clearance for the instant a burst commits — the 10 Hz withhold only runs from
	// the NEXT tick, so an unconditional StartWeaponFire at commit could put the first rounds into
	// (or through) our own wall.
	static bool IsBurstMuzzleClear(ACompanionCharacter* Companion, AActor* Target, TArrayView<AActor* const> IgnoredAttached)
	{
		if (!IsValid(Companion) || !IsValid(Target)) return false;
		AWeaponBase* W = Companion->GetCurrentWeapon();
		if (!IsValid(W)) return true;
		AActor* Blocker = nullptr;
		return HasLineOfSight(Companion->GetWorld(), W->GetMuzzleLocation(), Target, Companion, Blocker, IgnoredAttached);
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

	// Peek side from lean: Left/Right honored; Front (over-top) and None carry no authored side —
	// peek out of the edge nearest the target's bearing instead of defaulting Right (a Front cover
	// at a left corner would otherwise right-peek into the wall).
	static EPeekSide ResolveSideFromLean(ECoverLean Lean, const FCoverData& Data, const FVector& TargetLoc)
	{
		if (Lean == ECoverLean::Left)  return EPeekSide::Left;
		if (Lean == ECoverLean::Right) return EPeekSide::Right;
		const FVector FireFwd  = UCoverGeometryStatics::GetFireArcForward(Data);
		const FVector ToTarget = (TargetLoc - Data.Location).GetSafeNormal2D();
		return FVector::CrossProduct(FireFwd, ToTarget).Z >= 0.f ? EPeekSide::Right : EPeekSide::Left;
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

	// Combat-move diagnostic: one line per issued combat move with its origin site and direction
	// semantics. towardTargetDot < 0 = moving AWAY from the combat target (backpedal); distPlayer
	// tracks leash drift. Pins which mover walks the companion out of a fight.
	static void LogCombatMoveDiag(const ACompanionCharacter* Companion, const AActor* Target, const TCHAR* Site, const FVector& Dest)
	{
		if (!UE_LOG_ACTIVE(LogCompanionDiag, Log) || !IsValid(Companion)) return;
		const FVector MyLoc = Companion->GetActorLocation();
		FVector MoveDir = Dest - MyLoc;
		MoveDir.Z = 0.f;
		const float Disp = MoveDir.Size();
		if (Disp > KINDA_SMALL_NUMBER) MoveDir /= Disp;
		float TowardTargetDot = 0.f;
		float DistTarget = -1.f;
		if (IsValid(Target))
		{
			FVector ToTarget = Target->GetActorLocation() - MyLoc;
			ToTarget.Z = 0.f;
			DistTarget = ToTarget.Size();
			if (DistTarget > KINDA_SMALL_NUMBER && Disp > KINDA_SMALL_NUMBER)
				TowardTargetDot = FVector::DotProduct(MoveDir, ToTarget / DistTarget);
		}
		float DistPlayer = -1.f;
		if (const ACompanionAIController* AIC = Cast<ACompanionAIController>(Companion->GetController()))
		{
			if (const APawn* Player = AIC->GetPlayerCharacter())
				DistPlayer = FVector::Dist2D(MyLoc, Player->GetActorLocation());
		}
		UE_LOG(LogCompanionDiag, Log, TEXT("%s: [CombatMove] site=%s dest=%s disp=%.0f towardTargetDot=%+.2f distTarget=%.0f distPlayer=%.0f"),
			*Companion->GetName(), Site, *Dest.ToCompactString(), Disp, TowardTargetDot, DistTarget, DistPlayer);
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

	/** Angle-seek attacker scan: alive enemies around the player that are aggro'd AND actually
	 *  targeting the PLAYER (aim target mid-burst, or awareness combat target while still rotating).
	 *  Mirrors the BT service's focus tally — gathered fresh at point-of-use so no stale pointers. */
	static void GatherPlayerFocusedAttackers(UWorld* World, const APawn* PlayerPawn,
		const ACompanionCharacter* Companion, float Radius, TArray<AActor*, TInlineAllocator<8>>& Out)
	{
		Out.Reset();
		if (!World || !IsValid(PlayerPawn) || Radius <= 0.f) return;

		TArray<FOverlapResult> Overlaps;
		Overlaps.Reserve(16);
		FCollisionObjectQueryParams ObjParams(ECC_Pawn);
		FCollisionQueryParams Params(SCENE_QUERY_STAT(CompanionAngleSeekFocusScan), false);
		Params.AddIgnoredActor(Companion);
		Params.AddIgnoredActor(PlayerPawn);
		World->OverlapMultiByObjectType(Overlaps, PlayerPawn->GetActorLocation(), FQuat::Identity,
			ObjParams, FCollisionShape::MakeSphere(Radius), Params);

		for (const FOverlapResult& Overlap : Overlaps)
		{
			AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Overlap.GetActor());
			if (!IsValid(Enemy) || !Enemy->HasDetectedPlayer()) continue;
			const UHealthComponent* Health = Enemy->GetHealthComponent();
			if (Health && Health->IsDead()) continue;

			const AEnemyAIController* EnemyAIC = Cast<AEnemyAIController>(Enemy->GetController());
			const UEnemyAwarenessComponent* Awareness = EnemyAIC ? EnemyAIC->GetAwarenessComponent() : nullptr;
			if (Enemy->GetAIAimTarget() != PlayerPawn
				&& !(Awareness && Awareness->GetCombatTarget() == PlayerPawn))
				continue;

			Out.Add(Enemy);
			if (Out.Num() >= 8) break;
		}
	}

	/** Per-mode in-cover confidence: scale for the between-peek wait (bBurstClock=false, <1 =
	 *  peek sooner) or the burst countdown (bBurstClock=true, >1 = expose longer, applied as a
	 *  slower decrement). Combat is the boldest; Defensive (the ECompanionMode::Normal enumerator)
	 *  sits between it and Stealth, which is deliberately left at 1 in both channels — it is the one
	 *  mode the personality pass does not touch. 1 with no tuning.
	 *  Pressure01 composes on top when bPressureResponsiveCover is enabled. */
	static float ModePeekConfidenceScale(const ACompanionCharacter* Companion,
		const UCompanionTuningDataAsset* Tuning, bool bBurstClock, float Pressure01 = 0.f)
	{
		float Scale = 1.f;
		if (Tuning && IsValid(Companion))
		{
			switch (Companion->GetMode())
			{
			case ECompanionMode::Combat:
				Scale = bBurstClock
					? FMath::Max(1.f, Tuning->CombatBurstDurationMultiplier)
					: FMath::Max(0.01f, Tuning->CombatPeekCooldownMultiplier);
				break;
			case ECompanionMode::Normal:
				Scale = bBurstClock
					? FMath::Max(1.f, Tuning->DefensiveBurstDurationMultiplier)
					: FMath::Max(0.01f, Tuning->DefensivePeekCooldownMultiplier);
				break;
			default:
				break; // Stealth: unchanged, both channels stay at 1
			}
		}

		if (Tuning && Tuning->bPressureResponsiveCover && Pressure01 > 0.f)
		{
			if (bBurstClock)
			{
				Scale *= FMath::Lerp(1.f, Tuning->PressureBurstDurationMultiplierAtMax, Pressure01);
			}
			else
			{
				const float PressureCooldownScale = FMath::Lerp(1.f, Tuning->PressureCooldownScaleAtMax, Pressure01);
				Scale *= PressureCooldownScale;
				Scale = FMath::Max(Scale, 0.35f);
			}
		}
		return Scale;
	}

	/** Convenience overload for the handful of sites that do not already hold the tuning asset.
	 *  Costs a controller cast plus a tuning fetch — never reach for it on a per-frame path. */
	static float ModePeekConfidenceScale(const ACompanionCharacter* Companion, bool bBurstClock, float Pressure01 = 0.f)
	{
		const ACompanionAIController* AIC = IsValid(Companion)
			? Cast<ACompanionAIController>(Companion->GetController()) : nullptr;
		return ModePeekConfidenceScale(Companion, AIC ? AIC->GetTuning() : nullptr, bBurstClock, Pressure01);
	}

	/** Extra shortening of the wait before the FIRST peek at a freshly taken cover point. Ducking
	 *  into cover under fire and then standing behind it through a whole rolled cooldown is the
	 *  loudest "the companion is doing nothing" tell there is; the first answer wants to land about
	 *  a second in. 1 once any peek cycle has completed at this point.
	 *  Stealth is excluded for the same reason ModePeekConfidenceScale leaves it at 1 in both
	 *  channels: the mode is ring-fenced from the personality pass, and the combat task IS reachable
	 *  in Stealth once stealth breaks — without this test a broken-stealth firefight would peek
	 *  twice as fast on its first cycle at every fresh cover point. */
	static float FirstPeekWaitMultiplier(const ACompanionCharacter* Companion,
		const UCompanionTuningDataAsset* Tuning, int32 PeekCyclesAtCover)
	{
		if (!Tuning || PeekCyclesAtCover > 0) return 1.f;
		if (IsValid(Companion) && Companion->GetMode() == ECompanionMode::Stealth) return 1.f;
		return FMath::Clamp(Tuning->FirstPeekWaitScale, 0.25f, 1.f);
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
	// One completed expose/fire cycle at this point (enemy PeekCyclesAtCover parity).
	++PeekCyclesAtCover;

	// Enemy failed-peek parity: a peek burst that fired ZERO rounds is "looked and found no one" —
	// consecutive fruitless peeks release the blind cover to open-engage. Any actual firing also
	// clears the blind-hold clock: a point we shoot from is not a dead position.
	if (AmmoAtBurstStart >= 0 && IsValid(Companion))
	{
		if (Companion->GetCurrentAmmo() == AmmoAtBurstStart)
		{
			++FruitlessPeeks;
		}
		else
		{
			FruitlessPeeks = 0;
			BlindHoldTime = 0.f;
		}
	}
	AmmoAtBurstStart = -1;

	if (IsValid(Companion)) Companion->StopWeaponFire();

	bIsFiringBurst = false;
	// Fix 5: the burst is over — clear the muzzle-withhold latch so the next burst starts un-held.
	bStandBurstFireHeld = false;
	StandBurstMuzzleCheckTimer = 0.f;
	LastStandBurstResumeFireTime = 0.f;

	const ECoverHeight Height = UCoverGeometryStatics::GetCoverHeight(CoverData);

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
		const FVector HunkerLoc = CompanionCover::CompanionHunkerPosition(*Companion, CoverData, Standoff);
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

void UBTTask_CompanionCombat::ResolvePeekSideForCover(ACompanionCharacter* Companion, AActor* Target,
	const FCoverData& CoverData, const FVector& ThreatLoc)
{
	const bool bCrouched = UCoverGeometryStatics::GetCoverHeight(CoverData) == ECoverHeight::Crouch;
	CurrentLean = UCoverGeometryStatics::ChooseGapPeekSide(
		IsValid(Companion) ? Companion->GetWorld() : nullptr,
		CoverData, bCrouched, ThreatLoc, Target, Companion);
	// Mid-wall rule (user design, enemy visual parity): a both-side-flag point is not a corner —
	// a side peek from it reads as "peeking from the middle of the wall" (the diagonal eye→threat
	// verify can pass around a short wall's ends even from mid-wall). At crouch cover with an
	// over-top flag, go over the top instead; endpoint (single-flag) points keep their side peeks.
	if (bCrouched && CoverData.bFrontCoverCrouched
		&& CoverData.bLeftCoverCrouched && CoverData.bRightCoverCrouched
		&& (CurrentLean == ECoverLean::Left || CurrentLean == ECoverLean::Right))
		CurrentLean = ECoverLean::Front;
	// Crouch cover with no verified side gap = over-top (Front). Stand cover keeps None —
	// a tall wall with no side gap has no shot, and the roll must be able to reject it.
	if (CurrentLean == ECoverLean::None && bCrouched)
		CurrentLean = ECoverLean::Front;
	ResolvedPeekSide = ResolveSideFromLean(CurrentLean, CoverData, ThreatLoc);
}

void UBTTask_CompanionCombat::ResetPeekCycleCounters(ACompanionCharacter* Companion)
{
	PeekCyclesAtCover = 0;
	NoPeekLosStrikes = 0;
	if (IsValid(Companion))
		Companion->SetPeekCyclesAtCurrentCover(0);
}

void UBTTask_CompanionCombat::CommitSilentReposition(ACompanionCharacter* Companion, const FCover& ShuffleTarget, float Now)
{
	if (!IsValid(Companion) || !ShuffleTarget.IsValid()) return;

	RepositionTargetCover = ShuffleTarget;
	RepositionTargetWorldLoc = ShuffleTarget.Data.Location;
	CachedSlotForwardYaw = UCoverGeometryStatics::GetFireArcForward(ShuffleTarget.Data).Rotation().Yaw;

	// Stamp intended cover so other agents see the reservation during the walk.
	if (UWorld* IntentStampWorld = Companion->GetWorld())
	{
		if (UCoverReservationSubsystem* IntentStampSub = IntentStampWorld->GetSubsystem<UCoverReservationSubsystem>())
		{
			if (AController* IntentStampCtrl = Companion->GetController())
				IntentStampSub->SetIntendedCover(IntentStampCtrl, ShuffleTarget.Handle);
		}
	}

	if (CovDbg())
		UE_LOG(LogCompanionAI, Log, TEXT("[COVMOVE] %s SILENT-REPO-COMMIT to=%s cyclesAtOld=%d strikes=%d"),
			*Companion->GetName(), *ShuffleTarget.Data.Location.ToCompactString(), PeekCyclesAtCover, NoPeekLosStrikes);

	// Silent reposition: stay crouched, no montage, no aim, low-ready weapon.
	Companion->StopWeaponFire();
	Companion->SetAimTarget(nullptr);
	Companion->SetLowReadyAim(true);
	if (AAIController* AIC = Cast<AAIController>(Companion->GetController()))
		AIC->StopMovement();
	if (UCharacterMovementComponent* CMC = Companion->GetCharacterMovement())
		CMC->StopMovementImmediately();
	// Stay crouched — do NOT UnCrouch here.
	UE_LOG(LogCompanionAI, Log, TEXT("%s: PEEK-ACTION=Reposition-Silent ammo=%d"), *GetNameSafe(Companion), Companion->GetCurrentAmmo());
	CurrentBurstAction = EPeekAction::Reposition;
	LastDecisionTime = Now;
	TimeInCoverIdle = 0.f;
	LastRepositionDist = FVector::Dist(Companion->GetActorLocation(), RepositionTargetWorldLoc);
	RepositionElapsed = 0.f;
	RepositionStalledTime = 0.f;
	bRepositionStartLogged = true;
	if (bDebugLogging)
	{
		UE_LOG(LogCompanionDiag, Verbose, TEXT("%s: REPOSITION-START kind=silent toLoc=%s dist=%.0f isReloading=%d"),
			*Companion->GetName(), *RepositionTargetWorldLoc.ToString(),
			LastRepositionDist, (int32)Companion->IsReloading());
	}
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
		const FVector CurrentLoc = CompanionCover::CompanionHunkerPosition(*Companion, CoverData, AbortStandoff);
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
			// Fresh point = fresh side (CommitCoverSwitch pre-updates LastTickCoverHandle, so the
			// swap-detect resolve never fires for task-internal commits). Cycle counters reset
			// AFTER the ReturnToCover below — its increment belongs to the point we left.
			if (AActor* AbortTarget = Cast<AActor>(BB->GetValueAsObject(CombatTargetKey.SelectedKeyName)))
				ResolvePeekSideForCover(Companion, AbortTarget, RepositionTargetCover.Data, AITargeting::GetSightLocation(AbortTarget));
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
		if (bCloserToTarget)
		{
			// Committed to the reposition target — fresh point starts at zero cycles (the
			// ReturnToCover increment above belongs to the point we left).
			ResetPeekCycleCounters(Companion);
		}
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
		// Fresh point = fresh side, BEFORE EnterCoverPose below re-enters with the idle
		// (the old path re-entered with the previous point's side).
		if (AActor* ArrivalTarget = BB ? Cast<AActor>(BB->GetValueAsObject(CombatTargetKey.SelectedKeyName)) : nullptr)
			ResolvePeekSideForCover(Companion, ArrivalTarget, ArrivedCover.Data, AITargeting::GetSightLocation(ArrivalTarget));
		ResetPeekCycleCounters(Companion);
		RepositionTargetCover = FCover();
		RepositionStalledTime = 0.f;
		LastRepositionDist = 0.f;
		RepositionElapsed = 0.f;
		bRepositionStartLogged = false;
		CurrentBurstAction = EPeekAction::Hold;
		bJustRepositioned = true;
		PeekCooldown = FMath::RandRange(MinPeekCooldown, MaxPeekCooldown);
		TimeInCoverIdle = 0.f;
		const ECoverHeight ArrivedHeight = UCoverGeometryStatics::GetCoverHeight(ArrivedCover.Data);
		if (ArrivedHeight != ECoverHeight::Crouch)
		{
			if (UCompanionAnimInstance* CAI = Cast<UCompanionAnimInstance>(
				Companion->GetMesh() ? Companion->GetMesh()->GetAnimInstance() : nullptr))
				CAI->ClearCoverStrafeVelocity();
		}
		// Silent reposition: companion never left cover pose — skip the enter montage to avoid the bobbing animation.
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

		// Stop any lingering peek montage so cover-align inference doesn't stay
		// pinned to OverTop/StandPeek while walking.
		if (UAnimMontage* M = ActivePeekMontage.Get())
		{
			if (UCompanionAnimInstance* CAI = GetCompanionAnim(Companion))
			{
				if (CAI->Montage_IsPlaying(M))
					CAI->Montage_Stop(0.25f, M);
			}
		}
		ActivePeekMontage.Reset();

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
		// Fresh point = fresh side (see silent-arrival note).
		if (AActor* ArrivalTarget = BB ? Cast<AActor>(BB->GetValueAsObject(CombatTargetKey.SelectedKeyName)) : nullptr)
			ResolvePeekSideForCover(Companion, ArrivalTarget, ArrivedCover.Data, AITargeting::GetSightLocation(ArrivalTarget));
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
		// AFTER ReturnToCover: its ++PeekCyclesAtCover belongs to the point we LEFT — the fresh
		// point starts at zero cycles or the commit gate opens for free on every arrival.
		ResetPeekCycleCounters(Companion);
	}
}

void UBTTask_CompanionCombat::TickCornerPeekAction(ACompanionCharacter* Companion, UCompanionAnimInstance* Anim,
	const FCoverData& CoverData, AActor* Target, bool bSuppressed, bool bLowHp,
	TArrayView<AActor* const> IgnoredAttached, float DeltaSeconds)
{
	if (!IsValid(Companion) || !IsValid(Target)) return;

	// Root-motion path: while the peek montage drives the capsule the task only manages fire/LOS
	// and the burst clock — the manual home↔apex slide below would double the montage's motion.
	// Burst end / suppression jumps the montage to its Return section; once it finishes,
	// ReturnToCover's smooth snap settles any root-motion residue at the hunker.
	if (UAnimMontage* PeekM = ActivePeekMontage.Get())
	{
		if (Anim && Anim->Montage_IsPlaying(PeekM))
		{
			// BRANCH 1 owns the BurstTimer decrement (it runs before this dispatch) — don't double-count here.
			if (!bCornerPeekReturning && (bSuppressed || BurstTimer <= 0.f))
			{
				bCornerPeekReturning = true;
				if (bCornerPeekFiring)
				{
					Companion->StopWeaponFire();
					bCornerPeekFiring = false;
				}
				if (PeekM->GetSectionIndex(TEXT("Return")) == INDEX_NONE)
					Anim->Montage_Stop(0.25f, PeekM); // no Return section authored — blend out so the finalize below runs
				else if (Anim->Montage_GetCurrentSection(PeekM) != TEXT("Return"))
					Anim->Montage_JumpToSection(TEXT("Return"), PeekM);
			}

			CornerPeekLosCheckTimer -= DeltaSeconds;
			if (CornerPeekLosCheckTimer <= 0.f)
			{
				CornerPeekLosCheckTimer = 0.1f;
				AActor* Blocker = nullptr;
				AWeaponBase* W = Companion->GetCurrentWeapon();
				const FVector FireOrigin = IsValid(W) ? W->GetMuzzleLocation()
					: (Companion->GetActorLocation() + FVector(0.f, 0.f, StandFireEyeHeight));
				const bool bLos = HasLineOfSight(Companion->GetWorld(), FireOrigin, Target, Companion, Blocker, IgnoredAttached);
				const UCompanionTuningDataAsset* ConeTuning = GetCompanionTuning(Companion);
				const bool bCanFire = bLos && IsTargetInPeekCone(Companion, CoverData, Target->GetActorLocation(),
					ConeTuning ? ConeTuning->CoverPeekConeHalfAngleDeg : 75.f);
				if (!bCornerPeekReturning && bCanFire && !bCornerPeekFiring && PeekFireDelayRemaining <= 0.f)
				{
					Companion->StartWeaponFire();
					bCornerPeekFiring = true;
					UE_LOG(LogCompanionAI, Log, TEXT("%s: CORNER-PEEK-FIRE-START (montage) muzzle=%s"),
						*GetNameSafe(Companion), *FireOrigin.ToString());
				}
				else if (bCornerPeekFiring && (!bCanFire || bCornerPeekReturning))
				{
					Companion->StopWeaponFire();
					bCornerPeekFiring = false;
				}
			}
			return;
		}

		// Montage finished (Return completed or natural expiry) — settle at the hunker.
		if (bCornerPeekFiring)
		{
			Companion->StopWeaponFire();
			bCornerPeekFiring = false;
		}
		bIsFiringBurst = false;
		bCornerPeekReturning = false;
		ReturnToCover(Companion, Anim, CoverData, bSuppressed, bLowHp);
		return;
	}

	// Legacy in-place slide — only reachable when no peek montage is assigned.
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
			const UCompanionTuningDataAsset* ConeTuning = GetCompanionTuning(Companion);
			const bool bCanFire = bLos && IsTargetInPeekCone(Companion, CoverData, Target->GetActorLocation(),
				ConeTuning ? ConeTuning->CoverPeekConeHalfAngleDeg : 75.f);
			if (bCanFire && !bCornerPeekFiring && PeekFireDelayRemaining <= 0.f)
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
			else if (bCornerPeekFiring && !bCanFire)
			{
				Companion->StopWeaponFire();
				bCornerPeekFiring = false;
			}
		}

		const bool bAtApex = FVector::Dist(Next, ApexTarget) <= RepositionArrivalTolerance;
		if (bAtApex)
		{
			// At apex — burst timer now counts down "time firing at the corner". Mode confidence
			// + pressure: slower countdown = longer corner exposure.
			BurstTimer -= DeltaSeconds / ModePeekConfidenceScale(Companion, true, Pressure01);
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
	// Peek-LoS anchor at the threat's head — centre-mass traces reject candidates that could in
	// fact shoot a standing enemy over its cover. Body-protection stays on the passed ThreatLocation.
	const FVector ThreatSightLoc = IsValid(ThreatActor) ? AITargeting::GetSightLocation(ThreatActor) : ThreatLocation;

	TArray<FCover> Candidates;
	Candidates.Reserve(32);
	const FBoxSphereBounds SearchBounds(PawnLoc, FVector(ShuffleDistanceMax), ShuffleDistanceMax);
	CoverSys->GetCoverDataWithinBounds(SearchBounds, Candidates);

	// Hostile-adjacency reject (enemy-parity): a shuffle must not slide the companion onto a spot
	// an enemy is holding or heading to.
	FHostileAnchors ShuffleHostileAnchors;
	const bool bRejectHostileAdjacent = Tuning
		&& (Tuning->MinHostileCoverDistance > 0.f || Tuning->MinHostilePawnDistance > 0.f);
	if (bRejectHostileAdjacent)
		UCoverScoringStatics::GatherHostileAnchors(World, Companion, Controller, ShuffleHostileAnchors);

	FCover BestCover;
	float BestDistSq = FLT_MAX;

	for (const FCover& Candidate : Candidates)
	{
		if (!Candidate.IsValid()) continue;
		if (Candidate.Handle == CurrentCover.Handle) continue;
		if (!UCoverGeometryStatics::IsSameWall(CurrentCover.Data, Candidate.Data)) continue;

		AController* Occupant = CoverSys->GetOccupyingController(Candidate.Handle);
		if (Occupant && Occupant != Controller) continue;

		if (bRejectHostileAdjacent && UCoverScoringStatics::IsNearHostileAnchor(Candidate.Data.Location,
			ShuffleHostileAnchors, Tuning->MinHostileCoverDistance, Tuning->MinHostilePawnDistance))
			continue;

		const float DistSq = FVector::DistSquared(PawnLoc, Candidate.Data.Location);
		const float Dist = FMath::Sqrt(DistSq);
		if (Dist < ShuffleDistanceMin || Dist > ShuffleDistanceMax) continue;

		// Reject candidates that can't peek-shoot toward the threat (anti ping-pong).
		if (!UCoverGeometryStatics::CanPeekShoot(World, Candidate.Data,
			UCoverGeometryStatics::GetCoverHeight(Candidate.Data) == ECoverHeight::Crouch,
			ThreatSightLoc, StandFireEyeHeight, ThreatActor, Companion))
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
	BlindHoldTime = 0.f;
	FruitlessPeeks = 0;
	AmmoAtBurstStart = -1;
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
	if (UCoverGeometryStatics::GetCoverHeight(CoverData) == ECoverHeight::Crouch && !Companion->bIsCrouched)
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
	TArrayView<AActor* const> IgnoredAttached, float DeltaSeconds, const FCoverData* PeekConeCover)
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
	// Cone failure counts as "held" exactly like a blocked muzzle — continuing fire must respect
	// the same pose-reachable cone the burst-start gate used (no new trace budget: pure trig).
	bool bOutOfCone = false;
	if (PeekConeCover)
	{
		const UCompanionTuningDataAsset* ConeTuning = GetCompanionTuning(Companion);
		bOutOfCone = !IsTargetInPeekCone(Companion, *PeekConeCover, Target->GetActorLocation(),
			ConeTuning ? ConeTuning->CoverPeekConeHalfAngleDeg : 75.f);
	}
	const bool bBlocked = (bMuzzleBlocked && MuzzleHit.GetActor() != Target) || bOutOfCone;

	if (bBlocked && !bStandBurstFireHeld)
	{
		Companion->StopWeaponFire();
		bStandBurstFireHeld = true;
		if (bDebugLogging || FireDbg())
			UE_LOG(LogCompanionAI, Log, TEXT("%s: stand-burst FIRE HELD — muzzle blocked by %s"),
				*Companion->GetName(), *GetNameSafe(MuzzleHit.GetActor()));
		return;
	}

	if (!bBlocked && bStandBurstFireHeld)
	{
		// Peek fire delay still running — stay held; the animation hasn't reached exposure yet.
		if (PeekFireDelayRemaining > 0.f) return;

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
		if (bDebugLogging || FireDbg())
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
	// Try up to JiggleLosRetryCount random ground-plane offsets. Loose cover bias: among LoS-valid
	// candidates, prefer the one with a baked cover point nearest — the companion fights NEAR cover
	// (a trigger firing mid-burst has a duck spot) without locking into the cover anim loop.
	// Falls back to ZeroVector (sit on the already-LoS-safe anchor) if none pass.
	const ACompanionAIController* BiasAIC = Cast<ACompanionAIController>(Companion->GetController());
	const UCompanionTuningDataAsset* BiasTuning = BiasAIC ? BiasAIC->GetTuning() : nullptr;
	const float BiasRadius = BiasTuning ? BiasTuning->LooseCoverBiasRadius : 0.f;
	// Combat mode hugs obstacles harder — move-shoot BEHIND things, not a walking target in the open.
	const float BiasWeight = !BiasTuning ? 0.f
		: (Companion->GetMode() == ECompanionMode::Combat ? BiasTuning->CombatLooseCoverBiasWeight : BiasTuning->LooseCoverBiasWeight);
	const bool bBias = BiasRadius > 0.f && BiasWeight > 0.f;

	const int32 MaxTries = FMath::Max(1, JiggleLosRetryCount);
	FVector BestCandidate = FVector::ZeroVector;
	float BestRank = TNumericLimits<float>::Max();
	bool bFound = false;
	for (int32 i = 0; i < MaxTries; ++i)
	{
		const float Angle = FMath::FRandRange(0.f, 2.f * PI);
		const float Radius = FMath::FRandRange(0.f, JiggleRadius);
		const FVector Candidate(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 0.f);
		if (!PointHasLosToTarget(Companion, JiggleHome + Candidate, Target, IgnoredAttached)) continue;

		if (!bBias)
		{
			JiggleOffset = Candidate;
			JiggleRetargetTimer = JiggleRetargetInterval;
			return;
		}

		// Rank = cover distance + weight-scaled random noise: high weight → strictly prefers the
		// cover-nearest candidate, low weight → approaches the old random LoS pick. No-cover
		// candidates rank behind everything inside the radius.
		const float CoverDist = CompanionCover::DistToNearestCover(
			Companion->GetWorld(), JiggleHome + Candidate, BiasRadius, BiasAIC);
		const float Base = CoverDist >= 0.f ? CoverDist : BiasRadius * 2.f;
		const float Rank = Base + FMath::FRandRange(0.f, BiasRadius) / FMath::Max(BiasWeight, 0.01f);
		if (!bFound || Rank < BestRank)
		{
			bFound = true;
			BestRank = Rank;
			BestCandidate = Candidate;
		}
	}
	JiggleOffset = bFound ? BestCandidate : FVector::ZeroVector;
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
	const bool bAngleSeekLateral = bAngleSeekActive && AngleSeekSideSign != 0.f;
	if (Drift == EJiggleDrift::Hold && !bAngleSeekLateral) return;

	// Horizontal home->target axis; nudge JiggleHome toward (Closer) or away (Farther) along it.
	FVector ToTarget = Target->GetActorLocation() - JiggleHome;
	ToTarget.Z = 0.f;
	if (ToTarget.SizeSquared() <= KINDA_SMALL_NUMBER) return;
	const FVector ToTargetDir = ToTarget.GetSafeNormal();
	const float Sign = (Drift == EJiggleDrift::Closer) ? 1.f : -1.f;

	FVector Nudged = JiggleHome;
	if (Drift != EJiggleDrift::Hold)
		Nudged += ToTargetDir * (JiggleDriftStep * Sign);

	// Angle-seek: lateral flank component perpendicular to the target axis, applied BEFORE the
	// range clamp / nav projection / player keep-out so every existing guard still wins.
	if (bAngleSeekLateral)
		Nudged += FVector::CrossProduct(FVector::UpVector, ToTargetDir) * (AngleSeekSideSign * AngleSeekBiasResolved);

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

	LogCombatMoveDiag(Companion, Target,
		bAngleSeekLateral ? TEXT("jiggle-drift(angle-seek)") : TEXT("jiggle-drift"), Projected);
	JiggleHome = Projected;
}

// --- Angle-seek (get an angle on enemies hard-focusing the player) ---

bool UBTTask_CompanionCombat::TickAngleSeek(UBehaviorTreeComponent& OwnerComp, ACompanionCharacter* Companion,
	AActor* Target, const FVector& MyLocation, bool bPlayerTooFar, float DeltaSeconds)
{
	AngleSeekCooldownRemaining = FMath::Max(0.f, AngleSeekCooldownRemaining - DeltaSeconds);

	const ACompanionAIController* AIC = Cast<ACompanionAIController>(Companion->GetController());
	const UCompanionTuningDataAsset* Tuning = AIC ? AIC->GetTuning() : nullptr;
	if (!Tuning || !Tuning->bEnableAngleSeek)
	{
		EndAngleSeek(Companion, TEXT("disabled"));
		return false;
	}

	const int32 FocusedCount = Companion->GetPlayerFocusedEnemyCount();
	const float Supp01 = Companion->GetSuppression01();

	if (bAngleSeekActive)
	{
		AngleSeekTimeActive += DeltaSeconds;
		// Rising suppression = the flank drew fire off the player — mission accomplished, stand down.
		if (Supp01 > Tuning->AngleSeekPressureFrac) EndAngleSeek(Companion, TEXT("drawing-fire"));
		else if (FocusedCount < Tuning->AngleSeekMinFocusedEnemies) EndAngleSeek(Companion, TEXT("focus-dropped"));
		else if (Tuning->AngleSeekMaxTime > 0.f && AngleSeekTimeActive >= Tuning->AngleSeekMaxTime) EndAngleSeek(Companion, TEXT("max-time"));
		else if (bPlayerTooFar) EndAngleSeek(Companion, TEXT("leash"));
		return false;
	}

	AngleSeekEvalTimer -= DeltaSeconds;
	if (AngleSeekEvalTimer > 0.f) return false;
	AngleSeekEvalTimer = OpenEngageCoverReseekInterval;

	// Activation: player hard-focused, companion unpressured, in leash, not unbroken-Stealth.
	if (AngleSeekCooldownRemaining > 0.f || bPlayerTooFar) return false;
	if (Companion->IsStealthActive()) return false;
	if (FocusedCount < Tuning->AngleSeekMinFocusedEnemies) return false;
	if (Supp01 >= Tuning->AngleSeekPressureFrac) return false;
	if (Companion->IsSuppressed(Tuning->AngleSeekSmallWindow)) return false;

	UWorld* World = Companion->GetWorld();
	APawn* PlayerPawn = AIC->GetPlayerCharacter();
	if (!World || !IsValid(PlayerPawn)) return false;

	TArray<AActor*, TInlineAllocator<8>> Attackers;
	GatherPlayerFocusedAttackers(World, PlayerPawn, Companion, Tuning->PlayerThreatAwarenessRadius, Attackers);
	if (Attackers.Num() < Tuning->AngleSeekMinFocusedEnemies) return false;

	FVector Centroid = FVector::ZeroVector;
	for (const AActor* Attacker : Attackers) Centroid += Attacker->GetActorLocation();
	Centroid /= static_cast<float>(Attackers.Num());

	// Normal / broken-Stealth: prefer a logical cover with a firing line on the attackers.
	const bool bCombatMode = Companion->GetMode() == ECompanionMode::Combat;
	if (!bCombatMode && TryAngleSeekCoverCommit(OwnerComp, Companion, Target, MyLocation, Centroid, Attackers, *Tuning))
	{
		AngleSeekCooldownRemaining = Tuning->AngleSeekCooldown;
		Companion->StopWeaponFire();
		EndOpenAreaMoveShoot(Companion);
		FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
		return true;
	}

	// Move-shoot flank (Combat mode primary; Normal fallback when no cover qualifies): drift the
	// jiggle anchor laterally AWAY from the player->attackers axis so the companion opens a
	// crossfire angle instead of sharing the player's.
	FVector ToTarget = Target->GetActorLocation() - MyLocation;
	ToTarget.Z = 0.f;
	if (ToTarget.SizeSquared() <= KINDA_SMALL_NUMBER) return false;
	const FVector Perp = FVector::CrossProduct(FVector::UpVector, ToTarget.GetSafeNormal());
	FVector AwayFromPlayerLine = MyLocation
		- FMath::ClosestPointOnInfiniteLine(PlayerPawn->GetActorLocation(), Centroid, MyLocation);
	AwayFromPlayerLine.Z = 0.f;
	AngleSeekSideSign = FVector::DotProduct(Perp, AwayFromPlayerLine.GetSafeNormal()) >= 0.f ? 1.f : -1.f;
	AngleSeekBiasResolved = Tuning->AngleSeekLateralBias
		* (bCombatMode ? Tuning->AngleSeekCombatBiasMultiplier : 1.f);
	bAngleSeekActive = true;
	AngleSeekTimeActive = 0.f;
	if (bDebugLogging || CovDbg())
		UE_LOG(LogCompanionAI, Log, TEXT("[COVMOVE] %s ANGLE-SEEK start mode=%s focused=%d supp01=%.2f side=%+.0f bias=%.0f"),
			*Companion->GetName(), bCombatMode ? TEXT("Combat") : TEXT("Normal"),
			FocusedCount, Supp01, AngleSeekSideSign, AngleSeekBiasResolved);
	return false;
}

void UBTTask_CompanionCombat::EndAngleSeek(const ACompanionCharacter* Companion, const TCHAR* Reason)
{
	if (!bAngleSeekActive) return;
	bAngleSeekActive = false;
	AngleSeekTimeActive = 0.f;
	AngleSeekSideSign = 0.f;
	const ACompanionAIController* AIC = IsValid(Companion) ? Cast<ACompanionAIController>(Companion->GetController()) : nullptr;
	const UCompanionTuningDataAsset* Tuning = AIC ? AIC->GetTuning() : nullptr;
	AngleSeekCooldownRemaining = Tuning ? Tuning->AngleSeekCooldown : 5.f;
	if ((bDebugLogging || CovDbg()) && IsValid(Companion))
		UE_LOG(LogCompanionAI, Log, TEXT("[COVMOVE] %s ANGLE-SEEK end reason=%s"), *Companion->GetName(), Reason);
}

bool UBTTask_CompanionCombat::TryAngleSeekCoverCommit(UBehaviorTreeComponent& OwnerComp, ACompanionCharacter* Companion,
	AActor* Target, const FVector& MyLocation, const FVector& AttackerCentroid,
	TArrayView<AActor* const> Attackers, const UCompanionTuningDataAsset& Tuning)
{
	UWorld* World = Companion->GetWorld();
	ACoverSystem* CoverSys = ACoverSystem::GetCoverSystem(World);
	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	AController* Controller = Companion->GetController();
	if (!World || !CoverSys || !BB || !Controller) return false;
	UCoverReservationSubsystem* ResSub = World->GetSubsystem<UCoverReservationSubsystem>();

	const float CommitRadius = FMath::Min(Tuning.CoverSearchRadius, Tuning.CoverCommitMaxDistance);
	TArray<FCover> Candidates;
	Candidates.Reserve(32);
	const FBoxSphereBounds SearchBounds(MyLocation, FVector(CommitRadius), CommitRadius);
	CoverSys->GetCoverDataWithinBounds(SearchBounds, Candidates);

	const UCapsuleComponent* Cap = Companion->GetCapsuleComponent();
	const float Standoff = (Cap ? Cap->GetScaledCapsuleRadius() : 34.f) + 10.f;
	const float ArcCos = FMath::Cos(FMath::DegreesToRadians(Tuning.CoverFlankArcHalfAngleDeg));
	const UDoorRegistrySubsystem* DoorRegistry = World->GetSubsystem<UDoorRegistrySubsystem>();
	// Line tests run per real attacker (a virtual-centroid trace reads a clustered attacker's own
	// body as a blocker and rejects exactly the covers this feature wants). Bounded per candidate.
	const int32 MaxLineTests = FMath::Min(Attackers.Num(), 3);

	FCover Best;
	float BestDistSq = TNumericLimits<float>::Max();
	for (const FCover& Candidate : Candidates)
	{
		if (!Candidate.IsValid()) continue;
		const float DistSq = FVector::DistSquared(MyLocation, Candidate.Data.Location);
		if (DistSq > FMath::Square(CommitRadius) || DistSq >= BestDistSq) continue;
		AController* Occupant = CoverSys->GetOccupyingController(Candidate.Handle);
		if (Occupant && Occupant != Controller) continue;
		if (IsValid(ResSub) && ResSub->IsOnPostVacateCooldown(Candidate.Handle, Controller, Tuning.CoverSwitchPostVacateCooldown))
			continue;
		if (IsValid(ResSub) && ResSub->IsCoverIntendedByOther(Candidate.Handle, Controller)) continue;
		// A candidate behind a closed door is never a valid pick (same rule as the EQS DoorCrossing filter).
		if (IsValid(DoorRegistry) && DoorRegistry->AnyClosedDoorBlocksSegment(MyLocation, Candidate.Data.Location)) continue;

		// The line must be on the ATTACKERS, not the companion's own current target: arc toward
		// their centroid, then a verified peek-shot on at least one real attacker.
		const FVector ToCentroid2D = (AttackerCentroid - Candidate.Data.Location).GetSafeNormal2D();
		if (FVector::DotProduct(UCoverGeometryStatics::GetFireArcForward(Candidate.Data), ToCentroid2D) < ArcCos)
			continue;
		const bool bCandidateCrouch = UCoverGeometryStatics::GetCoverHeight(Candidate.Data) == ECoverHeight::Crouch;
		bool bLineOnAttacker = false;
		for (int32 i = 0; i < MaxLineTests && !bLineOnAttacker; ++i)
		{
			AActor* Attacker = Attackers[i];
			if (!IsValid(Attacker)) continue;
			bLineOnAttacker = UCoverGeometryStatics::CanPeekShoot(World, Candidate.Data, bCandidateCrouch,
				AITargeting::GetSightLocation(Attacker), StandFireEyeHeight, Attacker, Companion);
		}
		if (!bLineOnAttacker) continue;
		if (Tuning.bCoverRequiresBodyProtection
			&& !UCoverGeometryStatics::IsThreatCovered(World, Candidate.Data, AttackerCentroid,
				Standoff, Tuning.CoverProtectionChestHeight, Target, Companion))
			continue;

		Best = Candidate;
		BestDistSq = DistSq;
	}
	if (!Best.IsValid()) return false;

	// Stamp intent so MoveToCoverPoint's intent-restore wins over the BT loop's EQS re-pick (the
	// monitor-swap-stomp mechanism), and grant the companion so the commit gate skips the trigger
	// decline for this one commit.
	if (IsValid(ResSub)) ResSub->SetIntendedCover(Controller, Best.Handle);
	Companion->SetCoverCommitGrant(true);
	BB->SetValue<UBlackboardKeyType_Cover>(CoverTargetKey.GetSelectedKeyID(), Best);
	UE_LOG(LogCompanionAI, Log, TEXT("[COVMOVE] %s ANGLE-SEEK cover commit loc=%s dist=%.0f"),
		*Companion->GetName(), *Best.Data.Location.ToCompactString(), FMath::Sqrt(BestDistSq));
	return true;
}

void UBTTask_CompanionCombat::TryConsumePostKillHopBypass(const ACompanionCharacter* Companion,
	const UCompanionTuningDataAsset& Tuning)
{
	if (Tuning.CombatPostKillAdvanceWindow <= 0.f) return;

	const float KillTime = Companion->GetLastConfirmedKillTime();
	if (KillTime <= LastHopBypassKillTime) return; // this kill's bound is already spent

	const UWorld* World = Companion->GetWorld();
	if (!World) return;
	if ((World->GetTimeSeconds() - KillTime) > Tuning.CombatPostKillAdvanceWindow) return;

	// Spent on BOTH exits below. The caller runs the scan on every tick this returns to, so when the
	// cooldown has already expired the bound about to commit IS this kill's bound — and leaving the
	// stamp unspent there let the interval re-arm at that commit hand the same kill a second bound
	// on the next re-entry, still inside the window.
	LastHopBypassKillTime = KillTime;
	if (CombatAdvanceHopTimer <= 0.f) return; // already free to hop — no cooldown to clear
	CombatAdvanceHopTimer = 0.f;
}

bool UBTTask_CompanionCombat::TickCombatAdvanceHop(UBehaviorTreeComponent& OwnerComp, ACompanionCharacter* Companion,
	AActor* Target, const FVector& MyLocation, bool bPlayerTooFar, float DeltaSeconds)
{
	CombatAdvanceHopTimer = FMath::Max(0.f, CombatAdvanceHopTimer - DeltaSeconds);

	const ACompanionAIController* AIC = Cast<ACompanionAIController>(Companion->GetController());
	const UCompanionTuningDataAsset* Tuning = AIC ? AIC->GetTuning() : nullptr;
	if (!Tuning || !Tuning->bCombatAdvanceHops) return false;
	if (Companion->GetMode() != ECompanionMode::Combat) return false;
	if (bAngleSeekActive || bPlayerTooFar) return false;

	// Room to gain: only hop while a bound would still land outside the move-shoot ideal minimum.
	const float DistToTarget = FVector::Dist2D(MyLocation, Target->GetActorLocation());
	if (DistToTarget <= MoveShootIdealRangeMin + Tuning->CombatAdvanceHopMinGain) return false;

	// One free bound per confirmed kill, placed below EVERY guard that returns without running the
	// scan. Consuming the stamp on a tick that then bails leaves the cooldown at zero with nothing
	// to re-arm it, so a hop primed for CombatPostKillAdvanceWindow would stay primed for the rest
	// of the fight — and killing the enemy you were closest to, the common case, trips the
	// room-to-gain guard directly above. The remaining early-out below re-arms the timer itself.
	TryConsumePostKillHopBypass(Companion, *Tuning);
	if (CombatAdvanceHopTimer > 0.f) return false;

	UWorld* World = Companion->GetWorld();
	ACoverSystem* CoverSys = ACoverSystem::GetCoverSystem(World);
	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	AController* Controller = Companion->GetController();
	APawn* PlayerPawn = AIC->GetPlayerCharacter();
	if (!World || !CoverSys || !BB || !Controller)
	{
		CombatAdvanceHopTimer = OpenEngageCoverReseekInterval;
		return false;
	}
	UCoverReservationSubsystem* ResSub = World->GetSubsystem<UCoverReservationSubsystem>();

	const float SearchRadius = FMath::Min(Tuning->CoverSearchRadius, Tuning->CombatAdvanceHopMaxDistance);
	// The hop's own player leash can only ever TIGHTEN the combat leash — a designer raising
	// CombatAdvanceHopMaxPlayerDistance above CombatLeashDistance must not let a bound out past the
	// leash the rest of combat positioning respects.
	const float HopPlayerLeash = FMath::Min(Tuning->CombatLeashDistance, Tuning->CombatAdvanceHopMaxPlayerDistance);
	TArray<FCover> Candidates;
	Candidates.Reserve(32);
	const FBoxSphereBounds SearchBounds(MyLocation, FVector(SearchRadius), SearchRadius);
	CoverSys->GetCoverDataWithinBounds(SearchBounds, Candidates);

	const UCapsuleComponent* Cap = Companion->GetCapsuleComponent();
	const float Standoff = (Cap ? Cap->GetScaledCapsuleRadius() : 34.f) + 10.f;
	const float ArcCos = FMath::Cos(FMath::DegreesToRadians(Tuning->CoverFlankArcHalfAngleDeg));
	const FVector TargetLoc = Target->GetActorLocation();
	const FVector TargetSight = AITargeting::GetSightLocation(Target);
	const UDoorRegistrySubsystem* HopDoorRegistry = World->GetSubsystem<UDoorRegistrySubsystem>();

	// Nearest qualifying bound — short purposeful dashes, not the biggest land-grab available.
	FCover Best;
	float BestDistSq = TNumericLimits<float>::Max();
	for (const FCover& Candidate : Candidates)
	{
		if (!Candidate.IsValid()) continue;
		const float DistSq = FVector::DistSquared(MyLocation, Candidate.Data.Location);
		if (DistSq > FMath::Square(SearchRadius) || DistSq >= BestDistSq) continue;

		// Gains ground toward the threat, without hopping inside the ideal-range floor or past the leash.
		const float CandToThreat = FVector::Dist2D(Candidate.Data.Location, TargetLoc);
		if (CandToThreat > DistToTarget - Tuning->CombatAdvanceHopMinGain) continue;
		if (CandToThreat < MoveShootIdealRangeMin) continue;
		if (IsValid(PlayerPawn)
			&& FVector::Dist2D(Candidate.Data.Location, PlayerPawn->GetActorLocation()) > HopPlayerLeash)
			continue;

		AController* Occupant = CoverSys->GetOccupyingController(Candidate.Handle);
		if (Occupant && Occupant != Controller) continue;
		if (IsValid(ResSub) && ResSub->IsOnPostVacateCooldown(Candidate.Handle, Controller, Tuning->CoverSwitchPostVacateCooldown))
			continue;
		if (IsValid(ResSub) && ResSub->IsCoverIntendedByOther(Candidate.Handle, Controller)) continue;
		// A hop target behind a closed door is never valid (same rule as the EQS DoorCrossing filter).
		if (IsValid(HopDoorRegistry) && HopDoorRegistry->AnyClosedDoorBlocksSegment(MyLocation, Candidate.Data.Location))
			continue;

		const FVector ToThreat2D = (TargetLoc - Candidate.Data.Location).GetSafeNormal2D();
		if (FVector::DotProduct(UCoverGeometryStatics::GetFireArcForward(Candidate.Data), ToThreat2D) < ArcCos)
			continue;
		if (!UCoverGeometryStatics::CanPeekShoot(World, Candidate.Data,
			UCoverGeometryStatics::GetCoverHeight(Candidate.Data) == ECoverHeight::Crouch,
			TargetSight, StandFireEyeHeight, Target, Companion))
			continue;
		if (Tuning->bCoverRequiresBodyProtection
			&& !UCoverGeometryStatics::IsThreatCovered(World, Candidate.Data, TargetLoc,
				Standoff, Tuning->CoverProtectionChestHeight, Target, Companion))
			continue;

		Best = Candidate;
		BestDistSq = DistSq;
	}
	if (!Best.IsValid())
	{
		// Nothing hoppable right now — retry at decision cadence, not per tick.
		CombatAdvanceHopTimer = OpenEngageCoverReseekInterval;
		return false;
	}

	if (IsValid(ResSub)) ResSub->SetIntendedCover(Controller, Best.Handle);
	Companion->SetCoverCommitGrant(true);
	BB->SetValue<UBlackboardKeyType_Cover>(CoverTargetKey.GetSelectedKeyID(), Best);
	CombatAdvanceHopTimer = Tuning->CombatAdvanceHopInterval;
	UE_LOG(LogCompanionAI, Log, TEXT("[COVMOVE] %s ADVANCE-HOP commit loc=%s dash=%.0f gain=%.0f"),
		*Companion->GetName(), *Best.Data.Location.ToCompactString(), FMath::Sqrt(BestDistSq),
		DistToTarget - FVector::Dist2D(Best.Data.Location, TargetLoc));
	Companion->StopWeaponFire();
	EndOpenAreaMoveShoot(Companion);
	FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
	return true;
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
	const TCHAR* FanDirName = TEXT("?");
	bool bWideStep = false;
	bool bHaveDest = PickFanLosDestination(Companion, Target, IgnoredAttached, MoveShootStrafeDistance, Dest, &FanDirName);
	if (!bHaveDest)
	{
		bWideStep = true;
		bHaveDest = PickFanLosDestination(Companion, Target, IgnoredAttached, MoveShootStrafeDistance * WideStepFactor, Dest, &FanDirName);
	}

	// No LoS-clear fan point at either distance — hold (respect the reposition timer, don't thrash the nav query).
	if (!bHaveDest)
	{
		bMoveShootHolding = true;
		AIC->StopMovement();
		return;
	}

	bMoveShootHolding = false;
	MoveShootDestination = Dest;
	LogCombatMoveDiag(Companion, Target,
		*FString::Printf(TEXT("regain-los:%s%s"), FanDirName, bWideStep ? TEXT("(x2)") : TEXT("")), MoveShootDestination);
	AIC->MoveToLocation(MoveShootDestination, MoveShootAcceptRadius, false, true, false, true);
}

bool UBTTask_CompanionCombat::PickFanLosDestination(ACompanionCharacter* Companion, AActor* Target, TArrayView<AActor* const> IgnoredAttached, float StepDistance, FVector& OutDest, const TCHAR** OutDirName) const
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
	static const TCHAR* OffsetNames[] = { TEXT("right"), TEXT("left"), TEXT("back"), TEXT("back-right"), TEXT("back-left") };

	// Nearest valid candidate (smallest displacement from current location that regains LoS) — biases a
	// small back-step over a big lateral swing. Project + LoS-verify each, track the closest hit.
	bool bFound = false;
	float BestDistSq = TNumericLimits<float>::Max();
	for (int32 i = 0; i < UE_ARRAY_COUNT(Offsets); ++i)
	{
		FVector Candidate;
		if (!TryLateralLosDestination(Companion, Target, IgnoredAttached, Offsets[i].GetSafeNormal() * StepDistance, Candidate)) continue;
		const float DistSq = FVector::DistSquared(Candidate, MyLoc);
		if (DistSq >= BestDistSq) continue;
		BestDistSq = DistSq;
		OutDest = Candidate;
		if (OutDirName) *OutDirName = OffsetNames[i];
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
	const UCompanionTuningDataAsset* PullTuning = nullptr;
	if (ACompanionAIController* CompAIC = Cast<ACompanionAIController>(AIC))
	{
		Player = CompAIC->GetPlayerCharacter();
		if (const UCompanionTuningDataAsset* T = CompAIC->GetTuning())
		{
			PullTuning = T;
			const float LeashDist = Companion->GetMode() == ECompanionMode::Combat
				? T->CombatLeashDistance : T->NormalCombatLeashDistance;
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
	FVector Desired = PlayerLoc + FromPlayer.GetSafeNormal() * StopDist;

	// Pull-back prefers a cover-adjacent waypoint: if a baked cover sits near the desired point,
	// snap toward it so the catch-up path hugs cover instead of cutting straight through the open.
	// Bounded to the bias radius, so the snap can't drag the pull-back meaningfully off-course.
	if (PullTuning && PullTuning->LooseCoverBiasRadius > 0.f && PullTuning->LooseCoverBiasWeight > 0.f)
	{
		FVector NearCover;
		if (CompanionCover::NearestCoverLocation(Companion->GetWorld(), Desired,
			PullTuning->LooseCoverBiasRadius, AIC, NearCover))
			Desired = FVector(NearCover.X, NearCover.Y, Desired.Z);
	}

	FVector Projected;
	if (!ProjectToNav(Companion->GetWorld(), Desired, MoveShootNavProjectExtent, Projected))
	{
		bMoveShootHolding = true;
		AIC->StopMovement();
		return;
	}
	bMoveShootHolding = false;
	MoveShootDestination = Projected;
	LogCombatMoveDiag(Companion, Target, TEXT("player-pull"), MoveShootDestination);
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
		// Lower on teardown: this runs AFTER the service's edge-lower when the abort came from a
		// target clear (deferred BB-observer), and raising here past that latch left the weapon up
		// for the whole posture-decay window. Mid-task callers re-raise next tick anyway.
		Companion->SetLowReadyAim(true);
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
	CachedIdleHunkerHandle = FCoverHandle();
	ResetPeekCycleCounters(Companion);

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
	SpeculativePeekTimer = 0.f;
	BlindHoldTime = 0.f;
	FruitlessPeeks = 0;
	AmmoAtBurstStart = -1;
	PeekFireDelayRemaining = 0.f;
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
	bFinalApproachRetried = false;
	FinalApproachNoProgressTime = 0.f;
	LastFinalApproachPawnLoc = FVector::ZeroVector;
	// Fire itself already stopped above (StopWeaponFire); this clears the transit-fire latch.
	FinalApproachFire.Reset();
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
	GrenadeLosBlockedAccum = 0.f;
	// Angle-seek: deactivate but keep AngleSeekCooldownRemaining — target churn restarts this task
	// several times per fight, and a reset cooldown would let the flank re-arm instantly after
	// every kill (the exact ping-pong the cooldown exists to stop).
	bAngleSeekActive = false;
	AngleSeekTimeActive = 0.f;
	AngleSeekSideSign = 0.f;
	AngleSeekBiasResolved = 0.f;
	AngleSeekEvalTimer = 0.f;
	// Pressure tracking: reset distance sample so first tick after re-entry doesn't false-detect closing.
	PreviousNearestThreatDist = -1.f;
	Pressure01 = 0.f;
	bPreviousThreatWasClosing = false;
	PressureSampleTimer = 0.f;
	// Keep PeekImpulseCooldownRemaining across task restarts (same reason as angle-seek cooldown).
	// Corner apex cache + scorer viability: clear per task restart (cover handle may differ).
	CachedCornerApexHandle = FCoverHandle();
	bCachedCornerLeftFound = false;
	bCachedCornerRightFound = false;
	bLastScorerCornerLeftViable = false;
	bLastScorerCornerRightViable = false;
	LastScorerBestCornerSide = ECoverLean::None;
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
	// Long glides feed the cover strafe blend (silent-reposition parity) — without it the ABP
	// sits in cover idle while the actor translates, which reads as a slide. Short pop-fix
	// snaps (<=30cm) keep the idle montage running instead of re-bobbing it for a blink.
	const bool bAnimatedGlide = SmoothSnapInitialDist > 30.f;
	if (bAnimatedGlide)
	{
		if (UCompanionAnimInstance* CAI = GetCompanionAnim(Companion))
		{
			const FVector MoveDelta = NextLoc - Companion->GetActorLocation();
			CAI->SetCoverStrafeVelocity((DeltaSeconds > KINDA_SMALL_NUMBER)
				? (MoveDelta / DeltaSeconds)
				: FVector::ZeroVector);
		}
	}
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
		if (bAnimatedGlide)
		{
			// The strafe feed stopped the idle montage at glide start — the anim contract is that
			// the BT re-enters the pose on arrival (bPlayEnterMontage=false: no re-bob).
			if (UCompanionAnimInstance* CAI = GetCompanionAnim(Companion))
			{
				CAI->ClearCoverStrafeVelocity();
				CAI->EnterCoverPose(ResolvedPeekSide,
					bPendingCrouchAfterSnap ? ECoverHeight::Crouch : ECoverHeight::Stand, false);
			}
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
	// Cycle carry — same stamp as OnTaskFinished (abort wipes the handle before that runs).
	if (LastTickCoverHandle.IsValid())
	{
		PeekCycleCarryHandle = LastTickCoverHandle;
		PeekCycleCarryCount = PeekCyclesAtCover;
	}
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

		// Fresh commit cycle — the switch monitor's triggers-cleared exit dwells against this stamp
		// (its own arrival memory survives task restarts at a retained slot and reads pre-satisfied).
		Companion->StampCoverCommit();

		const FVector ArrivalLoc = Companion->GetActorLocation();
		ResolvePeekSideForCover(Companion, Target, Cover.Data, AITargeting::GetSightLocation(Target));
		// Cycles are per-physical-cover: re-claiming the point we just exited at (target died /
		// switched → task restart) keeps its earned cycles, or the monitor's G5 gate starves on
		// perpetual zeros. A genuinely different point starts fresh. Strikes always start fresh —
		// they are per-target blindness evidence.
		if (PeekCycleCarryHandle.IsValid() && Cover.Handle == PeekCycleCarryHandle)
		{
			PeekCyclesAtCover = PeekCycleCarryCount;
			NoPeekLosStrikes = 0;
			Companion->SetPeekCyclesAtCurrentCover(PeekCyclesAtCover);
		}
		else
		{
			ResetPeekCycleCounters(Companion);
			PeekCycleCarryHandle = Cover.Handle;
			PeekCycleCarryCount = 0;
		}
		if (CovDbg())
			UE_LOG(LogCompanionAI, Log, TEXT("[COVMOVE] %s CLAIM (ExecuteTask) loc=%s lean=%d"),
				*Companion->GetName(), *Cover.Data.Location.ToCompactString(), (int32)CurrentLean);

		const UCapsuleComponent* Cap = Companion->GetCapsuleComponent();
		const float Standoff = (Cap ? Cap->GetScaledCapsuleRadius() : 34.f) + 10.f;
		const FVector HunkerLoc = CompanionCover::CompanionHunkerPosition(*Companion, Cover.Data, Standoff);
		const float DistToHunker = FVector::Dist(ArrivalLoc, HunkerLoc);

		// Trust-the-delivered-spot guard: MoveToCoverPoint just delivered this pawn and stamped
		// HasCoverPosition using the SAME edge-align computation. The corner-march is trace-based
		// against live geometry (a pawn standing on the march line shifts the perceived corner), so
		// a recompute at task entry can disagree by metres with where the pawn was just correctly
		// delivered — and walking that order mid-fight is a silent, unarmed trek (playtest death:
		// 6s mannequin at a perfect peek corner). If the pawn is genuinely wall-adjacent where it
		// stands, the delivered position wins: enter cover HERE.
		bool bTrustArrivalSnap = false;
		if (DistToHunker > TrustArrivalMaxHunkerDivergence)
		{
			const FVector WallDir = Cover.Data.DirectionToWall.GetSafeNormal2D();
			UWorld* AdjWorld = Companion->GetWorld();
			if (AdjWorld && !WallDir.IsNearlyZero())
			{
				FCollisionQueryParams AdjParams(SCENE_QUERY_STAT(CoverArrivalWallAdjacent), false);
				AdjParams.AddIgnoredActor(Companion);
				AdjParams.AddIgnoredActor(Companion->GetCurrentWeapon());
				// ArrivalLoc is capsule centre (~chest) — same height band as the march's ProbeZ.
				FHitResult AdjHit;
				const float CapR = Cap ? Cap->GetScaledCapsuleRadius() : 34.f;
				bTrustArrivalSnap = AdjWorld->LineTraceSingleByChannel(AdjHit, ArrivalLoc,
					ArrivalLoc + WallDir * (Standoff + 60.f + CapR), ECC_Visibility, AdjParams);
			}
			if (bTrustArrivalSnap)
				UE_LOG(LogCompanionAI, Warning,
					TEXT("%s: HUNKER-DIVERGENCE-SNAP distToHunker=%.0f > %.0f, pawn wall-adjacent — entering cover at delivered position"),
					*Companion->GetName(), DistToHunker, TrustArrivalMaxHunkerDivergence);
		}

		// Entry sanity guard: MoveToCoverPoint delivers within ~45cm, so a hunker beyond the
		// edge-align divergence budget means the BB claim is stale/bogus — release for a re-pick
		// instead of physically trekking there in cover posture (the "crouch-walk across the room"
		// failure). MarkVacated so the EQS PostVacate filter blocks an instant re-pick of the point.
		// 2D: the hunker keeps the baked cover Z while the pawn sits at nav-biased Z — a 3D compare
		// would eat the divergence headroom in vertical offset and reject legitimate far-corner claims.
		// Skipped when the wall-adjacency check above already validated the delivered position.
		if (!bTrustArrivalSnap && FVector::Dist2D(ArrivalLoc, HunkerLoc) > FinalApproachMaxStartDist)
		{
			UE_LOG(LogCompanionAI, Warning,
				TEXT("%s: FINALAPPROACH-REJECT distToHunker2D=%.0f max=%.0f coverLoc=%s pawnLoc=%s — releasing stale claim for re-pick"),
				*Companion->GetName(), FVector::Dist2D(ArrivalLoc, HunkerLoc), FinalApproachMaxStartDist,
				*Cover.Data.Location.ToCompactString(), *ArrivalLoc.ToCompactString());
			if (UCoverReservationSubsystem* VacSub = Companion->GetWorld()->GetSubsystem<UCoverReservationSubsystem>())
			{
				if (AController* VacCtrl = Companion->GetController())
					VacSub->MarkVacated(Cover.Handle, VacCtrl);
			}
			ResetTaskState(Companion, BB, Cover.Handle, true);
			return EBTNodeResult::Failed;
		}

		if (DistToHunker <= FinalApproachAcceptRadius || bTrustArrivalSnap)
		{
			// Already at the cover point (or trusting the delivered spot over a divergent
			// recompute) — snap and enter cover immediately.
			if (AAIController* AIC = Cast<AAIController>(Companion->GetController()))
				AIC->StopMovement();
			if (UCharacterMovementComponent* CMC = Companion->GetCharacterMovement())
				CMC->StopMovementImmediately();

			const FRotator SlotYawRot(0.f, UCoverGeometryStatics::GetFireArcForward(Cover.Data).Rotation().Yaw, 0.f);
			if (bDebugLogging) UE_LOG(LogCompanionDiag, Log, TEXT("%s: ALREADY-CLOSE-SNAP distToHunker=%.1f acceptRadius=%.1f arrival=%s hunker=%s trustArrival=%d"),
				*Companion->GetName(), DistToHunker, FinalApproachAcceptRadius, *ArrivalLoc.ToString(), *HunkerLoc.ToString(), (int32)bTrustArrivalSnap);
			// Ground-snap: nav-mesh-arrival Z is biased above the floor; trace down to find the real floor.
			// Trust-snap keeps the pawn's own XY — the whole point is NOT walking to the recomputed hunker.
			FVector SnapLoc = bTrustArrivalSnap
				? ArrivalLoc
				: FVector(HunkerLoc.X, HunkerLoc.Y, ArrivalLoc.Z);
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
			const ECoverHeight ArrivalHeight = UCoverGeometryStatics::GetCoverHeight(Cover.Data);
			BeginSmoothSnap(Companion, SnapLoc, SlotYawRot, ArrivalHeight == ECoverHeight::Crouch,
				bTrustArrivalSnap ? TEXT("TrustArrival") : TEXT("AlreadyClose"));
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
			FinalApproachNoProgressTime = 0.f;
			LastFinalApproachPawnLoc = ArrivalLoc;

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
			TEXT("%s: COVER-CLAIMED height=%s lean=%d canPeek=%d distToHunker=%.0f coverLoc=%s"),
			*Companion->GetName(),
			(UCoverGeometryStatics::GetCoverHeight(Cover.Data) == ECoverHeight::Crouch) ? TEXT("Crouch") : TEXT("Stand"),
			(int32)CurrentLean, (int32)(CurrentLean != ECoverLean::None),
			DistToHunker, *Cover.Data.Location.ToCompactString());
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

uint8 UBTTask_CompanionCombat::GetEffectiveMaxHolds(const ACompanionCharacter* Companion) const
{
	const bool bStealth = IsValid(Companion) && Companion->GetMode() == ECompanionMode::Stealth;
	return bStealth ? StealthMaxConsecutiveHolds : MaxConsecutiveHolds;
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
		const bool bTargetDead = TargetHealth && TargetHealth->IsDead();
		if (bTargetDead)
		{
			// Clear the corpse from the BB NOW — waiting for the state service's next tick lets the
			// BT re-enter this task against the dead target several times per kill (reads as the
			// companion standing idle between kills instead of re-engaging).
			if (UBlackboardComponent* ClearBB = OwnerComp.GetBlackboardComponent())
				ClearBB->ClearValue(CombatTargetKey.SelectedKeyName);
		}
		return FinishLatentTask(OwnerComp, bTargetDead ? EBTNodeResult::Succeeded : EBTNodeResult::Failed);
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

		// Displacement-based no-progress bail: the path-follower can report Moving while the pawn
		// is physically wedged (playtest: 6s at a frozen dist, PF Moving throughout) — the Idle-based
		// stall above never fires there. Track real displacement; a sustained sub-threshold crawl
		// bails AND skips the retry: an identical re-issued move that produced 0cm stays at 0cm.
		const FVector PawnLocNow = Ctx.Companion->GetActorLocation();
		if (!bArrived && FVector::DistSquared(PawnLocNow, LastFinalApproachPawnLoc)
			< FMath::Square(FinalApproachMinProgressSpeed * DeltaSeconds))
			FinalApproachNoProgressTime += DeltaSeconds;
		else
			FinalApproachNoProgressTime = 0.f;
		LastFinalApproachPawnLoc = PawnLocNow;
		const bool bNoProgress = !bArrived && FinalApproachNoProgressTime >= FinalApproachNoProgressBailSeconds;
		if (bNoProgress && !bFinalApproachRetried)
		{
			UE_LOG(LogCompanionAI, Warning, TEXT("%s: FINALAPPROACH-NOPROGRESS frozen %.1fs at dist=%.0f — bailing without retry"),
				*Ctx.Companion->GetName(), FinalApproachNoProgressTime, Dist);
			bFinalApproachRetried = true;
		}

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

		if (!bArrived && !bTimedOut && !bStalled && !bNoProgress)
		{
			// Approach-fire during the walk: the final approach used to be a silent, aimed-but-
			// never-firing crouch-walk (the state service keeps the weapon up while no fire path
			// runs) — reuse MoveToCoverPoint's muzzle-gated transit fire so a clear line produces
			// shots on the way in.
			if (AAIController* FireAIC = Cast<AAIController>(Ctx.Companion->GetController()))
				CompanionCover::TickCoverApproachFire(Ctx.Companion, FireAIC, Ctx.Blackboard, FinalApproachFire, DeltaSeconds);
			return;
		}

		// Far-out failsafe: never glide across the room in cover pose. Re-issue the move once
		// (a crossing enemy can stall path-following well short of the slot), then release the
		// claim and stay in-task so the next tick falls into OpenEngage. Finishing Failed here
		// left CombatTarget set — BB observer aborts only fire on value CHANGE, so the tree sat
		// in FollowPlayer (companion trailing the player mid-firefight) until the state service
		// happened to write a different target identity.
		if (!bArrived && Dist > FinalApproachSnapMaxDist)
		{
			if (!bFinalApproachRetried)
			{
				bFinalApproachRetried = true;
				FinalApproachElapsed = 0.f;
				FinalApproachStalledTime = 0.f;
				FinalApproachNoProgressTime = 0.f;
				LastFinalApproachPawnLoc = Ctx.Companion->GetActorLocation();
				if (AAIController* RetryAIC = Cast<AAIController>(Ctx.Companion->GetController()))
					RetryAIC->MoveToLocation(FinalApproachTarget, FinalApproachAcceptRadius, false, true, true, true);
				UE_LOG(LogCompanionAI, Log, TEXT("%s: FINALAPPROACH-RETRY dist=%.0f timedOut=%d stalled=%d"),
					*Ctx.Companion->GetName(), Dist, (int32)bTimedOut, (int32)bStalled);
				return;
			}
			UE_LOG(LogCompanionAI, Warning, TEXT("%s: FINALAPPROACH-UNREACHABLE dist=%.0f — releasing claim, dropping to open-engage"),
				*Ctx.Companion->GetName(), Dist);
			if (AAIController* AIC = Cast<AAIController>(Ctx.Companion->GetController()))
				AIC->StopMovement();
			if (UCharacterMovementComponent* CMC = Ctx.Companion->GetCharacterMovement())
				CMC->StopMovementImmediately();
			// Stamp MarkVacated so the EQS PostVacate filter blocks an immediate re-pick of the
			// unreachable point — without it the scorer re-picks the slot it just failed to reach
			// (it scored best moments ago and is now free) and the approach/fail loop paces.
			// Local copy: ResetTaskState wipes the member it would otherwise alias (AbortTask parity).
			const FCoverHandle UnreachableHandle = LastTickCoverHandle;
			if (UnreachableHandle.IsValid())
			{
				if (UCoverReservationSubsystem* VacSub = Ctx.Companion->GetWorld()->GetSubsystem<UCoverReservationSubsystem>())
				{
					if (AController* VacCtrl = Ctx.Companion->GetController())
						VacSub->MarkVacated(UnreachableHandle, VacCtrl);
				}
			}
			// ResetTaskState clears CoverTarget + HasCoverPosition and every final-approach member,
			// so next tick computes bHasCover=false and runs branch 2 (OpenEngage) — the companion
			// keeps engaging from the open while the switch monitor hunts for a reachable slot.
			ResetTaskState(Ctx.Companion, Ctx.Blackboard, UnreachableHandle, true);
			return;
		}

		if (AAIController* AIC = Cast<AAIController>(Ctx.Companion->GetController()))
			AIC->StopMovement();
		if (UCharacterMovementComponent* CMC = Ctx.Companion->GetCharacterMovement())
			CMC->StopMovementImmediately();

		// Transit fire ends at the point — the cover FSM (peeks/bursts) owns fire from here on.
		// Keep focus so the companion faces the threat while the snap/EnterCoverPose runs.
		CompanionCover::StopCoverApproachFire(Ctx.Companion,
			Cast<AAIController>(Ctx.Companion->GetController()), FinalApproachFire, /*bKeepFocus=*/true);

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
			const ECoverHeight ApproachHeight = UCoverGeometryStatics::GetCoverHeight(ApproachCover.Data);
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
	// Head-height LoS anchor for peek tests — the state service and aim already trace to this
	// resolver; tracing to actor CENTRE reads a standing, firing enemy behind crouch cover as
	// "blocked" and starves every peek/relocate decision (27s hold-never-shoot playtest).
	const FVector TargetSightLoc = AITargeting::GetSightLocation(Ctx.Target);
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
		// Task-internal commits pre-update LastTickCoverHandle, so reaching here = an EXTERNAL
		// writer (the switch monitor) replaced our cover point.
		if (CovDbg())
			UE_LOG(LogCompanionAI, Log, TEXT("[COVMOVE] %s EXTERNAL-SWAP (monitor) newLoc=%s cyclesAtOld=%d"),
				*Ctx.Companion->GetName(), *Cover.Data.Location.ToCompactString(), PeekCyclesAtCover);
		ResolvePeekSideForCover(Ctx.Companion, Ctx.Target, Cover.Data, TargetSightLoc);
		ResetPeekCycleCounters(Ctx.Companion);
		BlockedRecheckHits = 0;
		BlindHoldTime = 0.f;
		FruitlessPeeks = 0;
		AmmoAtBurstStart = -1;
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

	// TEST LEVER (user-directed): with bIgnoreSuppressionInCover the companion just peeks —
	// suppression never gates this task. Raw value kept for the debug snapshot.
	const bool bSuppressedRaw = Ctx.Companion->IsSuppressed(SuppressionWindowSeconds);
	const bool bSuppressed = (TickTuning && TickTuning->bIgnoreSuppressionInCover) ? false : bSuppressedRaw;
	const bool bLowHp = Ctx.Companion->GetHealthFraction() < LowHealthFraction;

	// Mirror for the switch monitor's commit gate (G5) — one int copy per tick.
	Ctx.Companion->SetPeekCyclesAtCurrentCover(PeekCyclesAtCover);

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
		// Finish Succeeded, NOT Aborted: a spontaneous FinishLatentTask(Aborted) with no abort request
		// in flight latches the BT component's abort bookkeeping and the branch never resumes — live
		// PIE log showed the tree dead for 75s (services ticking, no task re-entry) after the monitor's
		// trigger-clear vacate tripped this guard. Keep the target too: the state service owns target
		// lifecycle, and the vacate is a deliberate transition to open-engage fighting.
		UE_LOG(LogCompanionAI, Warning, TEXT("Slot lost mid-task on companion %s — finishing for re-pick"), *GetNameSafe(Ctx.Companion));
		return FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
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
		const bool bShouldCrouch = UCoverGeometryStatics::GetCoverHeight(Cover.Data) == ECoverHeight::Crouch;
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

		// Edge-aligned hunker is static per cover point but runs a trace march — cache per handle
		// instead of recomputing on every idle tick.
		if (CachedIdleHunkerHandle != Cover.Handle)
		{
			const UCapsuleComponent* IdleCap = Ctx.Companion->GetCapsuleComponent();
			const float IdleStandoff = (IdleCap ? IdleCap->GetScaledCapsuleRadius() : 34.f) + 10.f;
			CachedIdleHunkerLoc = CompanionCover::CompanionHunkerPosition(*Ctx.Companion, Cover.Data, IdleStandoff);
			CachedIdleHunkerHandle = Cover.Handle;
		}
		const FVector HunkerLoc = CachedIdleHunkerLoc;

#if ENABLE_DRAW_DEBUG
		// CovDbg-only (NOT bDebugLogging - that flag ships enabled on the BT asset for the log
		// stream, and these spheres render as gameplay markers to a playtester).
		if (CovDbg())
		{
			const FVector HunkerPt = HunkerLoc + FVector(0.f, 0.f, 10.f);
			DrawDebugSphere(Ctx.Companion->GetWorld(), HunkerPt, 22.f, 8, FColor::Red, false, 0.f, 0, 1.5f);
			// Corner leans only — Front (over-the-top) has no lateral peek point; GetLeanPeekPosition
			// degenerates to the cover location and the line reads as a bogus corner pointer mid-wall.
			if (CurrentLean == ECoverLean::Left || CurrentLean == ECoverLean::Right)
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
			ResolvePeekSideForCover(Ctx.Companion, Ctx.Target, Cover.Data, TargetSightLoc);
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
			// Debounced like the compromise check — a single transient block (target mid-strafe, door
			// swing) must not dump the point; a genuinely blind point trips within ~2 evals.
			// Stamp MarkVacated so the EQS PostVacate filter blocks an immediate re-pick of the same blind point.
			if (!UCoverGeometryStatics::CanPeekShoot(Ctx.Companion->GetWorld(), Cover.Data,
				UCoverGeometryStatics::GetCoverHeight(Cover.Data) == ECoverHeight::Crouch,
				TargetSightLoc, StandFireEyeHeight, Ctx.Target, Ctx.Companion))
			{
				// Frozen while suppressed — "pinned and can't peek" must not count as "blind point";
				// invalidating here is exactly the fire-before-moving churn the gate exists to stop.
				if (!bSuppressed)
					++NoPeekLosStrikes;
				if (NoPeekLosStrikes >= DebounceRequired)
				{
					// Vacate-with-destination: only leave a blind point for a point verified to have
					// peek LoS. The old full invalidate handed the re-pick to EQS, whose peekable test
					// is flag-only — it kept returning the neighbouring blind point (ping-pong).
					const FCover BlindEscape = FindShuffleCover(Ctx.Companion, Cover, TargetLocation);
					if (BlindEscape.IsValid())
					{
						NoPeekLosStrikes = 0;
						UE_LOG(LogCompanionAI, Log, TEXT("%s: no-peek-los ESCAPE — silent shuffle to %s (cycles=%d dwell=%.1f)"),
							*Ctx.Companion->GetName(), *BlindEscape.Data.Location.ToCompactString(),
							PeekCyclesAtCover, TimeAtCurrentCover);
						if (UWorld* EscapeWorld = Ctx.Companion->GetWorld())
							CommitSilentReposition(Ctx.Companion, BlindEscape, EscapeWorld->GetTimeSeconds());
						return;
					}
					// Nowhere on this wall can shoot either — hold instead of vacating to an equally
					// blind EQS pick. Clamp strikes below threshold so the escape re-tries next eval;
					// the switch monitor's blind-current bypass covers the cross-wall escape.
					NoPeekLosStrikes = DebounceRequired - 1;
					if (bDebugLogging || CovDbg())
						UE_LOG(LogCompanionAI, Log, TEXT("%s: no-peek-los HOLD — no verified-LoS shuffle candidate (dwell=%.1f)"),
							*Ctx.Companion->GetName(), TimeAtCurrentCover);
				}
				else if (bDebugLogging || CovDbg())
					UE_LOG(LogCompanionAI, Log, TEXT("%s: no-peek-los strike %d/%d — holding"),
						*Ctx.Companion->GetName(), NoPeekLosStrikes, DebounceRequired);
			}
			else
			{
				NoPeekLosStrikes = 0;
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

				// Commit gate: hold the per-target trip at threshold until the point has served its
				// minimum peek cycles (re-checked every eval). The starvation backstop stays ungated —
				// it exists to break deadlocks and must not be starved.
				if (bPerTargetTripped && !bStarvationTripped
					&& PeekCyclesAtCover < (TickTuning ? TickTuning->MinPeekCyclesBeforeRelocate : 1))
				{
					CoverCompromiseConsecutiveCount = DebounceRequired;
					if (bDebugLogging || CovDbg())
						UE_LOG(LogCompanionAI, Log, TEXT("%s: compromise tripped but cycles=%d < min — holding invalidate"),
							*Ctx.Companion->GetName(), PeekCyclesAtCover);
				}
				else if (bPerTargetTripped || bStarvationTripped)
				{
					CoverCompromiseConsecutiveCount = 0;
					ArcStarvationCount = 0;
					UE_LOG(LogCompanionAI, Log,
						TEXT("%s: Cover INVALIDATE reason=%s outsideArc=%d bodyExposed=%d cycles=%d dwell=%.1f"),
						*Ctx.Companion->GetName(),
						bStarvationTripped ? TEXT("arc-starvation") : TEXT("flanked-compromised"),
						(int32)bOutsideArc, (int32)bBodyExposed, PeekCyclesAtCover, TimeAtCurrentCover);
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

		// --- Pressure signal + point-blank threat check (throttled ~5 Hz, one shared gather) ---
		PeekImpulseCooldownRemaining = FMath::Max(0.f, PeekImpulseCooldownRemaining - DeltaSeconds);
		PressureSampleTimer -= DeltaSeconds;
		const bool bPressureOn = TickTuning && TickTuning->bPressureResponsiveCover;
		const float PointBlankDist = TickTuning ? TickTuning->PointBlankAbandonDistance : 0.f;
		if (TickTuning && (bPressureOn || PointBlankDist > 0.f) && PressureSampleTimer <= 0.f)
		{
			PressureSampleTimer = 0.2f;

			TArray<AActor*, TInlineAllocator<8>> PressureThreats;
			GatherKnownThreats(Ctx.Companion, Ctx.Target, TickTuning->MaxThreatsForCoverScoring, PressureThreats);
			float NearestDist = TNumericLimits<float>::Max();
			AActor* NearestThreat = nullptr;
			for (AActor* const Threat : PressureThreats)
			{
				if (!IsValid(Threat)) continue;
				const float D = FVector::Dist2D(MyLocation, Threat->GetActorLocation());
				if (D < NearestDist) { NearestDist = D; NearestThreat = Threat; }
			}

			// A point-blank VISIBLE threat defeats cover logic: release to open-engage (turn and
			// fight, move-shoot) instead of cycling shuffles/covers with a chaser on our tail.
			if (PointBlankDist > 0.f && IsValid(NearestThreat) && NearestDist <= PointBlankDist)
			{
				const AAIController* LosCtrl = Cast<AAIController>(Ctx.Companion->GetController());
				if (LosCtrl && LosCtrl->LineOfSightTo(NearestThreat))
				{
					UE_LOG(LogCompanionAI, Log,
						TEXT("%s: Cover RELEASE reason=point-blank-threat dist=%.0f threat=%s — open-engage"),
						*Ctx.Companion->GetName(), NearestDist, *GetNameSafe(NearestThreat));
					if (UWorld* PbWorld = Ctx.Companion->GetWorld())
					{
						if (UCoverReservationSubsystem* PbSub = PbWorld->GetSubsystem<UCoverReservationSubsystem>())
						{
							if (AController* PbCtrl = Ctx.Companion->GetController())
								PbSub->MarkVacated(Cover.Handle, PbCtrl);
						}
					}
					Ctx.Blackboard->ClearValue(CoverTargetKey.GetSelectedKeyID());
					Ctx.Blackboard->SetValueAsBool(HasCoverPositionKey.SelectedKeyName, false);
					BlindHoldTime = 0.f;
					FruitlessPeeks = 0;
					return FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
				}
			}

			if (bPressureOn)
			{
				const float FarDist = TickTuning->PressureFarDistance;
				const float NearDist = TickTuning->PressureNearDistance;
				Pressure01 = (NearestDist < TNumericLimits<float>::Max())
					? FMath::Clamp((FarDist - NearestDist) / FMath::Max(FarDist - NearDist, 1.f), 0.f, 1.f)
					: 0.f;

				const bool bClosingNow = (PreviousNearestThreatDist >= 0.f && NearestDist < PreviousNearestThreatDist);
				const bool bClosingConfirmed = bClosingNow && bPreviousThreatWasClosing;
				bPreviousThreatWasClosing = bClosingNow;
				PreviousNearestThreatDist = (NearestDist < TNumericLimits<float>::Max()) ? NearestDist : -1.f;

				// Peek-now impulse: 2 consecutive closing samples crossing impulse distance, unsuppressed.
				bool bImpulseFired = false;
				if (!bSuppressed && PeekImpulseCooldownRemaining <= 0.f
					&& bClosingConfirmed && NearestDist < TNumericLimits<float>::Max()
					&& NearestDist <= TickTuning->PeekImpulseDistance)
				{
					const float WaitGateThreshold = (MinCoverIdleDwell + PeekCooldown)
						* ModePeekConfidenceScale(Ctx.Companion, TickTuning, false, Pressure01)
						* FirstPeekWaitMultiplier(Ctx.Companion, TickTuning, PeekCyclesAtCover);
					if (TimeInCoverIdle < WaitGateThreshold)
					{
						TimeInCoverIdle = WaitGateThreshold;
						PeekImpulseCooldownRemaining = TickTuning->PeekImpulseRearmSeconds;
						bImpulseFired = true;
					}
				}

				if (CovDbg())
				{
					UE_LOG(LogCompanionAI, Log,
						TEXT("[COVDBG] %s PRESSURE p01=%.2f nearDist=%.0f closing=%d confirmed=%d cooldownScale=%.2f impulse=%d"),
						*Ctx.Companion->GetName(), Pressure01, NearestDist, (int32)bClosingNow, (int32)bClosingConfirmed,
						ModePeekConfidenceScale(Ctx.Companion, TickTuning, false, Pressure01),
						(int32)bImpulseFired);
				}
			}
		}
		if (!bPressureOn)
			Pressure01 = 0.f;

		// Mode + pressure confidence shrink the whole wait; the first peek at a fresh point shrinks
		// it again on top, so taking cover under fire is answered rather than waited out.
		if (TimeInCoverIdle < (MinCoverIdleDwell + PeekCooldown)
			* ModePeekConfidenceScale(Ctx.Companion, TickTuning, false, Pressure01)
			* FirstPeekWaitMultiplier(Ctx.Companion, TickTuning, PeekCyclesAtCover)) return;

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
			NoPeekLosStrikes = 0;
			// Fix 3b: clear the just-repositioned latch — otherwise a stray set (e.g. an aborted move) could
			// keep the reposition/corner-peek family zeroed across the force re-roll and re-starve the roll.
			bJustRepositioned = false;
			LastDecisionTime = Now;
		}

		// Deep-debug: one line per decision ATTEMPT with the full counter state — with this plus
		// the [COVMOVE] trail, every reposition-without-firing has a named cause in the log.
		if (CovDbg())
		{
			UE_LOG(LogCompanionAI, Log,
				TEXT("[COVDBG] %s DECISION suppRaw=%d(gated=%d) supp01=%.2f hits4s=%d focused=%d cycles=%d strikes=%d holds=%d/%d dwellAtCover=%.1f idle=%.1f justRepo=%d lean=%d ammo=%d hp=%.2f"),
				*Ctx.Companion->GetName(), (int32)bSuppressedRaw, (int32)bSuppressed,
				Ctx.Companion->GetSuppression01(),
				Ctx.Companion->GetRecentDamageCount(TickTuning ? TickTuning->CoverCommitUnderFireWindow : 4.f),
				Ctx.Companion->GetPlayerFocusedEnemyCount(),
				PeekCyclesAtCover, NoPeekLosStrikes, ConsecutiveHolds, GetEffectiveMaxHolds(Ctx.Companion),
				TimeAtCurrentCover, TimeInCoverIdle, (int32)bJustRepositioned,
				(int32)CurrentLean, Ctx.Companion->GetCurrentAmmo(), Ctx.Companion->GetHealthFraction());
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
			UCoverGeometryStatics::GetCoverHeight(Cover.Data) == ECoverHeight::Crouch,
			TargetSightLoc, StandFireEyeHeight, Ctx.Target, Ctx.Companion);
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
			if (CovDbg())
				UE_LOG(LogCompanionAI, Log, TEXT("[COVDBG] %s GATE2 cover-LoS blocked hits=%d cycles=%d"),
					*Ctx.Companion->GetName(), BlockedRecheckHits, PeekCyclesAtCover);
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

				// Require 2 consecutive gated blocked checks (anti-thrash). No peek-cycle commit gate
				// here: a point that fails GATE2 can never complete a cycle, so gating the shuffle on
				// cycles deadlocked blind points into the strike-invalidate → EQS re-pick ping-pong.
				// FindShuffleCover verifies destination peek LoS, so this only ever moves to a point
				// that can actually shoot.
				if (BlockedRecheckHits >= 2)
				{
					const FCover BestCover = FindShuffleCover(Ctx.Companion, Cover, TargetLocation);
					if (BestCover.IsValid())
					{
						// Walked silent reposition (the old TeleportTo popped the companion across the
						// wall visibly). TickRepositionAction owns walk/stall/arrival; the BB swap and
						// side re-resolve happen at arrival, not here — the walk can still abort.
						BlockedRecheckHits = 0;
						if (UWorld* ShuffleWorld = Ctx.Companion->GetWorld())
							CommitSilentReposition(Ctx.Companion, BestCover, ShuffleWorld->GetTimeSeconds());
						return;
					}
				}
			}

			// Blind-hold release (enemy failed-peek relocate parity): the same-wall shuffle found
			// nothing and peeks keep coming up empty — this position cannot fight this target.
			// Release the cover to open-engage: move-shoot closes toward the fight and the EQS
			// re-covers near it once triggers re-trip. Pressure on the player shortens the patience —
			// the companion must not hide while the player is being pushed.
			BlindHoldTime += DeltaSeconds;
			{
				bool bPlayerPressured = false;
				if (PlayerPressureReleaseSeconds > 0.f)
				{
					if (const AEnemyCharacter* PressureEnemy = Cast<AEnemyCharacter>(Ctx.Target))
					{
						const AActor* PressurePlayer = Cast<AActor>(Ctx.Blackboard->GetValueAsObject(ACompanionAIController::BB_PlayerActor));
						bPlayerPressured = PressureEnemy->HasDetectedPlayer() && IsValid(PressurePlayer)
							&& FVector::DistSquared(PressurePlayer->GetActorLocation(), PressureEnemy->GetActorLocation())
								<= FMath::Square(PlayerPressureRadius);
					}
				}
				const bool bPressureTrip = bPlayerPressured && BlindHoldTime >= PlayerPressureReleaseSeconds;
				const bool bFruitlessTrip = FruitlessPeeksBeforeRelease > 0 && FruitlessPeeks >= FruitlessPeeksBeforeRelease;
				const bool bHoldTrip = BlindHoldReleaseSeconds > 0.f && BlindHoldTime >= BlindHoldReleaseSeconds;
				if (bPressureTrip || bFruitlessTrip || bHoldTrip)
				{
					UE_LOG(LogCompanionAI, Log,
						TEXT("%s: Cover RELEASE reason=%s blindHold=%.1f fruitless=%d — open-engage reposition"),
						*Ctx.Companion->GetName(),
						bPressureTrip ? TEXT("player-pressured") : bFruitlessTrip ? TEXT("fruitless-peeks") : TEXT("blind-hold"),
						BlindHoldTime, FruitlessPeeks);
					if (UWorld* ReleaseWorld = Ctx.Companion->GetWorld())
					{
						if (UCoverReservationSubsystem* ReleaseSub = ReleaseWorld->GetSubsystem<UCoverReservationSubsystem>())
						{
							if (AController* ReleaseCtrl = Ctx.Companion->GetController())
								ReleaseSub->MarkVacated(Cover.Handle, ReleaseCtrl);
						}
					}
					Ctx.Blackboard->ClearValue(CoverTargetKey.GetSelectedKeyID());
					Ctx.Blackboard->SetValueAsBool(HasCoverPositionKey.SelectedKeyName, false);
					BlindHoldTime = 0.f;
					FruitlessPeeks = 0;
					// A deliberate release must start the recommit cooldown, or MoveToCoverPoint
					// re-commits on the very next BT loop (low-HP trigger stays true) and the
					// companion duck-hops point to point instead of open-engaging.
					Ctx.Companion->StampNaturalCoverRelease();
					return FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
				}
			}

			// Speculative peek (enemy "peek for peeking's sake" parity): the target is known but
			// unseen from here and no verified shuffle destination exists — occasionally run the
			// normal peek decision anyway to LOOK. The burst-time muzzle/LoS gates withhold fire
			// unless the enemy is actually exposed, so a losing roll just costs a cautious look.
			bool bSpeculativePeek = false;
			if (!bSuppressed && SpeculativePeekChance > 0.f)
			{
				SpeculativePeekTimer += DeltaSeconds;
				if (SpeculativePeekTimer >= SpeculativePeekInterval)
				{
					SpeculativePeekTimer = 0.f;
					bSpeculativePeek = FMath::FRand() < SpeculativePeekChance;
				}
			}
			if (!bSpeculativePeek) return;
			if (bDebugLogging || CovDbg())
				UE_LOG(LogCompanionAI, Log, TEXT("%s: SPECULATIVE-PEEK roll passed — peeking without verified LoS"),
					*Ctx.Companion->GetName());
		}
		else
		{
			BlockedRecheckHits = 0;
			SpeculativePeekTimer = 0.f;
			BlindHoldTime = 0.f;
		}

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

		// Gate 4: only reachable at STAND cover now (ResolvePeekSideForCover maps crouch None→Front):
		// a tall wall with no verified side gap has no shot from this point. Hold tucked; the
		// debounced no-peek-los invalidate is the escape hatch.
		const bool bIsCrouchCover = UCoverGeometryStatics::GetCoverHeight(Cover.Data) == ECoverHeight::Crouch;
		if (CurrentLean == ECoverLean::None)
		{
			PeekCooldown = FMath::RandRange(MinPeekCooldown, MaxPeekCooldown);
			TimeInCoverIdle = 0.f;
			if (bDebugLogging || CovDbg())
				UE_LOG(LogCompanionAI, Log, TEXT("%s: STAY DOWN reason=stand-cover-no-side-gap"),
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
				if (bDebugLogging || CovDbg())
					UE_LOG(LogCompanionAI, Log, TEXT("%s: STAY DOWN reason=target-out-of-arc"), *Ctx.Companion->GetName());
				return;
			}
		}

		// Peek fire cone (enemy bEffectiveLOS parity): the burst must start with the target inside
		// the pose-reachable cone about wall-forward, measured from the PAWN (the arc gate above is
		// a flank detector about the cover point — this is the fire gate).
		if (!IsTargetInPeekCone(Ctx.Companion, Cover.Data, TargetLocation,
			TickTuning ? TickTuning->CoverPeekConeHalfAngleDeg : 75.f))
		{
			PeekCooldown = FMath::RandRange(MinPeekCooldown, MaxPeekCooldown);
			TimeInCoverIdle = 0.f;
			if (bDebugLogging || CovDbg())
				UE_LOG(LogCompanionAI, Log, TEXT("%s: STAY DOWN reason=target-out-of-peek-cone"), *Ctx.Companion->GetName());
			return;
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

		// Gate 5: roll the peek action.
		const bool bSideGap = (CurrentLean == ECoverLean::Left || CurrentLean == ECoverLean::Right);
		const int32 MinPeekCycles = TickTuning ? TickTuning->MinPeekCyclesBeforeRelocate : 1;
		const bool bNoFireAction = !bSideGap && !bStandEyeClear;
		const bool bRepoEligible = bNoFireAction
			|| (!bJustRepositioned && PeekCyclesAtCover >= MinPeekCycles);

		// Pressure-responsive hold-weight decay (Part B).
		const float PressureHoldScale = (TickTuning && TickTuning->bPressureResponsiveCover)
			? FMath::Lerp(1.f, TickTuning->PressureHoldWeightScaleAtMax, Pressure01)
			: 1.f;

		UE_LOG(LogCompanionAI, Log,
			TEXT("%s: PEEK-DECISION height=%s lean=%d sideGap=%d standEye=%d cycles=%d"),
			*Ctx.Companion->GetName(),
			bIsCrouchCover ? TEXT("Crouch") : TEXT("Stand"),
			(int32)CurrentLean, (int32)bSideGap, (int32)bStandEyeClear, PeekCyclesAtCover);

		EPeekAction Action = EPeekAction::Hold;
		const bool bWallhack = bIsCrouchCover && TickTuning && TickTuning->bCrouchPeekWallhackEnabled;

		if (bIsCrouchCover && bWallhack)
		{
			// --- Part A: scored crouch-peek selector (wallhack) ---
			const UCapsuleComponent* ScoreCap = Ctx.Companion->GetCapsuleComponent();
			const float ScoreCapR = ScoreCap ? ScoreCap->GetScaledCapsuleRadius() : 34.f;
			const float ScoreStandoff = ScoreCapR + 10.f;

			// Cache corner apexes per cover handle.
			if (CachedCornerApexHandle != Cover.Handle)
			{
				CachedCornerApexHandle = Cover.Handle;
				const float MaxReach = TickTuning->CrouchPeekMaxCornerReachCm;
				bCachedCornerLeftFound = UCoverGeometryStatics::TryGetCornerPeekApex(
					TickWorld, Cover.Data, ECoverLean::Left, ScoreStandoff, ScoreCapR, CornerPeekApexClearance,
					MaxReach, Ctx.Companion, CachedCornerApexLeft);
				bCachedCornerRightFound = UCoverGeometryStatics::TryGetCornerPeekApex(
					TickWorld, Cover.Data, ECoverLean::Right, ScoreStandoff, ScoreCapR, CornerPeekApexClearance,
					MaxReach, Ctx.Companion, CachedCornerApexRight);
			}

			// Gather extra threats (skip [0] which is the focus target).
			TArray<AActor*, TInlineAllocator<8>> ScorerThreats;
			GatherKnownThreats(Ctx.Companion, Ctx.Target,
				TickTuning->MaxThreatsForCoverScoring, ScorerThreats);
			TArray<AActor*, TInlineAllocator<4>> ExtraThreats;
			for (int32 i = 1; i < ScorerThreats.Num(); ++i)
				ExtraThreats.Add(ScorerThreats[i]);

			FCrouchPeekScoreParams ScoreParams;
			ScoreParams.CornerBaseScore = TickTuning->CrouchPeekCornerBaseScore;
			ScoreParams.OverTopBaseScore = TickTuning->CrouchPeekOverTopBaseScore;
			ScoreParams.CornerTieBonus = TickTuning->CrouchPeekCornerTieBonus;
			ScoreParams.ExtraThreatPenalty = TickTuning->CrouchPeekExtraThreatPenalty;
			ScoreParams.MinViableWeight = TickTuning->CrouchPeekMinViableWeight;
			ScoreParams.MaxCornerReachCm = TickTuning->CrouchPeekMaxCornerReachCm;
			ScoreParams.LowHpCornerBaseScore = TickTuning->LowHpCrouchPeekCornerBaseScore;
			ScoreParams.LowHpOverTopBaseScore = TickTuning->LowHpCrouchPeekOverTopBaseScore;
			ScoreParams.bLowHp = bLowHp;
			ScoreParams.bSpeculative = !bLosFromCover;
			ScoreParams.bStandEyeClear = bStandEyeClear;

			FCrouchPeekScores Scores;
			UCoverGeometryStatics::ScoreCrouchPeekOptions(TickWorld, Cover.Data,
				TargetSightLoc, ScoreStandoff,
				Ctx.Target, Ctx.Companion,
				bCachedCornerLeftFound, CachedCornerApexLeft,
				bCachedCornerRightFound, CachedCornerApexRight,
				MakeArrayView(ExtraThreats), ScoreParams, Scores);

			// Hoist scorer viability for the hold-cap promotion path.
			bLastScorerCornerLeftViable = Scores.bCornerLeftViable;
			bLastScorerCornerRightViable = Scores.bCornerRightViable;
			LastScorerBestCornerSide = Scores.BestCornerSide;

			const float BestCornerScore = FMath::Max(Scores.CornerLeftScore, Scores.CornerRightScore);
			const bool bAnyCornerViable = Scores.bCornerLeftViable || Scores.bCornerRightViable;

			// Split over-top score between Stand and Quick by existing weight ratio.
			const float StandBaseW = bLowHp ? LowHpStandWeight : StandWeight;
			const float QuickBaseW = bLowHp ? LowHpQuickWeight : QuickWeight;
			const float OverTopTotal = StandBaseW + QuickBaseW;
			const float StandFrac = (OverTopTotal > 0.f) ? (StandBaseW / OverTopTotal) : 0.5f;

			const float EffHoldWeight = (bLowHp ? LowHpHoldWeight : HoldWeight) * PressureHoldScale;
			const float RepoW = bRepoEligible ? (bLowHp ? LowHpRepositionWeight : RepositionWeight) : 0.f;
			const float StandUpRepoW = (bRepoEligible && bStandEyeClear)
				? (bLowHp ? LowHpStandUpAndRepositionWeight : StandUpAndRepositionWeight) : 0.f;

			const TPair<EPeekAction, float> ScoredWeights[] = {
				{ EPeekAction::CornerPeek,           BestCornerScore },
				{ EPeekAction::Stand,                Scores.OverTopScore * StandFrac },
				{ EPeekAction::Quick,                Scores.OverTopScore * (1.f - StandFrac) },
				{ EPeekAction::Hold,                 EffHoldWeight },
				{ EPeekAction::Reposition,           RepoW },
				{ EPeekAction::StandUpAndReposition, StandUpRepoW },
			};
			Action = RollPeekActionMulti(MakeArrayView(ScoredWeights));

			// Write CurrentLean/ResolvedPeekSide from the scorer's pick.
			if (Action == EPeekAction::CornerPeek && bAnyCornerViable)
			{
				CurrentLean = Scores.BestCornerSide;
				ResolvedPeekSide = ResolveSideFromLean(CurrentLean, Cover.Data, TargetLocation);
			}
			else if (Action == EPeekAction::Stand || Action == EPeekAction::Quick)
			{
				CurrentLean = ECoverLean::Front;
				ResolvedPeekSide = ResolveSideFromLean(CurrentLean, Cover.Data, TargetLocation);
			}

#if ENABLE_DRAW_DEBUG
			// CovDbg-only (NOT bDebugLogging — that flag ships enabled on the BT asset, and these
			// long target lines read as gameplay tracers to a playtester).
			if (CovDbg())
			{
				const FVector BaseHunker = UCoverGeometryStatics::GetHunkerPosition(Cover.Data, ScoreStandoff);
				const FVector OverTopEye = BaseHunker + FVector(0.f, 0.f, 150.f);
				auto DrawCandidate = [&](const FVector& Eye, bool bViable, const FColor& Color)
				{
					DrawDebugSphere(TickWorld, Eye, 10.f, 8, Color, false, 2.f, 0, 1.5f);
					const FColor LineColor = bViable ? FColor::Green : FColor::Red;
					DrawDebugLine(TickWorld, Eye, TargetSightLoc, LineColor, false, 2.f, 0, 1.f);
				};
				const FVector LeftEyeDraw = UCoverGeometryStatics::GetLeanPeekPosition(Cover.Data, ECoverLean::Left) + FVector(0.f, 0.f, 90.f);
				const FVector RightEyeDraw = UCoverGeometryStatics::GetLeanPeekPosition(Cover.Data, ECoverLean::Right) + FVector(0.f, 0.f, 90.f);
				if (bCachedCornerLeftFound)
					DrawCandidate(LeftEyeDraw, Scores.bCornerLeftViable, FColor::Blue);
				if (bCachedCornerRightFound)
					DrawCandidate(RightEyeDraw, Scores.bCornerRightViable, FColor::Orange);
				DrawCandidate(OverTopEye, Scores.bOverTopViable, FColor::Cyan);

				for (AActor* const Extra : ExtraThreats)
				{
					if (!IsValid(Extra)) continue;
					const FVector ExtraLoc = Extra->GetActorLocation() + FVector(0.f, 0.f, 90.f);
					if (Scores.bCornerLeftViable && bCachedCornerLeftFound)
						DrawDebugLine(TickWorld, LeftEyeDraw, ExtraLoc, FColor(128, 128, 255), false, 2.f, 0, 0.5f);
					if (Scores.bCornerRightViable && bCachedCornerRightFound)
						DrawDebugLine(TickWorld, RightEyeDraw, ExtraLoc, FColor(255, 178, 102), false, 2.f, 0, 0.5f);
					if (Scores.bOverTopViable)
						DrawDebugLine(TickWorld, OverTopEye, ExtraLoc, FColor(128, 255, 255), false, 2.f, 0, 0.5f);
				}
			}
#endif

			if (CovDbg())
			{
				UE_LOG(LogCompanionAI, Log,
					TEXT("[COVDBG] %s WALLHACK cL=%.1f(%d) cR=%.1f(%d) oT=%.1f(%d) bestCorner=%d geo=%d spec=%d extra=%d action=%d side=%d"),
					*Ctx.Companion->GetName(),
					Scores.CornerLeftScore, (int32)Scores.bCornerLeftViable,
					Scores.CornerRightScore, (int32)Scores.bCornerRightViable,
					Scores.OverTopScore, (int32)Scores.bOverTopViable,
					(int32)Scores.BestCornerSide, (int32)Scores.GeometricFallbackSide,
					(int32)ScoreParams.bSpeculative, ExtraThreats.Num(),
					(int32)Action, (int32)CurrentLean);
			}
		}
		else if (bSideGap)
		{
			// Stand cover (or legacy crouch path): verified side gap, corner peek primary.
			const TPair<EPeekAction, float> SideGapWeights[] = {
				{ EPeekAction::CornerPeek, bLowHp ? LowHpCornerPeekWeight : CornerPeekWeight },
				{ EPeekAction::Reposition, bRepoEligible
					? (bIsCrouchCover ? (bLowHp ? LowHpRepositionWeight : RepositionWeight)
					                  : (bLowHp ? LowHpRepositionWeightStand : RepositionWeightStand)) : 0.f },
				{ EPeekAction::Hold,       (bLowHp ? LowHpHoldWeight : HoldWeight) * PressureHoldScale },
			};
			Action = RollPeekActionMulti(MakeArrayView(SideGapWeights));

			// Legacy CoverEndpointStandPeekChance conversion (crouch corners, wallhack disabled).
			if (Action == EPeekAction::CornerPeek && bIsCrouchCover
				&& Cover.Data.bFrontCoverCrouched && bStandEyeClear
				&& FMath::FRand() < (TickTuning ? TickTuning->CoverEndpointStandPeekChance : 0.3f))
			{
				Action = EPeekAction::Stand;
			}
		}
		else if (bIsCrouchCover)
		{
			// Crouch cover, no side gap, wallhack disabled: legacy Front over-top roll.
			const float RepoW        = bRepoEligible ? (bLowHp ? LowHpRepositionWeight : RepositionWeight) : 0.f;
			const float StandUpRepoW = (bRepoEligible && bStandEyeClear) ? (bLowHp ? LowHpStandUpAndRepositionWeight : StandUpAndRepositionWeight) : 0.f;
			const float StandW = bStandEyeClear ? (bLowHp ? LowHpStandWeight : StandWeight) : 0.f;
			const float QuickW = bStandEyeClear ? (bLowHp ? LowHpQuickWeight : QuickWeight) : 0.f;
			const TPair<EPeekAction, float> FrontWeights[] = {
				{ EPeekAction::Stand,                StandW },
				{ EPeekAction::Quick,                QuickW },
				{ EPeekAction::Hold,                 (bLowHp ? LowHpHoldWeight : HoldWeight) * PressureHoldScale },
				{ EPeekAction::Reposition,           RepoW },
				{ EPeekAction::StandUpAndReposition, StandUpRepoW },
			};
			Action = RollPeekActionMulti(MakeArrayView(FrontWeights));
		}

		if (Action == EPeekAction::Hold)
		{
			const uint8 EffectiveMaxHolds = GetEffectiveMaxHolds(Ctx.Companion);
			if (ConsecutiveHolds < EffectiveMaxHolds)
			{
				++ConsecutiveHolds;
				PeekCooldown = FMath::RandRange(MinPeekCooldown, MaxPeekCooldown);
				TimeInCoverIdle = 0.f;
				UE_LOG(LogCompanionAI, Log, TEXT("%s: PEEK-ACTION=Hold ammo=%d"), *GetNameSafe(Ctx.Companion), Ctx.Companion->GetCurrentAmmo());
				if (bDebugLogging) UE_LOG(LogCompanionAI, Log, TEXT("%s: HOLD this cycle (%d/%d)"), *Ctx.Companion->GetName(), ConsecutiveHolds, EffectiveMaxHolds);
				return;
			}
			// Hold cap promotion: scorer-viable best option at crouch cover (wallhack), else legacy.
			if (bWallhack)
			{
				const bool bCornerViable = bLastScorerCornerLeftViable || bLastScorerCornerRightViable;
				if (bCornerViable)
				{
					Action = EPeekAction::CornerPeek;
					CurrentLean = LastScorerBestCornerSide;
					ResolvedPeekSide = ResolveSideFromLean(CurrentLean, Cover.Data, TargetLocation);
				}
				else if (bStandEyeClear)
				{
					Action = EPeekAction::Stand;
				}
				else if (bCachedCornerLeftFound || bCachedCornerRightFound)
				{
					// Speculative fallback: corner exists but LOS blocked -- peek to look.
					Action = EPeekAction::CornerPeek;
					if (bCachedCornerLeftFound && !bCachedCornerRightFound)
						CurrentLean = ECoverLean::Left;
					else if (bCachedCornerRightFound && !bCachedCornerLeftFound)
						CurrentLean = ECoverLean::Right;
					else
						CurrentLean = Cover.Data.bLeftCoverCrouched ? ECoverLean::Left : ECoverLean::Right;
					ResolvedPeekSide = ResolveSideFromLean(CurrentLean, Cover.Data, TargetLocation);
				}
				else if (bRepoEligible)
				{
					Action = EPeekAction::Reposition;
				}
				else
				{
					++ConsecutiveHolds;
					PeekCooldown = FMath::RandRange(MinPeekCooldown, MaxPeekCooldown);
					TimeInCoverIdle = 0.f;
					return;
				}
			}
			else if (bSideGap)
			{
				Action = EPeekAction::CornerPeek;
			}
			else if (bStandEyeClear)
			{
				Action = EPeekAction::Stand;
			}
			else if (bRepoEligible)
			{
				Action = EPeekAction::Reposition;
			}
			else
			{
				++ConsecutiveHolds;
				PeekCooldown = FMath::RandRange(MinPeekCooldown, MaxPeekCooldown);
				TimeInCoverIdle = 0.f;
				if (bDebugLogging)
					UE_LOG(LogCompanionAI, Log, TEXT("%s: HOLD-CAP promote suppressed — no side gap, stand-eye blocked, no reposition"),
						*Ctx.Companion->GetName());
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

			if (Action == EPeekAction::Reposition)
			{
				CommitSilentReposition(Ctx.Companion, ShuffleTarget, Now);
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

			// StandUpAndReposition: stand up, start burst in place (Phase A), then walk-and-fire (Phase B).
			if (CovDbg())
				UE_LOG(LogCompanionAI, Log, TEXT("[COVMOVE] %s STANDUP-REPO-COMMIT to=%s cyclesAtOld=%d"),
					*GetNameSafe(Ctx.Companion), *ShuffleTarget.Data.Location.ToCompactString(), PeekCyclesAtCover);
			UE_LOG(LogCompanionAI, Log, TEXT("%s: PEEK-ACTION=StandUpAndReposition ammo=%d"), *GetNameSafe(Ctx.Companion), Ctx.Companion->GetCurrentAmmo());
			CurrentBurstAction = EPeekAction::StandUpAndReposition;
			LastDecisionTime = Now;
			bRepositionStandPhase = true;
			bStandUpRepositionWalking = false;
			BurstTimer = FMath::RandRange(MinFireBurst, MaxFireBurst);
			AmmoAtBurstStart = Ctx.Companion->GetCurrentAmmo();
			bIsFiringBurst = true;
			if (AAIController* AIC = Cast<AAIController>(Ctx.Companion->GetController()))
				AIC->StopMovement();
			if (UCharacterMovementComponent* CMC = Ctx.Companion->GetCharacterMovement())
				CMC->StopMovementImmediately();
			UE_LOG(LogCompanionDiag, Verbose, TEXT("%s: [CrouchCall] t=%.3f site=StandUpRepoCommit action=UnCrouch"),
				*GetNameSafe(Ctx.Companion), Ctx.Companion->GetWorld() ? Ctx.Companion->GetWorld()->GetTimeSeconds() : 0.f);
			Ctx.Companion->UnCrouch();
			if (Cover.IsValid())
				CompanionSnapToCoverFacing(Ctx.Companion, Cover.Data);
			if (Anim)
			{
				Anim->ExitCoverPose();
				// Phase A fires in place — over-top montage at crouch cover (see Stand/Quick commit).
				ActivePeekMontage = bIsCrouchCover
					? Anim->PlayOverTopPeek(ResolvedPeekSide)
					: Anim->PlayPeekFire(ResolvedPeekSide);
			}
			// Muzzle-verified first shot: the 10 Hz withhold only runs from the next tick — a blocked
			// commit starts HELD and the withhold resumes fire when the muzzle clears. PeekFireDelaySeconds
			// likewise starts HELD until the animation reaches exposure.
			StandBurstMuzzleCheckTimer = 0.f;
			PeekFireDelayRemaining = PeekFireDelaySeconds;
			if (PeekFireDelayRemaining <= 0.f && IsBurstMuzzleClear(Ctx.Companion, Ctx.Target, TickIgnoredAttached))
			{
				Ctx.Companion->StartWeaponFire();
				bStandBurstFireHeld = false;
			}
			else
			{
				bStandBurstFireHeld = true;
			}
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
			// CornerPeek apex from the same corner march the edge-aligned home uses — the baked-point
			// ±offset apex sits in a different reference frame and lands short of the corner when the
			// bake is set back from it. Captured at commit — assumes the cover point's lean geometry
			// doesn't change mid-action.
			if (CurrentLean != ECoverLean::None)
			{
				const UCapsuleComponent* ApexCap = Ctx.Companion->GetCapsuleComponent();
				const float ApexCapR = ApexCap ? ApexCap->GetScaledCapsuleRadius() : 34.f;
				CornerPeekApexLocation = UCoverGeometryStatics::GetCornerPeekApex(
					Ctx.Companion->GetWorld(), Cover.Data, CurrentLean,
					ApexCapR + 10.f, ApexCapR, CornerPeekApexClearance, Ctx.Companion);
			}
			else
			{
				CornerPeekApexLocation = CornerPeekHomeLocation;
			}
			CurrentBurstAction = EPeekAction::CornerPeek;
			LastDecisionTime = Now;
			bCornerPeekReturning = false;
			AmmoAtBurstStart = Ctx.Companion->GetCurrentAmmo();
			PeekFireDelayRemaining = PeekFireDelaySeconds;
			bIsFiringBurst = true;
			BurstTimer = FMath::RandRange(MinFireBurst, MaxFireBurst);
			if (Cover.IsValid())
				CompanionSnapToCoverFacing(Ctx.Companion, Cover.Data);
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
			// CovDbg-only (NOT bDebugLogging) - same playtester-visibility rule as the idle draws.
			if (CovDbg())
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

		if (Cover.IsValid())
			CompanionSnapToCoverFacing(Ctx.Companion, Cover.Data);
		if (Anim)
		{
			Anim->ExitCoverPose();
			// Crouch cover: Stand/Quick IS the over-top — the LoU-parity montage owns the
			// stand-up-and-fire-over visual (a side-lean montage while standing reads broken).
			ActivePeekMontage = bIsCrouchCover
				? Anim->PlayOverTopPeek(ResolvedPeekSide)
				: Anim->PlayPeekFire(ResolvedPeekSide);
			if (bIsCrouchCover && !ActivePeekMontage.IsValid() && !bWarnedOverTopUnwired)
			{
				bWarnedOverTopUnwired = true;
				UE_LOG(LogCompanionAI, Warning,
					TEXT("%s: over-top peek montage unwired (CoverPeekOverTopMontage) — falling back to montage-less stand-up"),
					*GetNameSafe(Ctx.Companion));
			}
		}

		UE_LOG(LogCompanionDiag, Verbose, TEXT("%s: [CrouchCall] t=%.3f site=StandQuickPeekCommit action=UnCrouch"),
			*GetNameSafe(Ctx.Companion), Ctx.Companion->GetWorld() ? Ctx.Companion->GetWorld()->GetTimeSeconds() : 0.f);
		Ctx.Companion->UnCrouch();
		// Muzzle-verified first shot (mirrors the StandUpAndReposition commit). PeekFireDelaySeconds
		// starts the burst HELD so the withhold resumes fire once the animation has reached exposure.
		StandBurstMuzzleCheckTimer = 0.f;
		AmmoAtBurstStart = Ctx.Companion->GetCurrentAmmo();
		PeekFireDelayRemaining = PeekFireDelaySeconds;
		if (PeekFireDelayRemaining <= 0.f && IsBurstMuzzleClear(Ctx.Companion, Ctx.Target, TickIgnoredAttached))
		{
			Ctx.Companion->StartWeaponFire();
			bStandBurstFireHeld = false;
		}
		else
		{
			bStandBurstFireHeld = true;
		}
		bIsFiringBurst = true;
		DebugBurstLosCheckTimer = 0.f;
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
		// Peek fire delay: hold the first shot until the peek animation has reached exposure.
		if (PeekFireDelayRemaining > 0.f)
			PeekFireDelayRemaining -= DeltaSeconds;
		// Part C burst-clock fix: BurstTimer must NOT decrement while the peek-out animation
		// is still winding up, otherwise quick peeks expire before a single round fires.
		if (PeekFireDelayRemaining <= 0.f)
			BurstTimer -= DeltaSeconds / ModePeekConfidenceScale(Ctx.Companion, TickTuning, true, Pressure01);

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
		// While a root-motion peek montage plays, the montage owns yaw — no rotation writes
		// (the commit already snapped to wall-forward; Phase B stops the montage before walking).
		const bool bPeekMontageDriving = ActivePeekMontage.IsValid()
			&& Anim && Anim->Montage_IsPlaying(ActivePeekMontage.Get());
		if (!bPeekMontageDriving)
		{
			const bool bUseSlotForward = (CurrentBurstAction == EPeekAction::StandUpAndReposition
				&& bStandUpRepositionWalking && Cover.IsValid());
			const FRotator LookAtRot = (TargetLocation - MyLocation).Rotation();
			const FRotator DesiredRot = bUseSlotForward
				? FRotator(0.f, CachedSlotForwardYaw, 0.f)
				: FRotator(0.f, LookAtRot.Yaw, 0.f);
			Ctx.Companion->SetActorRotation(FMath::RInterpTo(Ctx.Companion->GetActorRotation(),
				DesiredRot, DeltaSeconds, Ctx.Companion->RotationInterpSpeed));
		}

		// Dispatch new multi-phase actions before the shared burst logic.
		if (CurrentBurstAction == EPeekAction::StandUpAndReposition && RepositionTargetCover.IsValid())
		{
			// Fix 2c: Phase A is stand-up fire IN PLACE at the hunker — same wall-burn exposure as the plain
			// Stand/Quick burst, but the dispatch returns before the muzzle-withhold ran below. Run it here as
			// a backstop so a mid-Phase-A occlusion pauses the trigger (no shots into our own wall). Phase B
			// walks and owns its own fire cadence, so only guard Phase A (bRepositionStandPhase).
			if (bRepositionStandPhase && !bSuppressed && !Ctx.Companion->IsReloading())
				TickStandBurstMuzzleWithhold(Ctx.Companion, Ctx.Target, TickIgnoredAttached, DeltaSeconds, Cover.IsValid() ? &Cover.Data : nullptr);
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
			if (Cover.IsValid() && UCoverGeometryStatics::GetCoverHeight(Cover.Data) == ECoverHeight::Crouch)
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
		TickStandBurstMuzzleWithhold(Ctx.Companion, Ctx.Target, TickIgnoredAttached, DeltaSeconds, Cover.IsValid() ? &Cover.Data : nullptr);

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
		// Burst ended without ReturnToCover — drop the fruitless-peek baseline or a later peek at a
		// different point inherits it and counts a phantom fruitless.
		AmmoAtBurstStart = -1;
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
				// Combat leash is decoupled from the follow-task sprint threshold: Combat mode
				// roams furthest (advances, leads); Normal-mode fights stay tighter in your fight.
				LeashDist = Ctx.Companion->GetMode() == ECompanionMode::Combat
					? T->CombatLeashDistance : T->NormalCombatLeashDistance;
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

		// Cover-commit gate (mirrors BTTask_MoveToCoverPoint's trigger model): only leave open-engage
		// for real cover when a situational trigger demands it — under fire, low HP, reloading/low
		// ammo, or outnumbered. Without this the reseek yanks the companion out of a stand-fight
		// toward any baked point in CoverSearchRadius.
		bool bCommitAllowed = true;
		if (CoverTuning)
		{
			AAIController* ReseekAIC = Cast<AAIController>(Ctx.Companion->GetController());
			const int32 ReseekThreats = CompanionCover::CountKnownThreats(ReseekAIC, CompanionCover::OutnumberedCountCap(*CoverTuning));
			const CompanionCover::FCoverTriggers ReseekTriggers = CompanionCover::EvaluateTriggers(
				*Ctx.Companion, *CoverTuning, ReseekThreats, /*bForRelease=*/false);
			bCommitAllowed = ReseekTriggers.Any();

			// Natural-cycling recommit cooldown — same bar as MoveToCoverPoint's commit gate.
			if (bCommitAllowed && CoverTuning->NaturalReleaseRecommitCooldown > 0.f && CoverWorld)
			{
				const float SinceRelease = CoverWorld->GetTimeSeconds() - Ctx.Companion->GetLastNaturalReleaseTime();
				if (SinceRelease < CoverTuning->NaturalReleaseRecommitCooldown
					&& !CompanionCover::HasPressureSpiked(*Ctx.Companion, *CoverTuning, ReseekThreats))
					bCommitAllowed = false;
			}

			// Low-HP dash gate — wounded-and-alone stays mobile unless a duck spot is close.
			if (bCommitAllowed && !CompanionCover::LowHealthDashAllowed(
					CoverWorld, MyLocation, Ctx.Companion->GetController(), *CoverTuning, ReseekTriggers))
				bCommitAllowed = false;

			if (bDebugLogging && !bCommitAllowed && ReseekTriggers.Any())
				UE_LOG(LogCompanionAI, Log, TEXT("%s: open-engage reseek SUPPRESSED (recommit-cooldown or low-hp dash gate)"),
					*Ctx.Companion->GetName());
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
				const UDoorRegistrySubsystem* OpenDoorRegistry = CoverWorld->GetSubsystem<UDoorRegistrySubsystem>();

				bool bFoundReachable = false;
				for (const FCover& Candidate : OpenEngageCandidates)
				{
					if (!Candidate.IsValid()) continue;
					// Bounds query is a box — enforce the commit radius as a true distance.
					if (FVector::DistSquared(MyLocation, Candidate.Data.Location) > FMath::Square(CommitRadius)) continue;
					// Same-floor gate — the box spans storeys; a slot one floor up is never a re-seek target.
					if (CoverTuning->CoverPickMaxZDelta > 0.f
						&& FMath::Abs(MyLocation.Z - Candidate.Data.Location.Z) > CoverTuning->CoverPickMaxZDelta) continue;
					AController* Occupant = CoverSys->GetOccupyingController(Candidate.Handle);
					if (Occupant && Occupant != CoverController) continue;
					if (IsValid(ResSub) && ResSub->IsOnPostVacateCooldown(Candidate.Handle, CoverController, CoverTuning->CoverSwitchPostVacateCooldown))
						continue;
					// Behind a closed door = not reachable for this probe (same rule as the EQS
					// DoorCrossing filter) — else the task exits to MoveToCoverPoint for a cover
					// the EQS re-pick will then reject.
					if (IsValid(OpenDoorRegistry) && OpenDoorRegistry->AnyClosedDoorBlocksSegment(MyLocation, Candidate.Data.Location))
						continue;

					// Fire-arc gate: target must be within the cover's engagement arc.
					const FVector ReseekToTarget2D = (TargetLocation - Candidate.Data.Location).GetSafeNormal2D();
					const FVector ReseekFireFwd    = UCoverGeometryStatics::GetFireArcForward(Candidate.Data);
					const float   ReseekArcDot     = FVector::DotProduct(ReseekFireFwd, ReseekToTarget2D);
					if (ReseekArcDot < ReseekArcCos) continue;

					// Peek-LoS gate: must be able to actually fire from this cover toward the threat.
					if (!UCoverGeometryStatics::CanPeekShoot(CoverWorld, Candidate.Data,
						UCoverGeometryStatics::GetCoverHeight(Candidate.Data) == ECoverHeight::Crouch,
						TargetSightLoc, StandFireEyeHeight, Ctx.Target, Ctx.Companion))
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

	// Angle-seek brain: 2+ enemies hard-focusing the player while the companion is unpressured →
	// actively work an angle on them (Normal: cover with a firing line; Combat: move-shoot flank).
	// Runs on the suppression "silent acknowledgement" — pressure on the companion disarms it.
	if (TickAngleSeek(OwnerComp, Ctx.Companion, Ctx.Target, MyLocation, bPlayerTooFar, DeltaSeconds))
		return;

	// Combat-mode advance hop: cover-to-cover bound that gains ground — cover as movement, not a
	// campsite (touch, peek, quick release via CombatCoverMaxCommitTime / trigger-clear exit).
	if (TickCombatAdvanceHop(OwnerComp, Ctx.Companion, Ctx.Target, MyLocation, bPlayerTooFar, DeltaSeconds))
		return;

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

		// An active angle-seek can't work a blocked line — its Combat-mode motor is the
		// jiggle-drift lateral bias, and jiggle is latched off below while LoS is blocked. All it
		// does here is hard-gate TickCombatAdvanceHop, the one system that CAN route around the
		// blocker; and because ResetTaskState clears the flag without stamping the cooldown, the
		// seek re-armed on every abandon/restart and the companion stood in a permanent
		// hold/abandon loop. End it (stamps AngleSeekCooldown) once the block outlives a
		// transient corner-clip.
		if (bAngleSeekActive && LosBlockedAccum >= AimDropOnLosBlockedSeconds)
			EndAngleSeek(Ctx.Companion, TEXT("los-blocked"));

		// --- Grenade lob (enemy-grenadier parity): the target has stayed hidden — flush it out.
		// CanThrow() covers supply/cooldown/telegraph-in-progress; a full window elapses between
		// attempts whether the throw commits or fails (range/arc). Player-safety: never throw when
		// the player stands inside blast radius + buffer of the landing point.
		if (UEnemyGrenadierComponent* GrenComp = Ctx.Companion->GetOrCreateGrenadierComponent())
		{
			GrenadeLosBlockedAccum += DeltaSeconds;
			const ACompanionAIController* GrenAIC = Cast<ACompanionAIController>(Ctx.Companion->GetController());
			const UCompanionTuningDataAsset* GrenTuning = GrenAIC ? GrenAIC->GetTuning() : nullptr;
			if (GrenTuning && GrenadeLosBlockedAccum >= GrenTuning->GrenadeLobTriggerLOSBlockedTime)
			{
				GrenadeLosBlockedAccum = 0.f;

				bool bPlayerSafe = true;
				if (const APawn* GrenPlayer = GrenAIC->GetPlayerCharacter())
				{
					const float SafeDist = GrenTuning->GrenadeDamageRadius + GrenTuning->GrenadePlayerSafetyBuffer;
					bPlayerSafe = FVector::DistSquared(GrenPlayer->GetActorLocation(), LastKnownTargetLocation)
						> FMath::Square(SafeDist);
				}

				if (bPlayerSafe && bHasLastKnownTargetLocation && !Ctx.Companion->IsReloading()
					&& GrenComp->CanThrow() && GrenComp->TryThrowAt(LastKnownTargetLocation))
				{
					if (bDebugLogging)
						UE_LOG(LogCompanionAI, Log, TEXT("%s: GRENADE LOB at last-known %s"),
							*Ctx.Companion->GetName(), *LastKnownTargetLocation.ToCompactString());
				}
			}
		}

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

		// Hold the abandon while a grenade wind-up plays — OnTaskFinished would cancel the throw
		// this same branch just committed.
		const UEnemyGrenadierComponent* AbandonGren = Ctx.Companion->GetGrenadierComponent();
		const bool bGrenadeWindingUp = IsValid(AbandonGren) && AbandonGren->IsTelegraphing();

		if (!bGrenadeWindingUp && LosBlockedAbandonSeconds > 0.f && LosBlockedAccum >= LosBlockedAbandonSeconds)
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
	GrenadeLosBlockedAccum = 0.f;

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

	// Mode burst personality for the open-engage branch — Combat fires longer and pauses shorter,
	// Defensive and Stealth both collapse to exactly 1. Hoisted out of the block below because this
	// path runs EVERY FRAME; it takes TickTuning directly so the resolve costs nothing at all here.
	// The pause scale is the reciprocal, so one lever moves both halves of the cadence in opposite
	// directions. Pressure is deliberately not passed: it is a cover concept, the open-engage
	// decrement has never been pressure-scaled, and Pressure01 can hold a stale value from an
	// earlier cover stint.
	const float ModeBurstScale = ModePeekConfidenceScale(Ctx.Companion, TickTuning, true);
	const float ModePauseScale = 1.f / FMath::Max(ModeBurstScale, KINDA_SMALL_NUMBER);

	if (!bReloadingNow)
	{
		BurstTimer -= bIsFiringBurst ? DeltaSeconds / ModeBurstScale : DeltaSeconds;

		if (bIsFiringBurst && BurstTimer <= 0.0f)
		{
			if (bDebugLogging)
				UE_LOG(LogCompanionAI, Log, TEXT("%s: FIRE STOP reason=burst-end-open"), *Ctx.Companion->GetName());
			Ctx.Companion->StopWeaponFire();
			bIsFiringBurst = false;
			BurstTimer = FirePauseDuration * ModePauseScale;
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
		// Cancel an in-flight grenade wind-up (enemy-task parity) — no-op unless telegraphing;
		// the cancel broadcast stops the throw montage.
		if (UEnemyGrenadierComponent* GrenComp = Companion->GetGrenadierComponent())
			GrenComp->CancelThrow();

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
		// Stamp the cycle carry before ResetTaskState wipes the live counter — ExecuteTask restores
		// it when the next claim lands on the same physical point. Guarded on a valid handle so the
		// post-AbortTask call (handle already wiped there) can't overwrite AbortTask's stamp.
		if (LastTickCoverHandle.IsValid())
		{
			PeekCycleCarryHandle = LastTickCoverHandle;
			PeekCycleCarryCount = PeekCyclesAtCover;
		}
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
