// Editor module for ProjectExtract — registers component visualizers and other editor-only
// tooling that must not live in the runtime Extraction module.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

class FExtractionEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
