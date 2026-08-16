// UAIOverlayCardWidget implementation.

#include "UI/AIOverlayCardWidget.h"
#include "Components/Image.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Components/Widget.h"
#include "Engine/World.h"
#include "EnemyTypes.h"
#include "TimerManager.h"

namespace
{
	void CollapseOrShow(UTextBlock* Text, bool bShow)
	{
		if (!Text) return;
		Text->SetVisibility(bShow ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	}

	void CollapseOrShow(UImage* Image, bool bShow)
	{
		if (!Image) return;
		Image->SetVisibility(bShow ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	}
}

void UAIOverlayCardWidget::InitializeForEnemy(const FEnemyOverlaySnapshot& Snapshot)
{
	Enemy = Snapshot.Enemy;
	bHasAppeared = false;

	UpdateSnapshot(Snapshot);
}

void UAIOverlayCardWidget::UpdateSnapshot(const FEnemyOverlaySnapshot& Snapshot)
{
	AwarenessState = Snapshot.AwarenessState;
	// Ordinal ordering matches EEnemyAwarenessState (Unaware=0 .. Combat=3) -- one chevron per step
	// past Unaware, capped at 3.
	ChevronCount = FMath::Clamp<int32>(static_cast<int32>(AwarenessState), 0, 3);
	bIsOfficer = Snapshot.bIsOfficer;

	SuspicionFill01 = Snapshot.SuspicionFill01;
	SuspicionRate = Snapshot.SuspicionRate;

	bInCover = Snapshot.bInCover;
	bPeeking = Snapshot.bPeeking;
	bBlindFiring = Snapshot.bBlindFiring;
	bCoverMoving = Snapshot.bCoverMoving;
	CoverHeight = Snapshot.CoverHeight;
	LeanDirection = Snapshot.LeanDirection;

	MoraleState = Snapshot.MoraleState;
	Morale01 = Snapshot.Morale01;
	bFearless = Snapshot.bFearless;

	Suppression01 = Snapshot.Suppression01;
	bSuppressed = Snapshot.bSuppressed;

	TargetKind = Snapshot.TargetKind;
	bForcedEngage = Snapshot.bForcedEngage;

	PushArchetypeAndAction(Snapshot);
	PushChevronsAndOfficer();
	PushCoverAndMorale(Snapshot);
	PushTargetAndForced(Snapshot);
	PushSuspicionFill();
	PushCoverDim();
}

void UAIOverlayCardWidget::UpdateProjection(const FVector2D& CardPosition, const FVector2D& AnchorPosition, bool bInBareAnchorOnly, float OverlayScale)
{
	// The layer only calls this for cards that resolved an on-screen projection this frame -- undo
	// any prior SetOffScreenHidden collapse.
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	SetRenderTranslation(CardPosition);
	SetRenderScale(FVector2D(OverlayScale, OverlayScale));

	AnchorOffsetFromCard = AnchorPosition - CardPosition;
	LeaderLineLength = static_cast<float>(AnchorOffsetFromCard.Size());
	LeaderLineAngleDegrees = static_cast<float>(FMath::RadiansToDegrees(
		FMath::Atan2(AnchorOffsetFromCard.Y, AnchorOffsetFromCard.X)));

	if (AnchorDiamondImage)
		AnchorDiamondImage->SetRenderTranslation(AnchorOffsetFromCard);

	const bool bChanged = (bInBareAnchorOnly != bBareAnchorOnly);
	bBareAnchorOnly = bInBareAnchorOnly;

	if (CardBody)
		CardBody->SetVisibility(bBareAnchorOnly ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);

	if (bChanged && IsConstructed())
		OnBareAnchorOnlyChanged(bBareAnchorOnly);

	if (!bHasAppeared)
	{
		bHasAppeared = true;
		if (IsConstructed()) OnCardAppeared();
	}
}

void UAIOverlayCardWidget::SetOffScreenHidden(bool bHidden)
{
	const ESlateVisibility Target = bHidden ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible;
	if (GetVisibility() == Target) return;

	SetVisibility(Target);
}

void UAIOverlayCardWidget::PlayFocusFlash(float DurationSeconds)
{
	if (FocusFlashChevronImage)
		FocusFlashChevronImage->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	if (IsConstructed()) OnFocusFlashTriggered(DurationSeconds);

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(FocusFlashTimerHandle);
		World->GetTimerManager().SetTimer(FocusFlashTimerHandle, this,
			&UAIOverlayCardWidget::HandleFocusFlashExpired, FMath::Max(DurationSeconds, 0.01f), false);
	}
}

void UAIOverlayCardWidget::HandleFocusFlashExpired()
{
	if (FocusFlashChevronImage)
		FocusFlashChevronImage->SetVisibility(ESlateVisibility::Collapsed);
}

void UAIOverlayCardWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// Never collapse this widget from its own code -- Slate does not tick a collapsed widget, so a
	// card that hides itself would never get the frame that brings it back. Matches
	// UObjectiveMarkerWidget's own rationale.
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	if (FocusFlashChevronImage)
		FocusFlashChevronImage->SetVisibility(ESlateVisibility::Collapsed);

	ApplyStaticColors();
}

void UAIOverlayCardWidget::NativeDestruct()
{
	if (UWorld* World = GetWorld())
		World->GetTimerManager().ClearTimer(FocusFlashTimerHandle);

	Super::NativeDestruct();
}

void UAIOverlayCardWidget::ApplyStaticColors()
{
	if (PlateBackgroundImage) PlateBackgroundImage->SetColorAndOpacity(PlateColor);
	if (KeylineImage) KeylineImage->SetColorAndOpacity(KeylineColor);
	if (KeylineDashedImage) KeylineDashedImage->SetColorAndOpacity(KeylineColor);

	if (ArchetypeText) ArchetypeText->SetColorAndOpacity(FSlateColor(InkColor));
	if (ActionText) ActionText->SetColorAndOpacity(FSlateColor(InkColor));
	if (TargetText) TargetText->SetColorAndOpacity(FSlateColor(InkColor));
	if (MoraleText) MoraleText->SetColorAndOpacity(FSlateColor(InkColor));
	if (ForcedTagText) ForcedTagText->SetColorAndOpacity(FSlateColor(InkColor));
	if (CoverStateText) CoverStateText->SetColorAndOpacity(FSlateColor(InkColor));

	// Colour budget enforced here: chevrons 1/2 are always ink, chevron 3 (Combat) and the officer
	// diamond are always accent -- the WBP cannot accidentally light either up in the wrong colour.
	if (AwarenessChevron1Image) AwarenessChevron1Image->SetColorAndOpacity(InkColor);
	if (AwarenessChevron2Image) AwarenessChevron2Image->SetColorAndOpacity(InkColor);
	if (AwarenessChevron3Image) AwarenessChevron3Image->SetColorAndOpacity(AccentColor);
	if (OfficerDiamondImage) OfficerDiamondImage->SetColorAndOpacity(AccentColor);
	if (FocusFlashChevronImage) FocusFlashChevronImage->SetColorAndOpacity(AccentColor);

	if (SuspicionFillBar) SuspicionFillBar->SetFillColorAndOpacity(AccentColor);
}

void UAIOverlayCardWidget::PushArchetypeAndAction(const FEnemyOverlaySnapshot& Snapshot)
{
	// The narrator/archetype DA already emit these upper-case -- no re-casing here (an extra FString +
	// FText allocation per card per reconcile for text that never needed it).
	if (ArchetypeText) ArchetypeText->SetText(Snapshot.ArchetypeLabel);
	if (ActionText) ActionText->SetText(Snapshot.ActionLabel);
}

void UAIOverlayCardWidget::PushChevronsAndOfficer()
{
	// Officer archetype swaps the whole chevron stack for a single diamond -- never both.
	CollapseOrShow(OfficerDiamondImage, bIsOfficer);
	CollapseOrShow(AwarenessChevron1Image, !bIsOfficer && ChevronCount >= 1);
	CollapseOrShow(AwarenessChevron2Image, !bIsOfficer && ChevronCount >= 2);
	CollapseOrShow(AwarenessChevron3Image, !bIsOfficer && ChevronCount >= 3);
}

void UAIOverlayCardWidget::PushCoverAndMorale(const FEnemyOverlaySnapshot& Snapshot)
{
	if (CoverStateText)
	{
		FText Label;
		// Priority order: the loudest cover behaviour wins the single-line slot.
		if (Snapshot.bBlindFiring) Label = FText::FromString(TEXT("BLIND FIRING"));
		else if (Snapshot.bPeeking) Label = FText::FromString(TEXT("PEEKING"));
		else if (Snapshot.bCoverMoving) Label = FText::FromString(TEXT("MOVING"));
		else if (Snapshot.bInCover) Label = FText::FromString(TEXT("IN COVER"));

		CoverStateText->SetText(Label);
		CoverStateText->SetVisibility(Label.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
	}

	if (MoraleText)
	{
		FText Label;
		// Snapshot.MoraleState is a raw uint8 mirror of EMoraleState's ordinals -- cast for a named switch.
		switch (static_cast<EMoraleState>(Snapshot.MoraleState))
		{
		case EMoraleState::Confident: Label = FText::FromString(TEXT("CONFIDENT")); break;
		case EMoraleState::Shaken:    Label = FText::FromString(TEXT("SHAKEN")); break;
		case EMoraleState::Broken:    Label = FText::FromString(TEXT("BROKEN")); break;
		default: break;
		}

		MoraleText->SetText(Label);
		MoraleText->SetVisibility(Snapshot.bFearless ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
	}
}

void UAIOverlayCardWidget::PushTargetAndForced(const FEnemyOverlaySnapshot& Snapshot)
{
	if (TargetText)
	{
		FText Label;
		// Never an object name -- only these two literal words are ever shown here.
		if (Snapshot.TargetKind == EOverlayTargetKind::Player) Label = FText::FromString(TEXT("PLAYER"));
		else if (Snapshot.TargetKind == EOverlayTargetKind::Companion) Label = FText::FromString(TEXT("COMPANION"));

		TargetText->SetText(Label);
		TargetText->SetVisibility(Label.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
	}

	CollapseOrShow(ForcedTagText, Snapshot.bForcedEngage);
	if (ForcedTagText && Snapshot.bForcedEngage)
		ForcedTagText->SetText(FText::FromString(TEXT("FORCED")));
}

void UAIOverlayCardWidget::PushSuspicionFill()
{
	if (!SuspicionFillBar) return;

	SuspicionFillBar->SetPercent(SuspicionFill01);

	const bool bChanging = FMath::Abs(SuspicionRate) > SuspicionChangeEpsilon;
	SuspicionFillBar->SetVisibility(bChanging ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
}

void UAIOverlayCardWidget::PushCoverDim()
{
	if (CardBody)
		CardBody->SetRenderOpacity(bInCover ? InCoverDimOpacity : 1.0f);

	// "Behind cover" dims, never hides -- the dashed keyline swap is the other half of that signal.
	if (KeylineImage) KeylineImage->SetVisibility(bInCover ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
	if (KeylineDashedImage) KeylineDashedImage->SetVisibility(bInCover ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
}
