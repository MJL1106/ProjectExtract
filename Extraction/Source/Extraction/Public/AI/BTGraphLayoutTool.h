// Editor-only dev utility — repositions BehaviorTree editor-graph nodes programmatically.
// The BT graph's NodePosX/Y have no edit specifier, so scripting layers (Python/NeoStack) cannot
// touch them; this is the only scriptable path (invoke via NeoStack reflection, like CoverEQSBuilder).
// ⚠ X-position defines child execution order in BT graphs — callers must preserve relative X order
// within each parent or the next graph rebuild will reorder children.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "BTGraphLayoutTool.generated.h"

class UBehaviorTree;

UCLASS()
class EXTRACTION_API UBTGraphLayoutTool : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	/** Applies a layout spec to a BehaviorTree asset's editor graph.
	 *  TreeAssetPath: object path of the BT asset (passed by the caller — not hardcoded here).
	 *  LayoutSpec: semicolon-separated moves, each "NodeName@curX,curY->newX,newY". The @curX,curY
	 *  clause is optional ("NodeName->newX,newY") and disambiguates duplicate node names by their
	 *  CURRENT position (exact match). Returns the number of nodes moved; the asset is marked dirty
	 *  (caller saves). */
	UFUNCTION(BlueprintCallable, Category = "Extraction|DevTools")
	static int32 ApplyBTGraphLayout(const FString& TreeAssetPath, const FString& LayoutSpec);
#endif
};
