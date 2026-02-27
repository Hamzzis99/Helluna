/**
 * STEvaluator_TargetSelector.h
 *
 * ─── 역할 ────────────────────────────────────────────────────
 * StateTree Evaluator: 플레이어 탐지 및 광폭화 이벤트 발송 전담.
 *
 * 매 틱(Custom Tick Rate 0.2초 권장) 수행하는 작업:
 *   1. AggroRange 안에 플레이어가 있는지 탐색
 *   2. 플레이어 발견 시 TargetData에 기록 + PlayerTargetingTime 누적
 *   3. PlayerTargetingTime >= EnrageDelay이면
 *      StateTree에 Enemy.Event.Enrage 이벤트 발송
 *   4. 이벤트 발송 후 bEnraged = true로 중복 발송 방지
 *
 * ─── 우주선 타겟은 담당하지 않는다 ──────────────────────────
 * 우주선 이동/공격 타겟은 STEvaluator_SpaceShip이 별도로 관리.
 * 이 Evaluator는 "광폭화 조건 감시"에만 집중한다.
 *
 * ─── TreeStart ────────────────────────────────────────────────
 * 게임 시작 시 우주선을 기본 타겟으로 세팅한다.
 * (플레이어 탐지 전까지 TargetData.TargetActor = 우주선)
 *
 * ─── Tick Rate 설정 방법 ──────────────────────────────────────
 * StateTree 에디터에서 이 Evaluator 선택 후
 * Details > Custom Tick Rate = 0.2 설정.
 * 코드로 조절 불가 (FStateTreeEvaluatorCommonBase에 GetTickInterval 없음).
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
	 * 출력 데이터: 플레이어/우주선 타겟 정보.
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

	/** 매 Tick(또는 Custom Tick Rate 주기)마다 호출: 플레이어 탐색 + 광폭화 감시 */
	virtual void Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;

public:
	/**
	 * 플레이어 탐지 범위 (cm).
	 * 이 범위 안에 들어온 플레이어를 타겟으로 삼고 PlayerTargetingTime 누적 시작.
	 */
	UPROPERTY(EditAnywhere, Category = "Config",
		meta = (DisplayName = "어그로 범위 (cm)", ClampMin = "100.0"))
	float AggroRange = 800.f;

	/**
	 * 플레이어를 탐지한 후 광폭화 이벤트를 발송할 때까지의 대기 시간 (초).
	 * 0으로 설정하면 탐지 즉시 광폭화.
	 * 이 값이 경과하면 StateTree에 Enemy.Event.Enrage 이벤트를 보내
	 * Enrage State로의 Transition을 트리거한다.
	 */
	UPROPERTY(EditAnywhere, Category = "Config",
		meta = (DisplayName = "광폭화 시작 시간 (초)", ClampMin = "0.0"))
	float EnrageDelay = 5.f;
};
