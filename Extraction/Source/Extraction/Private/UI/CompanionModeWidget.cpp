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
	}
	// Unbind from the companion we actually bound to, not whatever GetCompanion returns now
	// (the pawn may have been destroyed and re-spawned).
	if (ACompanionCharacter* BoundComp = BoundCompanionForCoveringFire.Get())
		BoundComp->OnCoveringFireTick.RemoveDynamic(this, &UCompanionModeWidget::HandleCoveringFireTick);
	BoundCompanionForCoveringFire.Reset();
	bCoveringFireDelegateBound = false;
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

	// Retry covering-fire delegate binding until the companion pawn spawns, and rebind if the
	// companion was destroyed and replaced (BoundCompanionForCoveringFire goes stale).
	if (!bCoveringFireDelegateBound || !BoundCompanionForCoveringFire.IsValid())
	{
		TimeSinceBindAttempt += InDeltaTime;
		if (TimeSinceBindAttempt >= BindRetryInterval)
		{
			TimeSinceBindAttempt = 0.f;
			TryBindCoveringFireDelegate();
		}
	}

	// Poll covering-fire row-4 availability while the picker is open.
	if (BoundCommandComponent->IsModeMenuOpen())
		BroadcastCoverMeAvailability(/*bForce=*/ false);
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

	// Attempt covering-fire delegate bind immediately; NativeTick retries if the companion
	// has not spawned yet.
	TryBindCoveringFireDelegate();
	return true;
}

bool UCompanionModeWidget::TryBindCoveringFireDelegate()
{
	if (!BoundCommandComponent.IsValid()) return false;

	ACompanionCharacter* Companion = BoundCommandComponent->GetCompanion();
	if (!IsValid(Companion)) return false;

	// If already bound to this exact companion, nothing to do.
	if (bCoveringFireDelegateBound && BoundCompanionForCoveringFire.Get() == Companion)
		return true;

	// Unbind from a stale companion before rebinding to the replacement.
	if (ACompanionCharacter* OldComp = BoundCompanionForCoveringFire.Get())
		OldComp->OnCoveringFireTick.RemoveDynamic(this, &UCompanionModeWidget::HandleCoveringFireTick);

	if (!Companion->OnCoveringFireTick.IsAlreadyBound(this, &UCompanionModeWidget::HandleCoveringFireTick))
		Companion->OnCoveringFireTick.AddDynamic(this, &UCompanionModeWidget::HandleCoveringFireTick);

	BoundCompanionForCoveringFire = Companion;
	bCoveringFireDelegateBound = true;
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

	// Seed availability on open so the first frame never shows a stale row —
	// bLastCoverMeAvailable persists across open/close cycles.
	if (bOpen)
		BroadcastCoverMeAvailability(/*bForce=*/ true);
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

void UCompanionModeWidget::BroadcastCoverMeAvailability(bool bForce)
{
	if (!BoundCommandComponent.IsValid()) return;
	const bool bAvail = BoundCommandComponent->IsCoverMeAvailable();
	float CooldownRemaining = 0.f;
	if (ACompanionCharacter* Comp = BoundCommandComponent->GetCompanion())
		CooldownRemaining = Comp->GetCoveringFireCooldownRemaining();
	// Force: always broadcast (menu just opened, stale bLastCoverMeAvailable).
	// Otherwise: skip when nothing has changed and no cooldown is counting down.
	if (bForce || bAvail != bLastCoverMeAvailable || CooldownRemaining > 0.f)
	{
		bLastCoverMeAvailable = bAvail;
		OnCoverMeAvailabilityChangedBP(bAvail, CooldownRemaining);
	}
}

void UCompanionModeWidget::HandleCoveringFireTick(float Remaining, bool bPaused)
{
	OnCoveringFireCountdownBP(Remaining, bPaused);
}
