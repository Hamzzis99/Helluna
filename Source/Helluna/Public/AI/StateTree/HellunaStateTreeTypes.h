/**
 * HellunaStateTreeTypes.h
 *
 * Actor 기반 StateTree에서 공통으로 사용하는 타입 정의.
 * Evaluator가 매 틱 계산한 타겟 정보를 Task/Condition이 공유한다.
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
 * STEvaluator_TargetSelector가 매 틱 채워주는 플레이어 타겟 정보.
 * Run/Attack State의 Task가 바인딩해서 사용한다.
 */
USTRUCT(BlueprintType)
struct HELLUNA_API FHellunaAITargetData
{
	GENERATED_BODY()

	/** 현재 추적 중인 플레이어 Actor */
	UPROPERTY()
	TWeakObjectPtr<AActor> TargetActor = nullptr;

	/** 타겟 종류 */
	UPROPERTY()
	EHellunaTargetType TargetType = EHellunaTargetType::SpaceShip;

	/** 타겟까지 거리 (매 틱 갱신) */
	UPROPERTY()
	float DistanceToTarget = 0.f;

	/** 플레이어를 추적 중인지 */
	UPROPERTY()
	bool bTargetingPlayer = false;

	/**
	 * 플레이어 락온 플래그.
	 * STTask_Enrage 진입 시 true → Evaluator가 타겟을 바꾸지 않는다.
	 */
	UPROPERTY()
	bool bPlayerLocked = false;

	/**
	 * 현재 광폭화 상태인지.
	 * STTask_Enrage EnterState에서 true, ExitState에서 false.
	 * Evaluator가 이벤트 중복 발송 방지에 사용.
	 */
	UPROPERTY()
	bool bEnraged = false;

	/**
	 * 플레이어를 타겟으로 삼기 시작한 후 경과 시간 (초).
	 * EnrageDelay 이상이 되면 Evaluator가 광폭화 이벤트를 발송한다.
	 */
	UPROPERTY()
	float PlayerTargetingTime = 0.f;

	bool IsTargetValid() const { return TargetActor.IsValid(); }
};

/**
 * STEvaluator_SpaceShip이 매 틱 채워주는 우주선 타겟 정보.
 * EnrageLoop State의 Task가 바인딩해서 사용한다.
 * 광폭화 후 타겟 전환을 코드 없이 에디터 바인딩만으로 해결한다.
 */
USTRUCT(BlueprintType)
struct HELLUNA_API FHellunaSpaceShipTargetData
{
	GENERATED_BODY()

	/** 우주선 Actor (BeginPlay에서 한 번 탐색 후 캐싱) */
	UPROPERTY()
	TWeakObjectPtr<AActor> TargetActor = nullptr;

	/** 우주선까지 거리 (매 틱 갱신) */
	UPROPERTY()
	float DistanceToTarget = 0.f;

	bool IsTargetValid() const { return TargetActor.IsValid(); }
};
