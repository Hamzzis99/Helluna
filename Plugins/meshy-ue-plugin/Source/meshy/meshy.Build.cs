// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;
using System;

public class meshy : ModuleRules
{
	public meshy(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		// 版本兼容性宏 - 支持 UE 5.6 和 5.7
		// 使用 ENGINE_MAJOR_VERSION 和 ENGINE_MINOR_VERSION 进行条件编译
		// 这些宏在引擎中已经定义，可以直接在 C++ 代码中使用
		
		PublicIncludePaths.AddRange(
			new string[] {
				"Runtime/Core/Public",
				"Runtime/CoreUObject/Public",
				"Runtime/Engine/Classes",
				"Runtime/Slate/Public",
				"Runtime/SlateCore/Public",
				"Runtime/AssetRegistry/Public",
				"Runtime/AssetTools/Public",
				"Runtime/Json/Public",
				"Runtime/JsonUtilities/Public",
				"Runtime/Networking/Public",
				"Runtime/Sockets/Public"
			}
		);
				
		
		PrivateIncludePaths.AddRange(
			new string[] {
				"Editor/UnrealEd/Public",
				"Editor/UnrealEd/Private",
				"Editor/EditorStyle/Public",
				"Editor/LevelEditor/Public",
				"Editor/LevelEditor/Private",
				"Editor/ToolMenus/Public",
				"Editor/ToolMenus/Private"
			}
		);
			
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"Projects",
				"Json",
				"Sockets",
				"Networking",
				"HTTP",
				"PakFile",
				"zlib",  // 添加zlib模块依赖
                // 添加UI相关模块
                "SlateCore",
                "Slate", 
                "UMG"
			}
		);
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
				"InputCore",
				"UnrealEd",
				"AssetTools",
				"AssetRegistry",
				"LevelEditor",
				"EditorStyle",
				"ToolMenus", 
                "ApplicationCore" // 添加ApplicationCore以支持Slate UI相关功能
			}
		);

		// 添加插件依赖
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"UnrealEd",
				"AssetTools"
			}
		);

		// 配置minizip支持 - 跨平台方案
		ConfigureMinizipSupport(Target);
	}
	
	private void ConfigureMinizipSupport(ReadOnlyTargetRules Target)
	{
		// 使用UE5内置的zlib和minizip
		string ZlibPath = Path.Combine(EngineDirectory, "Source", "ThirdParty", "zlib");
		
		if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            // Windows配置
            string ZlibIncludePath = Path.Combine(ZlibPath, "1.3", "include");
            string MinizipIncludePath = Path.Combine(ZlibPath, "1.3", "include", "minizip");
            
            PublicSystemIncludePaths.Add(ZlibIncludePath);
            PublicSystemIncludePaths.Add(MinizipIncludePath);
            
            // 确保链接zlib
            PublicDefinitions.Add("HAVE_ZLIB=1");
        }
        else if (Target.Platform == UnrealTargetPlatform.Mac)
        {
            // Mac配置
            string ZlibIncludePath = Path.Combine(ZlibPath, "1.3", "include");
            string MinizipIncludePath = Path.Combine(ZlibPath, "1.3", "include", "minizip");
            
            PublicSystemIncludePaths.Add(ZlibIncludePath);
            PublicSystemIncludePaths.Add(MinizipIncludePath);
            
            // Mac上的额外配置
            PublicDefinitions.Add("HAVE_ZLIB=1");
            PublicDefinitions.Add("HAVE_MINIZIP=1");
        }
        else
        {
            // 其他平台不支持
            System.Console.WriteLine("Warning: Meshy plugin only supports Windows and Mac platforms.");
        }
	}
}
