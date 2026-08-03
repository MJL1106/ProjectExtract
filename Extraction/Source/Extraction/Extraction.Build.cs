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
			"NavigationSystem",
			"AICoverSystem",
			"Niagara",
			"DeveloperSettings",
			"PhysicsCore"
		});

		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(new string[] {
				"UnrealEd",
				"AssetTools",
				"AssetRegistry"
			});
		}

		SetupGameplayDebuggerSupport(Target);

		// Public subfolder include paths
		PublicIncludePaths.AddRange(new string[] {
			"Extraction/Public/Commandlets",
			"Extraction/Public/Core",
			"Extraction/Public/Character",
			"Extraction/Public/Animation",
			"Extraction/Public/Game",
			"Extraction/Public/Components",
			"Extraction/Public/Movement",
			"Extraction/Public/UI",
			"Extraction/Public/Data",
			"Extraction/Public/Weapon",
			"Extraction/Public/Enemy",
			"Extraction/Public/Enemy/Components",
			"Extraction/Public/Enemy/Debug",
			"Extraction/Public/Enemy/Squad",
			"Extraction/Public/Enemy/Director",
			"Extraction/Public/Companion",
			"Extraction/Public/AI",
			"Extraction/Public/AI/BTS",
			"Extraction/Public/AI/Tasks",
			"Extraction/Public/AI/EQS",
			"Extraction/Public/AI/Navigation",
			"Extraction/Public/AI/Cover",
			"Extraction/Public/World",
			"Extraction/Public/Extractee",
			"Extraction/Public/Audio"
		});

		// Private subfolder include paths
		PrivateIncludePaths.AddRange(new string[] {
			"Extraction/Private/Commandlets",
			"Extraction/Private/Core",
			"Extraction/Private/Character",
			"Extraction/Private/Animation",
			"Extraction/Private/Game",
			"Extraction/Private/Components",
			"Extraction/Private/Movement",
			"Extraction/Private/UI",
			"Extraction/Private/Data",
			"Extraction/Private/Weapon",
			"Extraction/Private/Enemy",
			"Extraction/Private/Enemy/Components",
			"Extraction/Private/Enemy/Debug",
			"Extraction/Private/Enemy/Squad",
			"Extraction/Private/Enemy/Director",
			"Extraction/Private/Companion",
			"Extraction/Private/AI",
			"Extraction/Private/AI/BTS",
			"Extraction/Private/AI/Tasks",
			"Extraction/Private/AI/EQS",
			"Extraction/Private/AI/Navigation",
			"Extraction/Private/AI/Cover",
			"Extraction/Private/World",
			"Extraction/Private/Extractee",
			"Extraction/Private/Tests",
			"Extraction/Private/Audio"
		});
	}
}
