// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class Helluna : ModuleRules
{
    public Helluna(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        // 모듈 루트 폴더를 include 경로에 추가 (Helluna.h 접근용)
        PublicIncludePaths.Add(ModuleDirectory);

        // [PCG] UBT가 PCG 모듈 include 경로를 자동 해석하지 못하므로 명시적 추가
        string EngineDir = Path.GetFullPath(Target.RelativeEnginePath);
        string PCGPublic = Path.Combine(EngineDir, "Plugins", "PCG", "Source", "PCG", "Public");
        PublicIncludePaths.Add(PCGPublic);

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
            "SQLiteCore",

            // [로비/창고 시스템] JSON 직렬화 (부착물 attachments_json 컬럼용)
            "Json", "JsonUtilities",

            // [Phase 12g] 클립보드 복사 (파티 코드)
            "ApplicationCore",

            // [PCG] 밤 시작 시 런타임 PCG 생성 (장애물/환경 스폰)
            "PCG",

            // [패키징debug] PCGLandscapeCache 검증 (ALandscapeProxy / ULandscapeInfo / ULandscapeComponent)
            "Landscape",

            // [카메라 쉐이크] PerlinNoiseCameraShakePattern 사용
            "EngineCameras",

            // [그래픽 설정] RHI (GPU 벤더 감지 + GGPUFrameTime)
            "RHI",

            // [그래픽 설정] DLSS/Streamline/FSR Blueprint API
            "DLSSBlueprint",
            "StreamlineBlueprint",
            "StreamlineDLSSGBlueprint",
            "StreamlineReflexBlueprint"
        });
        PrivateDependencyModuleNames.AddRange(new string[] { "AssetRegistry" });

        // [TimeDistortion] 콘솔 커맨드로 PP 머티리얼 자동 생성 (에디터 전용)
        if (Target.bBuildEditor)
        {
            PrivateDependencyModuleNames.AddRange(new string[] { "UnrealEd", "MaterialEditor" });
        }

        // Uncomment if you are using Slate UI
        // PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

        // Uncomment if you are using online features
        // PrivateDependencyModuleNames.Add("OnlineSubsystem");

        // To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
    }
}