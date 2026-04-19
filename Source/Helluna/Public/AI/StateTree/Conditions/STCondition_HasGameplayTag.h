/**
 * STCondition_HasGameplayTag.h
 *
 * [RageBranchV1] Pawn 의 ASC 에 특정 GameplayTag 가 붙어 있는지 검사.
 * Why: 광폭화 진입 직후 "우주선 위에 올라탄 상태(State.Enemy.OnShip)" 여부로
 *      Run_Rage → Attack_Rage 경로와 Attackspaceship_jump_Rage 경로를 분기해야 함.
 * How to apply: Inrage 상태의 OnStateCompleted 전이(또는 분기 대상 상태의 enter condition)에 붙여서 사용.
 *
 * @author 김민우
 */

#pragma once

#include "CoreMinimal.h"
#include "Conditions/StateTreeAIConditionBase.h"
#include "GameplayTagContainer.h"
#include "STCondition_HasGameplayTag.generated.h"

USTRUCT()
struct FSTCondition_HasGameplayTagInstanceData
{
	GENERATED_BODY()
};

USTRUCT(meta = (DisplayName = "Helluna: Has Gameplay Tag (ASC)", Category = "Helluna|AI"))
struct HELLUNA_API FSTCondition_HasGameplayTag : public FStateTreeAIConditionBase
{
	GENERATED_BODY()

	using FInstanceDataType = FSTCondition_HasGameplayTagInstanceData;

protected:
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;

public:
	/** 검사 대상 GameplayTag (예: State.Enemy.OnShip). 정확 일치만 검사. */
	UPROPERTY(EditAnywhere, Category = "Config",
		meta = (DisplayName = "검사할 태그"))
	FGameplayTag Tag;

	/** true: 태그 보유 시 True / false: 태그 미보유 시 True. */
	UPROPERTY(EditAnywhere, Category = "Config",
		meta = (DisplayName = "보유 시 True (false=미보유 시 True)"))
	bool bMustHaveTag = true;
};
