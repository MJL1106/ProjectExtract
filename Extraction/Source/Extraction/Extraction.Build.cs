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
			"Slate"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });

		// Public subfolder include paths
		PublicIncludePaths.AddRange(new string[] {
			"Extraction/Public/Core",
			"Extraction/Public/Character",
			"Extraction/Public/Animation",
			"Extraction/Public/Game"
		});

		// Private subfolder include paths
		PrivateIncludePaths.AddRange(new string[] {
			"Extraction/Private/Core",
			"Extraction/Private/Character",
			"Extraction/Private/Animation",
			"Extraction/Private/Game"
		});
	}
}
