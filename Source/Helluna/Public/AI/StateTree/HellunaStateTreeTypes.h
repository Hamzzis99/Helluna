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
 * Evaluator가 매 틱 계산해서 채워주는 타겟 정보.
 * Task와 Condition이 ExternalData로 읽어 사용한다.
 */
USTRUCT(BlueprintType)
struct HELLUNA_API FHellunaAITargetData
{
	GENERATED_BODY()

	/** 현재 추적 대상 Actor */
	UPROPERTY()
	TWeakObjectPtr<AActor> TargetActor = nullptr;

	/** 타겟 종류 */
	UPROPERTY()
	EHellunaTargetType TargetType = EHellunaTargetType::SpaceShip;

	/** 타겟까지 거리 (매 틱 갱신) */
	UPROPERTY()
	float DistanceToTarget = 0.f;

	/** 타겟이 유효한지 */
	bool IsValid() const { return TargetActor.IsValid(); }
};
