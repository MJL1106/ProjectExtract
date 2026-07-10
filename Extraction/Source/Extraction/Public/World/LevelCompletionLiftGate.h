// ALevelCompletionLiftGate -- compilable stub for Task 5; full implementation in Task 6.
// The extraction target calls UnlockExit() on wave completion (EWaveCompletionAction::UnlockExit).

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LevelCompletionLiftGate.generated.h"

UCLASS()
class EXTRACTION_API ALevelCompletionLiftGate : public AActor
{
	GENERATED_BODY()

public:
	ALevelCompletionLiftGate();

	/** Unlock the exit gate. Task 6 implements the real logic; this stub just logs. */
	UFUNCTION(BlueprintCallable, Category = "LevelCompletion")
	void UnlockExit();
};
