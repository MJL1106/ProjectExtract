// ULevelFailedWidget -- full-screen failure overlay with a reason label and restart button.
// The WBP child provides layout/styling; this native base wires the restart call.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "LevelFailedWidget.generated.h"

class UTextBlock;
class UButton;

UCLASS()
class EXTRACTION_API ULevelFailedWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	ULevelFailedWidget(const FObjectInitializer& ObjectInitializer);

	/** Sets the player-facing failure reason displayed in ReasonText (if present). */
	UFUNCTION(BlueprintCallable, Category = "UI")
	void SetFailReason(const FText& Reason);

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	/** Optional title label (e.g. "MISSION FAILED"). Absent in the WBP is non-fatal. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TitleText;

	/** Optional reason label (e.g. "Your companion bled out"). Absent is non-fatal. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ReasonText;

	/** Required restart button. The WBP MUST contain a UButton named "RestartButton". */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> RestartButton;

private:
	UFUNCTION()
	void HandleRestartClicked();
};
