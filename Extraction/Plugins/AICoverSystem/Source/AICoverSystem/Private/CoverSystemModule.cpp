//// Copyright, (c) Sami Kangasmaa 2022

#include "CoverSystemModule.h"

#define LOCTEXT_NAMESPACE "FAICoverSystemModule"

DEFINE_LOG_CATEGORY(LogCoverSystem);

void FAICoverSystemModule::StartupModule()
{
}

void FAICoverSystemModule::ShutdownModule()
{
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FAICoverSystemModule, AICoverSystem)