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
#include "EnvironmentQuery/EnvQueryTypes.h"
#include "STTask_ChaseTarget.generated.h"

class AAIController;
class UEnvQuery;

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

	// EQS 결과로 받은 목적지 (-1이면 아직 결과 없음)
	UPROPERTY()
	FVector EQSDestination = FVector(FLT_MAX);

	// EQS 재실행까지 남은 시간
	UPROPERTY()
	float TimeUntilNextEQS = 0.f;
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
	UPROPERTY(EditAnywhere, Category = "설정",
		meta = (DisplayName = "재경로 탐색 간격 (초)",
			ToolTip = "플레이어 타겟을 다시 추적하는 간격입니다.",
			ClampMin = "0.1"))
	float RepathInterval = 0.5f;

	UPROPERTY(EditAnywhere, Category = "설정",
		meta = (DisplayName = "도착 허용 반경 (cm)",
			ToolTip = "이 거리 안에 들어오면 도착으로 판단합니다.",
			ClampMin = "10.0"))
	float AcceptanceRadius = 50.f;

	/**
	 * 플레이어 타겟 추적 시 사용할 EQS 에셋.
	 * 설정하면 타겟 직접 추적 대신 EQS로 최적 공격 위치를 찾아 이동.
	 * 비워두면 기존 MoveToActor 방식으로 동작.
	 */
	UPROPERTY(EditAnywhere, Category = "설정",
		meta = (DisplayName = "공격 위치 EQS",
			ToolTip = "EQ_HellunaAttackPosition 에셋을 연결하세요.\n비워두면 타겟에게 직접 달려갑니다."))
	TObjectPtr<UEnvQuery> AttackPositionQuery = nullptr;

	/**
	 * EQS를 다시 실행하는 간격 (초).
	 * 타겟이 움직이면 최적 위치도 바뀌므로 주기적으로 재실행.
	 */
	UPROPERTY(EditAnywhere, Category = "설정",
		meta = (DisplayName = "EQS 재실행 간격 (초)",
			ToolTip = "공격 위치 EQS를 다시 실행하는 간격입니다.\n값이 작을수록 더 자주 위치를 갱신합니다.",
			ClampMin = "0.1"))
	float EQSInterval = 1.0f;
};
