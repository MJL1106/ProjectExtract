// UCompanionRouteVisComponent — trivial editor-only marker, see header.

#include "Companion/CompanionRouteVisComponent.h"

UCompanionRouteVisComponent::UCompanionRouteVisComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	bIsEditorOnly = true;
}
