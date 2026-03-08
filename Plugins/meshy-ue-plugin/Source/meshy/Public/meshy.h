// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "Misc/EngineVersionComparison.h"

// UE 5.6/5.7 版本兼容性宏
#ifndef UE_5_07_OR_LATER
    #if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
        #define UE_5_07_OR_LATER 1
    #else
        #define UE_5_07_OR_LATER 0
    #endif
#endif

class FToolBarBuilder;
class FMenuBuilder;

class MESHY_API FMeshyModule : public IModuleInterface
{
public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	
	/** 切换 Meshy Bridge 服务器状态 */
	void ToggleMeshyBridge();
	
	/** 检查 Meshy Bridge 服务器是否运行中 */
	bool IsMeshyBridgeRunning() const;
	
private:
	void RegisterMenus();

private:
	TSharedPtr<class FUICommandList> PluginCommands;
};
