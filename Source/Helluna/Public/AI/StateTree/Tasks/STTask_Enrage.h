/**
 * STTask_Enrage.h
 *
 * StateTree Task: 광폭화 진입
 *
 * ─── 역할 ────────────────────────────────────────────────────
 *  1. EnterState: 타겟을 현재 플레이어로 고정(bPlayerLocked = true)
 *                 Enemy->EnterEnraged() 호출 (이동속도·배율 적용 + 몽타주)
 *                 → 이후 광폭화 해제/복귀는 StateTree Transition Condition에서 결정
 *
 *  2. Tick: 락온 대상 소멸(사망) 감지 시 즉시 Succeeded 반환
 *            → Evaluator가 다음 Tick에서 우주선으로 자동 복귀
 *
 *  3. ExitState: 비정상 종료(사망 등) 시 안전 정리
 *
 * ─── StateTree 에디터 배치 가이드 ───────────────────────────
 *  광폭화 State를 별도로 만들고:
 *
 *    Tasks:
 *      [0] STTask_Enrage       ← 이 Task (광폭화 진입·유지)
 *      [1] STTask_ChaseTarget  ← 플레이어 추적 (bPlayerLocked 상태)
 *      [2] STTask_AttackTarget ← 공격 (EnrageCooldownMultiplier 참조)
 *
 *    이 State에서 빠져나가는 Transition은 StateTree 에디터에서
 *    원하는 Condition(HP, 타이머, 이벤트 등)으로 자유롭게 설정.
 *
 * @author 김민우
 */

#pragma once

#include "CoreMinimal.h"
#include "StateTreeTaskBase.h"
#include "AI/StateTree/HellunaStateTreeTypes.h"
#include "STTask_Enrage.generated.h"

class AAIController;
class AHellunaEnemyCharacter;

USTRUCT()
struct FSTTask_EnrageInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AAIController> AIController = nullptr;

	/** Evaluator의 TargetData와 바인딩 (StateTree 에디터에서 연결) */
	UPROPERTY(EditAnywhere, Category = "Input/Output")
	FHellunaAITargetData TargetData;

	/** 광폭화가 이미 적용되었는지 (EnterState 중복 방지) */
	UPROPERTY()
	bool bEnrageApplied = false;

	/**
	 * 광폭화 몽타주 완료 플래그.
	 * AHellunaEnemyCharacter::OnEnrageMontageFinished 델리게이트가
	 * 세팅하면 Tick에서 Succeeded를 반환해 EnrageLoop State로 전환한다.
	 */
	UPROPERTY()
	bool bMontageFinished = false;
};

USTRUCT(meta = (DisplayName = "Helluna: Enrage", Category = "Helluna|AI"))
struct HELLUNA_API FSTTask_Enrage : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FSTTask_EnrageInstanceData;

protected:
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;

	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, float DeltaTime) const override;

	virtual void ExitState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
};
