// FExtractionEditorModule — registers/unregisters editor-only tooling:
//   - Companion route waypoint visualizer (component visualizer)
//   - WeaponCase "Case" property section (Details-panel filter pill)

#include "ExtractionEditorModule.h"
#include "CompanionRouteVisualizer.h"
#include "Companion/CompanionRouteVisComponent.h"
#include "Editor/UnrealEdEngine.h"
#include "UnrealEdGlobals.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "World/WeaponCase.h"

DEFINE_LOG_CATEGORY_STATIC(LogExtractionEditor, Log, All);

void FExtractionEditorModule::StartupModule()
{
	FPropertyEditorModule& PropEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
	TSharedRef<FPropertySection> CaseSection = PropEditor.FindOrCreateSection(
		AWeaponCase::StaticClass()->GetFName(), TEXT("Case"),
		NSLOCTEXT("Extraction", "WeaponCaseSection", "Case"));
	CaseSection->AddCategory(TEXT("Case"));

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
	if (FModuleManager::Get().IsModuleLoaded(TEXT("PropertyEditor")))
	{
		FPropertyEditorModule& PropEditor = FModuleManager::GetModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
		PropEditor.RemoveSection(AWeaponCase::StaticClass()->GetFName(), TEXT("Case"));
	}

	if (!GUnrealEd) return;

	GUnrealEd->UnregisterComponentVisualizer(UCompanionRouteVisComponent::StaticClass()->GetFName());
}

IMPLEMENT_MODULE(FExtractionEditorModule, ExtractionEditor)
