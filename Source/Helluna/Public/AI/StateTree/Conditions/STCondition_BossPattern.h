/**
 * STCondition_BossPattern.h
 *
 * StateTree Condition: 보스 패턴 상태 검사
 * - PatternPending : PendingPatternIndex >= 0 (패턴 발동 대기 중)
 * - PatternActive  : bPatternActive == true  (패턴 실행 중)
 * - PatternInactive: bPatternActive == false (패턴 비활성)
 *
 * @author 김민우
 */

#pragma once

#include "CoreMinimal.h"
#include "Conditions/StateTreeAIConditionBase.h"
#include "AI/StateTree/HellunaStateTreeBossTypes.h"
#include "STCondition_BossPattern.generated.h"

UENUM()
enum class EBossPatternConditionMode : uint8
{
	PatternPending   UMETA(DisplayName = "패턴 대기 중 (PendingIndex >= 0)"),
	PatternActive    UMETA(DisplayName = "패턴 실행 중 (bPatternActive)"),
	PatternInactive  UMETA(DisplayName = "패턴 비활성 (!bPatternActive)"),
};

USTRUCT()
struct FSTCondition_BossPatternInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Input")
	FBossPatternData PatternData;
};

USTRUCT(meta = (DisplayName = "Helluna: Boss Pattern Check", Category = "Helluna|AI"))
struct HELLUNA_API FSTCondition_BossPattern : public FStateTreeAIConditionBase
{
	GENERATED_BODY()

	using FInstanceDataType = FSTCondition_BossPatternInstanceData;

protected:
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;

public:
	UPROPERTY(EditAnywhere, Category = "Config",
		meta = (DisplayName = "검사 모드"))
	EBossPatternConditionMode Mode = EBossPatternConditionMode::PatternPending;
};
