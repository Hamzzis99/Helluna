/**
 * STCondition_InTargetRange.h
 *
 * StateTree Condition: 타겟과의 거리만으로 판정하는 단순 범위 조건.
 *
 * ─── 목적 ────────────────────────────────────────────────────────
 *  - 보스 공격처럼 "플레이어만 공격" 대상인 경우, 복잡한 Box 오버랩 대신
 *    Evaluator가 이미 계산한 `TargetData.DistanceToTarget`을 읽어
 *    [MinRange, MaxRange] 내부인지만 확인.
 *  - 우주선/구조물 타겟팅은 STCondition_InAttackZone 유지, 여기서는
 *    거리만 필요한 간단한 케이스 담당.
 *
 * @author 김민우
 */

#pragma once

#include "CoreMinimal.h"
#include "Conditions/StateTreeAIConditionBase.h"
#include "AI/StateTree/HellunaStateTreeTypes.h"
#include "STCondition_InTargetRange.generated.h"

class AAIController;

USTRUCT()
struct FSTCondition_InTargetRangeInstanceData
{
	GENERATED_BODY()

	/** 바인딩 호환성 (기존 InAttackZone과 동일 segment 유지용) — 실제 사용 안 함. */
	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<AAIController> AIController = nullptr;

	/** Evaluator에서 바인딩: 타겟 유효성 + 거리 */
	UPROPERTY(EditAnywhere, Category = "Input")
	FHellunaAITargetData TargetData;
};

USTRUCT(meta = (DisplayName = "Helluna: In Target Range", Category = "Helluna|AI"))
struct HELLUNA_API FSTCondition_InTargetRange : public FStateTreeAIConditionBase
{
	GENERATED_BODY()

	using FInstanceDataType = FSTCondition_InTargetRangeInstanceData;

protected:
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;

public:
	/** 이 거리 이상부터 허용 (cm). 0 = 하한 없음. */
	UPROPERTY(EditAnywhere, Category = "범위",
		meta = (DisplayName = "최소 거리 (cm)", ClampMin = "0.0"))
	float MinRange = 0.f;

	/** 이 거리 이하까지 허용 (cm). */
	UPROPERTY(EditAnywhere, Category = "범위",
		meta = (DisplayName = "최대 거리 (cm)", ClampMin = "0.0"))
	float MaxRange = 1000.f;

	/** 타겟이 없으면 이 값을 반환. 기본 false(타겟 없으면 조건 실패 → 전환 안 함). */
	UPROPERTY(EditAnywhere, Category = "범위",
		meta = (DisplayName = "타겟 없을 때 반환값"))
	bool bReturnIfNoTarget = false;

	/**
	 * 판정 반전 플래그.
	 *  true  (기본) : [Min, Max] 범위 안이면 true
	 *  false        : 범위 밖이면 true
	 */
	UPROPERTY(EditAnywhere, Category = "범위",
		meta = (DisplayName = "범위 내일 때 true"))
	bool bCheckInside = true;
};
