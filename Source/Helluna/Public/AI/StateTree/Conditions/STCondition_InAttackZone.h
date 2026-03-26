/**
 * STCondition_InAttackZone.h
 *
 * StateTree Condition: 몬스터 전방 공격존(Box)이 타겟 콜리전과 겹치는지 판정
 *
 * ─── 동작 방식 ─────────────────────────────────────────────────
 *  몬스터 전방에 Box 형태의 공격존을 배치하고
 *  UWorld::OverlapMultiByObjectType 으로 타겟 Actor의
 *  콜리전 컴포넌트와 실제로 겹치는지 물리적으로 판정한다.
 *
 *  → 우주선이 원형이 아니어도, 어떤 형태든 콜리전 메시에
 *    정확히 닿아야만 공격 판정이 나온다.
 *
 * @author 김민우
 */

#pragma once

#include "CoreMinimal.h"
#include "Conditions/StateTreeAIConditionBase.h"
#include "AI/StateTree/HellunaStateTreeTypes.h"
#include "STCondition_InAttackZone.generated.h"

class AAIController;

USTRUCT()
struct FSTCondition_InAttackZoneInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<AAIController> AIController = nullptr;

	UPROPERTY(EditAnywhere, Category = "Input")
	FHellunaAITargetData TargetData;
};

USTRUCT(meta = (DisplayName = "Helluna: In Attack Zone", Category = "Helluna|AI"))
struct HELLUNA_API FSTCondition_InAttackZone : public FStateTreeAIConditionBase
{
	GENERATED_BODY()

	using FInstanceDataType = FSTCondition_InAttackZoneInstanceData;

protected:
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;

public:
	/**
	 * 공격존 박스 반크기 (cm, 로컬 기준).
	 * X = 전방 깊이, Y = 좌우 폭, Z = 높이
	 */
	UPROPERTY(EditAnywhere, Category = "공격존",
		meta = (DisplayName = "공격존 반크기 (X전방, Y좌우, Z높이)"))
	FVector AttackZoneHalfExtent = FVector(100.f, 80.f, 80.f);

	/**
	 * 폰 위치로부터 전방으로 박스 중심을 얼마나 오프셋할지 (cm).
	 * 예: 100이면 폰 앞 100cm 지점이 박스 중심.
	 */
	UPROPERTY(EditAnywhere, Category = "공격존",
		meta = (DisplayName = "전방 오프셋 (cm)", ClampMin = "0.0"))
	float ForwardOffset = 100.f;

	/**
	 * 판정 반전 플래그.
	 * true  (기본) : 공격존이 타겟과 겹치면 true
	 * false        : 공격존이 타겟과 안 겹치면 true
	 */
	UPROPERTY(EditAnywhere, Category = "공격존",
		meta = (DisplayName = "오버랩 시 true"))
	bool bCheckInside = true;
};