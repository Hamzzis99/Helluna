/**
 * STTask_ChaseTarget.h
 *
 * StateTree Task: 타겟 추적 (이동)
 *
 * [방안 C 적용]
 * 우주선: 고정 오브젝트이므로 EnterState / 타겟 변경 시에만 MoveTo 한 번 발행.
 *         이후 Tick에서 재발행하지 않고 RVO(CrowdFollowingComponent)에 회피 위임.
 * 플레이어: 움직이는 대상이므로 RepathInterval 재발행 유지.
 *
 * @author 김민우
 */

#pragma once

#include "CoreMinimal.h"
#include "Tasks/StateTreeAITask.h"
#include "AI/StateTree/HellunaStateTreeTypes.h"
#include "STTask_ChaseTarget.generated.h"

class AAIController;

USTRUCT()
struct FSTTask_ChaseTargetInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AAIController> AIController = nullptr;

	UPROPERTY(EditAnywhere, Category = "Input")
	FHellunaAITargetData TargetData;

	UPROPERTY()
	TWeakObjectPtr<AActor> LastMoveTarget = nullptr;

	UPROPERTY()
	float TimeSinceRepath = 0.f;
};

USTRUCT(meta = (DisplayName = "Helluna: Chase Target", Category = "Helluna|AI"))
struct HELLUNA_API FSTTask_ChaseTarget : public FStateTreeAITaskBase
{
	GENERATED_BODY()

	using FInstanceDataType = FSTTask_ChaseTargetInstanceData;

protected:
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;

	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, float DeltaTime) const override;

	virtual void ExitState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;

public:
	UPROPERTY(EditAnywhere, Category = "Config",
		meta = (DisplayName = "재경로 탐색 간격 (초)", ClampMin = "0.1"))
	float RepathInterval = 0.5f;

	UPROPERTY(EditAnywhere, Category = "Config",
		meta = (DisplayName = "도착 허용 반경 (cm)", ClampMin = "10.0"))
	float AcceptanceRadius = 50.f;
};
