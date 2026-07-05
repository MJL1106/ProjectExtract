// Overhead companion mode indicator — pops on mode change, holds, then fades out.

#include "UI/CompanionModeIndicatorWidget.h"
#include "Companion/CompanionCharacter.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"

void UCompanionModeIndicatorWidget::SetCompanion(ACompanionCharacter* InCompanion)
{
	if (!IsValid(InCompanion)) return;
	if (Companion.Get() == InCompanion) return;

	if (ACompanionCharacter* Old = Companion.Get())
		Old->OnModeChanged.RemoveDynamic(this, &UCompanionModeIndicatorWidget::HandleModeChanged);

	Companion = InCompanion;
	InCompanion->OnModeChanged.AddDynamic(this, &UCompanionModeIndicatorWidget::HandleModeChanged);

	// Prime visuals for the current mode but stay hidden — the indicator only pops on changes.
	ApplyMode(InCompanion->GetMode());
	SetRenderOpacity(0.f);
	HoldRemaining = 0.f;
}

void UCompanionModeIndicatorWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetRenderOpacity(0.f);
}

void UCompanionModeIndicatorWidget::NativeDestruct()
{
	if (ACompanionCharacter* Comp = Companion.Get())
		Comp->OnModeChanged.RemoveDynamic(this, &UCompanionModeIndicatorWidget::HandleModeChanged);
	Companion.Reset();

	Super::NativeDestruct();
}

void UCompanionModeIndicatorWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (HoldRemaining > 0.f)
	{
		HoldRemaining -= InDeltaTime;
		return;
	}

	const float Opacity = GetRenderOpacity();
	if (Opacity > 0.f)
		SetRenderOpacity(FMath::Max(0.f, Opacity - InDeltaTime / FadeDuration));
}

void UCompanionModeIndicatorWidget::HandleModeChanged(ECompanionMode NewMode)
{
	ApplyMode(NewMode);
	SetRenderOpacity(1.f);
	HoldRemaining = HoldDuration;
}

void UCompanionModeIndicatorWidget::ApplyMode(ECompanionMode NewMode)
{
	FText Label;
	FLinearColor Color;
	switch (NewMode)
	{
	case ECompanionMode::Combat:  Label = CombatLabel;  Color = CombatColor;  break;
	case ECompanionMode::Stealth: Label = StealthLabel; Color = StealthColor; break;
	default:                      Label = NormalLabel;  Color = NormalColor;  break;
	}

	if (ModeIcon)
		ModeIcon->SetColorAndOpacity(Color);
	if (ModeLabel)
	{
		ModeLabel->SetText(Label);
		ModeLabel->SetColorAndOpacity(FSlateColor(Color));
	}
}
