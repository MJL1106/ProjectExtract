// UAIOverlayCardWidget -- one enemy's overlay card. Pure display sink: it owns no projection maths
// and runs no NativeTick of its own. UAIOverlayLayer resolves the shared camera/viewport context
// once per frame, projects every snapshot's WorldAnchor, declutters the whole set together (a card
// cannot declutter against siblings it cannot see), and pushes the result down via UpdateProjection.
// Gameplay data (archetype, action, suspicion, cover/morale flags) arrives separately via
// UpdateSnapshot on the layer's slower refresh cadence. This split is deliberate -- see
// AIOverlayLayer.h for the full rationale.
//
// Card anatomy: a 10px diamond pinned exactly on the projected WorldAnchor (AnchorDiamondImage,
// positioned directly via SetRenderTranslation -- an absolute offset, safe to drive from C++ without
// knowing the WBP's authored sizes), a leader line from the card body down to that diamond (NOT
// C++-rendered -- see AnchorOffsetFromCard/LeaderLineLength/LeaderLineAngleDegrees below), and the
// card body itself (300 slate units wide, floating 48 units above the anchor before declutter).
//
// Colour budget (enforced here, not left to the WBP): AccentColor (#FF4D03 signal orange) is applied
// to EXACTLY three things -- the suspicion fill, the third (Combat) awareness chevron, and the
// officer diamond -- per the approved look. The one deliberate exception is the 2s focus-flash stroke
// (PlayFocusFlash), which reuses AccentColor for a squad-event callout; flagged as an explicit,
// time-boxed exception to the "three things" rule, not a fourth steady-state use.
//
// WBP contract (all optional -- bind only what the design uses; a half-built WBP must not hard-fail):
//   UImage     "AnchorDiamondImage"     -- 10px diamond, pinned by SetRenderTranslation
//   UWidget    "CardBody"               -- the floating card (everything except the anchor diamond);
//                                          collapsed whole when this card loses the simultaneous-card
//                                          cap and falls back to a bare anchor
//   UImage     "PlateBackgroundImage"   -- near-black plate, tinted PlateColor
//   UImage     "KeylineImage"           -- solid 1px keyline, tinted KeylineColor
//   UImage     "KeylineDashedImage"     -- dashed variant, shown INSTEAD of KeylineImage while bInCover
//   UTextBlock "ArchetypeText"          -- archetype label (already upper-case from the source data --
//                                          this widget does not re-case it), ~20sp in the WBP
//   UTextBlock "ActionText"             -- action line (already upper-case; already minimum-hold
//                                          filtered by FAIActionNarrator -- do not add a second
//                                          debounce here), ~30sp semibold -- the most prominent
//                                          element on the card
//   UTextBlock "TargetText"             -- "PLAYER" / "COMPANION" only; collapsed otherwise (never an
//                                          object name)
//   UTextBlock "MoraleText"             -- collapsed while bFearless
//   UTextBlock "ForcedTagText"          -- "FORCED"; collapsed unless bForcedEngage
//   UTextBlock "CoverStateText"         -- one of "BLIND FIRING" / "PEEKING" / "MOVING" / "IN COVER";
//                                          collapsed when none apply
//   UProgressBar "SuspicionFillBar"     -- visible only while the fill is actively changing
//   UImage     "AwarenessChevron1Image" -- shown at Suspicious and above, tinted InkColor
//   UImage     "AwarenessChevron2Image" -- shown at Searching and above, tinted InkColor
//   UImage     "AwarenessChevron3Image" -- shown only at Combat, tinted AccentColor ("the Combat
//                                          chevron") -- collapsed (along with 1 and 2) when bIsOfficer
//   UImage     "OfficerDiamondImage"    -- shown instead of the chevron stack when bIsOfficer, tinted
//                                          AccentColor
//   UImage     "FocusFlashChevronImage" -- the 2s orange chevron stroke a banner focus call triggers;
//                                          collapsed outside PlayFocusFlash's window
//
// The leader line is deliberately NOT a bound widget: its authored base size is unknown to C++, so a
// target on-screen LENGTH cannot be reached via SetRenderScale without knowing it. Instead this widget
// exposes AnchorOffsetFromCard / LeaderLineLength / LeaderLineAngleDegrees as BlueprintReadOnly data;
// the WBP wires its own line image's render transform to those via a UMG property binding, exactly the
// way UObjectiveMarkerWidget leaves EdgeAngleDegrees for the WBP to apply to its chevron.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "AIOverlayTypes.h"
#include "AIOverlayCardWidget.generated.h"

class AEnemyCharacter;
class UImage;
class UProgressBar;
class UTextBlock;
class UWidget;

UCLASS(Abstract, Blueprintable)
class EXTRACTION_API UAIOverlayCardWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Called once by the layer right after CreateWidget, before this card is added to the canvas. */
	void InitializeForEnemy(const FEnemyOverlaySnapshot& Snapshot);

	/** Called on the layer's refresh cadence (not every frame) with fresh gameplay data -- archetype,
	 *  action, suspicion, cover/morale/target/forced-engage state. Never touches screen position. */
	void UpdateSnapshot(const FEnemyOverlaySnapshot& Snapshot);

	/** Called every frame (post-declutter) with this card's resolved screen transform. CardPosition
	 *  and AnchorPosition are both DPI-scaled slate units in the owning layer canvas's local space --
	 *  the same space UWidget::SetRenderTranslation works in. bInBareAnchorOnly is true when this
	 *  card lost the simultaneous-card-cap ranking this frame: the body collapses and only the anchor
	 *  diamond renders, pinned to AnchorPosition. OverlayScale is UAIOverlaySubsystem::GetOverlayScale
	 *  (ai.Overlay.Scale) applied as this card's render scale -- a director-facing knob for recording,
	 *  not a gameplay value; the anchor pin drifts fractionally off-pixel at scale != 1 as a result,
	 *  which is an acceptable trade for a live-tunable recording aid. */
	void UpdateProjection(const FVector2D& CardPosition, const FVector2D& AnchorPosition, bool bInBareAnchorOnly, float OverlayScale);

	/** One-shot: strokes FocusFlashChevronImage in AccentColor for DurationSeconds, then hides it
	 *  again. Triggered by UAIOverlayLayer when a drained squad event names this card's enemy as its
	 *  focus target (see AIOverlayBannerWidget.h). Safe to call while a previous flash is still
	 *  running -- restarts the timer rather than stacking. */
	void PlayFocusFlash(float DurationSeconds);

	/** Called by the layer once per frame for every card whose enemy did NOT resolve an on-screen
	 *  projection this tick (off-screen, or momentarily missing between reconciles) -- collapses the
	 *  whole card so it never shows a stale position. UpdateProjection un-hides it again on the next
	 *  frame the enemy is back on screen. Safe to call every frame; only writes Visibility on change. */
	void SetOffScreenHidden(bool bHidden);

	TWeakObjectPtr<AEnemyCharacter> GetEnemy() const { return Enemy; }

	// --- Per-refresh state the WBP reads (all pushed by UpdateSnapshot) ---

	UPROPERTY(BlueprintReadOnly, Category = "AIOverlay|Card")
	uint8 AwarenessState = 0;

	/** 0 (Unaware) .. 3 (Combat) -- one chevron per Suspicious/Searching/Combat step. Collapsed to the
	 *  officer diamond instead when bIsOfficer. */
	UPROPERTY(BlueprintReadOnly, Category = "AIOverlay|Card")
	int32 ChevronCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "AIOverlay|Card")
	bool bIsOfficer = false;

	UPROPERTY(BlueprintReadOnly, Category = "AIOverlay|Card")
	float SuspicionFill01 = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "AIOverlay|Card")
	float SuspicionRate = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "AIOverlay|Card")
	bool bInCover = false;

	UPROPERTY(BlueprintReadOnly, Category = "AIOverlay|Card")
	bool bPeeking = false;

	UPROPERTY(BlueprintReadOnly, Category = "AIOverlay|Card")
	bool bBlindFiring = false;

	UPROPERTY(BlueprintReadOnly, Category = "AIOverlay|Card")
	bool bCoverMoving = false;

	/** Raw uint8 mirror of FEnemyOverlaySnapshot::CoverHeight: 0 = Stand, 1 = Crouch (matches
	 *  ECoverHeight's own ordinals -- cast locally where a named comparison reads better). */
	UPROPERTY(BlueprintReadOnly, Category = "AIOverlay|Card")
	uint8 CoverHeight = 0;

	/** Peek side encoding from FEnemyOverlaySnapshot: -1 left, 0 none, +1 right, +2 over-the-top. NOT
	 *  ECoverLean -- the snapshot's own encoding has a fourth state ECoverLean cannot express. */
	UPROPERTY(BlueprintReadOnly, Category = "AIOverlay|Card")
	int32 LeanDirection = 0;

	/** Raw uint8 mirror of FEnemyOverlaySnapshot::MoraleState (EMoraleState's ordinals). */
	UPROPERTY(BlueprintReadOnly, Category = "AIOverlay|Card")
	uint8 MoraleState = 0;

	UPROPERTY(BlueprintReadOnly, Category = "AIOverlay|Card")
	float Morale01 = 1.f;

	UPROPERTY(BlueprintReadOnly, Category = "AIOverlay|Card")
	bool bFearless = false;

	UPROPERTY(BlueprintReadOnly, Category = "AIOverlay|Card")
	float Suppression01 = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "AIOverlay|Card")
	bool bSuppressed = false;

	UPROPERTY(BlueprintReadOnly, Category = "AIOverlay|Card")
	EOverlayTargetKind TargetKind = EOverlayTargetKind::None;

	UPROPERTY(BlueprintReadOnly, Category = "AIOverlay|Card")
	bool bForcedEngage = false;

	// --- Per-frame state the WBP reads (all pushed by UpdateProjection) ---

	UPROPERTY(BlueprintReadOnly, Category = "AIOverlay|Card|Projection")
	bool bBareAnchorOnly = false;

	/** Anchor's projected position minus this card's own render translation, in slate units -- feed
	 *  straight into the leader line image's render transform in the WBP (see class comment). */
	UPROPERTY(BlueprintReadOnly, Category = "AIOverlay|Card|Projection")
	FVector2D AnchorOffsetFromCard = FVector2D::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "AIOverlay|Card|Projection")
	float LeaderLineLength = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "AIOverlay|Card|Projection")
	float LeaderLineAngleDegrees = 0.f;

	// --- Events the WBP implements ---

	/** Fired once, the first frame this card resolves a projection. Intro pulse hook. */
	UFUNCTION(BlueprintImplementableEvent, Category = "AIOverlay|Card")
	void OnCardAppeared();

	/** Fired on every bBareAnchorOnly transition -- collapse/restore any WBP-side elements this
	 *  widget's own CardBody visibility toggle does not already cover (e.g. a intro/outro animation). */
	UFUNCTION(BlueprintImplementableEvent, Category = "AIOverlay|Card")
	void OnBareAnchorOnlyChanged(bool bInBareAnchorOnly);

	/** Fired alongside PlayFocusFlash's C++-driven chevron stroke, for a WBP flourish (e.g. a brief
	 *  scale pulse on the whole card). Purely additive -- an unimplemented event changes nothing. */
	UFUNCTION(BlueprintImplementableEvent, Category = "AIOverlay|Card")
	void OnFocusFlashTriggered(float DurationSeconds);

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	// --- Bound widgets (designer wires in WBP; all optional -- see class comment) ---

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> AnchorDiamondImage;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UWidget> CardBody;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> PlateBackgroundImage;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> KeylineImage;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> KeylineDashedImage;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ArchetypeText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ActionText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TargetText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> MoraleText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ForcedTagText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> CoverStateText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> SuspicionFillBar;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> AwarenessChevron1Image;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> AwarenessChevron2Image;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> AwarenessChevron3Image;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> OfficerDiamondImage;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> FocusFlashChevronImage;

	// --- Look (EditDefaultsOnly -- see AIOverlayLayer.h for the shared colour-budget rationale) ---

	/** Signal orange #FF4D03. Applied to exactly three steady-state things (suspicion fill, Combat
	 *  chevron, officer diamond) plus the time-boxed focus-flash stroke. */
	UPROPERTY(EditDefaultsOnly, Category = "AIOverlay|Card|Look")
	FLinearColor AccentColor = FLinearColor(1.0f, 0.301961f, 0.011765f, 1.0f);

	/** Near-black plate background. */
	UPROPERTY(EditDefaultsOnly, Category = "AIOverlay|Card|Look")
	FLinearColor PlateColor = FLinearColor(0.02f, 0.02f, 0.02f, 0.92f);

	/** Pure white ink for all text and the two non-Combat chevrons. */
	UPROPERTY(EditDefaultsOnly, Category = "AIOverlay|Card|Look")
	FLinearColor InkColor = FLinearColor(1.0f, 1.0f, 1.0f, 1.0f);

	/** 1px keyline at 40% opacity. */
	UPROPERTY(EditDefaultsOnly, Category = "AIOverlay|Card|Look")
	FLinearColor KeylineColor = FLinearColor(1.0f, 1.0f, 1.0f, 0.4f);

	/** Card body opacity while bInCover -- dims instead of hiding. The anchor diamond and leader line
	 *  data are NOT affected: "behind cover" is information, not a reason for the tracking dot itself
	 *  to fade. */
	UPROPERTY(EditDefaultsOnly, Category = "AIOverlay|Card|Look", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float InCoverDimOpacity = 0.55f;

	/** SuspicionRate magnitude below this counts as "not changing" -- collapses SuspicionFillBar. */
	UPROPERTY(EditDefaultsOnly, Category = "AIOverlay|Card|Tuning", meta = (ClampMin = "0.0"))
	float SuspicionChangeEpsilon = 0.01f;

private:
	void ApplyStaticColors();
	void PushArchetypeAndAction(const FEnemyOverlaySnapshot& Snapshot);
	void PushChevronsAndOfficer();
	void PushCoverAndMorale(const FEnemyOverlaySnapshot& Snapshot);
	void PushTargetAndForced(const FEnemyOverlaySnapshot& Snapshot);
	void PushSuspicionFill();
	void PushCoverDim();

	UFUNCTION()
	void HandleFocusFlashExpired();

	TWeakObjectPtr<AEnemyCharacter> Enemy;

	bool bHasAppeared = false;

	FTimerHandle FocusFlashTimerHandle;
};
