/**
 * STCondition_InRange.cpp
 *
 * @author 김민우
 */

#include "AI/StateTree/Conditions/STCondition_InRange.h"
#include "StateTreeExecutionContext.h"

bool FSTCondition_InRange::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	const FHellunaAITargetData& TargetData = InstanceData.TargetData;

	if (!TargetData.IsValid())
		return false;

	float EffectiveRange = Range;
	if (TargetData.TargetType == EHellunaTargetType::SpaceShip)
		EffectiveRange = SpaceRange;

	const bool bIsInside = (TargetData.DistanceToTarget <= EffectiveRange);
	return bCheckInside ? bIsInside : !bIsInside;
}
