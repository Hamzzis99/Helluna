/**
 * STCondition_InRange.cpp
 *
 * @author 김민우
 */

#include "AI/StateTree/Conditions/STCondition_InRange.h"
#include "StateTreeExecutionContext.h"
#include "Object/ResourceUsingObject/ResourceUsingObject_SpaceShip.h"

bool FSTCondition_InRange::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	const FHellunaAITargetData& TargetData = InstanceData.TargetData;

	// 타겟이 없을 때:
	// - bCheckInside=true  (범위 안 체크): 타겟 없으면 실패
	// - bCheckInside=false (범위 밖 체크): 타겟 없어도 통과 (Run 상태 진입 허용)
	if (!TargetData.IsValid())
		return !bCheckInside;

	float EffectiveRange = Range;
	if (TargetData.TargetType == EHellunaTargetType::SpaceShip)
		EffectiveRange = SpaceRange;

	const bool bIsInside = (TargetData.DistanceToTarget <= EffectiveRange);
	return bCheckInside ? bIsInside : !bIsInside;
}
