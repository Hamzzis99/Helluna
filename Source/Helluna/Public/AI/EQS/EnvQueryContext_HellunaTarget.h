/**
 * EnvQueryContext_HellunaTarget.h
 *
 * EQS Context: AI가 현재 추적 중인 타겟(플레이어 또는 우주선)을 쿼리 기준점으로 제공.
 * Generator에서 "타겟 주변" 점을 생성할 때 이 Context를 기준으로 사용한다.
 *
 * 사용 방법:
 *   EQS 에셋의 Generator → Center → EnvQueryContext_HellunaTarget 선택
 */

#pragma once

#include "CoreMinimal.h"
#include "EnvironmentQuery/EnvQueryContext.h"
#include "EnvQueryContext_HellunaTarget.generated.h"

UCLASS()
class HELLUNA_API UEnvQueryContext_HellunaTarget : public UEnvQueryContext
{
	GENERATED_BODY()

public:
	/**
	 * EQS 엔진이 호출: 이 Context가 나타내는 Actor 목록을 반환.
	 * Querier(쿼리를 실행한 AI 폰)의 ASC 또는 StateTree에서
	 * 현재 추적 타겟을 꺼내 OutActors에 추가한다.
	 */
	virtual void ProvideContext(FEnvQueryInstance& QueryInstance,
		FEnvQueryContextData& ContextData) const override;
};
