// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Inventory : ModuleRules
{
	public Inventory(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicIncludePaths.AddRange(
			new string[] {
			}
			);
				
		PrivateIncludePaths.AddRange(
			new string[] {
			}
			);
			
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"NetCore",
				"StructUtils",
				"GameplayTags",
				"GameplayAbilities", // TODO: [독립화] 졸작 후 삭제. GAS 미사용 프로젝트에서도 플러그인 사용 가능하게.
			}
			);
			
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore", 
				"EnhancedInput", 
				"UMG",
				"InputCore",
			}
			);
		
		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
			}
			);
	}
}
