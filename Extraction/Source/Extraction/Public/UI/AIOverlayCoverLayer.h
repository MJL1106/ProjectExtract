// UAIOverlayCoverLayer -- level-2-only cover geometry: diamonds on claimed cover points, tethers
// back to the claiming enemy, a chevron for peek side, unclaimed points as dim 1px dots. Owns ALL
// the geometry so UAIOverlayCardWidget can stay text-only -- that split is what stops the two layers
// fighting over the same visual space.
//
// No sub-widget class per point: the contract for this feature is four widget classes, this one
// included, so cover points are raw UImage children constructed at runtime via this widget's own
// WidgetTree->ConstructWidget and pooled (grown on demand, hidden via Collapsed rather than
// destroyed, never created inside NativeTick). Each pooled image is fully C++-owned, so -- unlike the
// leader line on UAIOverlayCardWidget -- its authored size is known (it does not exist until this
// widget creates it), so SetDesiredSizeOverride can safely stretch a tether to an exact on-screen
// length.
//
// Pools are split PER GLYPH (diamonds / chevrons / dots / tethers), not shared. SetBrush and
// SetDesiredSizeOverride both invalidate widget LAYOUT; a shared pool has a genuinely different brush
// at a given index frame to frame (whichever enemy/slot lands there this pass), which defeats any
// attempt to skip the setter when unchanged. A per-glyph pool draws from at most two brushes
// (diamond: crouch/stand; chevron: side/over-top) and a CONSTANT size, so FPooledImageVisual's cached
// last-applied brush/size/colour (below) is a hit on nearly every frame after the first, leaving only
// SetRenderTranslation/SetRenderTransformAngle -- render-only invalidation -- as the steady per-frame
// cost. This is the fix for the review finding that a 3400-unit top-down camera with
// UnclaimedSweepRadius=4000 put the arena's whole slot set through two full layout invalidations
// every frame on an ever-growing pool.
//
// Claimed points come from FEnemyOverlaySnapshot (bHasCoverSlot / CoverSlotLocation / CoverHeight /
// LeanDirection / Enemy) -- already available per enemy, no separate query needed, and inherently
// bounded by the (small) live enemy count. Unclaimed points come from
// UCoverRegistrySubsystem::GetSlotsInRadius around the camera, distance-sorted and capped to
// MaxUnclaimedDots on the sweep timer (cover geometry does not move frame to frame; GetSlotsInRadius
// itself is uncapped and can return an entire arena's registered slots). Claimed points and all
// screen projection run every frame in NativeTick, matching UAIOverlayLayer's own tick/timer split.
//
// No labels, no numbers, ever -- and deliberately NOT orange: the steady-state accent budget
// (suspicion fill, Combat chevron, officer diamond) is spent entirely by the card layer. This layer's
// geometry is white ink at reduced opacity only.
//
// WBP contract:
//   UCanvasPanel "GeometryCanvas" -- full-screen canvas the pooled point/tether images are parented
//                                    into at runtime. Must be left EMPTY -- every child here is
//                                    C++-owned and any designer-authored content parented here is
//                                    destroyed on the first pool grow.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Styling/SlateBrush.h"
#include "AIOverlayTypes.h"
#include "AIOverlayCoverLayer.generated.h"

class AAICoverSlot;
class AEnemyCharacter;
class UAIOverlayLayer;
class UCanvasPanel;
class UImage;

UCLASS(Abstract, Blueprintable)
class EXTRACTION_API UAIOverlayCoverLayer : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Called once by the owning UAIOverlayLayer right after CreateWidget. Lets this layer share the
	 *  owner's once-per-frame view context instead of resolving its own (see AIOverlayLayer.h). */
	void InitializeForOwner(UAIOverlayLayer* InOwningLayer);

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// --- Bound widget (designer wires in WBP) ---

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCanvasPanel> GeometryCanvas;

	// --- Look (deliberately no accent colour -- see class comment) ---

	UPROPERTY(EditDefaultsOnly, Category = "AIOverlay|Cover|Look")
	FLinearColor GeometryColor = FLinearColor(1.0f, 1.0f, 1.0f, 1.0f);

	UPROPERTY(EditDefaultsOnly, Category = "AIOverlay|Cover|Look", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ClaimedOpacity = 0.9f;

	UPROPERTY(EditDefaultsOnly, Category = "AIOverlay|Cover|Look", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float UnclaimedOpacity = 0.25f;

	UPROPERTY(EditDefaultsOnly, Category = "AIOverlay|Cover|Look", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float TetherOpacity = 0.4f;

	/** Assigned brushes -- filled diamond for crouched claimed cover. */
	UPROPERTY(EditDefaultsOnly, Category = "AIOverlay|Cover|Brushes")
	FSlateBrush ClaimedDiamondCrouchBrush;

	/** Hollow diamond for standing claimed cover. */
	UPROPERTY(EditDefaultsOnly, Category = "AIOverlay|Cover|Brushes")
	FSlateBrush ClaimedDiamondStandBrush;

	/** 1px dot for an unclaimed registered cover slot. */
	UPROPERTY(EditDefaultsOnly, Category = "AIOverlay|Cover|Brushes")
	FSlateBrush UnclaimedDotBrush;

	/** Chevron pointing left/right (rotated 180 deg for right) -- peek side -1/+1. Authored pointing
	 *  RIGHT at zero rotation, same convention as UObjectiveMarkerWidget's own chevron. */
	UPROPERTY(EditDefaultsOnly, Category = "AIOverlay|Cover|Brushes")
	FSlateBrush PeekSideChevronBrush;

	/** Distinct glyph for the +2 "over-the-top" peek state -- not a rotation of the side chevron. */
	UPROPERTY(EditDefaultsOnly, Category = "AIOverlay|Cover|Brushes")
	FSlateBrush PeekOverTopChevronBrush;

	/** 1px line, stretched via SetDesiredSizeOverride to the exact tether length each frame. */
	UPROPERTY(EditDefaultsOnly, Category = "AIOverlay|Cover|Brushes")
	FSlateBrush TetherLineBrush;

	// --- Tuning ---

	UPROPERTY(EditDefaultsOnly, Category = "AIOverlay|Cover|Tuning", meta = (ClampMin = "1.0"))
	float DiamondSize = 10.f;

	UPROPERTY(EditDefaultsOnly, Category = "AIOverlay|Cover|Tuning", meta = (ClampMin = "1.0"))
	float DotSize = 4.f;

	UPROPERTY(EditDefaultsOnly, Category = "AIOverlay|Cover|Tuning", meta = (ClampMin = "1.0"))
	float ChevronSize = 12.f;

	/** Screen-space offset (slate units) between a claimed diamond and its peek chevron -- the two
	 *  must not share a screen position, or a 12px chevron lands centred on and occludes the 10px
	 *  crouch/stand diamond it is meant to sit beside. Left/right lean offsets on X by this amount in
	 *  the corresponding direction; over-the-top offsets upward (-Y) by it. */
	UPROPERTY(EditDefaultsOnly, Category = "AIOverlay|Cover|Tuning", meta = (ClampMin = "0.0"))
	float ChevronOffsetFromDiamond = 14.f;

	UPROPERTY(EditDefaultsOnly, Category = "AIOverlay|Cover|Tuning", meta = (ClampMin = "0.5"))
	float TetherThickness = 1.f;

	/** How often (seconds) to re-sweep UCoverRegistrySubsystem for unclaimed slots near the camera.
	 *  Cover geometry is static -- this is a cheap, infrequent poll, not a per-frame cost. */
	UPROPERTY(EditDefaultsOnly, Category = "AIOverlay|Cover|Tuning", meta = (ClampMin = "0.05"))
	float UnclaimedSweepInterval = 0.5f;

	UPROPERTY(EditDefaultsOnly, Category = "AIOverlay|Cover|Tuning", meta = (ClampMin = "1.0"))
	float UnclaimedSweepRadius = 4000.f;

	/** Hard cap on unclaimed dots drawn, closest-to-camera first. GetSlotsInRadius is UNCAPPED --
	 *  without this, a wide sweep radius over a large arena from a high top-down camera can put every
	 *  registered slot on screen at once, each costing a fresh projection and (pre-cache) two layout
	 *  invalidations every frame. */
	UPROPERTY(EditDefaultsOnly, Category = "AIOverlay|Cover|Tuning", meta = (ClampMin = "1"))
	int32 MaxUnclaimedDots = 48;

private:
	/** One pooled image plus the last values actually applied to it. SetBrush / SetDesiredSizeOverride
	 *  / SetColorAndOpacity are skipped whenever the requested value already matches -- see the class
	 *  comment on why per-glyph pools make this a near-constant hit. Translation/rotation are NOT
	 *  cached -- those are render-only invalidation and are expected to change every frame. */
	struct FPooledImageVisual
	{
		TObjectPtr<UImage> Image = nullptr;

		/** Raw pointer into one of this widget's own EditDefaultsOnly FSlateBrush members -- stable
		 *  for the widget's lifetime, so pointer identity is a valid (and free) "same brush" test. */
		const FSlateBrush* LastBrush = nullptr;
		FVector2D LastSize = FVector2D::ZeroVector;
		FLinearColor LastColor = FLinearColor(ForceInitToZero);
		bool bInitialized = false;
	};

	FPooledImageVisual* AcquireDiamondImage(int32& InOutCursor);
	FPooledImageVisual* AcquireChevronImage(int32& InOutCursor);
	FPooledImageVisual* AcquireDotImage(int32& InOutCursor);
	FPooledImageVisual* AcquireTetherImage(int32& InOutCursor);

	/** Alignment doubles as the render-transform pivot: (0.5, 0.5) for point glyphs (diamond/chevron/
	 *  dot) so they sit CENTRED on their translation and, for the chevron specifically, ROTATE in
	 *  place rather than swinging around an off-centre point; (0, 0.5) for tethers, which must grow
	 *  FROM their origin point rather than straddle it. */
	FPooledImageVisual* AcquireFrom(TArray<FPooledImageVisual>& Pool, int32& InOutCursor, const FVector2D& Alignment);
	void ReleaseUnusedFrom(int32 DiamondCursor, int32 ChevronCursor, int32 DotCursor, int32 TetherCursor);

	void SweepUnclaimedSlots();

	UFUNCTION()
	void HandleSweepTimer();

	/** Applies Brush/Size/Color to Visual only where they differ from the cached last-applied values;
	 *  always applies ScreenPos/AngleDegrees (render-only). Size is the full (width, height) passed to
	 *  SetDesiredSizeOverride -- NOT a square edge length, so a tether's (Length, Thickness) rectangle
	 *  caches correctly instead of fighting a second, uncached SetDesiredSizeOverride call after this
	 *  one. Shared by every Place* below. */
	void ApplyPointCommon(FPooledImageVisual& Visual, const FSlateBrush& Brush, const FVector2D& ScreenPos,
		const FVector2D& Size, const FLinearColor& Color, float Opacity, float AngleDegrees);

	void PlaceDiamond(FPooledImageVisual& Visual, const FVector2D& ScreenPos, bool bFilled, float Opacity);
	void PlaceDot(FPooledImageVisual& Visual, const FVector2D& ScreenPos, float Opacity);

	/** DiamondScreenPos is the claimed diamond's own position -- the chevron is offset from it by
	 *  ChevronOffsetFromDiamond, never placed on top of it (see that property's comment). */
	void PlaceChevron(FPooledImageVisual& Visual, const FVector2D& DiamondScreenPos, int32 LeanDirection, float Opacity);

	void PlaceTether(FPooledImageVisual& Visual, const FVector2D& FromScreenPos, const FVector2D& ToScreenPos, float Opacity);

	TWeakObjectPtr<UAIOverlayLayer> OwningLayer;

	TArray<FPooledImageVisual> DiamondPool;
	TArray<FPooledImageVisual> ChevronPool;
	TArray<FPooledImageVisual> DotPool;
	TArray<FPooledImageVisual> TetherPool;

	TArray<TWeakObjectPtr<AAICoverSlot>> CachedUnclaimedSlots;

	FTimerHandle SweepTimerHandle;
};
