/**
 * STCondition_IsGrounded.h
 *
 * [AirborneEnrageV1] 캐릭터가 지면에 있을 때만 True. 공중(IsFalling)이면 False.
 * Why: 공중에서 광폭화가 발동하면 Run_Rage 로 바로 진입해 Chase 가 돌아가며 낙하 제어를
 *      빼앗는 문제가 있음. Run_Rage 에 이 조건을 붙이면 공중이면 Fail → 다음 형제 분기로 넘어감.
 * How to apply: Run_Rage 상태의 enter condition 으로 사용.
 *
 * @author 김민우
 */

#pragma once

#include "CoreMinimal.h"
#include "Conditions/StateTreeAIConditionBase.h"
#include "STCondition_IsGrounded.generated.h"

USTRUCT()
struct FSTCondition_IsGroundedInstanceData
{
	GENERATED_BODY()
};

USTRUCT(meta = (DisplayName = "Helluna: Is Grounded", Category = "Helluna|AI"))
struct HELLUNA_API FSTCondition_IsGrounded : public FStateTreeAIConditionBase
{
	GENERATED_BODY()

	using FInstanceDataType = FSTCondition_IsGroundedInstanceData;

protected:
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;

public:
	/** true 이면 "지면 위에 있음"을 참으로, false 이면 반대(공중)를 참으로 반환 */
	UPROPERTY(EditAnywhere, Category = "Config",
		meta = (DisplayName = "지면 체크 (false=공중)"))
	bool bRequireGrounded = true;
};
