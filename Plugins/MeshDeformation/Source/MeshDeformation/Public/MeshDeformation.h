// Gihyeon's Deformation Project (Helluna)

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogMeshDeform, Log, All);

class FMeshDeformationModule : public IModuleInterface
{
public:

	/** 모듈이 메모리에 로드될 때 호출됩니다. */
	virtual void StartupModule() override;
    
	/** 모듈이 언로드될 때 호출됩니다. */
	virtual void ShutdownModule() override;
};