// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Extraction : ModuleRules
{
	public Extraction(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AnimGraphRuntime",
			"UMG",
			"Slate",
			"SlateCore",
			"GameplayTags",
			"AIModule",
			"GameplayTasks",
			"NavigationSystem"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });

		// Public subfolder include paths
		PublicIncludePaths.AddRange(new string[] {
			"Extraction/Public/Core",
			"Extraction/Public/Character",
			"Extraction/Public/Animation",
			"Extraction/Public/Game",
			"Extraction/Public/Components",
			"Extraction/Public/UI",
			"Extraction/Public/Data",
			"Extraction/Public/Weapon",
			"Extraction/Public/Enemy",
			"Extraction/Public/Companion",
			"Extraction/Public/AI",
			"Extraction/Public/AI/BTS",
			"Extraction/Public/AI/Tasks",
			"Extraction/Public/AI/EQS"
		});

		// Private subfolder include paths
		PrivateIncludePaths.AddRange(new string[] {
			"Extraction/Private/Core",
			"Extraction/Private/Character",
			"Extraction/Private/Animation",
			"Extraction/Private/Game",
			"Extraction/Private/Components",
			"Extraction/Private/UI",
			"Extraction/Private/Data",
			"Extraction/Private/Weapon",
			"Extraction/Private/Enemy",
			"Extraction/Private/Companion",
			"Extraction/Private/AI",
			"Extraction/Private/AI/BTS",
			"Extraction/Private/AI/Tasks",
			"Extraction/Private/AI/EQS"
		});
	}
}
