/**
 * STEvaluator_SpaceShip.h
 *
 * StateTree Evaluator: 우주선 위치 정보를 매 틱 갱신해서 Task에 제공한다.
 *
 * EnrageLoop State의 ChaseTarget / AttackTarget Task는
 * 이 Evaluator의 SpaceShipData를 바인딩해서 사용한다.
 * 우주선이 씬에 없거나 소멸된 경우 TargetActor가 null로 유지된다.
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

	/** 우주선 타겟 데이터 (Task에서 바인딩해서 읽음) */
	UPROPERTY(EditAnywhere, Category = "Output")
	FHellunaAITargetData SpaceShipData;
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
	/** 우주선 액터를 찾을 때 사용하는 태그 */
	UPROPERTY(EditAnywhere, Category = "Config",
		meta = (DisplayName = "우주선 태그"))
	FName SpaceShipTag = FName("SpaceShip");
};
