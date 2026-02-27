/**
 * HellunaStateTreeTypes.h
 *
 * ─── 개요 ────────────────────────────────────────────────────
 * StateTree 전역에서 공유하는 AI 타겟 데이터 구조체 정의.
 *
 * ─── 데이터 흐름 ─────────────────────────────────────────────
 *
 *  [STEvaluator_TargetSelector]   [STEvaluator_SpaceShip]
 *         │ 매 틱 채움                    │ 매 틱 채움
 *         ▼                              ▼
 *   FHellunaAITargetData           FHellunaAITargetData
 *   (플레이어 타겟 정보)            (우주선 타겟 정보)
 *         │                              │
 *         ├── Run / Attack State Task 바인딩
 *         │       (에디터에서 TargetData 연결)
 *         │
 *         └── Enrage State Task 바인딩
 *                 (에디터에서 TargetData 연결)
 *
 *  EnrageLoop State는 SpaceShip Evaluator의 데이터를 바인딩
 *  → 광폭화 후 타겟 전환이 코드 없이 에디터 바인딩만으로 처리됨
 *
 * ─── 주의 ─────────────────────────────────────────────────────
 * StateTree의 Input/Output 바인딩은 단방향 복사이므로
 * Task에서 수정한 값이 Evaluator에 자동으로 반영되지 않는다.
 * bTargetingPlayer, bPlayerLocked처럼 Evaluator가 읽어야 하는 플래그는
 * 반드시 Output 바인딩으로 연결해야 Evaluator에서 볼 수 있다.
 *
 * @author 김민우
 */

#pragma once

#include "CoreMinimal.h"
#include "HellunaStateTreeTypes.generated.h"

/** 현재 AI가 추적 중인 타겟 종류 */
UENUM(BlueprintType)
enum class EHellunaTargetType : uint8
{
	SpaceShip  UMETA(DisplayName = "우주선"),
	Player     UMETA(DisplayName = "플레이어"),
};

/**
 * Evaluator가 매 틱 채워주는 타겟 정보.
 *
 * STEvaluator_TargetSelector  → 플레이어 추적용으로 채운다.
 * STEvaluator_SpaceShip       → 우주선 추적용으로 채운다.
 *
 * 같은 구조체를 두 Evaluator가 용도별로 사용하여
 * Task의 바인딩 타입이 통일되도록 한다.
 * (타입이 다르면 에디터에서 바인딩 자체가 불가)
 */
USTRUCT(BlueprintType)
struct HELLUNA_API FHellunaAITargetData
{
	GENERATED_BODY()

	/** 현재 추적 대상 Actor */
	UPROPERTY()
	TWeakObjectPtr<AActor> TargetActor = nullptr;

	/** 타겟 종류 (플레이어 / 우주선) */
	UPROPERTY()
	EHellunaTargetType TargetType = EHellunaTargetType::SpaceShip;

	/** 타겟까지 거리 (cm, 매 틱 갱신) */
	UPROPERTY()
	float DistanceToTarget = 0.f;

	/**
	 * 플레이어를 타겟으로 추적 중인지.
	 * true이면 Evaluator가 AggroRange 탐색을 중단하고
	 * 기존 플레이어를 계속 추적한다.
	 */
	UPROPERTY()
	bool bTargetingPlayer = false;

	/**
	 * 플레이어 락온 플래그.
	 * STTask_Enrage EnterState에서 true로 설정.
	 * true이면 Evaluator가 타겟을 절대 바꾸지 않는다.
	 * 광폭화 완료(ExitState) 또는 락온 대상 소멸 시 false로 해제.
	 */
	UPROPERTY()
	bool bPlayerLocked = false;

	/**
	 * 현재 광폭화 상태인지.
	 * STTask_Enrage EnterState에서 true, ExitState에서 false.
	 * Evaluator가 광폭화 이벤트 중복 발송을 방지하는 데 사용.
	 */
	UPROPERTY()
	bool bEnraged = false;

	/**
	 * 플레이어를 타겟으로 삼기 시작한 후 경과 시간 (초).
	 * Evaluator가 bTargetingPlayer == true 인 동안 매 틱 누적.
	 * EnrageDelay 이상이 되면 광폭화 이벤트를 발송한다.
	 * 우주선으로 타겟이 바뀌면 0으로 리셋.
	 */
	UPROPERTY()
	float PlayerTargetingTime = 0.f;

	/** 타겟 Actor가 유효한지 */
	bool HasValidTarget() const { return TargetActor.IsValid(); }
};
