/**
 * STTask_ChaseSpaceShip.h
 *
 * 우주선 추적 전용 StateTree Task.
 * 에디터에서 이동 전략(Direct / RandomOffset / EQS)을 토글할 수 있다.
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

UENUM(BlueprintType)
enum class EChaseShipStrategy : uint8
{
	Direct        UMETA(DisplayName = "Direct",        ToolTip = "우주선으로 직접 이동"),
	RandomOffset  UMETA(DisplayName = "RandomOffset",  ToolTip = "Stuck 시 좌/우 랜덤 우회"),
	EQS           UMETA(DisplayName = "EQS",           ToolTip = "EQS로 최적 위치 탐색 후 이동"),
};

USTRUCT()
struct FSTTask_ChaseSpaceShipInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AAIController> AIController = nullptr;

	UPROPERTY(EditAnywhere, Category = "Input")
	FHellunaAITargetData TargetData;

	UPROPERTY()
	float TimeSinceRepath = 0.f;

	UPROPERTY()
	float TimeUntilNextEQS = 0.f;

	UPROPERTY()
	float StuckCheckTimer = 0.f;

	UPROPERTY()
	int32 ConsecutiveStuckCount = 0;

	UPROPERTY()
	FVector LastCheckedLocation = FVector::ZeroVector;
};

USTRUCT(meta = (DisplayName = "Helluna: Chase SpaceShip", Category = "Helluna|AI"))
struct HELLUNA_API FSTTask_ChaseSpaceShip : public FStateTreeAITaskBase
{
	GENERATED_BODY()

	using FInstanceDataType = FSTTask_ChaseSpaceShipInstanceData;

protected:
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;

	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, float DeltaTime) const override;

	virtual void ExitState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;

public:
	UPROPERTY(EditAnywhere, Category = "설정",
		meta = (DisplayName = "이동 전략"))
	EChaseShipStrategy Strategy = EChaseShipStrategy::Direct;

	UPROPERTY(EditAnywhere, Category = "설정",
		meta = (DisplayName = "재경로 탐색 간격 (초)", ClampMin = "0.1"))
	float RepathInterval = 0.5f;

	UPROPERTY(EditAnywhere, Category = "설정",
		meta = (DisplayName = "도착 허용 반경 (cm)", ClampMin = "10.0"))
	float AcceptanceRadius = 50.f;

	UPROPERTY(EditAnywhere, Category = "설정",
		meta = (DisplayName = "공격 위치 EQS",
			ToolTip = "EQS 에셋. Strategy가 EQS일 때 사용."))
	TObjectPtr<UEnvQuery> AttackPositionQuery = nullptr;

	UPROPERTY(EditAnywhere, Category = "설정",
		meta = (DisplayName = "EQS 재실행 간격 (초)", ClampMin = "0.1"))
	float EQSInterval = 1.0f;

	UPROPERTY(EditAnywhere, Category = "설정",
		meta = (DisplayName = "Stuck 체크 간격 (초)", ClampMin = "0.1"))
	float StuckCheckInterval = 0.5f;

	UPROPERTY(EditAnywhere, Category = "설정",
		meta = (DisplayName = "Stuck 판정 거리 (cm)", ClampMin = "0.0"))
	float StuckDistThreshold = 30.f;

	UPROPERTY(EditAnywhere, Category = "설정",
		meta = (DisplayName = "우회 오프셋 (cm)", ClampMin = "0.0"))
	float DetourOffset = 600.f;
};
