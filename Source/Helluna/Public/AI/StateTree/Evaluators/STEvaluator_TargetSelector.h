/**
 * STEvaluator_TargetSelector.h
 *
 * StateTree Evaluator: 몬스터 AI의 타겟 선택 및 어그로 관리
 *
 * @author 김민우
 */

#pragma once

#include "CoreMinimal.h"
#include "StateTreeEvaluatorBase.h"
#include "AI/StateTree/HellunaStateTreeTypes.h"
#include "STEvaluator_TargetSelector.generated.h"

class AAIController;

USTRUCT()
struct FSTEvaluator_TargetSelectorInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AAIController> AIController = nullptr;

	UPROPERTY(EditAnywhere, Category = "Output")
	FHellunaAITargetData TargetData;

	UPROPERTY()
	TWeakObjectPtr<AActor> DamagedByActor = nullptr;
};

USTRUCT(meta = (DisplayName = "Helluna: Target Selector", Category = "Helluna|AI"))
struct HELLUNA_API FSTEvaluator_TargetSelector : public FStateTreeEvaluatorCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FSTEvaluator_TargetSelectorInstanceData;

protected:
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual void Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;

public:
	UPROPERTY(EditAnywhere, Category = "Config",
		meta = (DisplayName = "어그로 범위 (cm)", ClampMin = "100.0"))
	float AggroRange = 800.f;

	/**
	 * 플레이어를 타겟으로 삼기 시작한 후 광폭화 이벤트를 발송할 때까지의 대기 시간 (초).
	 * 이 시간이 경과하면 StateTree에 Enemy.Event.Enrage 이벤트를 보내
	 * Enrage State로의 Transition을 트리거한다.
	 * 0 으로 설정하면 플레이어 탐지 즉시 광폭화.
	 */
	UPROPERTY(EditAnywhere, Category = "Config",
		meta = (DisplayName = "광폭화 시작 시간 (초)", ClampMin = "0.0"))
	float EnrageDelay = 5.f;
};
