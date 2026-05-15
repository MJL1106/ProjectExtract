// NavLinkProxy subclass — wires the smart-link reached event to the agent's
// UTraversalComponent so authored traversals (vault / climb / mantle / drop-down)
// play the right montage and the pathfinder picks them up automatically.

#include "TraversalNavLink.h"
#include "NavLinkCustomComponent.h"
#include "Movement/TraversalComponent.h"
#include "Companion/CompanionCharacter.h"
#include "AI/CompanionAIController.h"
#include "AI/Cover/AICoverSlot.h"
#include "BehaviorTree/BlackboardComponent.h"

ATraversalNavLink::ATraversalNavLink()
{
	OnSmartLinkReached.AddDynamic(this, &ATraversalNavLink::HandleSmartLinkReached);
}

void ATraversalNavLink::HandleSmartLinkReached(AActor* Agent, const FVector& Destination)
{
	if (!IsValid(Agent))
		return;

	// Companion-only for now. Player traversal already runs via the trace-based path on the
	// owning client; nav-link entry from the player would re-broadcast OnTraversalStarted
	// and double-fire CompanionAIController's BB write path.
	ACompanionCharacter* Companion = Cast<ACompanionCharacter>(Agent);
	if (!Companion)
		return;

	UTraversalComponent* Traversal = Companion->GetTraversalComponent();
	if (!IsValid(Traversal))
		return;

	// Suppress nav-link traversal while the companion holds a claimed cover slot.
	if (ACompanionAIController* CompAIC = Cast<ACompanionAIController>(Companion->GetController()))
	{
		if (UBlackboardComponent* BB = CompAIC->GetBlackboardComponent())
		{
			AAICoverSlot* ActiveSlot = Cast<AAICoverSlot>(BB->GetValueAsObject(ACompanionAIController::BB_CoverSlot));
			if (IsValid(ActiveSlot) && ActiveSlot->IsClaimedBy(Companion))
			{
				ResumePathFollowing(Agent);
				return;
			}
		}
	}

	UNavLinkCustomComponent* SmartLink = GetSmartLinkComp();
	if (!IsValid(SmartLink))
		return;

	// GetStartPoint / GetEndPoint already return world-space (engine transforms the relative
	// link points by the owning actor's transform).
	const FVector LinkStart = SmartLink->GetStartPoint();
	const FVector LinkEnd = SmartLink->GetEndPoint();

	// Decide which end is "from" vs "to" based on the path follower's reported destination.
	// ANavLinkProxy supports both directions (LeftToRight, RightToLeft, BothWays) — Destination
	// is the endpoint the agent is heading to; the other endpoint is the start.
	const float DistDestToStart = FVector::DistSquared(Destination, LinkStart);
	const float DistDestToEnd = FVector::DistSquared(Destination, LinkEnd);
	const bool bDestIsLinkEnd = DistDestToEnd <= DistDestToStart;
	const FVector FinalStart = bDestIsLinkEnd ? LinkStart : LinkEnd;
	const FVector FinalEnd = bDestIsLinkEnd ? LinkEnd : LinkStart;

	const bool bStarted = Traversal->TryStartTraversalFromNavLink(TraversalType, FinalStart, FinalEnd, PlayRate);

	if (bStarted)
	{
		// Success — traversal is in flight (MOVE_Flying + snap interp). Do NOT resume path
		// following here; that would tell the path follower to move toward the next waypoint
		// while we're still mid-traversal, fighting the snap. The companion controller / the
		// traversal-ended montage delegate will pick path-follow back up via EndTraversal.
		return;
	}

	if (bFallbackTeleportOnFailure)
	{
		// Use TeleportTo (not SetActorLocation) so CMC state stays consistent. Matches the
		// per-obstacle fallback in BTTask_MirrorPlayerTraversal.
		Agent->TeleportTo(FinalEnd, Agent->GetActorRotation(), /*bIsATest=*/false, /*bNoCheck=*/false);
	}

	// Only resume on the fallback path — agent has effectively "crossed" the link via teleport
	// (or, if bFallbackTeleportOnFailure is false, never crossed; resuming lets the path follower
	// recompute / re-route).
	ResumePathFollowing(Agent);
}
