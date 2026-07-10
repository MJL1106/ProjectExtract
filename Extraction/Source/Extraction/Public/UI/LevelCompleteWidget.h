// ULevelCompleteWidget -- full-screen completion overlay with a restart button.
// The WBP child provides layout/styling; this native base wires the restart call.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "LevelCompleteWidget.generated.h"

class UTextBlock;
class UButton;

UCLASS()
class EXTRACTION_API ULevelCompleteWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	ULevelCompleteWidget(const FObjectInitializer& ObjectInitializer);

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	/** Optional title label (e.g. "LEVEL COMPLETE"). Absent in the WBP is non-fatal. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TitleText;

	/** Required restart button. The WBP MUST contain a UButton named "RestartButton". */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> RestartButton;

private:
	UFUNCTION()
	void HandleRestartClicked();
};
