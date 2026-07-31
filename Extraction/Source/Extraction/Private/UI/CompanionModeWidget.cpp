// HUD chip showing the companion's player-commanded mode.

#include "UI/CompanionModeWidget.h"
#include "Components/CompanionCommandComponent.h"
#include "Companion/CompanionCharacter.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"

void UCompanionModeWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (KeyHintText)
		KeyHintText->SetText(KeyHint);

	// Picker list starts collapsed — it only appears while the picker is open.
	if (ModeListPanel)
		ModeListPanel->SetVisibility(ESlateVisibility::Collapsed);

	// Pawn may not be possessed yet — NativeTick retries until bound.
	if (TryBindToCommandComponent())
		ApplyMode(BoundCommandComponent->GetCompanionMode(), /*bFromChange=*/ false);
}

void UCompanionModeWidget::NativeDestruct()
{
	if (UCompanionCommandComponent* CmdComp = BoundCommandComponent.Get())
	{
		CmdComp->OnCompanionModeChanged.RemoveDynamic(this, &UCompanionModeWidget::HandleModeChanged);
		CmdComp->OnModeMenuChanged.RemoveDynamic(this, &UCompanionModeWidget::HandleModeMenuChanged);
		if (ACompanionCharacter* Companion = CmdComp->GetCompanion())
			Companion->OnCoveringFireTick.RemoveDynamic(this, &UCompanionModeWidget::HandleCoveringFireTick);
	}
	BoundCommandComponent.Reset();

	Super::NativeDestruct();
}

void UCompanionModeWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (!BoundCommandComponent.IsValid())
	{
		TimeSinceBindAttempt += InDeltaTime;
		if (TimeSinceBindAttempt < BindRetryInterval) return;
		TimeSinceBindAttempt = 0.f;

		if (TryBindToCommandComponent())
			ApplyMode(BoundCommandComponent->GetCompanionMode(), /*bFromChange=*/ false);
		return;
	}

	// Poll covering-fire row-4 availability while the picker is open.
	if (BoundCommandComponent->IsModeMenuOpen())
	{
		const bool bAvail = BoundCommandComponent->IsCoverMeAvailable();
		float CooldownRemaining = 0.f;
		if (ACompanionCharacter* Comp = BoundCommandComponent->GetCompanion())
			CooldownRemaining = Comp->GetCoveringFireCooldownRemaining();
		// Always broadcast while open so the cooldown countdown updates continuously.
		if (bAvail != bLastCoverMeAvailable || CooldownRemaining > 0.f)
		{
			bLastCoverMeAvailable = bAvail;
			OnCoverMeAvailabilityChangedBP(bAvail, CooldownRemaining);
		}
	}
}

bool UCompanionModeWidget::TryBindToCommandComponent()
{
	if (BoundCommandComponent.IsValid()) return true;

	const APawn* Pawn = GetOwningPlayerPawn();
	if (!IsValid(Pawn)) return false;

	UCompanionCommandComponent* CmdComp = Pawn->FindComponentByClass<UCompanionCommandComponent>();
	if (!IsValid(CmdComp)) return false;

	BoundCommandComponent = CmdComp;
	CmdComp->OnCompanionModeChanged.AddDynamic(this, &UCompanionModeWidget::HandleModeChanged);
	CmdComp->OnModeMenuChanged.AddDynamic(this, &UCompanionModeWidget::HandleModeMenuChanged);
	HandleModeMenuChanged(CmdComp->IsModeMenuOpen()); // seed list/chip state if bound while already open

	// Bind the covering-fire countdown delegate from the companion itself.
	if (ACompanionCharacter* Companion = CmdComp->GetCompanion())
	{
		if (!Companion->OnCoveringFireTick.IsAlreadyBound(this, &UCompanionModeWidget::HandleCoveringFireTick))
			Companion->OnCoveringFireTick.AddDynamic(this, &UCompanionModeWidget::HandleCoveringFireTick);
	}
	return true;
}

void UCompanionModeWidget::HandleModeChanged(ECompanionMode NewMode)
{
	ApplyMode(NewMode, /*bFromChange=*/ true);
}

void UCompanionModeWidget::HandleModeMenuChanged(bool bOpen)
{
	if (ModeListPanel)
		ModeListPanel->SetVisibility(bOpen ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	if (ChipContainer)
		ChipContainer->SetVisibility(bOpen ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);

	OnModeMenuOpenChangedBP(bOpen, CurrentMode);
}

void UCompanionModeWidget::ApplyMode(ECompanionMode NewMode, bool bFromChange)
{
	CurrentMode = NewMode;

	FText Label;
	FLinearColor Color;
	switch (NewMode)
	{
	case ECompanionMode::Combat:  Label = CombatLabel;  Color = CombatColor;  break;
	case ECompanionMode::Stealth: Label = StealthLabel; Color = StealthColor; break;
	default:                      Label = NormalLabel;  Color = NormalColor;  break;
	}

	if (ModeText)
	{
		ModeText->SetText(Label);
		ModeText->SetColorAndOpacity(FSlateColor(Color));
	}
	if (ModeIcon)
		ModeIcon->SetColorAndOpacity(Color);

	if (bFromChange)
		OnModeChangedBP(NewMode);
}

void UCompanionModeWidget::HandleCoveringFireTick(float Remaining, bool bPaused)
{
	OnCoveringFireCountdownBP(Remaining, bPaused);
}
