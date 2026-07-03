// Editor-only utility: programmatically creates/overwrites four cover EQS query assets.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CoverEQSBuilder.generated.h"

UCLASS()
class EXTRACTION_API UCoverEQSBuilder : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	/**
	 * Build (or overwrite) the four cover EQS query assets in /Game/Core/AI/Cover/.
	 * Editor-only -- returns false at runtime.
	 * @param OutReport  Per-asset status lines.
	 */
	UFUNCTION(BlueprintCallable, Category = "AI|Cover")
	static bool BuildCoverQueries(FString& OutReport);
};
