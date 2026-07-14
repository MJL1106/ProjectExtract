#include "Character/PawnFloatDiag.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Character/ExtractionPlayer.h"
#include "Character/ExtractionPlayerInterface.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Animation/ExtractionAnimInstance.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "TraversalComponent.h"
#include "UObject/PropertyPortFlags.h"
#include "UObject/UnrealType.h"
#include "World/DoorBase.h"

DEFINE_LOG_CATEGORY_STATIC(LogPawnFloatDiag, Log, All);

static TAutoConsoleVariable<int32> CVarPawnFloatDiag(
	TEXT("debug.PawnFloat"), 0,
	TEXT("1 = log pawn float anomalies (walking on air, upward launch, mesh offset drift) for every character in the world."),
	ECVF_Default);

// Anomaly thresholds (cm / cm-per-second). Capsule >AirGap above traced ground while Walking,
// upward speed >LaunchVelZ while Falling, or mesh/root drifted >MeshDrift from its authored pose.
static constexpr float FloatDiag_AirGap = 15.f;
static constexpr float FloatDiag_LaunchVelZ = 100.f;
static constexpr float FloatDiag_MeshDrift = 15.f;
static constexpr float FloatDiag_LogInterval = 0.25f;

/** Camera-vs-body yaw separation (deg) that flags a pose latch, and how long it must persist.
 *  With yaw-follow active the actor snaps to control yaw every tick, so a sustained separation
 *  on a player-controlled character is impossible unless some system wedged body rotation. */
static constexpr float FloatDiag_AimDesyncYaw = 90.f;
static constexpr float FloatDiag_AimDesyncHold = 0.75f;

// Full gate dump for a player character: rotation alignment, mesh-component spin, anim vars,
// and every system flag that can wedge body pose. Shared by the AIM-DESYNC detector and the
// debug.PawnFloat 2 heartbeat.
static void LogPoseGates(const TCHAR* Tag, float Elapsed, ACharacter& Character,
	const USkeletalMeshComponent& MeshComp, const UCharacterMovementComponent& Move)
{
	const float ActorYaw = Character.GetActorRotation().Yaw;
	const float AimYaw = Character.GetBaseAimRotation().Yaw;
	const float DeltaYaw = FMath::FindDeltaAngleDegrees(ActorYaw, AimYaw);

	// Mesh-component spin: a system rotating the mesh instead of the actor shows here while
	// actor/control yaw stay aligned.
	float MeshRelYawDrift = 0.f;
	if (const ACharacter* CDO = Character.GetClass()->GetDefaultObject<ACharacter>())
		if (const USkeletalMeshComponent* CDOMesh = CDO->GetMesh())
			MeshRelYawDrift = FMath::FindDeltaAngleDegrees(
				CDOMesh->GetRelativeRotation().Yaw, MeshComp.GetRelativeRotation().Yaw);

	const UExtractionAnimInstance* ExtAnim = Cast<UExtractionAnimInstance>(MeshComp.GetAnimInstance());
	const IExtractionPlayerInterface* Iface = Cast<IExtractionPlayerInterface>(&Character);
	const AExtractionPlayer* Player = Cast<AExtractionPlayer>(&Character);
	const UTraversalComponent* Traversal = Iface ? Iface->GetTraversalComponent() : nullptr;
	const UAnimInstance* AnimInst = MeshComp.GetAnimInstance();

	// Bone-level evidence for the "legs fold up / body floats" pose: pelvis and foot height above
	// the capsule bottom. A grounded capsule with feet 40cm up IS the visual, quantified.
	const float CapsuleBottom = Character.GetActorLocation().Z
		- (Character.GetCapsuleComponent() ? Character.GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 0.f);
	auto BoneAboveGround = [&](const TCHAR* Bone) -> float
	{
		const FName BoneName(Bone);
		return MeshComp.GetBoneIndex(BoneName) != INDEX_NONE
			? MeshComp.GetBoneLocation(BoneName).Z - CapsuleBottom : -999.f;
	};

	// Nearest door + whether its input component is currently live on a controller — the float
	// tracks the UWC doors' proximity Trigger box, whose only world effect is EnableInput/DisableInput.
	const ADoorBase* NearestDoor = nullptr;
	float NearestDoorDist = 99999.f;
	for (TActorIterator<ADoorBase> It(Character.GetWorld()); It; ++It)
	{
		const float Dist = FVector::Dist(It->GetActorLocation(), Character.GetActorLocation());
		if (Dist < NearestDoorDist) { NearestDoorDist = Dist; NearestDoor = *It; }
	}

	// Generic anim-variable diff vs the class defaults: names ANY property (C++ or BP-added) that
	// something wrote since spawn — bool/numeric only, capped to keep the line readable.
	FString VarDiff;
	if (AnimInst)
	{
		const UObject* AnimCDO = AnimInst->GetClass()->GetDefaultObject();
		int32 Shown = 0;
		for (TFieldIterator<FProperty> PropIt(AnimInst->GetClass()); PropIt && Shown < 40; ++PropIt)
		{
			const FProperty* Prop = *PropIt;
			if (!Prop->IsA<FBoolProperty>() && !Prop->IsA<FNumericProperty>()) continue;
			const void* InstVal = Prop->ContainerPtrToValuePtr<void>(AnimInst);
			const void* CDOVal = Prop->ContainerPtrToValuePtr<void>(AnimCDO);
			if (Prop->Identical(InstVal, CDOVal)) continue;
			FString ValStr;
			Prop->ExportTextItem_Direct(ValStr, InstVal, nullptr, nullptr, PPF_None);
			VarDiff += FString::Printf(TEXT("%s=%s "), *Prop->GetName(), *ValStr);
			++Shown;
		}
	}

	UE_LOG(LogPawnFloatDiag, Warning,
		TEXT("[FloatDiag] %s %s %.1fs | deltaYaw=%.1f actorYaw=%.1f aimYaw=%.1f yawFollow=%d meshRelYawDrift=%.1f")
		TEXT(" | anim: AimYaw=%.1f AimPitch=%.1f Speed=%.0f vault=%d climb=%d mantle=%d")
		TEXT(" | traversalType=%d reviving=%d beingRevivedAnim=%d takedown=%d dbno=%d")
		TEXT(" | mode=%s montage=%s")
		TEXT(" | pelvisZ=%.0f footL=%.0f footR=%.0f rootZ=%.0f | door=%s dist=%.0f doorInput=%d")
		TEXT(" | floor: walkable=%d blocking=%d dist=%.1f comp=%s:%s")
		TEXT(" | vars: %s"),
		*Character.GetName(), Tag, Elapsed, DeltaYaw, ActorYaw, AimYaw,
		Character.bUseControllerRotationYaw ? 1 : 0, MeshRelYawDrift,
		ExtAnim ? ExtAnim->AimYaw : -999.f, ExtAnim ? ExtAnim->AimPitch : -999.f,
		ExtAnim ? ExtAnim->Speed : -999.f,
		ExtAnim ? (int32)ExtAnim->bIsVaulting : -1, ExtAnim ? (int32)ExtAnim->bIsClimbing : -1,
		ExtAnim ? (int32)ExtAnim->bIsMantling : -1,
		Traversal ? (int32)Traversal->GetActiveType() : -1,
		Player ? (int32)Player->IsRevivingTarget() : -1,
		Iface ? (int32)Iface->IsBeingRevivedMontagePlaying() : -1,
		Iface ? (int32)Iface->IsInTakedown() : -1,
		Iface ? (int32)Iface->GetIsDBNO() : -1,
		*UEnum::GetValueAsString(Move.MovementMode.GetValue()),
		*GetNameSafe(AnimInst ? AnimInst->GetCurrentActiveMontage() : nullptr),
		BoneAboveGround(TEXT("pelvis")), BoneAboveGround(TEXT("foot_l")),
		BoneAboveGround(TEXT("foot_r")), BoneAboveGround(TEXT("root")),
		*GetNameSafe(NearestDoor), NearestDoorDist,
		NearestDoor && NearestDoor->InputComponent ? 1 : 0,
		Move.CurrentFloor.bWalkableFloor ? 1 : 0, Move.CurrentFloor.bBlockingHit ? 1 : 0,
		Move.CurrentFloor.FloorDist,
		*GetNameSafe(Move.CurrentFloor.HitResult.GetActor()),
		*GetNameSafe(Move.CurrentFloor.HitResult.GetComponent()),
		*VarDiff);
}

// Sustained camera-vs-body desync on the player = the pose-latch float, regardless of which
// system wedged it. One line per second while active, dumping every candidate gate; an "ended"
// line records the episode length. Player-only: AI aim routinely leads its body.
static void LogAimDesync(ACharacter& Character, const USkeletalMeshComponent& MeshComp,
	const UCharacterMovementComponent& Move, UWorld& World)
{
	static TMap<TWeakObjectPtr<const ACharacter>, float> DesyncStart;
	static TMap<TWeakObjectPtr<const ACharacter>, float> LastDesyncLog;

	if (!Character.IsPlayerControlled()) return;

	const float ActorYaw = Character.GetActorRotation().Yaw;
	const float AimYaw = Character.GetBaseAimRotation().Yaw;
	const float DeltaYaw = FMath::FindDeltaAngleDegrees(ActorYaw, AimYaw);
	const float Now = World.GetTimeSeconds();

	if (FMath::Abs(DeltaYaw) <= FloatDiag_AimDesyncYaw)
	{
		if (const float* Started = DesyncStart.Find(&Character))
		{
			UE_LOG(LogPawnFloatDiag, Warning, TEXT("[FloatDiag] %s AIM-DESYNC ended after %.1fs"),
				*Character.GetName(), Now - *Started);
			DesyncStart.Remove(&Character);
		}
		return;
	}

	const float* Started = DesyncStart.Find(&Character);
	if (!Started)
	{
		DesyncStart.Add(&Character, Now);
		return;
	}
	if (Now - *Started < FloatDiag_AimDesyncHold) return;
	if (const float* Last = LastDesyncLog.Find(&Character); Last && Now - *Last < 1.f) return;
	LastDesyncLog.Add(&Character, Now);

	LogPoseGates(TEXT("AIM-DESYNC"), Now - *Started, Character, MeshComp, Move);
}

static void LogFloatAnomalies(ACharacter& Character)
{
	UWorld* World = Character.GetWorld();
	const UCharacterMovementComponent* Move = Character.GetCharacterMovement();
	const UCapsuleComponent* Capsule = Character.GetCapsuleComponent();
	const USkeletalMeshComponent* MeshComp = Character.GetMesh();
	if (!World || !Move || !Capsule || !MeshComp) return;

	LogAimDesync(Character, *MeshComp, *Move, *World);

	// Movement-mode transitions on the player: the door-proximity bounce presents as rapid
	// Walking<->Falling flicker against unwalkable door collision. Each transition logs the CMC's
	// actual floor resolution — the hit component names the offending surface directly.
	if (Character.IsPlayerControlled())
	{
		static TMap<TWeakObjectPtr<const ACharacter>, TEnumAsByte<EMovementMode>> LastMode;
		TEnumAsByte<EMovementMode>* PrevMode = LastMode.Find(&Character);
		if (!PrevMode || *PrevMode != Move->MovementMode)
		{
			if (PrevMode)
			{
				const FFindFloorResult& Floor = Move->CurrentFloor;
				UE_LOG(LogPawnFloatDiag, Warning,
					TEXT("[FloatDiag] %s MODE %s -> %s | velZ=%.0f Z=%.1f | floor: walkable=%d blocking=%d dist=%.1f comp=%s:%s"),
					*Character.GetName(),
					*UEnum::GetValueAsString(PrevMode->GetValue()),
					*UEnum::GetValueAsString(Move->MovementMode.GetValue()),
					Character.GetVelocity().Z, Character.GetActorLocation().Z,
					Floor.bWalkableFloor ? 1 : 0, Floor.bBlockingHit ? 1 : 0, Floor.FloorDist,
					*GetNameSafe(Floor.HitResult.GetActor()), *GetNameSafe(Floor.HitResult.GetComponent()));
			}
			LastMode.Add(&Character, Move->MovementMode);
		}
	}

	// Heartbeat (debug.PawnFloat 2): periodic gate dump for the player even when no detector
	// fires — guarantees correlated data for any user-observed float window.
	if (CVarPawnFloatDiag.GetValueOnGameThread() >= 2 && Character.IsPlayerControlled())
	{
		static TMap<TWeakObjectPtr<const ACharacter>, float> LastHeartbeat;
		const float Now = World->GetTimeSeconds();
		if (const float* Last = LastHeartbeat.Find(&Character); !Last || Now - *Last >= 2.f)
		{
			LastHeartbeat.Add(&Character, Now);
			LogPoseGates(TEXT("POSE-HEARTBEAT"), 0.f, Character, *MeshComp, *Move);
		}
	}

	// Yaw-follow transitions: three systems save/restore bUseControllerRotationYaw (revive lock,
	// being-revived, traversal) and a cross-clobbered restore leaves it stuck off — the pose-latch
	// float. Logging every flip (with what's playing) names the seam that dropped it.
	static TMap<TWeakObjectPtr<const ACharacter>, bool> LastYawFollow;
	bool* Prev = LastYawFollow.Find(&Character);
	if (!Prev || *Prev != Character.bUseControllerRotationYaw)
	{
		if (Prev)
		{
			const UAnimInstance* YawAnim = MeshComp->GetAnimInstance();
			UE_LOG(LogPawnFloatDiag, Warning, TEXT("[FloatDiag] %s bUseControllerRotationYaw -> %d | mode=%s montage=%s"),
				*Character.GetName(), Character.bUseControllerRotationYaw ? 1 : 0,
				*UEnum::GetValueAsString(Move->MovementMode.GetValue()),
				*GetNameSafe(YawAnim ? YawAnim->GetCurrentActiveMontage() : nullptr));
		}
		LastYawFollow.Add(&Character, Character.bUseControllerRotationYaw);
	}

	const FVector Loc = Character.GetActorLocation();
	const float CapsuleBottomZ = Loc.Z - Capsule->GetScaledCapsuleHalfHeight();

	// True ground under the capsule, independent of what the CMC thinks it stands on. Static-only
	// on purpose: a pawn based on a WorldDynamic object (door leaf) shows a big airGap + the base.
	// Doors don't count as ground either — a pawn perched on a Static door frame must read as
	// airborne (that perch IS the bug), so door hits are skipped and the trace continues below.
	FHitResult Ground;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PawnFloatDiagGround), false, &Character);
	const FVector TraceStart(Loc.X, Loc.Y, CapsuleBottomZ + 5.f);
	// Channel trace, not object-type: UWC prefab pieces (roofs, stairs) are WorldDynamic-typed
	// Movable components, so a static-only trace skips the surface underfoot and fabricates
	// air gaps (false WALKING-ON-AIR one step below on every stair descent).
	bool bGroundHit = false;
	for (int32 Attempt = 0; Attempt < 3; ++Attempt)
	{
		bGroundHit = World->LineTraceSingleByChannel(Ground, TraceStart,
			TraceStart - FVector(0, 0, 500.f), ECC_Visibility, Params);
		if (!bGroundHit || !Ground.GetActor() || !Ground.GetActor()->IsA<ADoorBase>()) break;
		Params.AddIgnoredActor(Ground.GetActor());
	}
	const float AirGap = bGroundHit ? (CapsuleBottomZ - Ground.ImpactPoint.Z) : 500.f;

	// Mesh drift: component-level offset from the authored relative pose, plus the root bone's
	// world height above the capsule bottom (catches animation/root-motion lift the component
	// offset can't see — the FP camera rides a head bone, so bone lift moves the camera too).
	float MeshRelDrift = 0.f;
	if (const ACharacter* CDO = Character.GetClass()->GetDefaultObject<ACharacter>())
		if (const USkeletalMeshComponent* CDOMesh = CDO->GetMesh())
		{
			// Crouch (and prone, which reuses the crouch path) legitimately raises the mesh's
			// relative Z by the capsule shrink — fold that into the expected pose or every
			// crouch reads as drift.
			float ExpectedRelZ = CDOMesh->GetRelativeLocation().Z;
			if (Character.bIsCrouched && CDO->GetCapsuleComponent())
				ExpectedRelZ += CDO->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight() - Move->GetCrouchedHalfHeight();
			MeshRelDrift = MeshComp->GetRelativeLocation().Z - ExpectedRelZ;
		}
	static const FName RootBoneName(TEXT("root"));
	const float RootBoneLift = MeshComp->GetBoneIndex(RootBoneName) != INDEX_NONE
		? MeshComp->GetBoneLocation(RootBoneName).Z - CapsuleBottomZ : 0.f;

	const bool bWalking = Move->MovementMode == MOVE_Walking || Move->MovementMode == MOVE_NavWalking;
	const bool bWalkingOnAir = bWalking && AirGap > FloatDiag_AirGap;
	// JumpCurrentCount == 0 filters engine jumps; an external launch (the anomaly) never sets it.
	const bool bLaunched = Move->MovementMode == MOVE_Falling && Character.GetVelocity().Z > FloatDiag_LaunchVelZ
		&& Character.JumpCurrentCount == 0;
	const bool bMeshDrifted = FMath::Abs(MeshRelDrift) > FloatDiag_MeshDrift || RootBoneLift > FloatDiag_MeshDrift;
	// Traversal-style float: Flying/None with the capsule off the ground (traversal execution
	// runs in MOVE_Flying with SetActorLocation lerps — no velocity, no Falling, invisible to
	// the two triggers above). MOVE_None corpses lie grounded, so the gap gate filters them.
	const bool bModeFloat = (Move->MovementMode == MOVE_Flying || Move->MovementMode == MOVE_None)
		&& AirGap > FloatDiag_AirGap;
	if (!bWalkingOnAir && !bLaunched && !bMeshDrifted && !bModeFloat) return;

	// Throttle per character so a sustained float doesn't spam every frame.
	static TMap<TWeakObjectPtr<const ACharacter>, float> LastLogTimes;
	const float Now = World->GetTimeSeconds();
	if (float* Last = LastLogTimes.Find(&Character); Last && Now - *Last < FloatDiag_LogInterval) return;
	LastLogTimes.Add(&Character, Now);

	const UPrimitiveComponent* Base = Move->GetMovementBase();
	const UAnimInstance* AnimInst = MeshComp->GetAnimInstance();
	const UAnimMontage* Montage = AnimInst ? AnimInst->GetCurrentActiveMontage() : nullptr;
	UE_LOG(LogPawnFloatDiag, Warning,
		TEXT("[FloatDiag] %s %s%s%s%s| mode=%s velZ=%.0f | airGap=%.1f ground=%s:%s | base=%s:%s | meshRelDrift=%.1f rootBoneLift=%.1f | montage=%s | at %s"),
		*Character.GetName(),
		bWalkingOnAir ? TEXT("WALKING-ON-AIR ") : TEXT(""),
		bLaunched ? TEXT("LAUNCHED ") : TEXT(""),
		bMeshDrifted ? TEXT("MESH-DRIFT ") : TEXT(""),
		bModeFloat ? TEXT("MODE-FLOAT ") : TEXT(""),
		*UEnum::GetValueAsString(Move->MovementMode.GetValue()),
		Character.GetVelocity().Z,
		AirGap,
		*GetNameSafe(Ground.GetActor()), *GetNameSafe(Ground.GetComponent()),
		Base ? *GetNameSafe(Base->GetOwner()) : TEXT("none"), *GetNameSafe(Base),
		MeshRelDrift, RootBoneLift,
		*GetNameSafe(Montage),
		*Loc.ToCompactString());
}

void UPawnFloatDiagSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	for (TActorIterator<ACharacter> It(GetWorld()); It; ++It)
		LogFloatAnomalies(**It);
}

bool UPawnFloatDiagSubsystem::IsTickable() const
{
	return Super::IsTickable() && CVarPawnFloatDiag.GetValueOnGameThread() != 0;
}

bool UPawnFloatDiagSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UPawnFloatDiagSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UPawnFloatDiagSubsystem, STATGROUP_Tickables);
}
