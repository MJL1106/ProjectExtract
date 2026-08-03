// FObjectiveChainWalker / FObjectiveConditionRules — the actor-free core of the objective chain:
// forward traversal with cycle detection, the prefix split a checkpoint fast-forward needs, and the
// group-completion rules. Templated on the node type so automation can exercise the traversal
// against a plain struct instead of spawning a level full of actors.

#pragma once

#include "CoreMinimal.h"

struct EXTRACTION_API FObjectiveChainWalker
{
	/** Walks Start -> GetNext(...) collecting each node in visit order. Stops at null or at the
	 *  first node seen twice. Returns false when the chain loops — OutOrder still holds the acyclic
	 *  prefix, so a caller can name exactly where the loop closes. */
	template <typename TNode, typename TGetNext>
	static bool Walk(TNode* Start, TGetNext&& GetNext, TArray<TNode*>& OutOrder)
	{
		OutOrder.Reset();
		TSet<const TNode*> Visited;
		for (TNode* Node = Start; Node != nullptr; Node = GetNext(Node))
		{
			bool bAlreadyVisited = false;
			Visited.Add(Node, &bAlreadyVisited);
			if (bAlreadyVisited) return false;
			OutOrder.Add(Node);
		}
		return true;
	}

	template <typename TNode, typename TGetNext>
	static bool HasCycle(TNode* Start, TGetNext&& GetNext)
	{
		TArray<TNode*> Order;
		return !Walk(Start, Forward<TGetNext>(GetNext), Order);
	}

	/** Splits the chain at the node whose id is TargetId: OutPrefix receives every node BEFORE it
	 *  (the beats a checkpoint resume must apply), and the matched node is returned. Null when the
	 *  id is not on the chain — including when a loop closes before reaching it. */
	template <typename TNode, typename TGetNext, typename TGetId>
	static TNode* SplitAtId(TNode* Start, TGetNext&& GetNext, TGetId&& GetId, FName TargetId,
		TArray<TNode*>& OutPrefix)
	{
		OutPrefix.Reset();
		if (TargetId.IsNone()) return nullptr;

		TArray<TNode*> Order;
		// A cyclic chain still yields a usable prefix — the resume point is either inside the
		// acyclic head or unreachable, and both are handled by the search below.
		Walk(Start, Forward<TGetNext>(GetNext), Order);

		for (int32 Index = 0; Index < Order.Num(); ++Index)
		{
			if (GetId(Order[Index]) != TargetId) continue;
			OutPrefix.Reserve(Index);
			OutPrefix.Append(Order.GetData(), Index);
			return Order[Index];
		}
		return nullptr;
	}
};

struct EXTRACTION_API FObjectiveConditionRules
{
	/** Container completion, all-vs-any. An empty tracked set never satisfies: a step that ticks
	 *  itself off the instant it activates hides the mis-wire instead of surfacing it. */
	static bool AreContainersSatisfied(int32 TrackedCount, int32 SatisfiedCount, bool bRequiresAll)
	{
		if (TrackedCount <= 0) return false;
		return bRequiresAll ? SatisfiedCount >= TrackedCount : SatisfiedCount > 0;
	}

	/** Enemy-group completion: every tracked slot down (dead or destroyed). Same empty-set rule. */
	static bool AreEnemiesSatisfied(int32 TrackedCount, int32 DownedCount)
	{
		return TrackedCount > 0 && DownedCount >= TrackedCount;
	}
};
