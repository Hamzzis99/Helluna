/**
 * STTask_ChaseSpaceShip.h
 *
 * StateTree Task: 우주선 추적 전용
 *
 * ─── 이동 전략 (에디터에서 토글) ───────────────────────────────
 *  Direct       : 우주선 방향 NavMesh 위 중간 지점으로 직진
 *                 → 몰려오는 느낌, 가장 단순
 *
 *  RandomOffset : Direct와 동일하되, Stuck 감지 시 좌/우 랜덤 오프셋으로 우회
 *                 → 막혔을 때 자연스러운 우회
 *
 *  EQS          : EQS 쿼리로 최적 공격 위치를 찾아 이동
 *                 → 가장 정밀한 포지셔닝
 *
 * ─── Stuck 감지 ────────────────────────────────────────────────
 *  속도가 아닌 위치 변화량으로 판정.
 *  StuckCheckInterval마다 이전 위치와 현재 위치의 거리를 비교,
 *  StuckDistThreshold 이하이면 Stuck으로 판정.
 *
 * @author 김민우
 */

#pragma once

#include "CoreMinimal.h"
#include "Tasks/StateTreeAITask.h"
#include "AI/StateTree/HellunaStateTreeTypes.h"
#include "EnvironmentQuery/EnvQueryTypes.h"
#include "STTask_ChaseSpaceShip.generated.h"

class AAIController;
class UEnvQuery;

/** 우주선 추적 이동 전략 */
UENUM()
enum class EChaseShipStrategy : uint8
{
	/** 우주선 방향 직진 (NavMesh 중간 지점) */
	Direct        UMETA(DisplayName = "직진"),

	/** 직진 + Stuck 시 좌/우 랜덤 오프셋 우회 */
	RandomOffset  UMETA(DisplayName = "직진 + 랜덤 우회"),

	/** EQS로 최적 위치 탐색 */
	EQS           UMETA(DisplayName = "EQS"),
};

USTRUCT()
struct FSTTask_ChaseSpaceShipInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AAIController> AIController = nullptr;

	UPROPERTY(EditAnywhere, Category = "Input")
	FHellunaAITargetData TargetData;

	// ── 내부 상태 ─────────────────────────────────────────────
	UPROPERTY()
	float TimeSinceRepath = 0.f;

	UPROPERTY()
	float TimeUntilNextEQS = 0.f;

	/** Stuck 감지용: 마지막 체크 시점의 위치 */
	UPROPERTY()
	FVector LastCheckedLocation = FVector::ZeroVector;

	/** Stuck 체크 타이머 */
	UPROPERTY()
	float StuckCheckTimer = 0.f;

	/** 연속 Stuck 횟수 (우회 강도 조절용) */
	UPROPERTY()
	int32 ConsecutiveStuckCount = 0;
};

USTRUCT(meta = (DisplayName = "Helluna: Chase SpaceShip", Category = "Helluna|AI"))
struct HELLUNA_API FSTTask_ChaseSpaceShip : public FStateTreeAITaskBase
{
	GENERATED_BODY()

	using FInstanceDataType = FSTTask_ChaseSpaceShipInstanceData;

protected:
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, float DeltaTime) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;

public:
	// ── 이동 전략 ──────────────────────────────────────────────

	UPROPERTY(EditAnywhere, Category = "전략",
		meta = (DisplayName = "이동 전략"))
	EChaseShipStrategy Strategy = EChaseShipStrategy::Direct;

	// ── 공통 설정 ──────────────────────────────────────────────

	UPROPERTY(EditAnywhere, Category = "공통",
		meta = (DisplayName = "도착 허용 반경 (cm)", ClampMin = "10.0"))
	float AcceptanceRadius = 50.f;

	UPROPERTY(EditAnywhere, Category = "공통",
		meta = (DisplayName = "재경로 탐색 간격 (초)", ClampMin = "0.1"))
	float RepathInterval = 2.0f;

	// ── Stuck 감지 (위치 기반) ─────────────────────────────────

	/** Stuck 체크 간격 (초). 이 간격마다 위치 변화량을 확인. */
	UPROPERTY(EditAnywhere, Category = "Stuck 감지",
		meta = (DisplayName = "Stuck 체크 간격 (초)", ClampMin = "0.1"))
	float StuckCheckInterval = 0.5f;

	/** 이 거리(cm) 이하로만 움직였으면 Stuck 판정. */
	UPROPERTY(EditAnywhere, Category = "Stuck 감지",
		meta = (DisplayName = "Stuck 거리 임계치 (cm)", ClampMin = "1.0"))
	float StuckDistThreshold = 30.f;

	// ── RandomOffset 전용 ──────────────────────────────────────

	/** Stuck 시 좌/우 오프셋 기본 크기 (cm). 연속 Stuck 시 배수 증가. */
	UPROPERTY(EditAnywhere, Category = "RandomOffset 전용",
		meta = (DisplayName = "우회 오프셋 (cm)", ClampMin = "50.0",
			EditCondition = "Strategy == EChaseShipStrategy::RandomOffset"))
	float DetourOffset = 300.f;

	// ── EQS 전용 ───────────────────────────────────────────────

	/** 우주선 주변 최적 공격 위치를 찾는 EQS 에셋 */
	UPROPERTY(EditAnywhere, Category = "EQS 전용",
		meta = (DisplayName = "공격 위치 EQS",
			EditCondition = "Strategy == EChaseShipStrategy::EQS"))
	TObjectPtr<UEnvQuery> AttackPositionQuery = nullptr;

	/** EQS 재실행 간격 (초) */
	UPROPERTY(EditAnywhere, Category = "EQS 전용",
		meta = (DisplayName = "EQS 재실행 간격 (초)", ClampMin = "0.1",
			EditCondition = "Strategy == EChaseShipStrategy::EQS"))
	float EQSInterval = 1.0f;
};