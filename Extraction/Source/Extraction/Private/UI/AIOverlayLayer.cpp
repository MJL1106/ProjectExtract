// UAIOverlayLayer implementation.

#include "UI/AIOverlayLayer.h"
#include "AIOverlaySubsystem.h"
// TWeakObjectPtr<AEnemyCharacter> needs the complete type for its is_convertible static_assert;
// the header only forward-declares it.
#include "EnemyCharacter.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/PanelWidget.h"
#include "Containers/ArrayView.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"
#include "UI/AIOverlayBannerWidget.h"
#include "UI/AIOverlayCardWidget.h"
#include "UI/AIOverlayCoverLayer.h"

const FScreenMarkerViewContext& UAIOverlayLayer::GetFrameViewContext()
{
	// One camera/viewport resolve per frame for the whole card set -- GetViewportScale re-queries the
	// DPI curve on every call in editor builds, so this is not free per card. Matches
	// UObjectiveMarkerLayer::GetFrameViewContext exactly.
	if (CachedContextFrame != GFrameCounter)
	{
		CachedContextFrame = GFrameCounter;
		if (!FScreenMarkerViewContext::Build(this, CachedViewContext))
			CachedViewContext = FScreenMarkerViewContext();
	}

	return CachedViewContext;
}

void UAIOverlayLayer::NativeConstruct()
{
	Super::NativeConstruct();

	SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	// Force a diff on the first NativeTick regardless of what the console var is already set to.
	CachedLevel = 0;
}

void UAIOverlayLayer::NativeDestruct()
{
	if (UWorld* World = GetWorld())
		World->GetTimerManager().ClearTimer(RefreshTimerHandle);

	DestroyBanner();
	DestroyCoverLayer();

	Super::NativeDestruct();
}

void UAIOverlayLayer::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// Single cheap getter, every frame, regardless of level -- this is the ONLY thing that still runs
	// at level 0, and it is what lets the overlay come back on its own once ai.Overlay is raised again
	// without a separate always-on polling timer. Everything expensive below is gated on CachedLevel.
	UAIOverlaySubsystem* Subsystem = ResolveSubsystem();
	const int32 Level = Subsystem ? Subsystem->GetOverlayLevel() : 0;
	if (Level != CachedLevel)
	{
		CachedLevel = Level;
		ReconcileLevel(Level);
	}

	if (CachedLevel <= 0) return; // Nothing ticks, nothing draws below this line.

	TickProjection();
	DrainSquadEventIfIdle();
}

UAIOverlaySubsystem* UAIOverlayLayer::ResolveSubsystem() const
{
	return UAIOverlaySubsystem::Get(this);
}

void UAIOverlayLayer::ReconcileLevel(int32 Level)
{
	UWorld* World = GetWorld();

	if (Level <= 0)
	{
		// Total teardown. GetSnapshots() is already empty at this level upstream, but reconciling
		// explicitly against an empty array (rather than assuming) keeps this path correct even if
		// that upstream guarantee ever changes, and it is what actually empties CardCanvas.
		if (World) World->GetTimerManager().ClearTimer(RefreshTimerHandle);

		ReconcileCards(TArray<FEnemyOverlaySnapshot>());
		DestroyBanner();
		DestroyCoverLayer();
		return;
	}

	// Non-zero: (re-)arm the periodic reconcile and run one pass immediately rather than waiting up
	// to RefreshInterval for the first cards/banner to appear.
	if (World && !World->GetTimerManager().IsTimerActive(RefreshTimerHandle))
	{
		World->GetTimerManager().SetTimer(RefreshTimerHandle, this,
			&UAIOverlayLayer::HandleRefreshTimer, RefreshInterval, true);
	}

	EnsureBanner();
	if (Level >= 2) EnsureCoverLayer();
	else DestroyCoverLayer();

	Refresh();
}

void UAIOverlayLayer::HandleRefreshTimer()
{
	Refresh();
}

void UAIOverlayLayer::Refresh()
{
	UAIOverlaySubsystem* Subsystem = ResolveSubsystem();
	if (!Subsystem) return;

	ReconcileCards(Subsystem->GetSnapshots());
}

void UAIOverlayLayer::ReconcileCards(const TArray<FEnemyOverlaySnapshot>& Snapshots)
{
	if (!CardCanvas || !CardWidgetClass) return;

	TSet<TWeakObjectPtr<AEnemyCharacter>> WantedEnemies;
	WantedEnemies.Reserve(Snapshots.Num());
	for (const FEnemyOverlaySnapshot& Snapshot : Snapshots)
		if (Snapshot.Enemy.IsValid()) WantedEnemies.Add(Snapshot.Enemy);

	// Reconcile via CardsByEnemy, not a CardCanvas walk -- this map is the single source of truth for
	// enemy-to-card identity (also backing FindCard and TickProjection's per-frame checks). A full
	// rebuild would reset every card's focus-flash timer and replay its appear pulse each time ANY
	// enemy's data changed, hence reconcile-by-identity rather than ClearChildren + CreateWidget.
	// No re-entrancy latch (unlike UObjectiveMarkerLayer's ReconcileMarkers): nothing this function
	// calls -- InitializeForEnemy/UpdateSnapshot -- fires a BlueprintImplementableEvent that could
	// call back into this reconcile, so the risk that latch guards against does not exist here.
	for (auto It = CardsByEnemy.CreateIterator(); It; ++It)
	{
		UAIOverlayCardWidget* Card = It.Value();
		if (!IsValid(Card) || !WantedEnemies.Contains(It.Key()))
		{
			if (IsValid(Card)) CardCanvas->RemoveChild(Card);
			It.RemoveCurrent();
		}
	}

	for (const FEnemyOverlaySnapshot& Snapshot : Snapshots)
	{
		AEnemyCharacter* Enemy = Snapshot.Enemy.Get();
		if (!Enemy) continue;

		if (UAIOverlayCardWidget* Existing = FindCard(Enemy))
		{
			Existing->UpdateSnapshot(Snapshot);
			continue;
		}

		SpawnCard(Snapshot);
	}
}

void UAIOverlayLayer::SpawnCard(const FEnemyOverlaySnapshot& Snapshot)
{
	UAIOverlayCardWidget* Card = CreateWidget<UAIOverlayCardWidget>(this, CardWidgetClass);
	if (!IsValid(Card)) return;

	Card->InitializeForEnemy(Snapshot);
	// Starts hidden until the very first on-screen projection arrives this frame or next --
	// UpdateProjection un-hides it; a card whose enemy is off-screen at the moment it is created
	// must not flash into view at the origin for one frame.
	Card->SetOffScreenHidden(true);

	UCanvasPanelSlot* CanvasSlot = CardCanvas->AddChildToCanvas(Card);
	if (!CanvasSlot) return;

	CanvasSlot->SetAnchors(FAnchors(0.f, 0.f));
	CanvasSlot->SetAutoSize(true);
	CanvasSlot->SetAlignment(FVector2D(0.5f, 0.5f));
	CanvasSlot->SetPosition(FVector2D::ZeroVector);

	CardsByEnemy.Add(Snapshot.Enemy, Card);
}

UAIOverlayCardWidget* UAIOverlayLayer::FindCard(AEnemyCharacter* Enemy) const
{
	if (!Enemy) return nullptr;

	// Construct the SAME key type the map stores: TMap<TWeakObjectPtr<T>, ...> hashes on weak-pointer
	// identity (object index + serial number), not the raw address, so looking up with a raw T* would
	// use a different (mismatched) hash and silently miss.
	const TObjectPtr<UAIOverlayCardWidget>* Found = CardsByEnemy.Find(TWeakObjectPtr<AEnemyCharacter>(Enemy));
	return (Found && IsValid(*Found)) ? Found->Get() : nullptr;
}

bool UAIOverlayLayer::IsCameraBlending() const
{
	const APlayerController* PC = GetOwningPlayer();
	if (!IsValid(PC) || !IsValid(PC->PlayerCameraManager)) return false;

	return PC->PlayerCameraManager->BlendTimeToGo > 0.f;
}

void UAIOverlayLayer::TickProjection()
{
	if (!CardCanvas) return;

	UAIOverlaySubsystem* Subsystem = ResolveSubsystem();
	if (!Subsystem) return;

	const TArray<FEnemyOverlaySnapshot>& Snapshots = Subsystem->GetSnapshots();

	// Stale-card hide runs FIRST and UNCONDITIONALLY -- even mid camera-blend/cut. A card's enemy
	// leaving GetSnapshots() entirely (HandleDeath's UnregisterEnemy) means it is gone, corpse and
	// all; that is independent of screen position, so freezing POSITION during a blend below must not
	// also freeze this, or a death mid-cut leaves a card floating over a corpse for the blend's
	// length plus up to RefreshInterval. O(cards), no CardCanvas walk: iterates CardsByEnemy directly.
	ScratchLiveEnemies.Reset();
	ScratchLiveEnemies.Reserve(Snapshots.Num());
	for (const FEnemyOverlaySnapshot& Snapshot : Snapshots)
		if (Snapshot.Enemy.IsValid()) ScratchLiveEnemies.Add(Snapshot.Enemy);

	for (const TPair<TWeakObjectPtr<AEnemyCharacter>, TObjectPtr<UAIOverlayCardWidget>>& Pair : CardsByEnemy)
	{
		if (IsValid(Pair.Value) && !ScratchLiveEnemies.Contains(Pair.Key))
			Pair.Value->SetOffScreenHidden(true);
	}

	// Freeze POSITION during camera blends: skip recomputing entirely, which leaves every still-live
	// card's last-applied SetRenderTranslation/SetRenderScale exactly where it was -- no separate
	// "last position" cache needed, Slate already remembers a widget's own render transform.
	if (IsCameraBlending()) return;

	const FScreenMarkerViewContext& Context = GetFrameViewContext();
	if (!Context.IsValid()) return;

	const float OverlayScale = Subsystem->GetOverlayScale();
	const FScreenMarkerClampParams ClampParams; // defaults -- off-screen cards are culled, not edge-clamped

	ScratchRanks.Reset();

	// Position is read FRESH from the subsystem every frame (not from the RefreshInterval-cadence
	// gameplay data pushed by ReconcileCards) -- an enemy's WorldAnchor moves every frame, and
	// resolving it only every RefreshInterval would visibly lag the card behind the enemy's head, the
	// same reason UObjectiveMarkerWidget re-resolves FObjectiveMarker::ResolveLocation every tick
	// instead of caching it at reconcile time.
	for (const FEnemyOverlaySnapshot& Snapshot : Snapshots)
	{
		AEnemyCharacter* Enemy = Snapshot.Enemy.Get();
		if (!Enemy) continue;

		UAIOverlayCardWidget* Card = FindCard(Enemy);
		if (!Card) continue; // Created on the next periodic reconcile, at most RefreshInterval away.

		const FScreenMarkerProjection Projection = FScreenMarkerProjection::Project(Context, Snapshot.WorldAnchor, ClampParams);

		FCardRank& Rank = ScratchRanks.AddDefaulted_GetRef();
		Rank.Card = Card;
		Rank.Snapshot = &Snapshot;
		Rank.bTouched = Projection.bIsValid && !Projection.bIsOffScreen;
		if (Rank.bTouched)
		{
			Rank.AnchorScreenPos = Projection.ScreenPosition;
			Rank.CardScreenPos = Projection.ScreenPosition - FVector2D(0.f, CardHeightAboveAnchor);
		}
	}

	DeclutterAndPush(ScratchRanks, OverlayScale);
}

void UAIOverlayLayer::DeclutterAndPush(TArray<FCardRank>& Ranks, float OverlayScale)
{
	if (Ranks.Num() == 0) return;

	const FVector2D ScreenCentre = CachedViewContext.ViewportSize * 0.5;

	// Touched (on-screen) entries sort first; within that group, cap ranking is awareness state desc,
	// then |suspicion rate| desc, then proximity to screen centre asc. Untouched (off-screen, but
	// still-live) entries trail in arbitrary order -- they are hidden below, never ranked or
	// decluttered. Strict weak ordering throughout: the AbsRateA/AbsRateB branch compares by !=, not
	// IsNearlyEqual (whose equivalence is not transitive, which introsort's unguarded inner loops
	// assume it is).
	Ranks.Sort([ScreenCentre](const FCardRank& A, const FCardRank& B)
	{
		if (A.bTouched != B.bTouched) return A.bTouched;
		if (!A.bTouched) return false; // Both untouched -- no ordering needed among hidden entries.

		if (A.Snapshot->AwarenessState != B.Snapshot->AwarenessState)
			return A.Snapshot->AwarenessState > B.Snapshot->AwarenessState;

		const float AbsRateA = FMath::Abs(A.Snapshot->SuspicionRate);
		const float AbsRateB = FMath::Abs(B.Snapshot->SuspicionRate);
		if (AbsRateA != AbsRateB)
			return AbsRateA > AbsRateB;

		return FVector2D::DistSquared(A.AnchorScreenPos, ScreenCentre) < FVector2D::DistSquared(B.AnchorScreenPos, ScreenCentre);
	});

	int32 TouchedCount = 0;
	while (TouchedCount < Ranks.Num() && Ranks[TouchedCount].bTouched) ++TouchedCount;

	// After the sort above, index < MaxSimultaneousCards (among touched entries) IS the full-card
	// test -- no separate set needed.
	const int32 FullCount = FMath::Min(TouchedCount, MaxSimultaneousCards);

	// Declutter only the full cards -- a bare-anchor entry has no body to collide with. Sorting the
	// [0, FullCount) sub-range of Ranks IN PLACE via a view avoids a second array entirely.
	TArrayView<FCardRank> FullView(Ranks.GetData(), FullCount);
	FullView.Sort([](const FCardRank& A, const FCardRank& B) { return A.CardScreenPos.Y < B.CardScreenPos.Y; });

	// Bounded single pass, top to bottom: push any card whose Y sits too close to the previous one
	// straight down by the shortfall. Not an iterative solver -- a genuinely crowded frame stacks
	// rather than converging perfectly; the leader line (computed below from the UNMOVED anchor) is
	// what sells the difference.
	for (int32 Index = 1; Index < FullCount; ++Index)
	{
		FCardRank& Prev = Ranks[Index - 1];
		FCardRank& Curr = Ranks[Index];

		const float MinY = Prev.CardScreenPos.Y + MinCardVerticalSpacing;
		if (Curr.CardScreenPos.Y < MinY)
			Curr.CardScreenPos.Y = MinY;
	}

	for (int32 Index = 0; Index < Ranks.Num(); ++Index)
	{
		FCardRank& Rank = Ranks[Index];
		if (!Rank.Card) continue;

		if (Index >= TouchedCount)
		{
			Rank.Card->SetOffScreenHidden(true);
			continue;
		}

		const bool bBareAnchorOnly = (Index >= FullCount);
		Rank.Card->UpdateProjection(Rank.CardScreenPos, Rank.AnchorScreenPos, bBareAnchorOnly, OverlayScale);
	}
}

void UAIOverlayLayer::DrainSquadEventIfIdle()
{
	if (!IsValid(BannerInstance) || !BannerInstance->IsIdle()) return;

	UAIOverlaySubsystem* Subsystem = ResolveSubsystem();
	if (!Subsystem) return;

	FSquadOverlayEvent Event;
	if (!Subsystem->DequeueSquadEvent(Event)) return;

	BannerInstance->PlayEvent(Event);

	// FocusTarget / Rally resolve Instigator to the calling OFFICER, not a target -- confirmed by the
	// coordinator after an earlier draft had this backwards. OfficerDown is deliberately excluded:
	// UnregisterEnemy drops the dying officer's snapshot (and this layer its card, within
	// RefreshInterval) the moment HandleDeath fires, while the banner only drains the event once
	// idle -- seconds later, behind up to two other queued events. FindCard is always null by then,
	// so an OfficerDown card flash was dead code; the banner's own bEmphasis + longer hold already
	// carries OfficerDown's distinct treatment.
	const bool bWantsFocusFlash = Event.Kind == EOverlaySquadEventKind::FocusTarget
		|| Event.Kind == EOverlaySquadEventKind::Rally;
	if (!bWantsFocusFlash) return;

	AEnemyCharacter* Officer = Event.Instigator.Get();
	if (!Officer) return;

	if (UAIOverlayCardWidget* Card = FindCard(Officer))
		Card->PlayFocusFlash(FocusFlashDurationSeconds);
}

void UAIOverlayLayer::EnsureBanner()
{
	if (IsValid(BannerInstance) || !BannerWidgetClass) return;

	BannerInstance = CreateWidget<UAIOverlayBannerWidget>(this, BannerWidgetClass);
	if (!IsValid(BannerInstance)) return;

	if (BannerSlot) BannerSlot->AddChild(BannerInstance);
}

void UAIOverlayLayer::DestroyBanner()
{
	if (!IsValid(BannerInstance)) return;

	BannerInstance->RemoveFromParent();
	BannerInstance = nullptr;
}

void UAIOverlayLayer::EnsureCoverLayer()
{
	if (IsValid(CoverLayerInstance) || !CoverLayerWidgetClass) return;

	CoverLayerInstance = CreateWidget<UAIOverlayCoverLayer>(this, CoverLayerWidgetClass);
	if (!IsValid(CoverLayerInstance)) return;

	CoverLayerInstance->InitializeForOwner(this);
	if (CoverLayerSlot) CoverLayerSlot->AddChild(CoverLayerInstance);
}

void UAIOverlayLayer::DestroyCoverLayer()
{
	if (!IsValid(CoverLayerInstance)) return;

	CoverLayerInstance->RemoveFromParent();
	CoverLayerInstance = nullptr;
}
