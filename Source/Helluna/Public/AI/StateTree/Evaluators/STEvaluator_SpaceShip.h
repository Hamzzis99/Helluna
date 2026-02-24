/**
 * STEvaluator_SpaceShip.h
 *
 * StateTree Evaluator: 우주선 타겟 정보를 매 틱 갱신한다.
 *
 * EnrageLoop State의 ChaseTarget / AttackTarget Task가
 * 이 Evaluator의 SpaceShipData를 바인딩해서 사용하면
 * 광폭화 후 타겟 전환을 코드 없이 에디터 바인딩만으로 해결할 수 있다.
 *
 * @author 김민우
 */

#pragma once

#include "CoreMinimal.h"
#include "StateTreeEvaluatorBase.h"
#include "AI/StateTree/HellunaStateTreeTypes.h"
#include "STEvaluator_SpaceShip.generated.h"

class AAIController;

USTRUCT()
struct FSTEvaluator_SpaceShipInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AAIController> AIController = nullptr;

	/** 매 틱 갱신되는 우주선 타겟 정보 (Task에서 바인딩) */
	UPROPERTY(EditAnywhere, Category = "Output")
	FHellunaSpaceShipTargetData SpaceShipData;
};

USTRUCT(meta = (DisplayName = "Helluna: SpaceShip Target", Category = "Helluna|AI"))
struct HELLUNA_API FSTEvaluator_SpaceShip : public FStateTreeEvaluatorCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FSTEvaluator_SpaceShipInstanceData;

protected:
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual void TreeStart(FStateTreeExecutionContext& Context) const override;
	virtual void Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;

public:
	/** 우주선 액터에 붙어있는 태그 */
	UPROPERTY(EditAnywhere, Category = "Config",
		meta = (DisplayName = "우주선 태그"))
	FName SpaceShipTag = FName("SpaceShip");
};
