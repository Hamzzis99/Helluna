/**
 * STEvaluator_BossTarget.h
 *
 * ─── 역할 ────────────────────────────────────────────────────
 * 보스 전용 StateTree Evaluator.
 *
 * ─── 타겟 선택 ──────────────────────────────────────────────
 *  1. 기본 타겟: 가장 가까운 플레이어 (우주선 아님)
 *  2. 타겟 고정: 한번 지정하면 RetargetInterval 동안 유지
 *  3. RetargetInterval 경과 → 가장 가까운 플레이어로 재탐색
 *  4. 타겟 소멸 → 즉시 재탐색
 *
 * ─── 출력 ────────────────────────────────────────────────────
 *  TargetData   : FHellunaAITargetData (기존 Chase/Attack/Condition 재활용)
 *  PatternData  : FBossPatternData     (패턴 Task 전용)
 *
 * ─── 패턴 감시 ──────────────────────────────────────────────
 *  매 틱 패턴 배열을 순회하며 조건 검사.
 *  우선순위: HP패턴 > 시간제 > 근접시간제
 *
 * @author 김민우
 */

#pragma once

#include "CoreMinimal.h"
#include "StateTreeEvaluatorBase.h"
#include "AI/StateTree/HellunaStateTreeTypes.h"
#include "AI/StateTree/HellunaStateTreeBossTypes.h"
#include "STEvaluator_BossTarget.generated.h"

class AAIController;
class UHellunaHealthComponent;

USTRUCT()
struct FSTEvaluator_BossTargetInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AAIController> AIController = nullptr;

	/** 출력: 기본 타겟 데이터 (기존 Task/Condition 바인딩용) */
	UPROPERTY(EditAnywhere, Category = "Output")
	FHellunaAITargetData TargetData;

	/** 출력: 패턴 감시 데이터 (BossPattern Task 바인딩용) */
	UPROPERTY(EditAnywhere, Category = "Output")
	FBossPatternData PatternData;

	/** 패턴 런타임 상태 */
	TArray<FBossPatternEntry> PatternRuntime;

	/** 리타겟 타이머 */
	float RetargetTimer = 0.f;

	/** HP 컴포넌트 캐시 */
	TWeakObjectPtr<UHellunaHealthComponent> CachedHealthComp;

	/** HP 컴포넌트 탐색 완료 여부 */
	bool bHealthCompSearched = false;
};

USTRUCT(meta = (DisplayName = "Helluna: Boss Target", Category = "Helluna|AI|Boss"))
struct HELLUNA_API FSTEvaluator_BossTarget : public FStateTreeEvaluatorCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FSTEvaluator_BossTargetInstanceData;

protected:
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual void TreeStart(FStateTreeExecutionContext& Context) const override;
	virtual void Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;

public:
	/** 타겟 재탐색 주기 (초). 이 시간 동안 현재 타겟 유지. */
	UPROPERTY(EditAnywhere, Category = "타겟",
		meta = (DisplayName = "타겟 재탐색 주기 (초)", ClampMin = "1.0"))
	float RetargetInterval = 5.f;

	/**
	 * 보스 패턴 목록.
	 * 에디터에서 패턴 GA 클래스, 트리거 유형, 조건값을 설정한다.
	 */
	UPROPERTY(EditAnywhere, Category = "패턴",
		meta = (DisplayName = "보스 패턴 목록", TitleProperty = "TriggerType"))
	TArray<FBossPatternEntry> PatternEntries;
};
