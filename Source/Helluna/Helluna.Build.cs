// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Helluna : ModuleRules
{
    public Helluna(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput" ,
            "GameplayAbilities", "GameplayTags","GameplayTasks", "AIModule", "NavigationSystem","AnimGraphRuntime", "MotionWarping",
            "DeveloperSettings", "Inventory", "UMG","Slate","SlateCore", "MassEntity","MassSpawner","MassRepresentation","MassActors",
            "MassMovement","MassLOD", "MassCrowd", "MeshDeformation", "Inventory"
        });
        PrivateDependencyModuleNames.AddRange(new string[] { });

        // Uncomment if you are using Slate UI
        // PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

        // Uncomment if you are using online features
        // PrivateDependencyModuleNames.Add("OnlineSubsystem");

        // To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
    }
}