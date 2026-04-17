/**
 * STCondition_BossPattern.cpp
 *
 * @author 김민우
 */

#include "AI/StateTree/Conditions/STCondition_BossPattern.h"
#include "StateTreeExecutionContext.h"

bool FSTCondition_BossPattern::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);
	const FBossPatternData& PD = Data.PatternData;

	switch (Mode)
	{
	case EBossPatternConditionMode::PatternPending:
		return PD.PendingPatternIndex >= 0;

	case EBossPatternConditionMode::PatternActive:
		return PD.bPatternActive;

	case EBossPatternConditionMode::PatternInactive:
		return !PD.bPatternActive;

	default:
		return false;
	}
}
