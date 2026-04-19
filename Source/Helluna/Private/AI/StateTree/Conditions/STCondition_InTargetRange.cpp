/**
 * STCondition_InTargetRange.cpp
 *
 * 단순 거리 범위 조건 구현.
 * Evaluator가 채운 TargetData.DistanceToTarget 값만 읽어 [Min, Max] 판정.
 *
 * @author 김민우
 */

#include "AI/StateTree/Conditions/STCondition_InTargetRange.h"
#include "StateTreeExecutionContext.h"

bool FSTCondition_InTargetRange::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	const FHellunaAITargetData& TD = InstanceData.TargetData;

	if (!TD.HasValidTarget())
	{
		return bReturnIfNoTarget;
	}

	const float Dist = TD.DistanceToTarget;
	const bool bInRange = (Dist >= MinRange) && (Dist <= MaxRange);
	return bInRange == bCheckInside;
}
