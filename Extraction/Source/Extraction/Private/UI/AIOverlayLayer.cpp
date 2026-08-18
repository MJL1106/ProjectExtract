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
	ReconcileCompanionCard(Subsystem->GetCompanionSnapshot());
}

void UAIOverlayLayer::ReconcileCompanionCard(const FEnemyOverlaySnapshot* Snapshot)
{
	if (!CardCanvas || !CardWidgetClass) return;

	if (!Snapshot)
	{
		if (IsValid(CompanionCard))
		{
			CardCanvas->RemoveChild(CompanionCard);
			CompanionCard = nullptr;
		}
		return;
	}

	if (IsValid(CompanionCard))
	{
		CompanionCard->UpdateSnapshot(*Snapshot);
		return;
	}

	CompanionCard = CreateWidget<UAIOverlayCardWidget>(this, CardWidgetClass);
	if (!IsValid(CompanionCard)) return;

	CompanionCard->InitializeForEnemy(*Snapshot);
	CompanionCard->SetOffScreenHidden(true);

	UCanvasPanelSlot* CanvasSlot = CardCanvas->AddChildToCanvas(CompanionCard);
	if (!CanvasSlot) return;

	CanvasSlot->SetAnchors(FAnchors(0.f, 0.f));
	CanvasSlot->SetAutoSize(true);
	CanvasSlot->SetAlignment(FVector2D(0.5f, 0.5f));
	CanvasSlot->SetPosition(FVector2D::ZeroVector);
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

		// Range cull rides the same flag as off-screen: a beyond-range enemy is hidden below, never
		// ranked or decluttered, so it also cannot displace a near enemy from the card cap.
		const float Distance = FVector::Dist(Context.ViewLocation, Snapshot.WorldAnchor);

		FCardRank& Rank = ScratchRanks.AddDefaulted_GetRef();
		Rank.Card = Card;
		Rank.Snapshot = &Snapshot;
		Rank.bTouched = Projection.bIsValid && !Projection.bIsOffScreen && Distance <= CardMaxDistance;
		if (Rank.bTouched)
		{
			Rank.DistanceScale = FMath::Clamp(
				CardReferenceDistance / FMath::Max(Distance, 1.f), CardMinDistanceScale, 1.f);
			Rank.AnchorScreenPos = Projection.ScreenPosition;
			// The float height rides the card's own scale so a shrunken far card hugs its anchor
			// instead of hovering a full-size gap above a half-size body.
			Rank.CardScreenPos = Projection.ScreenPosition
				- FVector2D(0.f, CardHeightAboveAnchor * Rank.DistanceScale);
		}
	}

	DeclutterAndPush(ScratchRanks, OverlayScale);

	// Companion card: projected with identical math but outside the enemy rank set — its identity
	// is the single held widget, and it must never compete with the enemy cards.
	if (IsValid(CompanionCard))
	{
		const FEnemyOverlaySnapshot* CompSnap = Subsystem->GetCompanionSnapshot();
		bool bShown = false;
		if (CompSnap)
		{
			const FScreenMarkerProjection Projection =
				FScreenMarkerProjection::Project(Context, CompSnap->WorldAnchor, ClampParams);
			const float Distance = FVector::Dist(Context.ViewLocation, CompSnap->WorldAnchor);
			if (Projection.bIsValid && !Projection.bIsOffScreen && Distance <= CardMaxDistance)
			{
				const float DistanceScale = FMath::Clamp(
					CardReferenceDistance / FMath::Max(Distance, 1.f), CardMinDistanceScale, 1.f);
				const FVector2D CardPos = Projection.ScreenPosition
					- FVector2D(0.f, CardHeightAboveAnchor * DistanceScale);
				CompanionCard->UpdateProjection(CardPos, Projection.ScreenPosition,
					/*bInBareAnchorOnly=*/false, OverlayScale * DistanceScale);
				bShown = true;
			}
		}
		if (!bShown) CompanionCard->SetOffScreenHidden(true);
	}
}

void UAIOverlayLayer::DeclutterAndPush(TArray<FCardRank>& Ranks, float OverlayScale)
{
	// No sort, no cap demotion, no push — every in-range on-screen enemy gets its full card pinned
	// directly above its head, and overlap is allowed. The ranked declutter this replaced re-ordered
	// and re-pushed cards every frame as ranks and positions shifted, which read as cards jumping
	// around the screen; a head-pinned card only ever moves with its own enemy.
	for (FCardRank& Rank : Ranks)
	{
		if (!Rank.Card) continue;

		if (!Rank.bTouched)
		{
			Rank.Card->SetOffScreenHidden(true);
			continue;
		}

		Rank.Card->UpdateProjection(Rank.CardScreenPos, Rank.AnchorScreenPos, /*bInBareAnchorOnly=*/false,
			OverlayScale * Rank.DistanceScale);
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
