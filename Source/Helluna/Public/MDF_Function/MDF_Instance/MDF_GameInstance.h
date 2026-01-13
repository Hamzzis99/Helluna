#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "MDF_GameInstance.generated.h"

UCLASS()
class HELLUNA_API UMDF_GameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	// 이 변수가 true면 "맵 이동 중", false면 "새 게임/재시작"
	UPROPERTY(BlueprintReadWrite, Category = "Game Flow")
	bool bIsMapTransitioning = false;
};