// UAIOverlayLayer -- full-screen HUD layer that owns one UAIOverlayCardWidget per enemy snapshot,
// the single UAIOverlayBannerWidget, and (level 2 only) the UAIOverlayCoverLayer. This is the class
// that fixes what UOverheadWidgetComponent could not: no occlusion trace (a camera straight above the
// action traces through the ceiling and hides everything permanently -- this layer never traces at
// all, it dims on the semantic FEnemyOverlaySnapshot::bInCover flag instead), no fixed-400-unit scale
// curve (screen-space projection via FScreenMarkerProjection scales correctly at any camera distance
// or FOV), and -- the one a per-actor component structurally cannot do -- a view of every card at
// once, which is what makes declutter possible at all.
//
// Positioning is ABSOLUTE player-screen space -- a PC-owned top-level widget, not a HUD module,
// mirroring UObjectiveMarkerLayer exactly (see that class for the same rationale).
//
// ================================ Contract this class consumes ================================
// UAIOverlaySubsystem (a UWorldSubsystem, owned by a parallel implementer -- NOT this file):
//   static UAIOverlaySubsystem* Get(const UObject* WorldContextObject);
//   bool  IsOverlayEnabled() const;                        // ai.Overlay > 0
//   int32 GetOverlayLevel() const;                         // 0 off / 1 cards+banner / 2 +cover
//   float GetOverlayScale() const;                         // ai.Overlay.Scale -- applied to card size
//   const TArray<FEnemyOverlaySnapshot>& GetSnapshots() const;
//   bool  DequeueSquadEvent(FSquadOverlayEvent& OutEvent);  // pops oldest, false when empty
//   const TArray<FSquadOverlayEvent>& PeekSquadEvents() const;
//   int32 NumPendingSquadEvents() const;
// While the overlay is off, GetSnapshots() returns empty and the event queue is kept drained
// upstream -- this layer does not special-case level 0 for data, only for whether it does any
// per-frame work at all (see ReconcileLevel).
//
// FSquadOverlayEvent (AIOverlayTypes.h):
//   EOverlaySquadEventKind Kind;  // None, SightingRelayed, FocusTarget, RoleClaimed, BoundingStarted,
//                                 // BoundingSwap, BoundingStopped, Rally, MemberDied, OfficerDown
//   FName SquadId;
//   FText Headline;               // e.g. "OFFICER DOWN" -- display verbatim, do not recompose
//   FText Detail;                 // e.g. "SQUAD MORALE -75" -- display verbatim
//   TWeakObjectPtr<AEnemyCharacter> Instigator;   // the OFFICER who called it, not a target
//   float TimeStamp;
// Card-flash matching: FocusTarget and Rally resolve Instigator to the calling officer (confirmed by
// the coordinator after an initial draft had this backwards -- FocusTarget's Instigator is the officer
// giving the order, not the player it targets; the player has no card to flash). This layer flashes
// whichever currently-live card's GetEnemy() matches Instigator on those two kinds. OfficerDown is
// deliberately EXCLUDED: UAIOverlaySubsystem::UnregisterEnemy drops the dying officer's snapshot (and
// this layer its card, within RefreshInterval) the moment HandleDeath fires, while the banner only
// drains the event once idle -- seconds later on a queue with two other events ahead of it. FindCard
// would always be null by then, so the card-side flash was dead code; the banner's own bEmphasis +
// longer hold already carries OfficerDown's distinct treatment.
//
// FEnemyOverlaySnapshot (AIOverlayTypes.h) -- verified directly against the landed header, not
// inferred: Enemy (TWeakObjectPtr<AEnemyCharacter>), WorldAnchor (FVector, head socket), ArchetypeLabel
// (FText), AwarenessState (raw uint8, ordinal matches EEnemyAwarenessState: Unaware=0 .. Combat=3),
// SuspicionFill01, SuspicionRate (float, EMA-signed), ActionLabel (FText, already hold-filtered by
// FAIActionNarrator -- do NOT add a second debounce on top, it would double-delay it), bInCover /
// bPeeking / bBlindFiring / bCoverMoving (bool), CoverHeight (raw uint8: 0 = Stand, 1 = Crouch --
// matches ECoverHeight's ordinals but is NOT that enum type), LeanDirection (int32: -1 left, 0 none,
// +1 right, +2 over-the-top -- NOT ECoverLean, which has no fourth state), MoraleState (raw uint8,
// matches EMoraleState's ordinals but is NOT that enum type; forced to Confident when bFearless),
// Morale01 (forced to 1 when bFearless), bFearless, Suppression01, bSuppressed,
// TargetKind (EOverlayTargetKind -- an actual enum, None/Player/Companion/Other), bForcedEngage,
// bIsOfficer, CoverSlotLocation (FVector), bHasCoverSlot (bool).
// =================================================================================================
//
// Two cadences, deliberately split:
//   - RefreshTimerHandle (every RefreshInterval): polls IsOverlayEnabled/GetOverlayLevel, reconciles
//     CardsByEnemy against GetSnapshots() (create/destroy -- the ONLY place cards are created or
//     destroyed, never in NativeTick), pushes fresh gameplay data into surviving cards via
//     UpdateSnapshot, and creates/destroys the banner and cover layer children as the level changes.
//   - NativeTick (every frame, gated on level -- see below): a stale-card hide pass runs FIRST and
//     UNCONDITIONALLY (any card whose enemy has left GetSnapshots() entirely is hidden immediately,
//     even mid camera-blend/cut -- see TickProjection's own comment for why this cannot wait for a
//     blend to finish or for the next RefreshInterval). Only THEN does position work check for a
//     camera blend and bail if one is in progress: it resolves the shared view context once, projects
//     every still-live card's WorldAnchor, declutters the whole touched-on-screen set in one bounded
//     pass, and pushes the result into each card via UpdateProjection. Also drains at most one squad
//     event per frame into the banner once it reports IsIdle().
//
// Level 0: the refresh reconcile above naturally empties the card canvas (GetSnapshots() is already
// empty) and destroys the banner/cover-layer children; RefreshTimerHandle itself is then cleared, so
// no periodic reconcile runs while off. Re-enabling is still detected: NativeTick always makes ONE
// single GetOverlayLevel() call every frame regardless of level (see its own comment) -- the moment
// that flips non-zero, ReconcileLevel re-arms RefreshTimerHandle. That one getter call is the only
// thing that "ticks" at level 0; everything else in NativeTick bails in the same line.
//
// WBP must contain:
//   UCanvasPanel "CardCanvas"  -- full-screen canvas the per-enemy cards are spawned into, left EMPTY
//   UPanelWidget "BannerSlot"  -- OPTIONAL single-child slot (e.g. a centred, top-anchored Overlay or
//                                 SizeBox) the banner widget is added into once, at construction
//   UPanelWidget "CoverLayerSlot" -- OPTIONAL single-child slot the cover layer widget is added into
//                                 only at level 2. Placed BEFORE CardCanvas in the widget tree so cover
//                                 geometry paints behind the cards, not over them.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "AIOverlayTypes.h"
#include "UI/ScreenMarkerProjection.h"
#include "AIOverlayLayer.generated.h"

class AEnemyCharacter;
class UAIOverlayBannerWidget;
class UAIOverlayCardWidget;
class UAIOverlayCoverLayer;
class UAIOverlaySubsystem;
class UCanvasPanel;
class UPanelWidget;

UCLASS(Abstract, Blueprintable)
class EXTRACTION_API UAIOverlayLayer : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Camera + viewport state for the current frame, rebuilt at most once per frame however many
	 *  cards/cover-layer callers ask for it this tick. Check IsValid() before use. Shared with
	 *  UAIOverlayCoverLayer the same way UObjectiveMarkerLayer shares it with its own markers. */
	const FScreenMarkerViewContext& GetFrameViewContext();

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// --- Bound widgets (designer wires in WBP) ---

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCanvasPanel> CardCanvas;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UPanelWidget> BannerSlot;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UPanelWidget> CoverLayerSlot;

	// --- Widget classes (assigned on the WBP subclass) ---

	UPROPERTY(EditDefaultsOnly, Category = "AIOverlay")
	TSubclassOf<UAIOverlayCardWidget> CardWidgetClass;

	UPROPERTY(EditDefaultsOnly, Category = "AIOverlay")
	TSubclassOf<UAIOverlayBannerWidget> BannerWidgetClass;

	UPROPERTY(EditDefaultsOnly, Category = "AIOverlay")
	TSubclassOf<UAIOverlayCoverLayer> CoverLayerWidgetClass;

	// --- Tuning: cadence ---

	UPROPERTY(EditDefaultsOnly, Category = "AIOverlay|Tuning", meta = (ClampMin = "0.02"))
	float RefreshInterval = 0.2f;

	// --- Tuning: cap + declutter ---

	/** Simultaneous full cards. Anything past the cap (ranked by awareness state, then absolute
	 *  suspicion rate, then proximity to screen centre) still gets a bare anchor diamond, never
	 *  nothing -- see UAIOverlayCardWidget::UpdateProjection's bInBareAnchorOnly. */
	UPROPERTY(EditDefaultsOnly, Category = "AIOverlay|Tuning|Declutter", meta = (ClampMin = "1"))
	int32 MaxSimultaneousCards = 8;

	/** Card floats this many slate units above its anchor before declutter pushes it further. */
	UPROPERTY(EditDefaultsOnly, Category = "AIOverlay|Tuning|Declutter", meta = (ClampMin = "0.0"))
	float CardHeightAboveAnchor = 48.f;

	/** Minimum vertical gap (slate units) between two cards' resolved Y positions. Cards closer than
	 *  this after sorting by screen Y get pushed apart in one bounded pass -- not an iterative solver,
	 *  so a genuinely crowded frame (five enemies in a 30m yard, per the approved design) settles into
	 *  a stack rather than converging perfectly; the leader line is what sells the difference. */
	UPROPERTY(EditDefaultsOnly, Category = "AIOverlay|Tuning|Declutter", meta = (ClampMin = "1.0"))
	float MinCardVerticalSpacing = 64.f;

	// --- Tuning: distance attenuation ---

	/** Distance (cm) at which a card renders at full scale; nearer never grows past 1, farther
	 *  shrinks as Reference/Distance. Without this every card renders authored-size regardless of
	 *  range and a room of patrollers fills the screen with same-size slabs. */
	UPROPERTY(EditDefaultsOnly, Category = "AIOverlay|Tuning|Distance", meta = (ClampMin = "50.0"))
	float CardReferenceDistance = 600.f;

	/** Floor of the distance shrink, so a far card stays legible instead of vanishing into a dot. */
	UPROPERTY(EditDefaultsOnly, Category = "AIOverlay|Tuning|Distance", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float CardMinDistanceScale = 0.45f;

	/** Beyond this (cm) the enemy gets no card at all this frame -- same treatment as off-screen.
	 *  Debug reach, not gameplay: far enough that the gym's cinematic cams still see every card,
	 *  near enough that a cross-map slab cannot occlude a first-person fight. */
	UPROPERTY(EditDefaultsOnly, Category = "AIOverlay|Tuning|Distance", meta = (ClampMin = "500.0"))
	float CardMaxDistance = 6000.f;

	// --- Tuning: focus flash ---

	UPROPERTY(EditDefaultsOnly, Category = "AIOverlay|Tuning|Banner", meta = (ClampMin = "0.1"))
	float FocusFlashDurationSeconds = 2.f;

private:
	// --- Refresh cadence (create/destroy; never per-frame) ---

	UFUNCTION()
	void HandleRefreshTimer();

	void Refresh();
	void ReconcileCards(const TArray<FEnemyOverlaySnapshot>& Snapshots);
	void ReconcileLevel(int32 Level);
	void SpawnCard(const FEnemyOverlaySnapshot& Snapshot);

	/** O(1) lookup via CardsByEnemy -- NOT a CardCanvas walk. Constructs a transient TWeakObjectPtr
	 *  for the query: TMap<TWeakObjectPtr<T>, ...> hashes on weak-pointer identity (object index +
	 *  serial number), not the raw address, so a raw-pointer overload would hash wrong and silently
	 *  miss -- the temporary must be the same key type the map stores. */
	UAIOverlayCardWidget* FindCard(AEnemyCharacter* Enemy) const;

	/** Enemy identity -> its card, maintained by ReconcileCards (the only place cards are created or
	 *  destroyed). Backs FindCard and the per-frame stale/live checks in TickProjection so neither
	 *  costs an O(N) Cast<> walk of CardCanvas. */
	TMap<TWeakObjectPtr<AEnemyCharacter>, TObjectPtr<UAIOverlayCardWidget>> CardsByEnemy;

	/** The companion's card — held apart from CardsByEnemy (keyed on enemies) and exempt from
	 *  the enemy declutter path so it can never be displaced by the card set. */
	UPROPERTY(Transient)
	TObjectPtr<UAIOverlayCardWidget> CompanionCard;

	void ReconcileCompanionCard(const FEnemyOverlaySnapshot* Snapshot);

	// --- Per-frame cadence ---

	/** No position smoothing, deliberately -- cards snap to the projected point every frame, same
	 *  rationale as UObjectiveMarkerWidget's own on-screen behaviour: interpolating a tracked point
	 *  during a camera turn lags the world, which reads worse than a snap. */
	void TickProjection();
	bool IsCameraBlending() const;

	struct FCardRank
	{
		TObjectPtr<UAIOverlayCardWidget> Card = nullptr;
		const FEnemyOverlaySnapshot* Snapshot = nullptr;
		FVector2D AnchorScreenPos = FVector2D::ZeroVector;
		FVector2D CardScreenPos = FVector2D::ZeroVector;

		/** Per-card distance attenuation, multiplied into the global overlay scale on push. */
		float DistanceScale = 1.f;

		/** True when this enemy resolved an on-screen projection THIS frame. False entries (enemy
		 *  still alive, just off-screen or behind camera) are hidden, never ranked or decluttered --
		 *  replaces a separately-built TouchedCards set with a field on the entry already being built. */
		bool bTouched = false;
	};

	/** Reused every tick so the declutter pass costs no per-frame allocation past initial growth. */
	TArray<FCardRank> ScratchRanks;

	/** Reused every tick by the stale-card hide pass (see TickProjection) -- avoids rebuilding a fresh
	 *  TSet of live enemies every frame. */
	TSet<TWeakObjectPtr<AEnemyCharacter>> ScratchLiveEnemies;

	void DeclutterAndPush(TArray<FCardRank>& Ranks, float OverlayScale);

	// --- Banner + cover layer lifecycle ---

	void EnsureBanner();
	void DestroyBanner();
	void EnsureCoverLayer();
	void DestroyCoverLayer();
	void DrainSquadEventIfIdle();

	UAIOverlaySubsystem* ResolveSubsystem() const;

	UPROPERTY()
	TObjectPtr<UAIOverlayBannerWidget> BannerInstance;

	UPROPERTY()
	TObjectPtr<UAIOverlayCoverLayer> CoverLayerInstance;

	FTimerHandle RefreshTimerHandle;

	int32 CachedLevel = 0;

	FScreenMarkerViewContext CachedViewContext;

	/** Frame the cached context was built on. MAX = never built. */
	uint64 CachedContextFrame = TNumericLimits<uint64>::Max();
};
