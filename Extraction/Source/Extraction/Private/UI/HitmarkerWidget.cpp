// UHitmarkerWidget implementation.

#include "UI/HitmarkerWidget.h"
#include "ExtractionPlayerController.h"
#include "Components/Image.h"
#include "Enemy/EnemyCharacter.h"

void UHitmarkerWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	if (MarkerRoot)
		MarkerRoot->SetRenderOpacity(0.f);
}

void UHitmarkerWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// Belt-and-braces with NativeOnInitialized: the marker must never be visible before the first hit.
	if (MarkerRoot && TimeRemaining <= 0.f)
		MarkerRoot->SetRenderOpacity(0.f);

	AExtractionPlayerController* PC = Cast<AExtractionPlayerController>(GetOwningPlayer());
	if (IsValid(PC) && !PC->OnDamageDealt.IsAlreadyBound(this, &UHitmarkerWidget::HandleDamageDealt))
		PC->OnDamageDealt.AddDynamic(this, &UHitmarkerWidget::HandleDamageDealt);
}

void UHitmarkerWidget::NativeDestruct()
{
	if (AExtractionPlayerController* PC = Cast<AExtractionPlayerController>(GetOwningPlayer()))
		PC->OnDamageDealt.RemoveDynamic(this, &UHitmarkerWidget::HandleDamageDealt);

	Super::NativeDestruct();
}

void UHitmarkerWidget::HandleDamageDealt(AActor* Victim, float Damage, float HeadshotDamage, bool bKilled, FVector WorldLocation)
{
	const bool bArmoredHead = [Victim]()
	{
		const AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Victim);
		return IsValid(Enemy) && Enemy->HasArmoredHead();
	}();

	ShowHit(HeadshotDamage > 0.f, bKilled, bArmoredHead);
}

void UHitmarkerWidget::ShowHit(bool bHeadshot, bool bKilled, bool bArmoredHead)
{
	if (!MarkerRoot) return;

	bKillAnim = bKilled;
	ActiveDuration = bKilled ? KillDuration : HitDuration;
	TimeRemaining = ActiveDuration;

	// Priority: headshot kill > kill > armoured headshot > headshot > body hit.
	FLinearColor Color;
	if (bKilled && bHeadshot)
		Color = HeadshotKillColor;
	else if (bKilled)
		Color = KillColor;
	else if (bHeadshot && bArmoredHead)
		Color = ArmoredHeadshotColor;
	else if (bHeadshot)
		Color = HeadshotColor;
	else
		Color = HitColor;
	SetTickColor(Color);

	// Battlefield style: headshots double every arm.
	const ESlateVisibility DoubleVis = bHeadshot ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed;
	if (DoubleTL) DoubleTL->SetVisibility(DoubleVis);
	if (DoubleTR) DoubleTR->SetVisibility(DoubleVis);
	if (DoubleBL) DoubleBL->SetVisibility(DoubleVis);
	if (DoubleBR) DoubleBR->SetVisibility(DoubleVis);

	// Hits punch in oversized and settle; kills start large and grow from there.
	const float StartScale = bKilled ? KillStartScale : HitPopScale;
	MarkerRoot->SetRenderScale(FVector2D(StartScale, StartScale));
	MarkerRoot->SetRenderOpacity(1.f);
}

void UHitmarkerWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (TimeRemaining <= 0.f || !MarkerRoot)
	{
		// Idle enforcement: some widget-startup path re-applies the archetype's visible state
		// after NativeOnInitialized/NativeConstruct — force the rest state every idle frame.
		if (MarkerRoot && MarkerRoot->GetRenderOpacity() > 0.f)
			MarkerRoot->SetRenderOpacity(0.f);
		return;
	}

	TimeRemaining -= InDeltaTime;
	if (TimeRemaining <= 0.f)
	{
		MarkerRoot->SetRenderOpacity(0.f);
		MarkerRoot->SetRenderScale(FVector2D(1.f, 1.f));
		return;
	}

	const float T = 1.f - (TimeRemaining / ActiveDuration); // 0 -> 1 over the animation
	const float Elapsed = ActiveDuration - TimeRemaining;

	if (bKillAnim)
	{
		// Expand with an ease-out so the growth reads early; hold bright, then eased fade.
		const float EasedT = 1.f - FMath::Square(1.f - T);
		const float Scale = FMath::Lerp(KillStartScale, KillEndScale, EasedT);
		MarkerRoot->SetRenderScale(FVector2D(Scale, Scale));

		constexpr float KillHoldFraction = 0.2f;
		const float FadeT = FMath::Clamp((T - KillHoldFraction) / (1.f - KillHoldFraction), 0.f, 1.f);
		MarkerRoot->SetRenderOpacity(1.f - FMath::Square(FadeT));
	}
	else
	{
		// Impact pop: settle from HitPopScale to 1.0 with an ease-out, then rest.
		const float SettleT = FMath::Clamp(Elapsed / PopSettleSeconds, 0.f, 1.f);
		const float Scale = FMath::Lerp(HitPopScale, 1.f, 1.f - FMath::Square(1.f - SettleT));
		MarkerRoot->SetRenderScale(FVector2D(Scale, Scale));

		// Hold at full opacity, then eased fade.
		const float FadeT = FMath::Clamp((T - HitHoldFraction) / FMath::Max(KINDA_SMALL_NUMBER, 1.f - HitHoldFraction), 0.f, 1.f);
		MarkerRoot->SetRenderOpacity(1.f - FMath::Square(FadeT));
	}
}

void UHitmarkerWidget::SetTickColor(const FLinearColor& Color)
{
	if (TickTL) TickTL->SetColorAndOpacity(Color);
	if (TickTR) TickTR->SetColorAndOpacity(Color);
	if (TickBL) TickBL->SetColorAndOpacity(Color);
	if (TickBR) TickBR->SetColorAndOpacity(Color);
	if (DoubleTL) DoubleTL->SetColorAndOpacity(Color);
	if (DoubleTR) DoubleTR->SetColorAndOpacity(Color);
	if (DoubleBL) DoubleBL->SetColorAndOpacity(Color);
	if (DoubleBR) DoubleBR->SetColorAndOpacity(Color);
}
