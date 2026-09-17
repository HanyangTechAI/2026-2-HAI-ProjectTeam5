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
			"Slate",

			// --- Learning Agents (RL) ---
			// LearningAgentsTraining 은 "Type": "Runtime" 이라 Shipping 에도 들어간다.
			// ASH_Arena 가 ALearningAgentsGymSimple 을 상속하고 학습/출고 양쪽에서 쓰이므로
			// 조건부로 묶으면 안 된다.
			"LearningAgents",
			"LearningAgentsTraining",
			// LearningAgentsTrainer.h (public) 가 LearningTrainer.h 를 include 하는데
			// LearningAgentsTraining 이 LearningTraining 을 private 로만 의존한다. 직접 추가해야 한다.
			"LearningTraining",
			"NavigationSystem",   // LearningAgentsTraining 의 public 의존
			"GameplayTags",       // 동일
			"PhysicsCore"         // 히트스캔 트레이스 채널
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });

		PublicIncludePaths.AddRange(new string[] {
			"HAI_Project_TEAM5",
			"HAI_Project_TEAM5/RL",
			"HAI_Project_TEAM5/Combat",
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
