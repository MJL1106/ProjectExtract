//// Copyright, (c) Sami Kangasmaa 2022

#pragma once

#include "CoreMinimal.h"
#include "CoverSystemVersion.h"
#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogCoverSystem, Log, All);

class FAICoverSystemModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

};
