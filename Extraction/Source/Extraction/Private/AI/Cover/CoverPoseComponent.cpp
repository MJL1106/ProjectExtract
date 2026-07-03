// CoverPoseComponent — minimal cover-pose state. No tick, no replication.

#include "CoverPoseComponent.h"
#include "EnemyDebug.h"
#include "GameFramework/Actor.h"

UCoverPoseComponent::UCoverPoseComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	bWantsInitializeComponent = false;
}

void UCoverPoseComponent::SetInCover(bool bNewInCover, ECoverHeight NewHeight)
{
	if (GetCoverAnimLogLevel() > 0 && bInCover != bNewInCover)
		UE_LOG(LogTemp, Log, TEXT("[COVERANIM] %s SetInCover %d height=%s"),
			*GetNameSafe(GetOwner()), bNewInCover ? 1 : 0,
			NewHeight == ECoverHeight::Crouch ? TEXT("Crouch") : TEXT("Stand"));
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
	if (GetCoverAnimLogLevel() > 0 && bPeeking != bNewPeeking)
		UE_LOG(LogTemp, Log, TEXT("[COVERANIM] %s SetPeeking %d lean=%d"),
			*GetNameSafe(GetOwner()), bNewPeeking ? 1 : 0, static_cast<int32>(LeanDirection));
	bPeeking = bNewPeeking;
}

void UCoverPoseComponent::SetCoverMoving(bool bMoving, ECoverLean Direction)
{
	bCoverMoving = bMoving;
	CoverMoveDirection = bMoving ? Direction : ECoverLean::None;
}

void UCoverPoseComponent::ResetCoverPose()
{
	if (GetCoverAnimLogLevel() > 0 && bInCover)
		UE_LOG(LogTemp, Log, TEXT("[COVERANIM] %s ResetCoverPose"), *GetNameSafe(GetOwner()));
	bInCover = false;
	CoverHeight = ECoverHeight::Stand;
	LeanDirection = ECoverLean::None;
	bBlindFiring = false;
	bPeeking = false;
	bCoverMoving = false;
	CoverMoveDirection = ECoverLean::None;
}
