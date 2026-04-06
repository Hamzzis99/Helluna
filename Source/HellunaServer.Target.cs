// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class HellunaServerTarget : TargetRules
{
	public HellunaServerTarget( TargetInfo Target) : base(Target)
	{
		Type = TargetType.Server;
		DefaultBuildSettings = BuildSettingsVersion.V6;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_7;
		ExtraModuleNames.Add("Helluna");

		// 서버에서 오디오 에셋 제거 - USoundWave IO Store 직렬화 크래시 방지
		bCompileAgainstApplicationCore = true;
		DisablePlugins.Add("AudioCapture");
	}
}
