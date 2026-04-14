// Capstone Project Helluna

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "TimeDistortionMaterialGenerator.generated.h"

/**
 * TimeDistortion PP 머티리얼 자동 생성 유틸리티.
 * Editor에서 한 번만 호출하면 M_PP_TimeDistortion 머티리얼 에셋이 생성된다.
 */
UCLASS()
class HELLUNA_API UTimeDistortionMaterialGenerator : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * 채도 제거 Post Process 머티리얼을 자동 생성한다.
	 * /Game/BossEvent/Materials/M_PP_TimeDistortion 경로에 저장.
	 * 이미 존재하면 기존 에셋을 반환한다.
	 *
	 * 에디터 콘솔: Helluna.GenerateTimeDistortionPP
	 */
	UFUNCTION(BlueprintCallable, Category = "TimeDistortion|Editor", meta = (CallInEditor = "true"))
	static UMaterialInterface* GenerateDesaturationMaterial();
};
