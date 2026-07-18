// Pure geometry and settings resolver for player weapon sights.

#include "Weapon/PlayerWeaponADSResolver.h"

#include "Components/SceneComponent.h"

namespace
{
	constexpr double RigidScaleTolerance = 0.0001;
	constexpr double KitDirectionTolerance = 0.0001;
	const FName LegacyFrontAimpoint(TEXT("FrontAimpoint"));
	const FName LegacyRearAimpoint(TEXT("RearAimpoint"));
	const FName LegacyOpticAimpoint(TEXT("OpticAimpoint"));

	bool IsFiniteVector(const FVector& Value)
	{
		return FMath::IsFinite(Value.X)
			&& FMath::IsFinite(Value.Y)
			&& FMath::IsFinite(Value.Z);
	}

	bool IsFiniteQuat(const FQuat& Value)
	{
		return FMath::IsFinite(Value.X)
			&& FMath::IsFinite(Value.Y)
			&& FMath::IsFinite(Value.Z)
			&& FMath::IsFinite(Value.W);
	}

	bool IsRigidTransform(const FTransform& Transform)
	{
		return IsFiniteVector(Transform.GetLocation())
			&& IsFiniteVector(Transform.GetScale3D())
			&& IsFiniteQuat(Transform.GetRotation())
			&& Transform.GetRotation().IsNormalized()
			&& Transform.GetScale3D().Equals(
				FVector::OneVector, RigidScaleTolerance);
	}

	FTransform Rigidize(const FTransform& Transform)
	{
		return FTransform(
			Transform.GetRotation().GetNormalized(),
			Transform.GetLocation(),
			FVector::OneVector);
	}

	bool GetLegacySocketInHandSpace(
		const USceneComponent& Component,
		FName SocketName,
		const FTransform& HandSocketWorld,
		FTransform& OutSocketInHandSpace)
	{
		OutSocketInHandSpace = FTransform::Identity;
		if (!IsRigidTransform(HandSocketWorld)
			|| !Component.DoesSocketExist(SocketName))
			return false;

		const FTransform SocketWorld =
			Component.GetSocketTransform(SocketName, RTS_World);
		if (!IsRigidTransform(SocketWorld)) return false;

		const FTransform Relative =
			Rigidize(SocketWorld).GetRelativeTransform(Rigidize(HandSocketWorld));
		if (!IsRigidTransform(Relative)) return false;
		OutSocketInHandSpace = Rigidize(Relative);
		return true;
	}

	bool IsValidADSSettings(const FPlayerWeaponADSDefaults& ADS)
	{
		return FMath::IsFinite(ADS.FieldOfView)
			&& ADS.FieldOfView >= 20.f
			&& ADS.FieldOfView <= 120.f
			&& FMath::IsFinite(ADS.TransitionTime)
			&& ADS.TransitionTime > 0.f
			&& FMath::IsFinite(ADS.SensitivityMultiplier)
			&& ADS.SensitivityMultiplier > 0.f
			&& FMath::IsFinite(ADS.AimDistanceFromCameraCm)
			&& ADS.AimDistanceFromCameraCm >= 0.f
			&& FMath::IsFinite(ADS.EyeReliefCm)
			&& ADS.EyeReliefCm >= 0.f;
	}
}

bool FPlayerWeaponADSResolver::ResolveIrons(
	const FTransform& RearInHandSpace,
	const FTransform& FrontInHandSpace,
	const FPlayerWeaponADSDefaults& BaseADS,
	FPlayerWeaponResolvedSight& OutSight)
{
	OutSight = FPlayerWeaponResolvedSight();
	if (!IsRigidTransform(RearInHandSpace)
		|| !IsRigidTransform(FrontInHandSpace)
		|| !IsValidADSSettings(BaseADS))
		return false;

	FVector Direction =
		FrontInHandSpace.GetLocation() - RearInHandSpace.GetLocation();
	const FVector Up = RearInHandSpace.GetUnitAxis(EAxis::Z);
	const FVector Right = RearInHandSpace.GetUnitAxis(EAxis::Y);
	// The project rejects degenerate/vertical authored pairs even though the
	// vendor graph returns a gimbal-edge result for them.
	if (!Direction.Normalize(KitDirectionTolerance)
		|| FVector::CrossProduct(Direction, Up).IsNearlyZero())
		return false;

	const double PitchRadians = -FMath::Asin(FMath::Clamp(
		FVector::DotProduct(Direction, Up), -1.0, 1.0));
	const double YawRadians = FMath::Asin(FMath::Clamp(
		FVector::DotProduct(Direction, Right), -1.0, 1.0));
	const FQuat PitchCorrection(Right, PitchRadians);
	const FQuat YawCorrection(Up, YawRadians);
	FQuat Rotation = YawCorrection * PitchCorrection
		* RearInHandSpace.GetRotation();
	if (!IsFiniteQuat(Rotation)) return false;
	Rotation.Normalize();

	OutSight.bIsValid = true;
	OutSight.AimSourceInHandSpace = FTransform(
		Rotation, RearInHandSpace.GetLocation(), FVector::OneVector);
	OutSight.ADSSettings = BaseADS;
	return true;
}

bool FPlayerWeaponADSResolver::ResolveOptic(
	FName OpticId,
	const FTransform& AimPointInHandSpace,
	const FPlayerWeaponADSDefaults& BaseADS,
	const FPlayerWeaponOpticOverride& OpticOverride,
	FPlayerWeaponResolvedSight& OutSight)
{
	OutSight = FPlayerWeaponResolvedSight();
	const FPlayerWeaponADSDefaults ResolvedADS =
		MergeADSSettings(BaseADS, OpticOverride);
	if (OpticId.IsNone()
		|| !IsRigidTransform(AimPointInHandSpace)
		|| !IsValidADSSettings(ResolvedADS))
		return false;

	OutSight.bIsValid = true;
	OutSight.bUsesOptic = true;
	OutSight.OpticId = OpticId;
	OutSight.AimSourceInHandSpace = Rigidize(AimPointInHandSpace);
	OutSight.ADSSettings = ResolvedADS;
	return true;
}

bool FPlayerWeaponADSResolver::ResolveAttachmentRootTransform(
	const FTransform& WeaponMountInWeaponRootSpace,
	const FTransform& AttachmentMountInAttachmentRootSpace,
	FTransform& OutAttachmentRootInWeaponRootSpace)
{
	OutAttachmentRootInWeaponRootSpace = FTransform::Identity;
	if (!IsRigidTransform(WeaponMountInWeaponRootSpace)
		|| !IsRigidTransform(AttachmentMountInAttachmentRootSpace))
		return false;

	const FTransform RigidWeaponMount = Rigidize(WeaponMountInWeaponRootSpace);
	const FTransform RigidAttachmentMount =
		Rigidize(AttachmentMountInAttachmentRootSpace);
	const FTransform Placement =
		RigidAttachmentMount.Inverse() * RigidWeaponMount;
	if (!IsRigidTransform(Placement)) return false;

	OutAttachmentRootInWeaponRootSpace = FTransform(
		Placement.GetRotation().GetNormalized(),
		Placement.GetLocation(),
		FVector::OneVector);
	return true;
}

bool FPlayerWeaponADSResolver::ResolveLegacyIronSockets(
	const USceneComponent& RearComponent,
	const USceneComponent& FrontComponent,
	const FTransform& HandSocketWorld,
	const FPlayerWeaponADSDefaults& BaseADS,
	FPlayerWeaponResolvedSight& OutSight)
{
	OutSight = FPlayerWeaponResolvedSight();
	FTransform RearInHandSpace;
	FTransform FrontInHandSpace;
	if (!GetLegacySocketInHandSpace(
		RearComponent, LegacyRearAimpoint, HandSocketWorld, RearInHandSpace)
		|| !GetLegacySocketInHandSpace(
			FrontComponent, LegacyFrontAimpoint, HandSocketWorld, FrontInHandSpace))
		return false;

	return ResolveIrons(
		RearInHandSpace, FrontInHandSpace, BaseADS, OutSight);
}

bool FPlayerWeaponADSResolver::ResolveLegacyOpticSocket(
	FName OpticId,
	const USceneComponent& OpticComponent,
	const FTransform& HandSocketWorld,
	const FPlayerWeaponADSDefaults& BaseADS,
	const FPlayerWeaponOpticOverride& OpticOverride,
	FPlayerWeaponResolvedSight& OutSight)
{
	OutSight = FPlayerWeaponResolvedSight();
	FTransform AimPointInHandSpace;
	if (!GetLegacySocketInHandSpace(
		OpticComponent,
		LegacyOpticAimpoint,
		HandSocketWorld,
		AimPointInHandSpace))
		return false;

	return ResolveOptic(
		OpticId, AimPointInHandSpace, BaseADS, OpticOverride, OutSight);
}

FPlayerWeaponADSDefaults FPlayerWeaponADSResolver::MergeADSSettings(
	const FPlayerWeaponADSDefaults& BaseADS,
	const FPlayerWeaponOpticOverride& OpticOverride)
{
	FPlayerWeaponADSDefaults Result = BaseADS;
	if (OpticOverride.bOverrideFieldOfView)
		Result.FieldOfView = OpticOverride.FieldOfView;
	if (OpticOverride.bOverrideTransitionTime)
		Result.TransitionTime = OpticOverride.TransitionTime;
	if (OpticOverride.bOverrideSensitivity)
		Result.SensitivityMultiplier = OpticOverride.SensitivityMultiplier;
	if (OpticOverride.bOverrideAimDistance)
		Result.AimDistanceFromCameraCm =
			OpticOverride.AimDistanceFromCameraCm;
	if (OpticOverride.bOverrideEyeRelief)
		Result.EyeReliefCm = OpticOverride.EyeReliefCm;
	return Result;
}
