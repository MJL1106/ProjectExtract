// AExtracteeAIController -- minimal AI controller for the extractee. No behavior tree, no
// perception: the character's C++ state machine drives MoveTo/focus directly on this controller.

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "ExtracteeAIController.generated.h"

UCLASS()
class EXTRACTION_API AExtracteeAIController : public AAIController
{
	GENERATED_BODY()

public:
	AExtracteeAIController();
};
