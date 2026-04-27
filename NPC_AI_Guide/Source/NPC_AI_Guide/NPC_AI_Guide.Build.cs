// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class NPC_AI_Guide : ModuleRules
{
	public NPC_AI_Guide(ReadOnlyTargetRules Target) : base(Target)
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
			// NPC AI Guide additions
			"Http",
			"Json",
			"JsonUtilities",
			"NavigationSystem"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });

		PublicIncludePaths.AddRange(new string[] {
			"NPC_AI_Guide",
			"NPC_AI_Guide/AI",
			"NPC_AI_Guide/Variant_Platforming",
			"NPC_AI_Guide/Variant_Platforming/Animation",
			"NPC_AI_Guide/Variant_Combat",
			"NPC_AI_Guide/Variant_Combat/AI",
			"NPC_AI_Guide/Variant_Combat/Animation",
			"NPC_AI_Guide/Variant_Combat/Gameplay",
			"NPC_AI_Guide/Variant_Combat/Interfaces",
			"NPC_AI_Guide/Variant_Combat/UI",
			"NPC_AI_Guide/Variant_SideScrolling",
			"NPC_AI_Guide/Variant_SideScrolling/AI",
			"NPC_AI_Guide/Variant_SideScrolling/Gameplay",
			"NPC_AI_Guide/Variant_SideScrolling/Interfaces",
			"NPC_AI_Guide/Variant_SideScrolling/UI"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
