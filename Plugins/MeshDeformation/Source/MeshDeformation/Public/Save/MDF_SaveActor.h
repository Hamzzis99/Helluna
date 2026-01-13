// Gihyeon's Deformation Project (Helluna)
// MDF_SaveActor.h

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "Components/MDF_DeformableComponent.h" // FMDFHitData 구조체를 쓰기 위해 필수
#include "MDF_SaveActor.generated.h"

/**
 * [기술적 이유]
 * 언리얼의 UPROPERTY 시스템은 TMap의 Value로 TArray를 직접 넣는 것을 지원하지 않습니다.
 * 따라서 데이터를 한 번 감싸주는 래퍼(Wrapper) 구조체를 사용합니다.
 */
USTRUCT(BlueprintType)
struct FMDFHistoryWrapper
{
	GENERATED_BODY()

	// 1. 변형 히스토리 (기존)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MDF Data")
	TArray<FMDFHitData> History;
};

/**
 * [Step 11] 플러그인 전용 세이브 게임 클래스
 * 맵 이동이나 게임 종료 시 데이터를 디스크에 영구 보관하는 클래스입니다.
 */
// [주의] 만약 이 파일을 플러그인 폴더가 아니라 Source 폴더로 옮겼다면 
// MESHDEFORMATION_API를 HELLUNA_API로 변경해야 할 수도 있습니다. (에러 날 경우 수정)
UCLASS()
class MESHDEFORMATION_API UMDF_SaveActor : public USaveGame
{
	GENERATED_BODY()

public:
	// =========================================================================================
	// [기현 작업 영역] MDF 변형 데이터 저장소
	// Key: 컴포넌트 GUID (신분증) / Value: 찌그러짐 기록 (Wrapper)
	// =========================================================================================
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "MDF Save")
	TMap<FGuid, FMDFHistoryWrapper> SavedDeformationMap;

    
	// =========================================================================================
	// [팀원 가이드: 여기에 추가 데이터를 정의하세요]
	// 작성자: 김기현
	// 설명: 우주선 수리 진행도나 현재 페이즈(낮/밤)를 저장하고 싶다면 아래에 변수를 추가하세요.
	// =========================================================================================
    
	/* [예시 코드 - 주석을 풀고 사용하세요]
	
	// 1. 우주선 수리 진행도 (0 ~ 100)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Team Data")
	int32 SavedSpaceShipRepairProgress = 0;

	// 2. 현재 페이즈 (낮/밤) - Enum 타입이 있다면 사용 가능
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Team Data")
	int32 SavedCurrentPhaseIndex = 0; 
	
	*/
};