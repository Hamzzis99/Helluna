/**
 * HellunaStateTreeBossTypes.h
 *
 * 보스 StateTree 전용 타입 정의 (패턴 시스템).
 *
 * ─── 설계 원칙 ──────────────────────────────────────────────
 * 기본 타겟 데이터는 FHellunaAITargetData를 그대로 재활용한다.
 * 기존 STTask_ChaseTarget, STCondition_InRange 등을 바인딩 호환으로 사용.
 * 이 헤더에는 보스 고유의 패턴 시스템 타입만 정의한다.
 *
 * ─── 패턴 트리거 종류 ───────────────────────────────────────
 *  Timer          : 전투 시작 후 N초마다 발동
 *  HPThreshold    : 보스 HP가 특정 비율 이하로 떨어지면 발동
 *  ProximityTimer : 플레이어가 범위 내에 N초 이상 머물면 발동
 *
 * @author 김민우
 */

#pragma once

#include "CoreMinimal.h"
#include "HellunaStateTreeBossTypes.generated.h"

/** 보스 패턴 트리거 유형 */
UENUM(BlueprintType)
enum class EBossPatternTriggerType : uint8
{
	Timer           UMETA(DisplayName = "시간제"),
	HPThreshold     UMETA(DisplayName = "HP 기준"),
	ProximityTimer  UMETA(DisplayName = "근접 시간제"),
};

/**
 * 보스 패턴 정의 한 줄.
 * StateTree 에디터에서 배열로 관리한다.
 */
USTRUCT(BlueprintType)
struct HELLUNA_API FBossPatternEntry
{
	GENERATED_BODY()

	/** 이 패턴을 발동시킬 GA 클래스 */
	UPROPERTY(EditAnywhere, Category = "Pattern",
		meta = (DisplayName = "패턴 GA 클래스"))
	TSubclassOf<class UHellunaEnemyGameplayAbility> PatternAbilityClass;

	/** 트리거 유형 */
	UPROPERTY(EditAnywhere, Category = "Pattern",
		meta = (DisplayName = "트리거 유형"))
	EBossPatternTriggerType TriggerType = EBossPatternTriggerType::Timer;

	/**
	 * Timer: 전투 시작 후 이 시간(초)마다 발동.
	 * ProximityTimer: 플레이어 근접 후 이 시간(초) 경과 시 발동.
	 * HPThreshold: 사용 안 함.
	 */
	UPROPERTY(EditAnywhere, Category = "Pattern",
		meta = (DisplayName = "트리거 시간 (초)", ClampMin = "0.0",
			EditCondition = "TriggerType != EBossPatternTriggerType::HPThreshold"))
	float TriggerTime = 15.f;

	/**
	 * HPThreshold: 보스 HP 비율(0~1)이 이 값 이하이면 발동.
	 * Timer/ProximityTimer: 사용 안 함.
	 */
	UPROPERTY(EditAnywhere, Category = "Pattern",
		meta = (DisplayName = "HP 기준 비율 (0~1)", ClampMin = "0.0", ClampMax = "1.0",
			EditCondition = "TriggerType == EBossPatternTriggerType::HPThreshold"))
	float HPThreshold = 0.5f;

	/**
	 * ProximityTimer: 플레이어가 이 범위(cm) 안에 있어야 타이머 누적.
	 * Timer/HPThreshold: 사용 안 함.
	 */
	UPROPERTY(EditAnywhere, Category = "Pattern",
		meta = (DisplayName = "근접 판정 범위 (cm)", ClampMin = "100.0",
			EditCondition = "TriggerType == EBossPatternTriggerType::ProximityTimer"))
	float ProximityRange = 800.f;

	/** 발동 후 쿨다운 (초). 0이면 1회만 발동. */
	UPROPERTY(EditAnywhere, Category = "Pattern",
		meta = (DisplayName = "쿨다운 (초)", ClampMin = "0.0"))
	float Cooldown = 0.f;

	/** 이미 발동했는지 (1회성 패턴용, 런타임) */
	bool bTriggered = false;

	/** 현재 쿨다운 남은 시간 (런타임) */
	float CooldownRemaining = 0.f;

	/** 누적 타이머 (런타임 — Timer/ProximityTimer 공용) */
	float AccumulatedTime = 0.f;
};

/**
 * 보스 패턴 감시 데이터 (Evaluator → Pattern Task 전달용).
 * 기본 타겟 정보는 FHellunaAITargetData를 사용하고,
 * 이 구조체는 패턴 시스템 고유 필드만 담는다.
 */
USTRUCT(BlueprintType)
struct HELLUNA_API FBossPatternData
{
	GENERATED_BODY()

	/** 보스 HP 비율 (0~1) */
	UPROPERTY()
	float HPRatio = 1.f;

	/** 전투 시작 후 경과 시간 (초) */
	UPROPERTY()
	float CombatElapsedTime = 0.f;

	/** 발동 요청된 패턴 인덱스 (-1이면 없음) */
	UPROPERTY()
	int32 PendingPatternIndex = -1;

	/** 패턴 실행 중 여부 (Chase/Attack Task가 이 값을 읽어 멈춤) */
	UPROPERTY()
	bool bPatternActive = false;
};
