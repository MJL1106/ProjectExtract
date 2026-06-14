// Copyright Epic Games, Inc. All Rights Reserved.

#include "Extraction.h"
#include "Modules/ModuleManager.h"
#include "ExtractionTypes.h"

#if WITH_GAMEPLAY_DEBUGGER
#include "GameplayDebugger.h"
#include "GameplayDebuggerCategory_Enemy.h"
#endif

class FExtractionModule : public FDefaultGameModuleImpl
{
public:
	virtual void StartupModule() override
	{
#if WITH_GAMEPLAY_DEBUGGER
		IGameplayDebugger& GD = IGameplayDebugger::Get();
		GD.RegisterCategory(
			TEXT("Enemy"),
			IGameplayDebugger::FOnGetCategory::CreateStatic(&FGameplayDebuggerCategory_Enemy::MakeInstance),
			EGameplayDebuggerCategoryState::EnabledInGame);
		GD.NotifyCategoriesChanged();
#endif
	}

	virtual void ShutdownModule() override
	{
#if WITH_GAMEPLAY_DEBUGGER
		if (IGameplayDebugger::IsAvailable())
		{
			IGameplayDebugger& GD = IGameplayDebugger::Get();
			GD.UnregisterCategory(TEXT("Enemy"));
			GD.NotifyCategoriesChanged();
		}
#endif
	}
};

IMPLEMENT_PRIMARY_GAME_MODULE(FExtractionModule, Extraction, "Extraction");

DEFINE_LOG_CATEGORY(LogExtraction)

// --- Gameplay Tags ---
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Character_Enemy, "Character.Enemy", "Identifies enemy characters for AI targeting and combat systems");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Character_Companion, "Character.Companion", "Identifies companion characters for team identification and AI systems");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Character_Player, "Character.Player", "Identifies player characters for AI targeting and team systems");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_BT_EnemyCombat, "BT.EnemyCombat", "Dynamic subtree injection tag for enemy combat behaviour trees");
