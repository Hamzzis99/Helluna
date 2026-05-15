// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class HellunaClientTarget : TargetRules
{
	public HellunaClientTarget( TargetInfo Target) : base(Target)
	{
		Type = TargetType.Client;
		DefaultBuildSettings = BuildSettingsVersion.V6;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_7;
		ExtraModuleNames.Add("Helluna");

		// [Phase21-C][Diag] Shipping 빌드에서도 진단 로그 보존
		bUseLoggingInShipping = true;
	}
}
