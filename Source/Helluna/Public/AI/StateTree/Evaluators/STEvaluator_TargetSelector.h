/**
 * STEvaluator_TargetSelector.h
 *
 * ─── 역할 ────────────────────────────────────────────────────
 * StateTree Evaluator: 타겟 선택 + 어그로 관리 + 광폭화 감시 전담.
 *
 * ─── 행동 로직 ───────────────────────────────────────────────
 *  1. 기본 타겟: 우주선 (SpaceShip)
 *  2. 어그로 범위 내 플레이어 또는 터렛 발견 → 타겟 전환
 *  3. 타겟 고정: 한 대상을 지정하면 어그로 범위 밖으로 나가기 전까지 유지
 *  4. 타겟이 어그로 범위 밖으로 이탈 → 우주선으로 복귀
 *  5. 플레이어 타겟 중 EnrageDelay 경과 → 광폭화 이벤트 발송
 *  6. 광폭화 → 우주선 고정, 더 이상 타겟 전환 없음
 *  7. 우주선 공격 중(SpaceShipAttackRange 내) → 어그로 전환 차단
 *
 * ─── 광폭화 타이머 ──────────────────────────────────────────
 *  플레이어를 타겟으로 추적하는 동안에만 누적.
 *  터렛 타겟 중에는 누적하지 않음 (고정물이라 카이팅 불가).
 *  우주선으로 복귀하면 타이머 리셋.
 *
 * @author 김민우
 */

#pragma once

#include "CoreMinimal.h"
#include "StateTreeEvaluatorBase.h"
#include "AI/StateTree/HellunaStateTreeTypes.h"
#include "STEvaluator_TargetSelector.generated.h"

class AAIController;

USTRUCT()
struct FSTEvaluator_TargetSelectorInstanceData
{
	GENERATED_BODY()

	/** StateTree Context: AIController (에디터에서 자동 바인딩) */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AAIController> AIController = nullptr;

	/**
	 * 출력 데이터: 타겟 정보.
	 * Run/Attack/Enrage State의 Task들이 이 값을 바인딩해서 사용한다.
	 */
	UPROPERTY(EditAnywhere, Category = "Output")
	FHellunaAITargetData TargetData;

	/** (미사용 예정) 피격 이벤트로 어그로 전환할 때 사용 */
	UPROPERTY()
	TWeakObjectPtr<AActor> DamagedByActor = nullptr;
};

USTRUCT(meta = (DisplayName = "Helluna: Target Selector", Category = "Helluna|AI"))
struct HELLUNA_API FSTEvaluator_TargetSelector : public FStateTreeEvaluatorCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FSTEvaluator_TargetSelectorInstanceData;

protected:
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	/** StateTree 시작 시 1회 호출: 우주선을 기본 타겟으로 초기화 */
	virtual void TreeStart(FStateTreeExecutionContext& Context) const override;

	/** 매 Tick: 타겟 선택 + 어그로 이탈 감지 + 광폭화 감시 */
	virtual void Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;

public:
	/**
	 * 어그로 탐지 범위 (cm).
	 * 이 범위 안에 들어온 플레이어/터렛을 타겟으로 삼는다.
	 * 타겟이 이 범위 밖으로 나가면 우주선으로 복귀한다.
	 */
	UPROPERTY(EditAnywhere, Category = "Config",
		meta = (DisplayName = "어그로 범위 (cm)", ClampMin = "100.0"))
	float AggroRange = 800.f;

	/**
	 * 우주선 공격 판정 범위 (cm).
	 * 이 값 이하면 "공격 사거리 근처" → 플레이어가 어그로 범위에 들어와도 타겟 전환 차단.
	 * 실제 공격 발동 판정은 StateTree transition의 InAttackZone Condition이 담당.
	 */
	UPROPERTY(EditAnywhere, Category = "Config",
		meta = (DisplayName = "우주선 공격 판정 범위 (cm)", ClampMin = "50.0"))
	float SpaceShipAttackRange = 500.f;

	/**
	 * 플레이어를 타겟으로 삼은 후 광폭화까지의 대기 시간 (초).
	 * 0이면 즉시 광폭화.
	 * 터렛 타겟 중에는 이 타이머가 누적되지 않는다.
	 */
	UPROPERTY(EditAnywhere, Category = "Config",
		meta = (DisplayName = "광폭화 시작 시간 (초)", ClampMin = "0.0"))
	float EnrageDelay = 5.f;

	/**
	 * 한 포탑에 동시에 어그로를 가질 수 있는 최대 몬스터 수.
	 * 이 수를 초과하면 새 몬스터는 해당 포탑을 무시하고 우주선으로 계속 이동.
	 */
	UPROPERTY(EditAnywhere, Category = "Config",
		meta = (DisplayName = "포탑당 최대 어그로 수", ClampMin = "1", ClampMax = "20"))
	int32 MaxMonstersPerTurret = 5;
};
