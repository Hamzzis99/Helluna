/**
 * STTask_ChaseTarget.h
 *
 * StateTree Task: Ship Chase (RUSH + DIRECT) / Player / Turret
 *
 * [Phase 1: RUSH]
 *   각 몬스터가 서로 다른 각도(0, 60, 120...)를 할당받아 링 포인트로 이동.
 *   여러 몬스터가 다른 NavMesh 경로를 타게 만들어 경로 혼잡 방지.
 *
 * [Phase 2: DIRECT APPROACH]
 *   SpreadPhaseRadius 이내로 들어오면 MoveToActor(Ship) 직선 추격.
 *   도착 판정은 하지 않음 — Chase→Attack transition의
 *   InAttackZone Condition이 공격 돌입을 담당.
 *
 * [Stuck -> Direct Move]
 *   N회 연속 끼이면 길찾기를 끄고 우주선 방향으로 AddMovementInput.
 *   CMC Walking 모드가 지면/충돌 처리.
 *
 * [Player]
 *   MoveToActor with RepathInterval. Optional EQS.
 *
 * @author
 */

#pragma once

#include "CoreMinimal.h"
#include "Tasks/StateTreeAITask.h"
#include "AI/StateTree/HellunaStateTreeTypes.h"
#include "EnvironmentQuery/EnvQueryTypes.h"
#include "STTask_ChaseTarget.generated.h"

class AAIController;
class UEnvQuery;

UENUM()
enum class EChasePhase : uint8
{
	Rush,
	Spread,
};

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

	UPROPERTY()
	float TimeUntilNextEQS = 0.f;

	// --- Ship chase ---

	UPROPERTY()
	EChasePhase Phase = EChasePhase::Rush;

	UPROPERTY()
	float AssignedAngleDeg = 0.f;

	UPROPERTY()
	FVector LastCheckedLocation = FVector::ZeroVector;

	UPROPERTY()
	float StuckCheckTimer = 0.f;

	UPROPERTY()
	int32 ConsecutiveStuckCount = 0;

	/** pathfinding OFF direct movement mode */
	UPROPERTY()
	bool bDirectMoveMode = false;

	UPROPERTY()
	float DiagTimer = 0.f;

	// [ChaseSimpV1][LCv14] 링 반경 도달 후 정지 + 주시 상태 플래그.
	UPROPERTY()
	bool bSpreadArrived = false;

	// [ChaseSimpV1][LCv14] DirectMode 사이드스텝 고정 방향.
	// 0=미정, +1=오른쪽(시계), -1=왼쪽(반시계). 한 번 정해지면 Spread 도착 전까지 유지.
	UPROPERTY()
	int8 SidestepSign = 0;

	// --- Player chase ---

	UPROPERTY()
	int32 PlayerStuckCount = 0;

	UPROPERTY()
	float PlayerStuckTimer = 0.f;

	UPROPERTY()
	FVector PlayerLastCheckedLocation = FVector::ZeroVector;
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

	EStateTreeRunStatus TickShipChase(FStateTreeExecutionContext& Context,
		FInstanceDataType& Data, AAIController* AIC, APawn* Pawn,
		AActor* Ship, float DeltaTime) const;

	EStateTreeRunStatus TickPlayerChase(FInstanceDataType& Data,
		AAIController* AIC, APawn* Pawn, AActor* Target, float DeltaTime) const;

	EStateTreeRunStatus TickTurretChase(FInstanceDataType& Data,
		AAIController* AIC, APawn* Pawn, AActor* Turret, float DeltaTime) const;

public:

	// ==========================================================

	UPROPERTY(EditAnywhere, Category = "공통",
		meta = (DisplayName = "경로 재탐색 주기",
			ToolTip = "MoveTo 경로 재탐색 주기 (초).\n플레이어: 재추적 주기.\n우주선: 웨이포인트 갱신 주기.",
			ClampMin = "0.1"))
	float RepathInterval = 0.5f;

	UPROPERTY(EditAnywhere, Category = "공통",
		meta = (DisplayName = "도착 판정 반경",
			ToolTip = "MoveTo 도착 판정 반경 (cm).",
			ClampMin = "10.0"))
	float AcceptanceRadius = 50.f;

	// ==========================================================

	UPROPERTY(EditAnywhere, Category = "우주선",
		meta = (DisplayName = "돌진→산개 전환 거리",
			ToolTip = "돌진(먼 링) -> 산개(가까운 링) 페이즈 전환 거리 (cm).\n돌진 페이즈는 먼 거리에서 각도별로 경로를 분산시키는 역할.",
			ClampMin = "100.0"))
	float SpreadPhaseRadius = 1200.f;

	UPROPERTY(EditAnywhere, Category = "우주선",
		meta = (DisplayName = "산개 접근 반경 (링 반경)",
			ToolTip = "몬스터가 각도별로 에워싸는 링의 반경 (cm).\n이 반경에 도달하면 정지 + 우주선 주시 → 자연스러운 에워싸기.\n값이 작을수록 더 가까이 붙음.",
			ClampMin = "50.0"))
	float SpreadApproachRadius = 350.f;

	UPROPERTY(EditAnywhere, Category = "우주선",
		meta = (DisplayName = "돌진 웨이포인트 간격",
			ToolTip = "돌진 페이즈에서 한 웨이포인트당 최대 이동 거리 (cm).\n각 몬스터가 서로 다른 각도의 링 포인트로 향하게 해 경로 분산.",
			ClampMin = "100.0"))
	float RushWaypointStep = 800.f;

	// [ChaseSimpV1][LCv14] 미사용 — 자산 직렬화 호환성 유지용 (실제 링 반경은 SpreadApproachRadius).
	UPROPERTY()
	float StandoffRadius = 350.f;

	UPROPERTY(EditAnywhere, Category = "우주선",
		meta = (DisplayName = "재추격 마진",
			ToolTip = "에워싸기 도착 후 우주선이 이만큼 움직이면 다시 추격 시작 (cm).\n우주선 드리프트에 대응해 링을 유지.",
			ClampMin = "10.0"))
	float SpreadReEngageMargin = 300.f;

	UPROPERTY(EditAnywhere, Category = "우주선",
		meta = (DisplayName = "직선이동 도착 강제 거리",
			ToolTip = "직선이동 모드에서 링 반경에 이 거리 이내로 접근하면 도착 처리 (cm).\n경로찾기 실패 상태에서도 에워싸기가 성립하게 함.",
			ClampMin = "10.0"))
	float DirectModeForceArriveThreshold = 150.f;

	// ==========================================================

	UPROPERTY(EditAnywhere, Category = "끼임 감지",
		meta = (DisplayName = "끼임 감지 주기",
			ToolTip = "몬스터 끼임 감지 주기 (초).",
			ClampMin = "0.1"))
	float StuckCheckInterval = 0.4f;

	UPROPERTY(EditAnywhere, Category = "끼임 감지",
		meta = (DisplayName = "끼임 판정 거리",
			ToolTip = "감지 주기 동안 이 거리 미만으로 이동하면 끼임 판정 (cm).",
			ClampMin = "1.0"))
	float StuckDistThreshold = 50.f;

	UPROPERTY(EditAnywhere, Category = "끼임 감지",
		meta = (DisplayName = "끼임 시 각도 회전량",
			ToolTip = "끼임 감지 시 할당 각도를 이만큼 회전 (도).",
			ClampMin = "10.0"))
	float AngleRotationOnStuck = 90.f;

	UPROPERTY(EditAnywhere, Category = "끼임 감지",
		meta = (DisplayName = "직선이동 전환 횟수",
			ToolTip = "이 횟수만큼 연속 끼이면 길찾기를 끄고 직선 이동으로 전환.\nNavMesh 경계 끼임 해결용.",
			ClampMin = "1"))
	int32 DirectMoveStuckThreshold = 2;

	UPROPERTY(EditAnywhere, Category = "끼임 감지",
		meta = (DisplayName = "경로실패 시 즉시 직선이동",
			ToolTip = "ON: MoveTo 경로 실패 시 즉시 직선이동 전환.\nOFF: 끼임 감지만으로 전환."))
	bool bInstantDirectModeOnPathFail = false;

	// ==========================================================

	UPROPERTY(EditAnywhere, Category = "플레이어",
		meta = (DisplayName = "플레이어 추격 직선이동 횟수",
			ToolTip = "플레이어 추격 중 이 횟수만큼 연속 끼이면 직선이동으로 전환.",
			ClampMin = "2"))
	int32 PlayerDirectMoveThreshold = 4;

	// ==========================================================

	UPROPERTY(EditAnywhere, Category = "터렛",
		meta = (DisplayName = "터렛 대기 반경",
			ToolTip = "터렛 중심에서 몬스터가 산개하여 멈추는 거리 (cm).\n터렛을 에워싸는 반경입니다.",
			ClampMin = "50.0"))
	float TurretStandoffRadius = 150.f;

	// ==========================================================

	UPROPERTY(EditAnywhere, Category = "EQS",
		meta = (DisplayName = "공격 위치 EQS",
			ToolTip = "플레이어 공격 위치용 EQS 에셋. 비우면 직접 MoveToActor 사용."))
	TObjectPtr<UEnvQuery> AttackPositionQuery = nullptr;

	UPROPERTY(EditAnywhere, Category = "EQS",
		meta = (DisplayName = "EQS 실행 주기",
			ToolTip = "EQS 반복 실행 주기 (초).",
			ClampMin = "0.1"))
	float EQSInterval = 1.0f;
};
