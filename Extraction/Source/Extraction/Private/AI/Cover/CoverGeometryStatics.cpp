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

namespace CoverCornerMarch
{
	constexpr float StepSize = 25.f;   // lateral march resolution — corner resolved to ±half step
	constexpr float MarchMax = 300.f;  // how far past the baked point a corner may be found
	constexpr float MarchMin = -100.f; // how far back to search when the point is already past the edge
	constexpr float ProbeZ = 60.f;     // chest-ish height: under crouch walls (118), above ground clutter

	/** Contiguous chest-height trace scan from Base along Lateral resolving the wall corner distance.
	 *  Contiguity matters: a parallel wall further along can't fake the corner. Returns false when no
	 *  wall exists anywhere in range. bOutExhausted = the wall extended past MarchMax (the returned
	 *  "corner" is the march limit, not a real edge). */
	static bool MarchToCornerLat(const UWorld* World, const FVector& Base, const FVector& Lateral,
		const FVector& WallDir, float TraceLen, const FCollisionQueryParams& Params,
		float& OutCornerLat, bool& bOutExhausted)
	{
		bOutExhausted = false;

		auto WallAt = [&](float D) -> bool
		{
			const FVector Start = Base + Lateral * D + FVector(0.f, 0.f, ProbeZ);
			FHitResult Hit;
			return World->LineTraceSingleByChannel(Hit, Start, Start + WallDir * TraceLen, ECC_Visibility, Params);
		};

		if (WallAt(0.f))
		{
			float D = 0.f;
			while (D + StepSize <= MarchMax && WallAt(D + StepSize)) D += StepSize;
			bOutExhausted = (D + StepSize > MarchMax);
			OutCornerLat = D + StepSize * 0.5f;
			return true;
		}

		// Baked point already past the corner — walk back until the wall reappears.
		float D = 0.f;
		while (D - StepSize >= MarchMin)
		{
			D -= StepSize;
			if (WallAt(D)) { OutCornerLat = D + StepSize * 0.5f; return true; }
		}
		return false;
	}
}

FVector UCoverGeometryStatics::GetEdgeAlignedHunkerPosition(const UWorld* World, const FCoverData& Data,
	float Standoff, float CapsuleRadius, float CornerGap, const AActor* IgnoreActor)
{
	const FVector Base = GetHunkerPosition(Data, Standoff);
	if (!World) return Base;

	// Only endpoint points (exactly one side flag for the derived stance) have a corner to align to.
	const bool bCrouched = GetCoverHeight(Data) == ECoverHeight::Crouch;
	const bool bLeft  = bCrouched ? Data.bLeftCoverCrouched  : Data.bLeftCoverStanding;
	const bool bRight = bCrouched ? Data.bRightCoverCrouched : Data.bRightCoverStanding;
	if (bLeft == bRight) return Base;

	const FVector WallDir = Data.DirectionToWall.GetSafeNormal2D();
	const FVector Lateral = Data.Rotation.RotateVector(FVector::RightVector).GetSafeNormal2D()
		* (bRight ? 1.f : -1.f);

	FCollisionQueryParams Params(SCENE_QUERY_STAT(CoverEdgeAlign), false);
	if (IgnoreActor) Params.AddIgnoredActor(IgnoreActor);

	float CornerLat;
	bool bExhausted;
	if (!CoverCornerMarch::MarchToCornerLat(World, Base, Lateral, WallDir, Standoff + 60.f, Params, CornerLat, bExhausted))
		return Base;

	const float DesiredLat = FMath::Clamp(CornerLat - (CapsuleRadius + CornerGap),
		CoverCornerMarch::MarchMin, CoverCornerMarch::MarchMax);
	return Base + Lateral * DesiredLat;
}

FVector UCoverGeometryStatics::GetCornerPeekApex(const UWorld* World, const FCoverData& Data,
	ECoverLean Lean, float Standoff, float CapsuleRadius, float ClearanceMargin, const AActor* IgnoreActor)
{
	if (Lean != ECoverLean::Left && Lean != ECoverLean::Right)
		return GetLeanPeekPosition(Data, Lean);
	if (!World)
		return GetLeanPeekPosition(Data, Lean);

	const FVector Base = GetHunkerPosition(Data, Standoff);
	const FVector WallDir = Data.DirectionToWall.GetSafeNormal2D();
	const FVector Lateral = Data.Rotation.RotateVector(FVector::RightVector).GetSafeNormal2D()
		* (Lean == ECoverLean::Right ? 1.f : -1.f);

	FCollisionQueryParams Params(SCENE_QUERY_STAT(CoverCornerApex), false);
	if (IgnoreActor) Params.AddIgnoredActor(IgnoreActor);

	float CornerLat;
	bool bExhausted;
	if (!CoverCornerMarch::MarchToCornerLat(World, Base, Lateral, WallDir, Standoff + 60.f, Params, CornerLat, bExhausted)
		|| bExhausted)
		return GetLeanPeekPosition(Data, Lean);

	// Past the corner by a full capsule + margin, staying on the hunker line so the peek walk
	// parallels the wall instead of grazing it.
	return Base + Lateral * (CornerLat + CapsuleRadius + ClearanceMargin);
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

ECoverLean UCoverGeometryStatics::ResolveLeanSideExplicit(const FCoverData& Data,
	bool bLeftValid, bool bRightValid, bool bFrontValid, const FVector& ThreatLoc)
{
	if (!bLeftValid && !bRightValid && !bFrontValid)
		return ECoverLean::None;

	if (bLeftValid && !bRightValid && !bFrontValid) return ECoverLean::Left;
	if (bRightValid && !bLeftValid && !bFrontValid) return ECoverLean::Right;
	if (bFrontValid && !bLeftValid && !bRightValid) return ECoverLean::Front;

	// Multiple valid: prefer the side whose peek position is angularly closest to the threat.
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
		const FVector FrontDir = Data.DirectionToWall.GetSafeNormal2D();
		const float Dot = FVector::DotProduct(FrontDir, CoverToThreat);
		if (Dot > BestDot) { BestDot = Dot; BestLean = ECoverLean::Front; }
	}

	return BestLean;
}

namespace
{
	/** Eye heights for the peek-gap clearance traces (cover Location sits on the ground plane). */
	constexpr float CoverPeekStandEyeHeight = 150.f;
	constexpr float CoverPeekCrouchEyeHeight = 90.f;
}

ECoverLean UCoverGeometryStatics::TryOppositeEndpointSide(UWorld* World, const FCoverData& Data, bool bCrouched,
	const FVector& ThreatLoc, const AActor* Target, const APawn* Pawn, ECoverLean BakedSide)
{
	if (!World) return ECoverLean::None;
	// Opposite of Left = Right and vice versa; only side peeks have opposites.
	ECoverLean OppSide = ECoverLean::None;
	if (BakedSide == ECoverLean::Left)  OppSide = ECoverLean::Right;
	if (BakedSide == ECoverLean::Right) OppSide = ECoverLean::Left;
	if (OppSide == ECoverLean::None) return ECoverLean::None;

	const float EyeHeight = bCrouched ? CoverPeekCrouchEyeHeight : CoverPeekStandEyeHeight;
	const FVector PeekPos = GetLeanPeekPosition(Data, OppSide);
	const FVector Eye = PeekPos + FVector(0.f, 0.f, EyeHeight);

	FCollisionQueryParams Params(SCENE_QUERY_STAT(CoverEndpointOpposite), false);
	Params.AddIgnoredActor(Pawn);
	Params.AddIgnoredActor(Target);

	// Clearance: corner point → peek eye must be unobstructed (blocks = neighbour wall geometry).
	const FVector Origin = Data.Location + FVector(0.f, 0.f, EyeHeight);
	if (World->LineTraceTestByChannel(Origin, Eye, ECC_Visibility, Params))
		return ECoverLean::None;

	// LOS: eye → threat must be clear.
	if (World->LineTraceTestByChannel(Eye, ThreatLoc, ECC_Visibility, Params))
		return ECoverLean::None;

	return OppSide;
}

ECoverLean UCoverGeometryStatics::ChooseGapPeekSide(UWorld* World, const FCoverData& Data, bool bCrouched,
	const FVector& ThreatLoc, const AActor* Target, const APawn* Pawn)
{
	const bool bLeftBaked  = bCrouched ? Data.bLeftCoverCrouched  : Data.bLeftCoverStanding;
	const bool bRightBaked = bCrouched ? Data.bRightCoverCrouched : Data.bRightCoverStanding;

	// Wall-end point = exactly one baked side flag for the current stance. Verify the opposite,
	// unflagged side by runtime traces; add it to the candidate pool when valid.
	bool bLeftValid  = bLeftBaked;
	bool bRightValid = bRightBaked;

	const bool bExactlyOneSide = (bLeftBaked != bRightBaked); // XOR
	if (bExactlyOneSide && World)
	{
		const ECoverLean BakedSide = bLeftBaked ? ECoverLean::Left : ECoverLean::Right;
		const ECoverLean OppVerified = TryOppositeEndpointSide(
			World, Data, bCrouched, ThreatLoc, Target, Pawn, BakedSide);
		if (OppVerified == ECoverLean::Left)  bLeftValid  = true;
		if (OppVerified == ECoverLean::Right) bRightValid = true;
	}

	// Geometry-first: at a wall-end point the single baked flag IS the nearest open corner — the
	// agent should idle toward it and try it first, regardless of where it believes the threat is.
	// The perceived threat position is stale exactly when this runs (tucked behind cover, no LOS),
	// so a threat-angle preference here degenerates to Front (over-top dominates the dot) and kills
	// corner peeks entirely.
	const ECoverLean GeometricSide = bExactlyOneSide
		? (bLeftBaked ? ECoverLean::Left : ECoverLean::Right) : ECoverLean::None;

	// ResolveLeanSideExplicit uses flag-OR-verified validity for the preference dot.
	const ECoverLean Preferred = ResolveLeanSideExplicit(
		Data, bLeftValid, bRightValid, bCrouched && Data.bFrontCoverCrouched, ThreatLoc);

	// Candidate order: geometric corner first, then the threat-preferred side, then the remainder.
	TArray<ECoverLean, TInlineAllocator<3>> Sides;
	if (GeometricSide != ECoverLean::None) Sides.Add(GeometricSide);
	if ((Preferred == ECoverLean::Left || Preferred == ECoverLean::Right) && !Sides.Contains(Preferred))
		Sides.Add(Preferred);
	if (bLeftValid  && !Sides.Contains(ECoverLean::Left))  Sides.Add(ECoverLean::Left);
	if (bRightValid && !Sides.Contains(ECoverLean::Right)) Sides.Add(ECoverLean::Right);

	if (World && !Sides.IsEmpty())
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(CoverPeekGap), false);
		Params.AddIgnoredActor(Pawn);
		Params.AddIgnoredActor(Target);
		const float EyeHeight = bCrouched ? CoverPeekCrouchEyeHeight : CoverPeekStandEyeHeight;
		for (const ECoverLean Side : Sides)
		{
			const FVector Eye = GetLeanPeekPosition(Data, Side) + FVector(0.f, 0.f, EyeHeight);
			if (!World->LineTraceTestByChannel(Eye, ThreatLoc, ECC_Visibility, Params))
				return Side;
		}
	}

	// No side verified against the (possibly stale) threat point: the real corner still beats a
	// threat-dot guess — falling to Front here is what caused permanent over-top stand peeks.
	return GeometricSide != ECoverLean::None ? GeometricSide : Preferred;
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
