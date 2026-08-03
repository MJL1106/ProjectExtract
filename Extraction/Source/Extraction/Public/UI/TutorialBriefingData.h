// UTutorialBriefingData -- every line of copy shown on the controls briefing screen, in one asset.
//
// Asset-agnostic by construction: rows reference a UInputAction, never a key letter, so the briefing
// reads the player's live binding rather than a string that goes stale the moment anything is remapped.
// A row whose key genuinely cannot come from an action (the takedown confirm keys change with the
// prompt context) carries a literal KeyOverride instead.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "TutorialBriefingData.generated.h"

class UInputAction;

/**
 * One line of the briefing. Doubles as a column heading when bIsHeading is set, so a column is a
 * single flat array rather than a nested structure the designer has to keep in sync.
 */
USTRUCT(BlueprintType)
struct FTutorialControlRow
{
	GENERATED_BODY()

	/** What the control is called ("Move", "Stealth"). Headings use this for the heading text. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tutorial")
	FText Label;

	/** What it does / when to use it. Leave empty on simple rows — the row widget collapses the block. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tutorial", meta = (MultiLine = true))
	FText Detail;

	/** Optional. When set, the key text is resolved from the player's live binding at display time.
	 *  If the action's mapping context is not active (e.g. takedown confirms) and the resolve comes
	 *  back "[unbound]", KeyOverride is used as a fallback — so both fields can be set safely. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tutorial")
	TObjectPtr<UInputAction> Action;

	/** Literal key text. Used when Action is unset, OR when a set Action resolves to "[unbound]"
	 *  because its mapping context is not active at briefing time. Both fields may be populated:
	 *  Action wins whenever it resolves to a real key, KeyOverride catches the rest. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tutorial")
	FText KeyOverride;

	/** Renders as a group heading rather than a control row. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tutorial")
	bool bIsHeading = false;

	/** THE key resolution rule, in one place: if Action is set and resolves to a real key, use that;
	 *  if Action is unset or resolves to "[unbound]", use KeyOverride; if neither, empty (the row
	 *  widget collapses the key block). */
	FText ResolveKeyText(const UObject* WorldContextObject) const;
};

/**
 * Two columns of briefing copy: the basics on the left, the companion in depth on the right.
 */
UCLASS(BlueprintType)
class EXTRACTION_API UTutorialBriefingData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Optional screen title. Left empty, the WBP's own authored title text is kept. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tutorial|Header")
	FText Title;

	/** Optional screen subtitle. Left empty, the WBP's authored text is kept. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tutorial|Header")
	FText Subtitle;

	/** Optional confirm-button label. Left empty, the WBP's authored text is kept. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tutorial|Header")
	FText ConfirmLabel;

	/** Left column: movement, combat and gear basics. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tutorial|Basics", meta = (TitleProperty = "Label"))
	TArray<FTutorialControlRow> BasicsRows;

	/** Right column: companion modes and commands, grouped by heading rows. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tutorial|Companion", meta = (TitleProperty = "Label"))
	TArray<FTutorialControlRow> CompanionRows;
};
