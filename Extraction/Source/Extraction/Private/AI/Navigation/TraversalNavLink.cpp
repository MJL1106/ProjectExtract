// NavLinkProxy subclass — wires the smart-link reached event to the agent's
// UTraversalComponent so authored traversals (vault / climb / mantle / drop-down)
// play the right montage and the pathfinder picks them up automatically.

#include "TraversalNavLink.h"
#include "NavLinkCustomComponent.h"
#include "AI/Navigation/NavigationRelevantData.h"
#include "Movement/TraversalComponent.h"
#include "Companion/CompanionCharacter.h"
#include "AI/CompanionAIController.h"
#include "AI/Cover/AICoverSlot.h"
#include "BehaviorTree/BlackboardComponent.h"

bool ATraversalNavLink::UsesSmartLinkTraversal() const
{
	return TraversalType == ETraversalType::Vault
		|| TraversalType == ETraversalType::Climb
		|| TraversalType == ETraversalType::Mantle;
}

ATraversalNavLink::ATraversalNavLink()
{
	OnSmartLinkReached.AddDynamic(this, &ATraversalNavLink::HandleSmartLinkReached);
	// Smart link is enabled per-type in OnConstruction, not here — TraversalType is
	// not yet set at construction time (UPROPERTY defaults apply after CDO init).
}

void ATraversalNavLink::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	if (!UsesSmartLinkTraversal())
	{
		// DropDown / Jump / SprintJump — leave engine defaults intact:
		// simple PointLink active, smart link dormant. Native fall behaviour is preserved.
		return;
	}

	UNavLinkCustomComponent* SmartLink = GetSmartLinkComp();
	if (!SmartLink || PointLinks.Num() == 0)
		return;

	// Copy the designer-positioned simple-link geometry (relative points) into the smart link.
	// SetLinkData takes RELATIVE coordinates — PointLinks store relative points.
	// GetStartPoint/GetEndPoint in HandleSmartLinkReached returns world space; those reads are left as-is.
	SmartLink->SetLinkData(PointLinks[0].Left, PointLinks[0].Right, PointLinks[0].Direction);

	// Enable the smart link and assert relevancy so the pathfinder routes through it and
	// OnSmartLinkReached fires. Re-asserted here on every construction/reconstruction so placed
	// instances saved with the old default (bSmartLinkIsRelevant = false) are corrected on load.
	SmartLink->SetEnabled(true);
	SmartLink->SetNavigationRelevancy(true);
	bSmartLinkIsRelevant = true;

	// NOTE: PointLinks are NOT cleared here. They are the geometry source for reconstruction and
	// the editor widget handles. Duplicate-connection suppression is handled in GetNavigationData
	// and GetNavigationLinksArray for both baked and runtime nav — verify with `show navigation`
	// that exactly one off-mesh connection appears per up-type link and the reached callback fires.

	// TODO: SmartLink->SetSupportedAgents(...) to the companion's nav agent selector when a
	// shared agent config is available from a data asset or project settings. Engine default
	// (all agents) is acceptable for now since HandleSmartLinkReached guards on ACompanionCharacter.
}

void ATraversalNavLink::GetNavigationData(FNavigationRelevantData& Data) const
{
	// For up-types (Vault/Climb/Mantle) suppress the simple PointLink from baked nav generation.
	// Only the smart link (contributed via SmartLinkComp when navigation-relevant) routes for these.
	// Keeping both would produce two coincident off-mesh connections; the pathfinder may pick the
	// simple one, which has no reached callback — the root cause of the "down works, up doesn't" bug.
	// For down-types, defer to Super to preserve their simple-link / native-fall behaviour.
	if (!UsesSmartLinkTraversal())
		Super::GetNavigationData(Data);
	// For up-types: do nothing — SmartLinkComp's nav data is collected separately by the nav system.
}

bool ATraversalNavLink::GetNavigationLinksArray(TArray<FNavigationLink>& OutLink, TArray<FNavigationSegmentLink>& OutSegments) const
{
	// For up-types, suppress the simple PointLink from the runtime nav octree query.
	// Return false with empty arrays so the nav system sees no simple connection here;
	// the SmartLinkComp provides the only connection (appended by the engine via the component path).
	// For down-types, delegate to Super which appends PointLinks and the smart link modifier if active.
	if (UsesSmartLinkTraversal())
		return false;

	return Super::GetNavigationLinksArray(OutLink, OutSegments);
}

void ATraversalNavLink::HandleSmartLinkReached(AActor* Agent, const FVector& Destination)
{
	if (!IsValid(Agent))
		return;

	// Companion-only for now. Player traversal already runs via the trace-based path on the
	// owning client; nav-link entry from the player would re-broadcast OnTraversalStarted
	// and double-fire CompanionAIController's BB write path.
	// Resume path following for any non-companion AI so they don't stall on the link.
	ACompanionCharacter* Companion = Cast<ACompanionCharacter>(Agent);
	if (!Companion)
	{
		ResumePathFollowing(Agent);
		return;
	}

	UTraversalComponent* Traversal = Companion->GetTraversalComponent();
	if (!IsValid(Traversal))
		return;

	// Suppress nav-link traversal while the companion holds a claimed cover slot,
	// or while a mirror-player-traversal task is active. Triggering both paths
	// simultaneously would fight the BB state and leave path following unresolved.
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

			if (BB->GetValueAsBool(ACompanionAIController::BB_PlayerTraversalActive))
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
		// Traversal is in flight (MOVE_Flying + snap interp). Resume path following only after
		// it ends — binding a one-shot lambda to OnTraversalEnded so we don't fight the snap.
		// The handle is heap-allocated inside a TSharedRef so the lambda can safely self-remove.
		// Weak-capture both the nav link and the agent so destruction during traversal is safe.
		// bFired guards against double-execution (e.g. warp cancels mid-montage, EndTraversal
		// broadcasts, then worst-case end timer also broadcasts). Unbind BEFORE resuming — resume
		// can re-enter link handling, and unbinding first prevents the lambda from firing again.
		TWeakObjectPtr<ATraversalNavLink> WeakSelf(this);
		TWeakObjectPtr<AActor> WeakAgent(Agent);
		TWeakObjectPtr<UTraversalComponent> WeakTrav(Traversal);
		TSharedRef<FDelegateHandle> Handle = MakeShared<FDelegateHandle>();
		TSharedRef<bool> bFired = MakeShared<bool>(false);
		*Handle = Traversal->OnTraversalEnded.AddLambda([WeakSelf, WeakAgent, WeakTrav, Handle, bFired]()
		{
			if (*bFired) return;
			*bFired = true;
			if (WeakTrav.IsValid())
				WeakTrav->OnTraversalEnded.Remove(*Handle);
			if (WeakSelf.IsValid() && WeakAgent.IsValid())
				WeakSelf->ResumePathFollowing(WeakAgent.Get());
		});
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
