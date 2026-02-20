/**
 * STTask_Death.h
 *
 * StateTree Task: 몬스터 사망 처리
 *
 * @author 김민우
 */

#pragma once

#include "CoreMinimal.h"
#include "Tasks/StateTreeAITask.h"
#include "STTask_Death.generated.h"

class AAIController;

USTRUCT()
struct FSTTask_DeathInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AAIController> AIController = nullptr;

	UPROPERTY()
	bool bMontageFinished = false;

	UPROPERTY()
	float FallbackTimer = 0.f;

	UPROPERTY()
	bool bUseFallback = false;
};

USTRUCT(meta = (DisplayName = "Helluna: Death", Category = "Helluna|AI"))
struct HELLUNA_API FSTTask_Death : public FStateTreeAITaskBase
{
	GENERATED_BODY()

	using FInstanceDataType = FSTTask_DeathInstanceData;

protected:
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual EStateTreeRunStatus EnterState(
		FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;

	virtual EStateTreeRunStatus Tick(
		FStateTreeExecutionContext& Context,
		const float DeltaTime) const override;

	virtual void ExitState(
		FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;

public:
	UPROPERTY(EditAnywhere, Category = "Config",
		meta = (DisplayName = "사망 대기 시간 (초)", ClampMin = "0.1"))
	float FallbackDuration = 2.f;
};
