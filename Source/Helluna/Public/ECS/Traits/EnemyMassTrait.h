/**
 * EnemyMassTrait.h
 *
 * 하이브리드 ECS 시스템의 Trait 정의.
 * MassEntityConfig 에셋의 Traits 배열에 추가하여 에디터에서 설정한다.
 *
 * 역할:
 * - 에디터에서 적 클래스(EnemyClass)와 전환 거리(SpawnThreshold)를 설정
 * - BuildTemplate에서 FEnemySpawnStateFragment(상태 추적)와
 *   FEnemyDataFragment(설정 데이터)를 Mass Entity 아키타입에 등록
 * - AddFragment_GetRef<T>()로 에디터 UPROPERTY 값을 Fragment에 주입
 *
 * ※ UMassEntityTraitBase는 MassSpawner 모듈의 "MassEntityTraitBase.h"에 정의.
 * ※ BuildTemplate 시그니처 (UE 5.7.2):
 *   virtual void BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const override;
 */

// File: Source/Helluna/Public/ECS/Traits/EnemyMassTrait.h

#pragma once

#include "CoreMinimal.h"
#include "MassEntityTraitBase.h"
#include "EnemyMassTrait.generated.h"

// 전방선언
class AHellunaEnemyCharacter;
struct FMassEntityTemplateBuildContext;

/**
 * UEnemyMassTrait
 *
 * MassEntityConfig 에셋에서 에디터로 설정하는 커스텀 Trait.
 * Mass Entity 아키타입에 적 스폰 관련 Fragment를 추가한다.
 */
UCLASS(DisplayName = "Enemy Mass Trait (ECS 적 스폰)")
class HELLUNA_API UEnemyMassTrait : public UMassEntityTraitBase
{
	GENERATED_BODY()

public:
	/**
	 * BuildTemplate: Mass Entity 아키타입에 Fragment를 등록하는 핵심 함수.
	 * UE 5.7.2 시그니처: BuildContext + const UWorld& World + const 한정자 필수.
	 */
	virtual void BuildTemplate(
		FMassEntityTemplateBuildContext& BuildContext,
		const UWorld& World) const override;

protected:
	/** 스폰할 적 블루프린트 클래스. 에디터에서 BP_HellunaEnemyCharacter 등을 지정 */
	UPROPERTY(EditAnywhere, Category = "Enemy Spawn Config",
		meta = (DisplayName = "Enemy Class (적 블루프린트)", AllowAbstract = "false"))
	TSubclassOf<AHellunaEnemyCharacter> EnemyClass;

	/** Actor 전환 거리 임계값 (단위: cm). 기본값 8000cm = 80m */
	UPROPERTY(EditAnywhere, Category = "Enemy Spawn Config",
		meta = (DisplayName = "Spawn Threshold (전환 거리 cm)", ClampMin = "100.0", ClampMax = "100000.0", UIMin = "100.0", UIMax = "50000.0"))
	float SpawnThreshold = 8000.f;
};
