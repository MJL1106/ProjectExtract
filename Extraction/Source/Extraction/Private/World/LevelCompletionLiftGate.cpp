// ALevelCompletionLiftGate -- compilable stub (Task 5). Task 6 replaces the body.

#include "LevelCompletionLiftGate.h"
#include "Extraction.h"

ALevelCompletionLiftGate::ALevelCompletionLiftGate()
{
	PrimaryActorTick.bCanEverTick = false;
}

void ALevelCompletionLiftGate::UnlockExit()
{
	UE_LOG(LogExtraction, Log, TEXT("%s: UnlockExit called (stub -- Task 6 implements real logic)"), *GetName());
}
