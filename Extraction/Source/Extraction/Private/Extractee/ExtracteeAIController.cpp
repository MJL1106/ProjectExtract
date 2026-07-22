#include "Extractee/ExtracteeAIController.h"

AExtracteeAIController::AExtracteeAIController()
{
	// Focus-driven facing: the character toggles SetFocus/ClearFocus per state and reads the
	// resulting control rotation through bUseControllerDesiredRotation.
	bSetControlRotationFromPawnOrientation = false;
}
