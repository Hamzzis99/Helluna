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

            "DeveloperSettings", "UMG", "Slate","SlateCore", 

            //Mass 관련 모듈들
            "MassMovement","MassLOD", "MassCrowd","MassEntity","MassSpawner","MassRepresentation","MassActors", "MassSimulation",
            

            "DeveloperSettings", "UMG", "Slate","SlateCore", "MassEntity","MassSpawner","MassRepresentation","MassActors",
            "MassMovement","MassLOD", "MassCrowd",

            // === ECS 하이브리드 시스템용 모듈 (Mass Entity + Actor 전환) ===
            "MassCommon",         // Mass 공통 Fragment, 유틸리티 (FTransformFragment 등)
            "MassSimulation",     // Mass 시뮬레이션 서브시스템 (Processor 실행 관리)
            "MassSignals",        // Mass Signal 시스템 (StateTree 깨우기, 엔티티 간 신호)
            "MassAIBehavior",     // StateTree + Mass 연동 (UMassStateTreeTrait 등)
            "MassNavigation",     // Mass 네비게이션 (이동/회피)
            "StateTreeModule",    // StateTree 런타임 (StateTree 에셋 실행)
            "GameplayStateTreeModule", // StateTreeAIComponent (Actor용 StateTree)
            "NetCore",            // 네트워크 코어 (리플리케이션 기반 - 향후 MassReplication 대비)

            //김기현이 따로 추가한 파일들
            "MeshDeformation", "Inventory", "Niagara", "GeometryFramework",

            // [로비/창고 시스템] SQLite 백엔드 (Phase 1)
            // 엔진 내장 SQLiteCore 모듈 — 별도 설치 불필요
            // TODO: [SQL전환] REST API/PostgreSQL 전환 시 이 모듈 의존성을 교체
            "SQLiteCore"
        });
        PrivateDependencyModuleNames.AddRange(new string[] { });

        // Uncomment if you are using Slate UI
        // PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

        // Uncomment if you are using online features
        // PrivateDependencyModuleNames.Add("OnlineSubsystem");

        // To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
    }
}