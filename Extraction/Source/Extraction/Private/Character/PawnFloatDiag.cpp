#include "Character/PawnFloatDiag.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
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

static void LogFloatAnomalies(ACharacter& Character)
{
	UWorld* World = Character.GetWorld();
	const UCharacterMovementComponent* Move = Character.GetCharacterMovement();
	const UCapsuleComponent* Capsule = Character.GetCapsuleComponent();
	const USkeletalMeshComponent* MeshComp = Character.GetMesh();
	if (!World || !Move || !Capsule || !MeshComp) return;

	const FVector Loc = Character.GetActorLocation();
	const float CapsuleBottomZ = Loc.Z - Capsule->GetScaledCapsuleHalfHeight();

	// True ground under the capsule, independent of what the CMC thinks it stands on. Static-only
	// on purpose: a pawn based on a WorldDynamic object (door leaf) shows a big airGap + the base.
	// Doors don't count as ground either — a pawn perched on a Static door frame must read as
	// airborne (that perch IS the bug), so door hits are skipped and the trace continues below.
	FHitResult Ground;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PawnFloatDiagGround), false, &Character);
	const FVector TraceStart(Loc.X, Loc.Y, CapsuleBottomZ + 5.f);
	bool bGroundHit = false;
	for (int32 Attempt = 0; Attempt < 3; ++Attempt)
	{
		bGroundHit = World->LineTraceSingleByObjectType(Ground, TraceStart,
			TraceStart - FVector(0, 0, 500.f),
			FCollisionObjectQueryParams(FCollisionObjectQueryParams::InitType::AllStaticObjects), Params);
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
