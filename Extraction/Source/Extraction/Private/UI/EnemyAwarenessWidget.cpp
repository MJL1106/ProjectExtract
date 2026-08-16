// Per-enemy awareness meter widget implementation.

#include "EnemyAwarenessWidget.h"
#include "EnemyCharacter.h"
#include "EnemyAIController.h"
#include "EnemyAwarenessComponent.h"
#include "AI/Cover/CoverPoseComponent.h"
#include "Components/Image.h"
#include "Materials/MaterialInstanceDynamic.h"

const FName UEnemyAwarenessWidget::ParamFill      = TEXT("Fill");
const FName UEnemyAwarenessWidget::ParamFillColor = TEXT("FillColor");
const FName UEnemyAwarenessWidget::ParamPulse     = TEXT("Pulse");

void UEnemyAwarenessWidget::SetEnemy(AEnemyCharacter* InEnemy)
{
	Enemy = InEnemy;
	// CachedAwareness resolved lazily in NativeTick — controller may not have possessed yet.
}

void UEnemyAwarenessWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// Always Slate-visible so NativeTick keeps running; render opacity hides it until awareness builds.
	// Collapsed would stop ticking entirely — deadlock where the widget can never un-hide itself.
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	SetRenderOpacity(0.f);

	if (!IsValid(FillImage)) return;

	FillMID = FillImage->GetDynamicMaterial();
}

void UEnemyAwarenessWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	TimeSinceLastUpdate += InDeltaTime;
	if (TimeSinceLastUpdate < UpdateInterval) return;
	TimeSinceLastUpdate = 0.f;

	// Resolve awareness component lazily — controller may possess after widget construction.
	if (!CachedAwareness.IsValid())
	{
		if (!Enemy.IsValid())
		{
			SetRenderOpacity(0.f);
			return;
		}

		AEnemyAIController* AIC = Cast<AEnemyAIController>(Enemy->GetController());
		if (!AIC)
		{
			SetRenderOpacity(0.f);
			return;
		}

		UEnemyAwarenessComponent* Comp = AIC->GetAwarenessComponent();
		if (!IsValid(Comp))
		{
			SetRenderOpacity(0.f);
			return;
		}

		CachedAwareness = Comp;
	}

	const EEnemyAwarenessState State = CachedAwareness->GetAwarenessState();
	const float TargetMeter = CachedAwareness->GetAwarenessMeter01();

	// Smooth the displayed fill toward the raw target (gives "fills up / drains back" behaviour).
	// UpdateInterval is used as delta so the interp rate is frame-rate-independent across throttle ticks.
	DisplayMeter = FMath::FInterpTo(DisplayMeter, TargetMeter, UpdateInterval, MeterInterpSpeed);

	// Dim (not hide) while the enemy holds cover — the overhead widget's own occlusion exemption
	// keeps it visible, this makes it read as "behind cover" rather than full brightness.
	const UCoverPoseComponent* CoverPose = Enemy.IsValid() ? Enemy->GetCoverPoseComponent() : nullptr;
	const bool bInCover = IsValid(CoverPose) && CoverPose->bInCover;
	const float TargetCoverDimAlpha = bInCover ? InCoverOpacity : 1.f;
	CoverDimAlpha = FMath::FInterpTo(CoverDimAlpha, TargetCoverDimAlpha, UpdateInterval, CoverDimInterpSpeed);

	// Full-combat hide: the locked red ring is pure view clutter mid-fight. Fade out (not snap)
	// so the Combat entry reads as the meter "locking in" before it clears the screen.
	const bool bCombatHidden = bHideInCombat && State == EEnemyAwarenessState::Combat;
	CombatHideAlpha = FMath::FInterpTo(CombatHideAlpha, bCombatHidden ? 0.f : 1.f, UpdateInterval, CombatHideInterpSpeed);

	// Collapse (and bail early) while DisplayMeter is negligible — handles Unaware and the
	// tail of a draining Searching state without leaving an empty ring visible.
	if (DisplayMeter <= VisibleMeterThreshold)
	{
		SetRenderOpacity(0.f);
		return;
	}

	// Fade-in: ramp render opacity from 0→1 over the first FadeInThreshold of fill,
	// giving a real fade-in on spawn and a fade-out as the meter drains near zero, then dim for cover.
	SetRenderOpacity(FMath::Clamp(DisplayMeter / FadeInThreshold, 0.f, 1.f) * CoverDimAlpha * CombatHideAlpha);

	if (!IsValid(FillMID)) return;

	// Change-detection: skip MID writes when the smoothed value has settled (perf: 20 enemies × 10Hz).
	const bool bMeterChanged = !FMath::IsNearlyEqual(DisplayMeter, LastWrittenMeter, KINDA_SMALL_NUMBER);
	const FLinearColor NewColor = ResolveColor(State, DisplayMeter);
	const bool bColorChanged = !NewColor.Equals(LastWrittenColor);
	const float NewPulse = State == EEnemyAwarenessState::Combat ? 1.f : 0.f;
	const bool bPulseChanged = !FMath::IsNearlyEqual(NewPulse, LastWrittenPulse, KINDA_SMALL_NUMBER);

	if (bMeterChanged)
	{
		FillMID->SetScalarParameterValue(ParamFill, DisplayMeter);
		LastWrittenMeter = DisplayMeter;
	}

	if (bColorChanged)
	{
		FillMID->SetVectorParameterValue(ParamFillColor, NewColor);
		LastWrittenColor = NewColor;
	}

	if (bPulseChanged)
	{
		FillMID->SetScalarParameterValue(ParamPulse, NewPulse);
		LastWrittenPulse = NewPulse;
	}
}

FLinearColor UEnemyAwarenessWidget::ResolveColor(EEnemyAwarenessState State, float Meter) const
{
	if (State == EEnemyAwarenessState::Combat) return CombatColor;

	// Lerp Low→Mid in [0, 0.5], Mid→High in [0.5, 1].
	if (Meter <= 0.5f)
		return FLinearColor::LerpUsingHSV(LowColor, MidColor, Meter * 2.f);

	return FLinearColor::LerpUsingHSV(MidColor, HighColor, (Meter - 0.5f) * 2.f);
}
