/**
 * STTask_AttackTarget.h
 *
 * StateTree Task: 타겟 공격 실행
 *
 * @author 김민우
 */

#pragma once

#include "CoreMinimal.h"
#include "Tasks/StateTreeAITask.h"
#include "AI/StateTree/HellunaStateTreeTypes.h"
#include "STTask_AttackTarget.generated.h"

class AAIController;
class APawn;

USTRUCT()
struct FSTTask_AttackTargetInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AAIController> AIController = nullptr;

	UPROPERTY(EditAnywhere, Category = "Input")
	FHellunaAITargetData TargetData;

	UPROPERTY()
	float AttackCooldownRemaining = 0.f;
};

USTRUCT(meta = (DisplayName = "Helluna: Attack Target", Category = "Helluna|AI"))
struct HELLUNA_API FSTTask_AttackTarget : public FStateTreeAITaskBase
{
	GENERATED_BODY()

	using FInstanceDataType = FSTTask_AttackTargetInstanceData;

protected:
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;

	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, float DeltaTime) const override;

	virtual void ExitState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;

public:
	UPROPERTY(EditAnywhere, Category = "Config",
		meta = (DisplayName = "공격 쿨다운 (초)", ClampMin = "0.0"))
	float AttackCooldown = 1.5f;

	UPROPERTY(EditAnywhere, Category = "Config",
		meta = (DisplayName = "타겟 회전 속도", ClampMin = "0.0"))
	float RotationSpeed = 10.f;
};
