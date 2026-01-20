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

	// 2. [New] 저장된 체력 (HP)
	// 기본값 0.0f (로드 시 데이터가 없으면 0으로 초기화됨을 방지하기 위해 로직에서 처리 필요)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MDF Data")
	float SavedHP = 0.0f;
};

/**
 * [Step 11] 플러그인 전용 세이브 게임 클래스
 * 맵 이동이나 게임 종료 시 데이터를 디스크에 영구 보관하는 클래스입니다.
 */
// [주의] 컴파일 에러 시 MESHDEFORMATION_API를 프로젝트 설정에 맞는 API명(예: MDF_API)으로 변경하세요.
UCLASS()
class MESHDEFORMATION_API UMDF_SaveActor : public USaveGame
{
	GENERATED_BODY()

public:
	/** * 저장된 변형 데이터 장부
	 * Key: 컴포넌트 GUID (신분증)
	 * Value: 히스토리 + 체력 데이터 (Wrapper로 포장됨)
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "MDF Save")
	TMap<FGuid, FMDFHistoryWrapper> SavedDeformationMap;
};