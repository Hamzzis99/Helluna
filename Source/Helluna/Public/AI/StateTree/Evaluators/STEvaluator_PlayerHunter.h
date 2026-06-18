/**
 * STEvaluator_PlayerHunter.h
 *
 * ─── 역할 ────────────────────────────────────────────────────
 * StateTree Evaluator (헌터 전용): 오직 "가장 가까운 플레이어"만 타겟으로 채운다.
 *
 * STEvaluator_TargetSelector 와 달리 우주선·터렛·어그로 범위·광폭화 로직이
 * 전혀 없다. 매 틱 최근접 플레이어를 찾아 TargetData 에 기록할 뿐이다.
 *
 * ─── 호환성 ──────────────────────────────────────────────────
 * 인스턴스 데이터(AIController + TargetData)의 프로퍼티 이름·타입을
 * STEvaluator_TargetSelector 와 동일하게 맞춰, 같은 트리의 Task 들이
 * TargetData 출력을 그대로 바인딩할 수 있게 한다.
 *
 * @author 김민우
 */

#pragma once

#include "CoreMinimal.h"
#include "StateTreeEvaluatorBase.h"
#include "AI/StateTree/HellunaStateTreeTypes.h"
#include "STEvaluator_PlayerHunter.generated.h"

class AAIController;

USTRUCT()
struct FSTEvaluator_PlayerHunterInstanceData
{
	GENERATED_BODY()

	/** StateTree Context: AIController (에디터에서 자동 바인딩) */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AAIController> AIController = nullptr;

	/**
	 * 출력 데이터: 타겟 정보 (항상 최근접 플레이어).
	 * Run/Attack State 의 Task 들이 이 값을 바인딩해서 사용한다.
	 * STEvaluator_TargetSelector 와 동일 타입/이름이라 바인딩 호환.
	 */
	UPROPERTY(EditAnywhere, Category = "Output")
	FHellunaAITargetData TargetData;
};

USTRUCT(meta = (DisplayName = "Helluna: Player Hunter", Category = "Helluna|AI"))
struct HELLUNA_API FSTEvaluator_PlayerHunter : public FStateTreeEvaluatorCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FSTEvaluator_PlayerHunterInstanceData;

protected:
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	/** StateTree 시작 시 1회: 타겟 초기화 (플레이어 타입, 아직 미지정). */
	virtual void TreeStart(FStateTreeExecutionContext& Context) const override;

	/** 매 Tick: 최근접 플레이어를 찾아 TargetData 에 기록 + Focus. */
	virtual void Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
};
