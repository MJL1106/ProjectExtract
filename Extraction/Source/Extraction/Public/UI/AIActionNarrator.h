// FAIActionNarrator -- turns an enemy's live state into one short on-screen action label.

#pragma once

#include "CoreMinimal.h"

class AEnemyCharacter;

/**
 * Three tiers, first hit wins:
 *   1. Physical state (reloading / grenade telegraph / sniper telegraph) — what the viewer can see.
 *   2. Active BT task CLASS → label. Class, not NodeName: a designer renaming a node must not
 *      silently break the label.
 *   3. EFireTaskPhase published by the combat-fire task.
 *
 * One instance per tracked enemy: it holds the minimum-hold state that keeps the label readable.
 * Expose runs 0.2s and Recover 0.15s, so at the 10Hz snapshot rate the raw label strobes and
 * reads as noise — the hold turns a burst into PEEKING OUT → FIRING → DUCKING BACK.
 *
 * Plain struct, not a UObject: it owns no references and is stored by value in the subsystem's
 * registry. Allocates no FString on the resolve path (unlike UBehaviorTreeComponent::DescribeActiveTasks).
 */
struct EXTRACTION_API FAIActionNarrator
{
	/** Resolves, hold-filters, and returns the label for Enemy. Call at the overlay snapshot rate. */
	const FText& Update(const AEnemyCharacter* Enemy, float WorldTime);

	/** The label as last committed by Update. No re-resolve, no side effects. */
	const FText& GetLabel() const { return CurrentLabel; }

private:

	/** Runs the three tiers. bOutResolved is false when no tier produced a label this pass. */
	FText ResolveLabel(const AEnemyCharacter* Enemy, bool& bOutResolved) const;

	FText CurrentLabel;

	/** First label seen during a hold, flushed when the hold expires — so a 0.2s Expose beat
	 *  is played rather than dropped. Only one is kept; a backlog would lag the card. */
	FText PendingLabel;
	bool  bHasPending = false;

	float LabelSetTime = -1e9f;

	/** Last pass a tier produced anything. Drives the null-hold independently of LabelSetTime. */
	float LastResolvedTime = -1e9f;

	/** Minimum seconds a committed label stays on screen. 0.3s is three snapshots — still well
	 *  clear of strobing — and short enough that the depth-1 queue never accumulates lag across a
	 *  repeating peek-fire cycle. A longer hold with a deeper queue was the alternative and is
	 *  worse: each queued label costs a full hold to play, so on a loop the card drifts further
	 *  behind the enemy every cycle instead of settling. */
	static constexpr float MinLabelHoldSeconds = 0.3f;

	/** GetActiveNode() returns null during BT restarts and between aborts — hold rather than
	 *  blank, or the card flickers empty on every branch switch. */
	static constexpr float NullLabelHoldSeconds = 0.5f;

	/** Max age passed to AEnemyCharacter::HasFreshOverlayFirePhase — the staleness test itself
	 *  lives on the character, which knows whether the publishing task is still active. */
	static constexpr float FirePhaseStaleSeconds = 0.35f;
};
