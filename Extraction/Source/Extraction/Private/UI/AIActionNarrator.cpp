// FAIActionNarrator -- three-tier action label resolution with a minimum on-screen hold.

#include "AIActionNarrator.h"

#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BTTaskNode.h"
// GetParentNode() returns UBTCompositeNode*; without the full type it cannot upcast to UBTNode*.
#include "BehaviorTree/BTCompositeNode.h"

#include "AIOverlayTypes.h"
#include "EnemyCharacter.h"
#include "EnemyAIController.h"
#include "EnemyGrenadierComponent.h"
#include "EnemySniperTelegraphComponent.h"
#include "WeaponBase.h"

#include "BTTask_EnemyBoundingAdvance.h"
#include "BTTask_EnemyCombatFire.h"
#include "BTTask_EnemyFaceSuspicion.h"
#include "BTTask_EnemyFallback.h"
#include "BTTask_EnemyFlank.h"
#include "BTTask_EnemyMelee.h"
#include "BTTask_EnemyMoveAndShoot.h"
#include "BTTask_EnemyMoveToCover.h"
#include "BTTask_EnemyPatrol.h"
#include "BTTask_EnemySearchLastKnown.h"
#include "BTTask_EnemySuppressFire.h"
#include "BTTask_HeavySuppress.h"
#include "BTTask_MoveToCoverPoint.h"
#include "BTTask_OfficerCommand.h"
#include "BTTask_RusherAdvance.h"
#include "BTTask_RusherCircleStrafe.h"
#include "BTTask_SniperNest.h"

#define LOCTEXT_NAMESPACE "AIOverlay"

namespace
{
	/** Class-name → label. Keyed by UClass FName rather than UClass* so a Live Coding patch that
	 *  re-creates the class object cannot leave a dangling key, and so nothing here is a
	 *  non-UPROPERTY UObject pointer. Built once on first use. */
	const TMap<FName, FText>& GetTaskLabelMap()
	{
		static const TMap<FName, FText> Map = []()
		{
			TMap<FName, FText> M;
			M.Reserve(16);
			M.Add(UBTTask_EnemyPatrol::StaticClass()->GetFName(),          LOCTEXT("ActPatrol",    "PATROLLING"));
			M.Add(UBTTask_EnemyFaceSuspicion::StaticClass()->GetFName(),   LOCTEXT("ActHeard",     "HEARD SOMETHING"));
			M.Add(UBTTask_EnemySearchLastKnown::StaticClass()->GetFName(), LOCTEXT("ActSearch",    "INVESTIGATING"));
			M.Add(UBTTask_EnemyFallback::StaticClass()->GetFName(),        LOCTEXT("ActFallback",  "FALLING BACK"));
			M.Add(UBTTask_EnemySuppressFire::StaticClass()->GetFName(),    LOCTEXT("ActSuppress",  "SUPPRESSING"));
			M.Add(UBTTask_HeavySuppress::StaticClass()->GetFName(),        LOCTEXT("ActSuppress",  "SUPPRESSING"));
			M.Add(UBTTask_EnemyBoundingAdvance::StaticClass()->GetFName(), LOCTEXT("ActBounding",  "BOUNDING FORWARD"));
			M.Add(UBTTask_EnemyFlank::StaticClass()->GetFName(),           LOCTEXT("ActFlank",     "FLANKING"));
			M.Add(UBTTask_EnemyMoveToCover::StaticClass()->GetFName(),     LOCTEXT("ActTakeCover", "TAKING COVER"));
			M.Add(UBTTask_MoveToCoverPoint::StaticClass()->GetFName(),     LOCTEXT("ActTakeCover", "TAKING COVER"));
			M.Add(UBTTask_EnemyMoveAndShoot::StaticClass()->GetFName(),    LOCTEXT("ActAdvance",   "ADVANCING"));
			M.Add(UBTTask_RusherAdvance::StaticClass()->GetFName(),        LOCTEXT("ActCharge",    "CHARGING"));
			M.Add(UBTTask_RusherCircleStrafe::StaticClass()->GetFName(),   LOCTEXT("ActCircle",    "CIRCLING"));
			M.Add(UBTTask_EnemyMelee::StaticClass()->GetFName(),           LOCTEXT("ActMelee",     "CLOSING TO MELEE"));
			M.Add(UBTTask_SniperNest::StaticClass()->GetFName(),           LOCTEXT("ActPerch",     "FINDING A PERCH"));
			M.Add(UBTTask_OfficerCommand::StaticClass()->GetFName(),       LOCTEXT("ActCommand",   "DIRECTING THE SQUAD"));
			return M;
		}();
		return Map;
	}

	/** Uppercased NodeName for a task class with no mapped label. Cached per class, so the FString
	 *  work and the editor warning each happen exactly once per unmapped class. */
	FText GetFallbackLabel(const UBTTaskNode* Task)
	{
		static TMap<FName, FText> FallbackCache;

		const FName ClassName = Task->GetClass()->GetFName();
		if (const FText* Found = FallbackCache.Find(ClassName)) return *Found;

		FString Upper = Task->NodeName.IsEmpty() ? ClassName.ToString() : Task->NodeName;
		Upper.ToUpperInline();

		const FText Label = FText::AsCultureInvariant(Upper);
		FallbackCache.Add(ClassName, Label);

#if WITH_EDITOR
		UE_LOG(LogAIOverlay, Warning,
			TEXT("No overlay action label mapped for BT task class %s — falling back to '%s'."),
			*ClassName.ToString(), *Upper);
#endif
		return Label;
	}

	/** Walks the active node up to the nearest task, then that task's class chain to a mapped label.
	 *  bOutDeferToFirePhase is set for UBTTask_EnemyCombatFire, whose label comes from tier 3. */
	FText ResolveTaskLabel(const UBehaviorTreeComponent* BTComp, bool& bOutDeferToFirePhase)
	{
		bOutDeferToFirePhase = false;

		const UBTNode* Node = BTComp->GetActiveNode();
		const UBTTaskNode* Task = nullptr;
		while (Node)
		{
			Task = Cast<UBTTaskNode>(Node);
			if (Task) break;
			Node = Node->GetParentNode();
		}
		if (!Task) return FText::GetEmpty();

		static const FName FireTaskClassName = UBTTask_EnemyCombatFire::StaticClass()->GetFName();
		const TMap<FName, FText>& LabelMap = GetTaskLabelMap();

		// Climb the chain so a Blueprint or C++ subclass of a mapped task inherits its label.
		for (const UClass* Class = Task->GetClass();
			Class && Class != UBTTaskNode::StaticClass();
			Class = Class->GetSuperClass())
		{
			const FName ClassName = Class->GetFName();
			if (ClassName == FireTaskClassName)
			{
				bOutDeferToFirePhase = true;
				return FText::GetEmpty();
			}
			if (const FText* Found = LabelMap.Find(ClassName)) return *Found;
		}

		return GetFallbackLabel(Task);
	}
}

// ---------------------------------------------------------------------
// Resolution
// ---------------------------------------------------------------------

FText FAIActionNarrator::ResolveLabel(const AEnemyCharacter* Enemy, bool& bOutResolved) const
{
	bOutResolved = false;
	if (!IsValid(Enemy)) return FText::GetEmpty();

	// --- Tier 1: physical state beats the tree, because it is what the viewer can see ---

	if (const AWeaponBase* Weapon = Enemy->GetCurrentWeapon())
	{
		if (Weapon->IsReloading())
		{
			bOutResolved = true;
			return LOCTEXT("ActReload", "RELOADING");
		}
	}

	if (const UEnemyGrenadierComponent* Grenadier = Enemy->GetGrenadierComponent())
	{
		if (Grenadier->IsTelegraphing())
		{
			bOutResolved = true;
			return LOCTEXT("ActGrenade", "THROWING GRENADE");
		}
	}

	if (const UEnemySniperTelegraphComponent* Sniper = Enemy->GetSniperTelegraphComponent())
	{
		if (Sniper->IsTelegraphing())
		{
			bOutResolved = true;
			return LOCTEXT("ActLiningUp", "LINING UP SHOT");
		}
	}

	// --- Tier 2: active BT task class ---

	bool bDeferToFirePhase = false;
	AEnemyAIController* AIC = Cast<AEnemyAIController>(Enemy->GetController());
	const UBehaviorTreeComponent* BTComp = IsValid(AIC) ? Cast<UBehaviorTreeComponent>(AIC->GetBrainComponent()) : nullptr;

	if (IsValid(BTComp))
	{
		const FText TaskLabel = ResolveTaskLabel(BTComp, bDeferToFirePhase);
		if (!bDeferToFirePhase && !TaskLabel.IsEmpty())
		{
			bOutResolved = true;
			return TaskLabel;
		}
	}

	// --- Tier 3: fire-loop phase published by the combat-fire task ---

	if (!Enemy->HasFreshOverlayFirePhase(FirePhaseStaleSeconds)) return FText::GetEmpty();

	if (Enemy->IsOverlayBlindFiring() && Enemy->GetOverlayFirePhase() == EFireTaskPhase::Fire)
	{
		bOutResolved = true;
		return LOCTEXT("ActBlindFire", "BLIND FIRING");
	}

	bOutResolved = true;
	switch (Enemy->GetOverlayFirePhase())
	{
	case EFireTaskPhase::Acquire:      return LOCTEXT("ActAcquire",  "SIGHTING UP");
	case EFireTaskPhase::Expose:       return LOCTEXT("ActExpose",   "PEEKING OUT");
	case EFireTaskPhase::Fire:         return LOCTEXT("ActFire",     "FIRING");
	case EFireTaskPhase::Recover:      return LOCTEXT("ActRecover",  "DUCKING BACK");
	case EFireTaskPhase::Pause:        return LOCTEXT("ActPause",    "HOLDING COVER");
	case EFireTaskPhase::Pursuing:     return LOCTEXT("ActPursue",   "PUSHING FORWARD");
	case EFireTaskPhase::SeekingCover: return LOCTEXT("ActSeek",     "MOVING TO COVER");
	default: break;
	}

	bOutResolved = false;
	return FText::GetEmpty();
}

// ---------------------------------------------------------------------
// Hold filter
// ---------------------------------------------------------------------

const FText& FAIActionNarrator::Update(const AEnemyCharacter* Enemy, float WorldTime)
{
	bool bResolved = false;
	const FText Raw = ResolveLabel(Enemy, bResolved);

	if (!bResolved)
	{
		// BT restart / abort gap. Keep the last label briefly rather than blanking the card.
		if (WorldTime - LastResolvedTime < NullLabelHoldSeconds) return CurrentLabel;

		if (!CurrentLabel.IsEmpty())
		{
			CurrentLabel = FText::GetEmpty();
			PendingLabel = FText::GetEmpty();
			bHasPending = false;
			LabelSetTime = WorldTime;
		}
		return CurrentLabel;
	}

	LastResolvedTime = WorldTime;

	if (WorldTime - LabelSetTime < MinLabelHoldSeconds)
	{
		// Queue the first distinct label seen during the hold so a short beat still gets its turn.
		if (!bHasPending && !Raw.EqualTo(CurrentLabel))
		{
			PendingLabel = Raw;
			bHasPending = true;
		}
		return CurrentLabel;
	}

	const FText& Next = bHasPending ? PendingLabel : Raw;
	if (!Next.EqualTo(CurrentLabel))
	{
		CurrentLabel = Next;
		LabelSetTime = WorldTime;
	}

	PendingLabel = FText::GetEmpty();
	bHasPending = false;
	return CurrentLabel;
}

#undef LOCTEXT_NAMESPACE
