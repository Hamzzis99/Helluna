// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Helluna : ModuleRules
{
    public Helluna(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        // 모듈 루트 폴더를 include 경로에 추가 (Helluna.h 접근용)
        PublicIncludePaths.Add(ModuleDirectory);

        PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput" , "StructUtils", 
            "GameplayAbilities", "GameplayTags","GameplayTasks", "AIModule", "NavigationSystem","AnimGraphRuntime", "MotionWarping",
            "DeveloperSettings", "UMG", "Slate","SlateCore", "MassEntity","MassSpawner","MassRepresentation","MassActors",
            "MassMovement","MassLOD", "MassCrowd", 
            
            //김기현이 따로 추가한 파일들
            "MeshDeformation", "Inventory", "Niagara", "GeometryFramework"
        });
        PrivateDependencyModuleNames.AddRange(new string[] { });

        // Uncomment if you are using Slate UI
        // PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

        // Uncomment if you are using online features
        // PrivateDependencyModuleNames.Add("OnlineSubsystem");

        // To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
    }
}