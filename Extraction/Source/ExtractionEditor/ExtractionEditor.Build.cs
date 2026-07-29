// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ExtractionEditor : ModuleRules
{
	public ExtractionEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"Slate",
			"SlateCore",
			"UnrealEd",
			"InputCore",
			"Extraction",
			"PropertyEditor"
		});
	}
}
