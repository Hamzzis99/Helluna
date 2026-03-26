/**
 * EnvQueryContext_SpaceShip.h
 *
 * EQS Context: 우주선 Actor를 쿼리 기준점으로 제공.
 * Generator에서 "우주선 주변" 점을 생성할 때 이 Context를 기준으로 사용한다.
 *
 * 사용 방법:
 *   EQS 에셋의 Generator -> Center -> EnvQueryContext_SpaceShip 선택
 *
 * 우주선 탐색 방식:
 *   1순위: AIController의 FocusActor가 우주선 태그를 가지고 있으면 사용
 *   2순위: 월드에서 SpaceShipTag로 Actor 탐색
 *
 * @author 김민우
 */

#pragma once

#include "CoreMinimal.h"
#include "EnvironmentQuery/EnvQueryContext.h"
#include "EnvQueryContext_SpaceShip.generated.h"

UCLASS()
class HELLUNA_API UEnvQueryContext_SpaceShip : public UEnvQueryContext
{
	GENERATED_BODY()

public:
	virtual void ProvideContext(FEnvQueryInstance& QueryInstance,
		FEnvQueryContextData& ContextData) const override;

	/** 우주선 Actor를 찾을 때 사용하는 태그 */
	UPROPERTY(EditDefaultsOnly, Category = "설정",
		meta = (DisplayName = "우주선 태그"))
	FName SpaceShipTag = FName("SpaceShip");
};