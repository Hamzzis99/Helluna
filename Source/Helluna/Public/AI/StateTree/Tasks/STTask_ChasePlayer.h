/**
 * STTask_ChasePlayer.h
 *
 * StateTree Task: 플레이어 추적 전용
 * - RepathInterval마다 MoveToActor 재발행
 * - EQS 설정 시 공격 위치 계산 후 이동
 * - 이동 중 타겟 방향 회전
 */

#pragma once

#include "CoreMinimal.h"
#include "Tasks/StateTreeAITask.h"
#include "AI/StateTree/HellunaStateTreeTypes.h"
#include "EnvironmentQuery/EnvQueryTypes.h"
#include "STTask_ChasePlayer.generated.h"

class AAIController;
class UEnvQuery;

USTRUCT()
struct FSTTask_ChasePlayerInstanceData
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

	UPROPERTY()
	float TimeUntilNextEQS = 0.f;
};

USTRUCT(meta = (DisplayName = "Helluna: Chase Player", Category = "Helluna|AI"))
struct HELLUNA_API FSTTask_ChasePlayer : public FStateTreeAITaskBase
{
	GENERATED_BODY()

	using FInstanceDataType = FSTTask_ChasePlayerInstanceData;

protected:
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, float DeltaTime) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;

public:
	UPROPERTY(EditAnywhere, Category = "설정",
		meta = (DisplayName = "재경로 탐색 간격 (초)", ClampMin = "0.1"))
	float RepathInterval = 0.5f;

	UPROPERTY(EditAnywhere, Category = "설정",
		meta = (DisplayName = "도착 허용 반경 (cm)", ClampMin = "10.0"))
	float AcceptanceRadius = 50.f;

	UPROPERTY(EditAnywhere, Category = "설정",
		meta = (DisplayName = "공격 위치 EQS"))
	TObjectPtr<UEnvQuery> AttackPositionQuery = nullptr;

	UPROPERTY(EditAnywhere, Category = "설정",
		meta = (DisplayName = "EQS 재실행 간격 (초)", ClampMin = "0.1"))
	float EQSInterval = 1.0f;
};
