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

	UPROPERTY()
	bool bTargetingPlayer = false;
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

	UPROPERTY(EditAnywhere, Category = "Config",
		meta = (DisplayName = "추적 포기 범위 (cm)", ClampMin = "100.0"))
	float LeashRange = 1500.f;

	UPROPERTY(EditAnywhere, Category = "Config",
		meta = (DisplayName = "우주선 태그"))
	FName SpaceShipTag = FName("SpaceShip");
};
