// Per-enemy awareness meter widget — floats above the enemy head, filled 0→1 as suspicion rises.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "EnemyTypes.h"
#include "EnemyAwarenessWidget.generated.h"

class UImage;
class UMaterialInstanceDynamic;
class AEnemyCharacter;
class UEnemyAwarenessComponent;

/**
 * Circular meter drawn above the enemy.
 * Collapses while fully Unaware; grows and shifts colour through Suspicious→Searching→Combat.
 *
 * Requires a Widget Blueprint with:
 *   - UImage named "FillImage"            (drives a dynamic material instance; params: Fill scalar, FillColor vector)
 *   - UImage named "RingBackground"       (optional static backdrop ring — BindWidgetOptional)
 *
 * The owning EnemyCharacter calls SetEnemy() in BeginPlay. NativeTick resolves the awareness
 * component lazily (controller may not have possessed yet at construction time).
 */
UCLASS()
class EXTRACTION_API UEnemyAwarenessWidget : public UUserWidget
{
	GENERATED_BODY()

public:

	/** Called by AEnemyCharacter::BeginPlay once the widget is initialised. */
	void SetEnemy(AEnemyCharacter* InEnemy);

	// Material parameter names — the in-engine material must expose exactly these two params.
	static const FName ParamFill;
	static const FName ParamFillColor;

protected:

	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// --- Bound widgets ---

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> FillImage;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> RingBackground;

	// --- Designer-tunable colour stops ---

	UPROPERTY(EditAnywhere, Category = "Awareness")
	FLinearColor LowColor = FLinearColor(0.4f, 0.4f, 0.4f, 1.f);   // dim grey (near 0)

	UPROPERTY(EditAnywhere, Category = "Awareness")
	FLinearColor MidColor = FLinearColor(1.f, 0.85f, 0.f, 1.f);     // yellow (mid suspicion)

	UPROPERTY(EditAnywhere, Category = "Awareness")
	FLinearColor HighColor = FLinearColor(1.f, 0.4f, 0.f, 1.f);     // orange (high suspicion)

	UPROPERTY(EditAnywhere, Category = "Awareness")
	FLinearColor CombatColor = FLinearColor(1.f, 0.05f, 0.f, 1.f);  // red (Combat lock)

private:

	/** Tick accumulator — widget work runs every UpdateInterval seconds, not every frame. */
	float TimeSinceLastUpdate = 0.f;
	static constexpr float UpdateInterval = 0.1f;

	/** DisplayMeter below this threshold collapses the widget (drain-then-hide). */
	static constexpr float VisibleMeterThreshold = 0.02f;

	/** Widget render opacity ramps from 0→1 as DisplayMeter crosses this range (fade-in). */
	static constexpr float FadeInThreshold = 0.05f;

	/** Interp speed applied to DisplayMeter each UpdateInterval tick. */
	static constexpr float MeterInterpSpeed = 12.f;

	/** Smoothed display value — interps toward the raw awareness meter, drives all visuals. */
	float DisplayMeter = 0.f;

	/** Change-detection caches — MID writes are skipped when nothing has changed. */
	float LastWrittenMeter = -1.f;
	FLinearColor LastWrittenColor = FLinearColor::Transparent;

	TWeakObjectPtr<AEnemyCharacter> Enemy;
	TWeakObjectPtr<UEnemyAwarenessComponent> CachedAwareness;

	/** Accumulator for the diagnostic log throttle (logs once per DiagLogInterval). */
	float DiagLogAccumulator = 0.f;
	static constexpr float DiagLogInterval = 1.f;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> FillMID;

	/** Returns the colour to push to the MID for the given state and meter fill. */
	FLinearColor ResolveColor(EEnemyAwarenessState State, float Meter) const;

	/** Diagnostic helper — emits a 1Hz log line when enemy.AwarenessMeterLog > 0. */
	void EmitDiagLog(int32 State, float Target, float Display);
};
