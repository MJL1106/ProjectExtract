// FExtractionEditorModule — registers/unregisters the companion route waypoint visualizer.

#include "ExtractionEditorModule.h"
#include "CompanionRouteVisualizer.h"
#include "Companion/CompanionRouteVisComponent.h"
#include "Editor/UnrealEdEngine.h"
#include "UnrealEdGlobals.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogExtractionEditor, Log, All);

void FExtractionEditorModule::StartupModule()
{
	if (!GUnrealEd)
	{
		UE_LOG(LogExtractionEditor, Warning,
			TEXT("GUnrealEd is null in StartupModule — component visualizers will not register. "
			     "Check that ExtractionEditor's LoadingPhase is PostEngineInit in the .uproject."));
		return;
	}

	GUnrealEd->RegisterComponentVisualizer(
		UCompanionRouteVisComponent::StaticClass()->GetFName(),
		MakeShareable(new FCompanionRouteVisualizer));
}

void FExtractionEditorModule::ShutdownModule()
{
	if (!GUnrealEd) return;

	GUnrealEd->UnregisterComponentVisualizer(UCompanionRouteVisComponent::StaticClass()->GetFName());
}

IMPLEMENT_MODULE(FExtractionEditorModule, ExtractionEditor)
