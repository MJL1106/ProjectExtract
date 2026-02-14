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
			"UMG",
			"Slate"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });
	}
}
