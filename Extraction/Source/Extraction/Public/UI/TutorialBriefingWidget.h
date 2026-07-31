// UTutorialBriefingWidget -- the full-screen controls briefing shown at the start of a tutorial map.
// The WBP child owns all layout, styling and the near-opaque backdrop that covers the HUD; this native
// base builds the two columns from a UTutorialBriefingData asset and routes the confirm click back to
// the player controller.
//
// Key text is resolved live from the player's bindings, which is why this subscribes to
// ControlMappingsRebuiltDelegate: the briefing is created during level start, before Enhanced Input
// has rebuilt its mapping table, so the first resolve legitimately comes back empty-handed. See the
// timing note in KeybindHintLibrary.h.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "TutorialBriefingWidget.generated.h"

class UButton;
class UEnhancedInputLocalPlayerSubsystem;
class UTextBlock;
class UTutorialBriefingData;
class UTutorialControlRowWidget;
class UVerticalBox;
struct FTutorialControlRow;

UCLASS()
class EXTRACTION_API UTutorialBriefingWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UTutorialBriefingWidget(const FObjectInitializer& ObjectInitializer);

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	/** Required. The only way out of the briefing. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> ConfirmButton;

	/** Required. Left column container — movement, combat and gear basics.
	 *  Must be left EMPTY in the WBP: all children are generated at runtime and ClearChildren()
	 *  wipes any designer-authored static children on every rebuild. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UVerticalBox> BasicsBox;

	/** Required. Right column container — companion modes and commands.
	 *  Must be left EMPTY in the WBP: same ClearChildren() caveat as BasicsBox. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UVerticalBox> CompanionBox;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TitleText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> SubtitleText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ConfirmLabelText;

	/** Row widget class (assigned in BP defaults — no C++ asset path). */
	UPROPERTY(EditDefaultsOnly, Category = "UI|Tutorial")
	TSubclassOf<UTutorialControlRowWidget> RowWidgetClass;

	/** Briefing copy (assigned in BP defaults — no C++ asset path). */
	UPROPERTY(EditDefaultsOnly, Category = "UI|Tutorial")
	TObjectPtr<UTutorialBriefingData> BriefingData;

private:
	/** Builds both columns, then applies the current key text to every row. */
	void SyncColumns();

	/** Reuses the column's existing row widgets when the row count still matches — a mapping rebuild
	 *  changes key text only, so there is nothing to gain from tearing the column down and rebuilding. */
	void SyncColumn(UVerticalBox* Box, const TArray<FTutorialControlRow>& Rows, TArray<TObjectPtr<UTutorialControlRowWidget>>& RowWidgets);

	void CreateColumnRows(UVerticalBox* Box, int32 RowCount, TArray<TObjectPtr<UTutorialControlRowWidget>>& RowWidgets);

	/** Pushes Title/Subtitle/ConfirmLabel from the data asset, leaving WBP-authored text alone where
	 *  the asset supplies nothing. */
	void ApplyHeaderCopy();

	UFUNCTION()
	void HandleConfirmClicked();

	UFUNCTION()
	void HandleControlMappingsRebuilt();

	UPROPERTY()
	TArray<TObjectPtr<UTutorialControlRowWidget>> BasicsRowWidgets;

	UPROPERTY()
	TArray<TObjectPtr<UTutorialControlRowWidget>> CompanionRowWidgets;

	/** Non-owning — the subsystem outlives the widget in practice, but the widget must be able to
	 *  unsubscribe from whatever it actually subscribed to without assuming it is still there. */
	TWeakObjectPtr<UEnhancedInputLocalPlayerSubsystem> BoundInputSubsystem;
};
