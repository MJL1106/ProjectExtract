// Copyright Epic Games, Inc. All Rights Reserved.

#include "Extraction.h"
#include "Modules/ModuleManager.h"
#include "ExtractionTypes.h"

IMPLEMENT_PRIMARY_GAME_MODULE( FDefaultGameModuleImpl, Extraction, "Extraction" );

DEFINE_LOG_CATEGORY(LogExtraction)

// --- Gameplay Tags ---
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Character_Enemy, "Character.Enemy", "Identifies enemy characters for AI targeting and combat systems");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Character_Companion, "Character.Companion", "Identifies companion characters for team identification and AI systems");