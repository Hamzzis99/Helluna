/**
 * STTask_BossPattern.h
 *
 * StateTree Task: 보스 패턴 실행기.
 *
 * ─── 동작 ────────────────────────────────────────────────────
 *  - Evaluator의 PendingPatternIndex를 감시
 *  - 인덱스가 유효하면 해당 패턴 GA를 발동
 *  - GA 활성 중에는 bPatternActive = true (Chase/Attack이 멈춤)
 *  - GA 종료 → bPatternActive = false, PendingPatternIndex 초기화
 *
 * ─── 패턴 트리거 유형 ───────────────────────────────────────
 *  Timer:          전투 시작 후 N초마다 (Evaluator가 관리)
 *  HPThreshold:    HP가 특정 비율 이하 (Evaluator가 관리)
 *  ProximityTimer: 플레이어 근접 후 N초 경과 (Evaluator가 관리)
 *
 * @author 김민우
 */

#pragma once

#include "CoreMinimal.h"
#include "Tasks/StateTreeAITask.h"
#include "AI/StateTree/HellunaStateTreeTypes.h"
#include "AI/StateTree/HellunaStateTreeBossTypes.h"
#include "STTask_BossPattern.generated.h"

class AAIController;
class UHellunaEnemyGameplayAbility;

USTRUCT()
struct FSTTask_BossPatternInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AAIController> AIController = nullptr;

	/** 패턴 데이터 (Evaluator에서 바인딩 — bPatternActive, PendingPatternIndex 사용) */
	UPROPERTY(EditAnywhere, Category = "Input/Output")
	FBossPatternData BossPatternData;

	/** 현재 실행 중인 패턴 GA 클래스 (nullptr이면 없음) */
	UPROPERTY()
	TSubclassOf<UHellunaEnemyGameplayAbility> ActivePatternGA;

	/** 패턴 배열 참조 (Evaluator와 동일한 배열) */
	TArray<FBossPatternEntry> PatternEntries;
};

USTRUCT(meta = (DisplayName = "Helluna: Boss Pattern", Category = "Helluna|AI|Boss"))
struct HELLUNA_API FSTTask_BossPattern : public FStateTreeAITaskBase
{
	GENERATED_BODY()

	using FInstanceDataType = FSTTask_BossPatternInstanceData;

protected:
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;

	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, float DeltaTime) const override;

	virtual void ExitState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;

public:
	/**
	 * 패턴 목록 (Evaluator의 PatternEntries와 동일한 배열).
	 * Evaluator에서 PendingPatternIndex로 인덱스를 주면
	 * 이 배열에서 해당 GA를 꺼내 발동한다.
	 *
	 * [중요] Evaluator와 동일한 순서로 설정해야 한다.
	 */
	UPROPERTY(EditAnywhere, Category = "설정",
		meta = (DisplayName = "보스 패턴 목록 (Evaluator와 동일)", TitleProperty = "TriggerType"))
	TArray<FBossPatternEntry> PatternEntries;
};
