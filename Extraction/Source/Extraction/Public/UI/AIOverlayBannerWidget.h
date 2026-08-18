// UAIOverlayBannerWidget -- one centred squad-event strip. Displays exactly one FSquadOverlayEvent
// at a time (2.5s hold + 0.4s fade by default); UAIOverlayLayer owns the depth-3 queue via
// UAIOverlaySubsystem::DequeueSquadEvent and only calls PlayEvent() while IsIdle() is true, so this
// widget never has to arbitrate between two events itself.
//
// Text is NOT composed here -- FSquadOverlayEvent::Headline/Detail arrive already authored by the
// narrator ("OFFICER DOWN" / "SQUAD MORALE -75"); this widget only displays them.
//
// OfficerDown gets the most prominent treatment per design intent: a longer hold
// (OfficerDownHoldDurationSeconds) and bEmphasis=true on OnBannerEventShown, so the WBP can scale up
// text/weight for that one beat. Deliberately NOT a new accent-colour use -- the steady-state look's
// three-things orange budget (suspicion fill, Combat chevron, officer diamond) stays exactly three;
// prominence here is duration + a WBP-side type-scale hook, not colour.
//
// WBP contract (all optional -- bind only what the design uses):
//   UTextBlock "HeadlineText" -- e.g. "OFFICER DOWN"
//   UTextBlock "DetailText"   -- e.g. "SQUAD MORALE -75"; collapsed when Detail is empty
//   UWidget    "BannerRoot"   -- the whole strip; opacity/visibility driven by the phase transitions
//                                below if bound, otherwise this widget's own root is used

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "AIOverlayTypes.h"
#include "AIOverlayBannerWidget.generated.h"

class UTextBlock;
class UWidget;

UCLASS(Abstract, Blueprintable)
class EXTRACTION_API UAIOverlayBannerWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** True while no event is live and this widget is safe to feed via PlayEvent. UAIOverlayLayer
	 *  checks this before every DequeueSquadEvent call -- a queued event stays queued (subsystem-side,
	 *  depth 3) until the banner is idle again. */
	bool IsIdle() const { return CurrentPhase == EBannerPhase::Idle; }

	/** Begins the hold+fade cycle for a freshly dequeued event. The caller (UAIOverlayLayer) is
	 *  expected to have checked IsIdle() first; calling this while an event is already live restarts
	 *  the cycle for the new event rather than queuing behind the old one -- the subsystem-side queue
	 *  is the only place events wait. */
	void PlayEvent(const FSquadOverlayEvent& Event);

	// --- State the WBP reads ---

	UPROPERTY(BlueprintReadOnly, Category = "AIOverlay|Banner")
	EOverlaySquadEventKind CurrentKind = EOverlaySquadEventKind::None;

	/** True only for the live OfficerDown beat -- drives the WBP's extra-prominence styling. */
	UPROPERTY(BlueprintReadOnly, Category = "AIOverlay|Banner")
	bool bEmphasis = false;

	// --- Events the WBP implements ---

	/** Fired at the start of the hold window. Play the entrance here. */
	UFUNCTION(BlueprintImplementableEvent, Category = "AIOverlay|Banner")
	void OnBannerEventShown(EOverlaySquadEventKind Kind, const FText& Headline, const FText& Detail, bool bInEmphasis);

	/** Fired when the hold window ends and the 0.4s fade begins. Play the fade-out animation here --
	 *  C++ tracks FadeDurationSeconds independently to know when IsIdle() becomes true again, so the
	 *  WBP animation length does not need to match exactly, only to look right within the window. */
	UFUNCTION(BlueprintImplementableEvent, Category = "AIOverlay|Banner")
	void OnBannerEventFading();

	/** Fired once the fade window elapses and this widget returns to Idle. */
	UFUNCTION(BlueprintImplementableEvent, Category = "AIOverlay|Banner")
	void OnBannerEventHidden();

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	// --- Bound widgets (designer wires in WBP; all optional) ---

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> HeadlineText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> DetailText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UWidget> BannerRoot;

	// --- Look ---

	UPROPERTY(EditDefaultsOnly, Category = "AIOverlay|Banner|Look")
	FLinearColor InkColor = FLinearColor(1.0f, 1.0f, 1.0f, 1.0f);

	UPROPERTY(EditDefaultsOnly, Category = "AIOverlay|Banner|Look")
	FLinearColor PlateColor = FLinearColor(0.02f, 0.02f, 0.02f, 0.92f);

	UPROPERTY(EditDefaultsOnly, Category = "AIOverlay|Banner|Look")
	FLinearColor KeylineColor = FLinearColor(1.0f, 1.0f, 1.0f, 0.4f);

	// --- Timing ---

	UPROPERTY(EditDefaultsOnly, Category = "AIOverlay|Banner|Timing", meta = (ClampMin = "0.1"))
	float HoldDurationSeconds = 2.5f;

	UPROPERTY(EditDefaultsOnly, Category = "AIOverlay|Banner|Timing", meta = (ClampMin = "0.05"))
	float FadeDurationSeconds = 0.4f;

	/** Hold duration substituted for OfficerDown specifically -- "the beat an entire filmed sequence
	 *  is built around" gets more time on screen than a routine sighting relay. */
	UPROPERTY(EditDefaultsOnly, Category = "AIOverlay|Banner|Timing", meta = (ClampMin = "0.1"))
	float OfficerDownHoldDurationSeconds = 4.0f;

private:
	enum class EBannerPhase : uint8 { Idle, Holding, Fading };

	void BeginFade();
	void EndCycle();
	void ApplyStaticColors();

	UFUNCTION()
	void HandleHoldExpired();

	UFUNCTION()
	void HandleFadeExpired();

	EBannerPhase CurrentPhase = EBannerPhase::Idle;
	FTimerHandle PhaseTimerHandle;
};
