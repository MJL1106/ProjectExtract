// CoverGeometryStatics — stateless FCoverData geometry helpers.

#include "CoverGeometryStatics.h"
#include "Engine/World.h"
#include "CollisionQueryParams.h"

ECoverHeight UCoverGeometryStatics::GetCoverHeight(const FCoverData& Data)
{
	// Stand when a standing side-peek exists AND no front-crouch flag (tall wall: concealed standing,
	// side-peek possible, no fire-over-the-top). Everything else is Crouch.
	const bool bHasStandingSide = Data.bLeftCoverStanding || Data.bRightCoverStanding;
	if (bHasStandingSide && !Data.bFrontCoverCrouched)
		return ECoverHeight::Stand;
	return ECoverHeight::Crouch;
}

FVector UCoverGeometryStatics::GetHunkerPosition(const FCoverData& Data, float Standoff)
{
	return Data.Location - Data.DirectionToWall.GetSafeNormal2D() * Standoff;
}

FVector UCoverGeometryStatics::GetApproachPosition(const FCoverData& Data, const FVector& PawnLoc, float Standoff)
{
	FVector Pos = GetHunkerPosition(Data, Standoff);
	Pos.Z = PawnLoc.Z;
	return Pos;
}

FVector UCoverGeometryStatics::GetLeanPeekPosition(const FCoverData& Data, ECoverLean Lean, float LeanOffset)
{
	if (Lean == ECoverLean::None || Lean == ECoverLean::Front)
		return Data.Location;

	const FVector Lateral = Data.Rotation.RotateVector(FVector::RightVector);
	if (Lean == ECoverLean::Right)
		return Data.Location + Lateral * LeanOffset;

	// Left
	return Data.Location - Lateral * LeanOffset;
}

ECoverLean UCoverGeometryStatics::ResolveLeanSide(const FCoverData& Data, bool bCrouched, const FVector& ThreatLoc)
{
	const bool bLeftValid  = bCrouched ? Data.bLeftCoverCrouched  : Data.bLeftCoverStanding;
	const bool bRightValid = bCrouched ? Data.bRightCoverCrouched : Data.bRightCoverStanding;
	const bool bFrontValid = bCrouched && Data.bFrontCoverCrouched;

	if (!bLeftValid && !bRightValid && !bFrontValid)
		return ECoverLean::None;

	// If only one side is valid, use it directly
	if (bLeftValid && !bRightValid && !bFrontValid) return ECoverLean::Left;
	if (bRightValid && !bLeftValid && !bFrontValid) return ECoverLean::Right;
	if (bFrontValid && !bLeftValid && !bRightValid) return ECoverLean::Front;

	// Multiple valid: prefer the side whose peek position is angularly closest to the threat
	const FVector Lateral = Data.Rotation.RotateVector(FVector::RightVector);
	const FVector CoverToThreat = (ThreatLoc - Data.Location).GetSafeNormal2D();

	ECoverLean BestLean = ECoverLean::None;
	float BestDot = -2.f;

	if (bRightValid)
	{
		const float Dot = FVector::DotProduct(Lateral, CoverToThreat);
		if (Dot > BestDot) { BestDot = Dot; BestLean = ECoverLean::Right; }
	}
	if (bLeftValid)
	{
		const float Dot = FVector::DotProduct(-Lateral, CoverToThreat);
		if (Dot > BestDot) { BestDot = Dot; BestLean = ECoverLean::Left; }
	}
	if (bFrontValid)
	{
		// Front peek direction = fire arc forward (over the wall, toward the covered side)
		const FVector FrontDir = Data.DirectionToWall.GetSafeNormal2D();
		const float Dot = FVector::DotProduct(FrontDir, CoverToThreat);
		if (Dot > BestDot) { BestDot = Dot; BestLean = ECoverLean::Front; }
	}

	return BestLean;
}

bool UCoverGeometryStatics::IsThreatCovered(const UObject* WorldContextObject, const FCoverData& Data,
	const FVector& ThreatLoc, float Standoff, float ChestHeight,
	const AActor* IgnoreThreatActor, const AActor* IgnorePawn)
{
	const UWorld* World = IsValid(WorldContextObject) ? WorldContextObject->GetWorld() : nullptr;
	if (!World) return false;

	const FVector BodyPos = GetHunkerPosition(Data, Standoff) + FVector(0.f, 0.f, ChestHeight);

	FCollisionQueryParams Params(SCENE_QUERY_STAT(CoverBodyProtect), false);
	if (IgnoreThreatActor) Params.AddIgnoredActor(IgnoreThreatActor);
	if (IgnorePawn) Params.AddIgnoredActor(IgnorePawn);

	FHitResult Hit;
	const bool bBlocked = World->LineTraceSingleByChannel(Hit, BodyPos, ThreatLoc, ECC_Visibility, Params);
	return bBlocked && Hit.GetActor() != IgnoreThreatActor;
}

FVector UCoverGeometryStatics::GetFireArcForward(const FCoverData& Data)
{
	// DirectionToWall points from the agent position INTO the wall — i.e. toward the side the
	// cover protects from. Good cover has this pointing AT the threat (the plugin's own
	// ParallelToCover test scores Dot(DirectionToWall, ToContext) high), so the fire/arc
	// forward IS DirectionToWall, not its negation.
	return Data.DirectionToWall.GetSafeNormal2D();
}

bool UCoverGeometryStatics::IsSameWall(const FCoverData& A, const FCoverData& B, float CosThreshold)
{
	const FVector DirA = A.DirectionToWall.GetSafeNormal2D();
	const FVector DirB = B.DirectionToWall.GetSafeNormal2D();
	const float Dot2D = FVector2D::DotProduct(FVector2D(DirA), FVector2D(DirB));
	return Dot2D >= CosThreshold;
}

bool UCoverGeometryStatics::HasThreatFacingSideFlag(const FCoverData& Data, const FVector& ThreatLoc, bool bCrouched)
{
	const FVector Lateral = Data.Rotation.RotateVector(FVector::RightVector);
	const FVector ToThreat2D = (ThreatLoc - Data.Location).GetSafeNormal2D();
	const float LateralDot = FVector::DotProduct(Lateral, ToThreat2D);

	// Positive dot = threat is toward +Right = Right lean side
	if (LateralDot >= 0.f)
		return bCrouched ? Data.bRightCoverCrouched : Data.bRightCoverStanding;

	return bCrouched ? Data.bLeftCoverCrouched : Data.bLeftCoverStanding;
}

bool UCoverGeometryStatics::CanPeekShoot(const UObject* WorldContextObject, const FCoverData& Data,
	bool bCrouched, const FVector& ThreatLoc, float EyeHeight,
	const AActor* IgnoreThreatActor, const AActor* IgnorePawn,
	float LeanOffset)
{
	const UWorld* World = IsValid(WorldContextObject) ? WorldContextObject->GetWorld() : nullptr;
	if (!World) return false;

	const ECoverLean Lean = ResolveLeanSide(Data, bCrouched, ThreatLoc);
	if (Lean == ECoverLean::None) return false;

	// Front = stand-up-and-fire-over-the-wall: the eye is at standing height for the shot, not at
	// the chest height the corner leans use. A chest-height trace reads the cover wall itself as
	// "blocked" and rejects every mid-wall crouch point — with correct bakes that starves the
	// reseek/shuffle paths of cover entirely. 150 = AgentMaxHeightStanding(180) - 30, the same
	// height the AICS bake's own front-cover check traces at.
	constexpr float FrontPeekEyeHeight = 150.f;
	const float EffectiveEyeHeight = (Lean == ECoverLean::Front)
		? FMath::Max(EyeHeight, FrontPeekEyeHeight) : EyeHeight;
	const FVector PeekPos = GetLeanPeekPosition(Data, Lean, LeanOffset) + FVector(0.f, 0.f, EffectiveEyeHeight);

	FCollisionQueryParams Params(SCENE_QUERY_STAT(CoverPeekShoot), false);
	if (IgnoreThreatActor) Params.AddIgnoredActor(IgnoreThreatActor);
	if (IgnorePawn) Params.AddIgnoredActor(IgnorePawn);

	FHitResult Hit;
	const bool bBlocked = World->LineTraceSingleByChannel(Hit, PeekPos, ThreatLoc, ECC_Visibility, Params);
	// Clear LOS = can peek-shoot
	return !bBlocked || Hit.GetActor() == IgnoreThreatActor;
}
