// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class HAI_Project_TEAM5 : ModuleRules
{
	public HAI_Project_TEAM5(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"UMG",
			"Slate"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });

		PublicIncludePaths.AddRange(new string[] {
			"HAI_Project_TEAM5",
			"HAI_Project_TEAM5/Variant_Platforming",
			"HAI_Project_TEAM5/Variant_Platforming/Animation",
			"HAI_Project_TEAM5/Variant_Combat",
			"HAI_Project_TEAM5/Variant_Combat/AI",
			"HAI_Project_TEAM5/Variant_Combat/Animation",
			"HAI_Project_TEAM5/Variant_Combat/Gameplay",
			"HAI_Project_TEAM5/Variant_Combat/Interfaces",
			"HAI_Project_TEAM5/Variant_Combat/UI",
			"HAI_Project_TEAM5/Variant_SideScrolling",
			"HAI_Project_TEAM5/Variant_SideScrolling/AI",
			"HAI_Project_TEAM5/Variant_SideScrolling/Gameplay",
			"HAI_Project_TEAM5/Variant_SideScrolling/Interfaces",
			"HAI_Project_TEAM5/Variant_SideScrolling/UI"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
