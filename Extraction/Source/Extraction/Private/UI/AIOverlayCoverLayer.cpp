// UAIOverlayCoverLayer implementation.

#include "UI/AIOverlayCoverLayer.h"
#include "AI/Cover/AICoverSlot.h"
#include "AI/Cover/CoverRegistrySubsystem.h"
#include "AIOverlaySubsystem.h"
#include "Blueprint/WidgetTree.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"
#include "UI/AIOverlayLayer.h"
#include "UI/ScreenMarkerProjection.h"

void UAIOverlayCoverLayer::InitializeForOwner(UAIOverlayLayer* InOwningLayer)
{
	OwningLayer = InOwningLayer;
}

void UAIOverlayCoverLayer::NativeConstruct()
{
	Super::NativeConstruct();

	// Never collapse this widget itself -- Slate does not tick a collapsed widget (see the card
	// widget's own NativeConstruct comment for the same reasoning).
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	SweepUnclaimedSlots();

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(SweepTimerHandle, this,
			&UAIOverlayCoverLayer::HandleSweepTimer, UnclaimedSweepInterval, true);
	}
}

void UAIOverlayCoverLayer::NativeDestruct()
{
	if (UWorld* World = GetWorld())
		World->GetTimerManager().ClearTimer(SweepTimerHandle);

	Super::NativeDestruct();
}

void UAIOverlayCoverLayer::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (!GeometryCanvas) return;

	UAIOverlaySubsystem* Subsystem = UAIOverlaySubsystem::Get(this);
	// Owner destroys this widget on the level<2 transition; this is a defensive bail only, not the
	// primary teardown path (see UAIOverlayLayer::ReconcileLevel).
	if (!Subsystem || Subsystem->GetOverlayLevel() < 2) return;

	APlayerController* PC = GetOwningPlayer();
	if (IsValid(PC) && IsValid(PC->PlayerCameraManager) && PC->PlayerCameraManager->BlendTimeToGo > 0.f)
		return; // Freeze during camera blends -- same reasoning as UAIOverlayLayer's own tick.

	const FScreenMarkerViewContext* Context = nullptr;
	FScreenMarkerViewContext LocalContext;
	if (UAIOverlayLayer* Owner = OwningLayer.Get())
	{
		const FScreenMarkerViewContext& Shared = Owner->GetFrameViewContext();
		if (Shared.IsValid()) Context = &Shared;
	}
	if (!Context)
	{
		if (!FScreenMarkerViewContext::Build(this, LocalContext)) return;
		Context = &LocalContext;
	}

	const FScreenMarkerClampParams ClampParams;
	int32 DiamondCursor = 0;
	int32 ChevronCursor = 0;
	int32 DotCursor = 0;
	int32 TetherCursor = 0;

	for (const FEnemyOverlaySnapshot& Snapshot : Subsystem->GetSnapshots())
	{
		if (!Snapshot.bHasCoverSlot) continue;

		const FScreenMarkerProjection SlotProjection =
			FScreenMarkerProjection::Project(*Context, Snapshot.CoverSlotLocation, ClampParams);
		// Cull off-screen/behind-camera rather than edge-clamp -- this is world geometry, not a HUD
		// marker that needs to stay reachable at the screen edge.
		if (!SlotProjection.bIsValid || SlotProjection.bIsOffScreen) continue;

		// CoverHeight is a raw uint8 mirror of ECoverHeight's ordinals (0 = Stand, 1 = Crouch).
		const bool bFilled = (static_cast<ECoverHeight>(Snapshot.CoverHeight) == ECoverHeight::Crouch);
		if (FPooledImageVisual* Diamond = AcquireDiamondImage(DiamondCursor))
			PlaceDiamond(*Diamond, SlotProjection.ScreenPosition, bFilled, ClaimedOpacity);

		if (Snapshot.LeanDirection != 0)
		{
			if (FPooledImageVisual* Chevron = AcquireChevronImage(ChevronCursor))
				PlaceChevron(*Chevron, SlotProjection.ScreenPosition, Snapshot.LeanDirection, ClaimedOpacity);
		}

		if (Snapshot.Enemy.IsValid())
		{
			const FScreenMarkerProjection EnemyProjection =
				FScreenMarkerProjection::Project(*Context, Snapshot.WorldAnchor, ClampParams);
			if (EnemyProjection.bIsValid && !EnemyProjection.bIsOffScreen)
			{
				if (FPooledImageVisual* Tether = AcquireTetherImage(TetherCursor))
					PlaceTether(*Tether, SlotProjection.ScreenPosition, EnemyProjection.ScreenPosition, TetherOpacity);
			}
		}
	}

	// CachedUnclaimedSlots is already distance-sorted and capped to MaxUnclaimedDots by
	// SweepUnclaimedSlots -- this loop's cost is bounded regardless of how many slots the level has.
	for (const TWeakObjectPtr<AAICoverSlot>& WeakSlot : CachedUnclaimedSlots)
	{
		// Named CoverSlot, not Slot: a local called Slot shadows UWidget::Slot (C4458, warnings-as-errors).
		AAICoverSlot* CoverSlot = WeakSlot.Get();
		// Claim state is re-checked every frame (not just at sweep time) so a slot that gets claimed
		// mid-sweep-interval loses its dot immediately instead of lagging up to UnclaimedSweepInterval.
		if (!IsValid(CoverSlot) || CoverSlot->IsClaimed()) continue;

		const FScreenMarkerProjection DotProjection =
			FScreenMarkerProjection::Project(*Context, CoverSlot->GetStandPosition(), ClampParams);
		if (!DotProjection.bIsValid || DotProjection.bIsOffScreen) continue;

		if (FPooledImageVisual* Dot = AcquireDotImage(DotCursor))
			PlaceDot(*Dot, DotProjection.ScreenPosition, UnclaimedOpacity);
	}

	ReleaseUnusedFrom(DiamondCursor, ChevronCursor, DotCursor, TetherCursor);
}

void UAIOverlayCoverLayer::HandleSweepTimer()
{
	SweepUnclaimedSlots();
}

void UAIOverlayCoverLayer::SweepUnclaimedSlots()
{
	UWorld* World = GetWorld();
	if (!World) return;

	UCoverRegistrySubsystem* Registry = World->GetSubsystem<UCoverRegistrySubsystem>();
	if (!Registry) return;

	FVector Origin = FVector::ZeroVector;
	if (APlayerController* PC = GetOwningPlayer())
	{
		FVector ViewLocation;
		FRotator ViewRotation;
		PC->GetPlayerViewPoint(ViewLocation, ViewRotation);
		Origin = ViewLocation;
	}

	TArray<AAICoverSlot*> SlotsInRadius;
	Registry->GetSlotsInRadius(Origin, UnclaimedSweepRadius, SlotsInRadius);

	SlotsInRadius.RemoveAll([](const AAICoverSlot* CoverSlot) { return !IsValid(CoverSlot); });

	// Closest-to-camera first, then capped to MaxUnclaimedDots. GetSlotsInRadius itself is uncapped --
	// a wide UnclaimedSweepRadius from a high top-down camera can return an entire arena's registered
	// slots, and drawing every one of them every frame is what made this layer's per-frame cost scale
	// with level size instead of with what is actually on screen. Sorting/truncating here (the 0.5s
	// sweep timer), not in NativeTick, keeps the per-frame render loop bounded regardless.
	SlotsInRadius.Sort([Origin](const AAICoverSlot& A, const AAICoverSlot& B)
	{
		return FVector::DistSquared(A.GetStandPosition(), Origin) < FVector::DistSquared(B.GetStandPosition(), Origin);
	});

	const int32 Count = FMath::Min(SlotsInRadius.Num(), MaxUnclaimedDots);

	CachedUnclaimedSlots.Reset(Count);
	for (int32 Index = 0; Index < Count; ++Index)
		CachedUnclaimedSlots.Add(SlotsInRadius[Index]);
}

UAIOverlayCoverLayer::FPooledImageVisual* UAIOverlayCoverLayer::AcquireDiamondImage(int32& InOutCursor)
{
	return AcquireFrom(DiamondPool, InOutCursor, FVector2D(0.5f, 0.5f));
}

UAIOverlayCoverLayer::FPooledImageVisual* UAIOverlayCoverLayer::AcquireChevronImage(int32& InOutCursor)
{
	return AcquireFrom(ChevronPool, InOutCursor, FVector2D(0.5f, 0.5f));
}

UAIOverlayCoverLayer::FPooledImageVisual* UAIOverlayCoverLayer::AcquireDotImage(int32& InOutCursor)
{
	return AcquireFrom(DotPool, InOutCursor, FVector2D(0.5f, 0.5f));
}

UAIOverlayCoverLayer::FPooledImageVisual* UAIOverlayCoverLayer::AcquireTetherImage(int32& InOutCursor)
{
	return AcquireFrom(TetherPool, InOutCursor, FVector2D(0.f, 0.5f));
}

UAIOverlayCoverLayer::FPooledImageVisual* UAIOverlayCoverLayer::AcquireFrom(TArray<FPooledImageVisual>& Pool, int32& InOutCursor, const FVector2D& Alignment)
{
	if (!GeometryCanvas) return nullptr;

	if (InOutCursor < Pool.Num())
	{
		FPooledImageVisual& Visual = Pool[InOutCursor];
		++InOutCursor;
		return &Visual;
	}

	UImage* NewImage = WidgetTree ? WidgetTree->ConstructWidget<UImage>(UImage::StaticClass()) : nullptr;
	if (!NewImage) return nullptr;

	if (UCanvasPanelSlot* CanvasSlot = GeometryCanvas->AddChildToCanvas(NewImage))
	{
		CanvasSlot->SetAnchors(FAnchors(0.f, 0.f));
		CanvasSlot->SetAutoSize(true);
		CanvasSlot->SetAlignment(Alignment);
		CanvasSlot->SetPosition(FVector2D::ZeroVector);
	}
	// Pivot matches alignment: a point glyph rotates/sits centred in place, a tether rotates around
	// (and grows from) its own origin point.
	NewImage->SetRenderTransformPivot(Alignment);

	FPooledImageVisual NewVisual;
	NewVisual.Image = NewImage;
	Pool.Add(NewVisual);

	++InOutCursor;
	return &Pool.Last();
}

void UAIOverlayCoverLayer::ReleaseUnusedFrom(int32 DiamondCursor, int32 ChevronCursor, int32 DotCursor, int32 TetherCursor)
{
	auto ReleaseTail = [](TArray<FPooledImageVisual>& Pool, int32 TailStart)
	{
		for (int32 Index = TailStart; Index < Pool.Num(); ++Index)
			if (Pool[Index].Image) Pool[Index].Image->SetVisibility(ESlateVisibility::Collapsed);
	};

	ReleaseTail(DiamondPool, DiamondCursor);
	ReleaseTail(ChevronPool, ChevronCursor);
	ReleaseTail(DotPool, DotCursor);
	ReleaseTail(TetherPool, TetherCursor);
}

void UAIOverlayCoverLayer::ApplyPointCommon(FPooledImageVisual& Visual, const FSlateBrush& Brush,
	const FVector2D& ScreenPos, const FVector2D& Size, const FLinearColor& Color, float Opacity, float AngleDegrees)
{
	UImage* Image = Visual.Image;
	if (!Image) return;

	Image->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	const FVector2D& DesiredSize = Size;
	const FLinearColor FinalColor(Color.R, Color.G, Color.B, Color.A * Opacity);

	// Pointer identity, not a deep FSlateBrush compare -- Brush is always one of this widget's own
	// EditDefaultsOnly members, whose address is stable for the widget's lifetime.
	if (!Visual.bInitialized || Visual.LastBrush != &Brush)
	{
		Image->SetBrush(Brush);
		Visual.LastBrush = &Brush;
	}

	if (!Visual.bInitialized || Visual.LastSize != DesiredSize)
	{
		Image->SetDesiredSizeOverride(DesiredSize);
		Visual.LastSize = DesiredSize;
	}

	if (!Visual.bInitialized || Visual.LastColor != FinalColor)
	{
		Image->SetColorAndOpacity(FinalColor);
		Visual.LastColor = FinalColor;
	}

	Visual.bInitialized = true;

	// Render-only invalidation -- safe (and expected) to call every frame.
	Image->SetRenderTransformAngle(AngleDegrees);
	Image->SetRenderTranslation(ScreenPos);
}

void UAIOverlayCoverLayer::PlaceDiamond(FPooledImageVisual& Visual, const FVector2D& ScreenPos, bool bFilled, float Opacity)
{
	ApplyPointCommon(Visual, bFilled ? ClaimedDiamondCrouchBrush : ClaimedDiamondStandBrush,
		ScreenPos, FVector2D(DiamondSize, DiamondSize), GeometryColor, Opacity, 0.f);
}

void UAIOverlayCoverLayer::PlaceDot(FPooledImageVisual& Visual, const FVector2D& ScreenPos, float Opacity)
{
	ApplyPointCommon(Visual, UnclaimedDotBrush, ScreenPos, FVector2D(DotSize, DotSize), GeometryColor, Opacity, 0.f);
}

void UAIOverlayCoverLayer::PlaceChevron(FPooledImageVisual& Visual, const FVector2D& DiamondScreenPos, int32 LeanDirection, float Opacity)
{
	// Offset away from the diamond -- placed AT DiamondScreenPos, a 12px chevron lands centred on and
	// occludes the 10px diamond it is meant to sit beside (see ChevronOffsetFromDiamond's comment).
	FVector2D Offset = FVector2D::ZeroVector;
	if (LeanDirection == 2) Offset = FVector2D(0.f, -ChevronOffsetFromDiamond);      // Over-the-top: above.
	else if (LeanDirection < 0) Offset = FVector2D(-ChevronOffsetFromDiamond, 0.f);  // Lean left.
	else if (LeanDirection > 0) Offset = FVector2D(ChevronOffsetFromDiamond, 0.f);   // Lean right.

	const FVector2D ChevronScreenPos = DiamondScreenPos + Offset;

	if (LeanDirection == 2)
	{
		ApplyPointCommon(Visual, PeekOverTopChevronBrush, ChevronScreenPos, FVector2D(ChevronSize, ChevronSize), GeometryColor, Opacity, 0.f);
		return;
	}

	// Base chevron brush authored pointing RIGHT at zero rotation -- same convention
	// UObjectiveMarkerWidget's own chevron uses. -1 (left) is a 180 deg flip.
	const float Angle = (LeanDirection < 0) ? 180.f : 0.f;
	ApplyPointCommon(Visual, PeekSideChevronBrush, ChevronScreenPos, FVector2D(ChevronSize, ChevronSize), GeometryColor, Opacity, Angle);
}

void UAIOverlayCoverLayer::PlaceTether(FPooledImageVisual& Visual, const FVector2D& FromScreenPos, const FVector2D& ToScreenPos, float Opacity)
{
	const FVector2D Delta = ToScreenPos - FromScreenPos;
	const float Length = static_cast<float>(Delta.Size());
	const float Angle = static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(Delta.Y, Delta.X)));

	// Brush and colour are genuinely constant here (always TetherLineBrush / GeometryColor*TetherOpacity)
	// so ApplyPointCommon's cache still skips those setters most frames; DesiredSize is the ACTUAL
	// (Length, Thickness) rectangle -- not squared -- so it caches correctly too, and changes only
	// when the enemy or the cover point actually moves on screen.
	ApplyPointCommon(Visual, TetherLineBrush, FromScreenPos, FVector2D(FMath::Max(Length, 1.f), TetherThickness),
		GeometryColor, Opacity, Angle);
}
