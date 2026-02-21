/**
 * STCondition_InRange.h
 *
 * StateTree Condition: 타겟이 특정 범위 내에 있는지 검사
 *
 * @author 김민우
 */

#pragma once

#include "CoreMinimal.h"
#include "Conditions/StateTreeAIConditionBase.h"
#include "AI/StateTree/HellunaStateTreeTypes.h"
#include "STCondition_InRange.generated.h"

USTRUCT()
struct FSTCondition_InRangeInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Input")
	FHellunaAITargetData TargetData;
};

USTRUCT(meta = (DisplayName = "Helluna: In Range", Category = "Helluna|AI"))
struct HELLUNA_API FSTCondition_InRange : public FStateTreeAIConditionBase
{
	GENERATED_BODY()

	using FInstanceDataType = FSTCondition_InRangeInstanceData;

protected:
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;

public:
	UPROPERTY(EditAnywhere, Category = "Config",
		meta = (DisplayName = "플레이어 판정 범위 (cm)", ClampMin = "0.0"))
	float Range = 200.f;

	UPROPERTY(EditAnywhere, Category = "Config",
		meta = (DisplayName = "우주선 판정 범위 (cm)", ClampMin = "0.0"))
	float SpaceRange = 800.f;

	UPROPERTY(EditAnywhere, Category = "Config",
		meta = (DisplayName = "범위 내부 체크"))
	bool bCheckInside = true;
};
