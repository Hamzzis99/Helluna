// MDF_GameStateInterface.h

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Components/MDF_DeformableComponent.h" 
#include "MDF_GameStateInterface.generated.h"

UINTERFACE(MinimalAPI)
class UMDF_GameStateInterface : public UInterface
{
	GENERATED_BODY()
};

class MESHDEFORMATION_API IMDF_GameStateInterface
{
	GENERATED_BODY()

public:
	// FGuid ID -> const FGuid& ID 로 변경 (참조 전달이 더 효율적)
	virtual void SaveMDFData(const FGuid& ID, const TArray<FMDFHitData>& History) = 0;
	virtual bool LoadMDFData(const FGuid& ID, TArray<FMDFHitData>& OutHistory) = 0;
};