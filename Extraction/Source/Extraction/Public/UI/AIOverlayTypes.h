// Shared value types for the AI overlay: the per-enemy snapshot the cards render from, the
// squad event the banner drains, and the overlay log category. Produced by UAIOverlaySubsystem,
// consumed by the overlay widget layer. Single-player — nothing here is ever replicated.

#pragma once

#include "CoreMinimal.h"
#include "AIOverlayTypes.generated.h"

class AEnemyCharacter;

DECLARE_LOG_CATEGORY_EXTERN(LogAIOverlay, Log, All);

/** Who an enemy is currently engaging — drives the card's target chip. */
UENUM()
enum class EOverlayTargetKind : uint8
{
	None,
	Player,
	Companion,
	Other
};

/** Squad-level beat worth calling out on the banner. */
UENUM()
enum class EOverlaySquadEventKind : uint8
{
	None,
	SightingRelayed,
	FocusTarget,
	RoleClaimed,
	BoundingStarted,
	BoundingSwap,
	BoundingStopped,
	Rally,
	MemberDied,
	OfficerDown
};

/**
 * One enemy's overlay state as of the last 10Hz snapshot pass. Pure value type — the card widget
 * reads it and never reaches back into the AI. Enemy is weak: a snapshot can outlive its pawn by
 * up to one pass, and a card over a corpse is the failure this guards against.
 */
USTRUCT()
struct EXTRACTION_API FEnemyOverlaySnapshot
{
	GENERATED_BODY()

	TWeakObjectPtr<class AEnemyCharacter> Enemy;

	/** Head socket location — the card's world attach point. */
	FVector   WorldAnchor      = FVector::ZeroVector;

	/** UEnemyArchetypeData::DisplayName, or the archetype enum's display text when that is empty. */
	FText     ArchetypeLabel;

	/** EEnemyAwarenessState as uint8. */
	uint8     AwarenessState   = 0;

	float     SuspicionFill01  = 0.f;

	/** Per second, EMA-smoothed, signed. Differentiated across snapshots by the subsystem. */
	float     SuspicionRate    = 0.f;

	/** Held label from FAIActionNarrator — already minimum-hold filtered. */
	FText     ActionLabel;

	bool      bInCover = false, bPeeking = false, bBlindFiring = false, bCoverMoving = false;

	/** ECoverHeight as uint8 (0 = Stand, 1 = Crouch). */
	uint8     CoverHeight = 0;

	/** -1 lean left, 0 none, +1 lean right, +2 over-the-top (ECoverLean::Front). */
	int32     LeanDirection = 0;

	/** EMoraleState as uint8. Forced to Confident when bFearless. */
	uint8     MoraleState = 0;

	/** Forced to 1 when bFearless. */
	float     Morale01 = 1.f;

	bool      bFearless = false;
	float     Suppression01 = 0.f;
	bool      bSuppressed = false;
	EOverlayTargetKind TargetKind = EOverlayTargetKind::None;

	/** Debug stand-and-shoot or auto-engage is on — this enemy reads Combat with no visible cause. */
	bool      bForcedEngage = false;

	bool      bIsOfficer = false;
	FVector   CoverSlotLocation = FVector::ZeroVector;
	bool      bHasCoverSlot = false;
};

/**
 * A squad coordination beat, queued for the banner. Broadcast by UEnemySquad through
 * FOnSquadOverlayEvent; the subsystem holds the last few and the banner widget drains them.
 */
USTRUCT()
struct EXTRACTION_API FSquadOverlayEvent
{
	GENERATED_BODY()

	EOverlaySquadEventKind Kind = EOverlaySquadEventKind::None;

	/** Squad that raised the event. */
	FName SquadId;

	/** Short all-caps line, e.g. "OFFICER: FOCUS FIRE". */
	FText Headline;

	/** Optional second line, e.g. who acted on whom. */
	FText Detail;

	/** The member that raised it, when there is one. */
	TWeakObjectPtr<class AEnemyCharacter> Instigator;

	/** World seconds at broadcast — stamped by the subsystem on receipt. */
	float TimeStamp = 0.f;
};

/** Squad → overlay egress. Declared here so UEnemySquad and the overlay share one signature. */
DECLARE_MULTICAST_DELEGATE_OneParam(FOnSquadOverlayEvent, const FSquadOverlayEvent& /*Event*/);
