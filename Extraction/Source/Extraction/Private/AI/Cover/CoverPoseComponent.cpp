// CoverPoseComponent — minimal cover-pose state. No tick, no replication.

#include "CoverPoseComponent.h"
#include "GameFramework/Actor.h"

UCoverPoseComponent::UCoverPoseComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	bWantsInitializeComponent = false;
}

void UCoverPoseComponent::SetInCover(bool bNewInCover, ECoverHeight NewHeight)
{
	bInCover = bNewInCover;
	CoverHeight = NewHeight;
}

void UCoverPoseComponent::SetLean(ECoverLean NewLean)
{
	LeanDirection = NewLean;
}

void UCoverPoseComponent::SetBlindFiring(bool bNewBlindFiring)
{
	bBlindFiring = bNewBlindFiring;
}

void UCoverPoseComponent::SetPeeking(bool bNewPeeking)
{
	bPeeking = bNewPeeking;
}

void UCoverPoseComponent::SetCoverMoving(bool bMoving, ECoverLean Direction)
{
	bCoverMoving = bMoving;
	CoverMoveDirection = bMoving ? Direction : ECoverLean::None;
}

void UCoverPoseComponent::ResetCoverPose()
{
	bInCover = false;
	CoverHeight = ECoverHeight::Stand;
	LeanDirection = ECoverLean::None;
	bBlindFiring = false;
	bPeeking = false;
	bCoverMoving = false;
	CoverMoveDirection = ECoverLean::None;
}
