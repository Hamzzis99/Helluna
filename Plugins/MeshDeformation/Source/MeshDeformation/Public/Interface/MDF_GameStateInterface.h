#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Components/MDF_DeformableComponent.h" // FMDFHitData 구조체 인식을 위해 필요
#include "MDF_GameStateInterface.generated.h"

// [Step 9] 언리얼 리플렉션 시스템용 빈 클래스 (UInterface 규칙)
UINTERFACE(MinimalAPI)
class UMDF_GameStateInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * [Step 9: 인터페이스 규약]
 * 이 인터페이스를 상속받은 GameState는 "변형 데이터 저장소" 역할을 수행할 수 있습니다.
 * 플러그인은 이 인터페이스를 통해 게임(Game) 프로젝트의 GameState와 대화합니다.
 */
class MESHDEFORMATION_API IMDF_GameStateInterface
{
	GENERATED_BODY()

public:
	// 1. 데이터를 저장해달라고 요청하는 함수
	virtual void SaveMDFData(const FGuid& ID, const TArray<FMDFHitData>& Data) = 0;

	// 2. 데이터를 불러달라고 요청하는 함수
	virtual bool LoadMDFData(const FGuid& ID, TArray<FMDFHitData>& OutData) = 0;
};