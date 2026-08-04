// UTutorialBriefingWidget -- native base for the controls briefing screen.

#include "UI/TutorialBriefingWidget.h"

#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "EnhancedInputSubsystemInterface.h"
#include "EnhancedInputSubsystems.h"
#include "Kismet/GameplayStatics.h"

#include "Game/ExtractionPlayerController.h"
#include "UI/KeybindHintLibrary.h"
#include "UI/TutorialBriefingData.h"
#include "UI/TutorialControlRowWidget.h"

UTutorialBriefingWidget::UTutorialBriefingWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// The controller focuses the widget root for gamepad/keyboard; an unfocusable root declines it.
	SetIsFocusable(true);
}

void UTutorialBriefingWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (IsValid(ConfirmButton))
		ConfirmButton->OnClicked.AddUniqueDynamic(this, &UTutorialBriefingWidget::HandleConfirmClicked);

	if (UEnhancedInputLocalPlayerSubsystem* Input = UKeybindHintLibrary::FindLocalPlayerInputSubsystem(this))
	{
		BoundInputSubsystem = Input;
		// Kept for mid-session rebinds only. The first paint must NOT depend on it.
		Input->ControlMappingsRebuiltDelegate.AddUniqueDynamic(this, &UTutorialBriefingWidget::HandleControlMappingsRebuilt);

		// ControlMappingsRebuiltDelegate is broadcast from the EnhancedInput module tick, and the
		// controller pauses the game in the same step that creates this widget — a deferred rebuild
		// may never flush, and world timers do not advance while paused, so no retry can rescue it.
		// bForceImmediately is the one path that rebuilds the mapping table synchronously, so every
		// key below resolves to a real binding rather than latching at "[unbound]" for good.
		// Deliberately here and not in HandleControlMappingsRebuilt, which would re-enter.
		FModifyContextOptions RebuildOptions;
		RebuildOptions.bForceImmediately = true;
		Input->RequestRebuildControlMappings(RebuildOptions);
	}

	ApplyHeaderCopy();
	SyncColumns();
	HideOtherWidgets();
}

void UTutorialBriefingWidget::NativeDestruct()
{
	RestoreHiddenWidgets();
	if (IsValid(ConfirmButton))
		ConfirmButton->OnClicked.RemoveDynamic(this, &UTutorialBriefingWidget::HandleConfirmClicked);

	if (UEnhancedInputLocalPlayerSubsystem* Input = BoundInputSubsystem.Get())
		Input->ControlMappingsRebuiltDelegate.RemoveDynamic(this, &UTutorialBriefingWidget::HandleControlMappingsRebuilt);
	BoundInputSubsystem.Reset();

	Super::NativeDestruct();
}

void UTutorialBriefingWidget::HandleControlMappingsRebuilt()
{
	// The first resolve ran before Enhanced Input had a mapping table. This is the correct answer.
	SyncColumns();
}

void UTutorialBriefingWidget::ApplyHeaderCopy()
{
	if (!IsValid(BriefingData)) return;

	if (IsValid(TitleText) && !BriefingData->Title.IsEmptyOrWhitespace())
		TitleText->SetText(BriefingData->Title);

	if (IsValid(SubtitleText) && !BriefingData->Subtitle.IsEmptyOrWhitespace())
		SubtitleText->SetText(BriefingData->Subtitle);

	if (IsValid(ConfirmLabelText) && !BriefingData->ConfirmLabel.IsEmptyOrWhitespace())
		ConfirmLabelText->SetText(BriefingData->ConfirmLabel);
}

void UTutorialBriefingWidget::SyncColumns()
{
	// A null class or asset here means an empty briefing with a working confirm button — recoverable,
	// but silently wrong, so say so rather than shipping a blank screen.
	if (!ensureMsgf(IsValid(BriefingData), TEXT("BriefingData not assigned on %s — the briefing will render no rows."), *GetName()))
		return;
	if (!ensureMsgf(RowWidgetClass != nullptr, TEXT("RowWidgetClass not assigned on %s — the briefing will render no rows."), *GetName()))
		return;

	SyncColumn(BasicsBox, BriefingData->BasicsRows, BasicsRowWidgets);
	SyncColumn(CompanionBox, BriefingData->CompanionRows, CompanionRowWidgets);
}

void UTutorialBriefingWidget::SyncColumn(UVerticalBox* Box, const TArray<FTutorialControlRow>& Rows, TArray<TObjectPtr<UTutorialControlRowWidget>>& RowWidgets)
{
	if (!IsValid(Box)) return;

	if (RowWidgets.Num() != Rows.Num()) CreateColumnRows(Box, Rows.Num(), RowWidgets);

	// Min() rather than Rows.Num(): a creation failure leaves the column short, and a short column
	// beats an out-of-bounds read. Index alignment holds because CreateColumnRows stops on failure.
	const int32 Count = FMath::Min(Rows.Num(), RowWidgets.Num());
	for (int32 Index = 0; Index < Count; ++Index)
	{
		UTutorialControlRowWidget* RowWidget = RowWidgets[Index];
		if (!IsValid(RowWidget)) continue;

		const FTutorialControlRow& Row = Rows[Index];
		RowWidget->SetRowContent(Row.ResolveKeyText(this), Row.Label, Row.Detail, Row.bIsHeading);
	}
}

void UTutorialBriefingWidget::CreateColumnRows(UVerticalBox* Box, int32 RowCount, TArray<TObjectPtr<UTutorialControlRowWidget>>& RowWidgets)
{
	Box->ClearChildren();
	RowWidgets.Reset();
	RowWidgets.Reserve(RowCount);

	for (int32 Index = 0; Index < RowCount; ++Index)
	{
		UTutorialControlRowWidget* RowWidget = CreateWidget<UTutorialControlRowWidget>(this, RowWidgetClass);
		if (!IsValid(RowWidget)) break;

		Box->AddChildToVerticalBox(RowWidget);
		RowWidgets.Add(RowWidget);
	}
}

void UTutorialBriefingWidget::HideOtherWidgets()
{
	TArray<UUserWidget*> AllWidgets;
	UWidgetBlueprintLibrary::GetAllWidgetsOfClass(this, AllWidgets, UUserWidget::StaticClass(), true);

	// Restore rather than discard: a second capture without an intervening restore would scan a HUD
	// this function had already collapsed, record nothing, and leave every widget hidden for good.
	RestoreHiddenWidgets();
	HiddenWidgets.Reserve(AllWidgets.Num());

	for (UUserWidget* Widget : AllWidgets)
	{
		if (!IsValid(Widget)) continue;
		if (Widget == this) continue;
		// Skipped at CAPTURE, not at restore: an excluded widget must never enter HiddenWidgets,
		// or a later restore hands it a visibility it never asked for.
		if (ExcludedRootClass && Widget->IsA(ExcludedRootClass)) continue;

		const ESlateVisibility Current = Widget->GetVisibility();
		// Already hidden for its own reasons — leave it alone, and do not record it so restore
		// cannot wrongly make it visible afterwards.
		if (Current == ESlateVisibility::Collapsed || Current == ESlateVisibility::Hidden) continue;

		HiddenWidgets.Add({ Widget, Current });
		Widget->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UTutorialBriefingWidget::RestoreHiddenWidgets()
{
	for (const FHiddenWidgetRecord& Record : HiddenWidgets)
	{
		UUserWidget* Widget = Record.Widget.Get();
		if (!IsValid(Widget)) continue;

		Widget->SetVisibility(Record.PriorVisibility);
	}

	HiddenWidgets.Reset();
}

void UTutorialBriefingWidget::HandleConfirmClicked()
{
	APlayerController* PC = GetOwningPlayer();

	if (AExtractionPlayerController* ExtractionPC = Cast<AExtractionPlayerController>(PC))
	{
		ExtractionPC->DismissTutorialBriefing();
		return;
	}

	// An unexpected controller class must still be able to get OUT. Returning early here would leave
	// the game paused, input UI-only and the cursor up with the screen's only input already spent —
	// an unrecoverable soft-lock. Dismissing without the controller's cleanup is strictly better.
	UGameplayStatics::SetGamePaused(this, false);

	if (IsValid(PC))
	{
		PC->SetInputMode(FInputModeGameOnly());
		PC->SetShowMouseCursor(false);
	}

	RemoveFromParent();
}
