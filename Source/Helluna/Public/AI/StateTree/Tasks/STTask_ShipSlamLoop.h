/**
 * STTask_ShipSlamLoop.h
 *
 * [ShipSlamV1] 우주선 상단 내려찍기 루프 Task.
 *
 * 목적:
 *  - V3 Attackspaceship_jump_Rage 의 `OnStateCompleted → self` 자기루프 제거.
 *    transition 재진입 시 delay 미설정 + GA 몬타지 재시작 타이밍 겹침으로 움찔 발생.
 *  - 하나의 Task 내부에서 공격 GA 발동 → 쿨다운 대기 → 다음 GA 발동을 반복.
 *  - 우주선에서 이탈 감지 시 OnShip 태그를 제거하고 Failed 리턴 → 상위 재선택.
 *
 * 최적화 (오픈월드 100+ 몬스터 가정):
 *  - ShipCheckInterval(기본 0.5초) 단위로만 floor/tag 검사. 매 틱 검사 X.
 *  - 공격 몬타주 재생 중에는 회전 생략 (AttackTarget 패턴 재사용).
 *  - ASC Ability 탐색을 매 Tick 2번에서 최소화.
 *
 * @author minwoo (V4)
 */

#pragma once

#include "CoreMinimal.h"
#include "Tasks/StateTreeAITask.h"
#include "AI/StateTree/HellunaStateTreeTypes.h"
#include "STTask_ShipSlamLoop.generated.h"

class AAIController;
class UHellunaEnemyGameplayAbility;

USTRUCT()
struct FSTTask_ShipSlamLoopInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AAIController> AIController = nullptr;

	UPROPERTY(EditAnywhere, Category = "Input")
	FHellunaAITargetData TargetData;

	/** 다음 공격까지 남은 쿨다운(초). */
	UPROPERTY()
	float CooldownRemaining = 0.f;

	/** 다음 우주선 이탈 체크 예정 시간(초, WorldTime 기준). */
	UPROPERTY()
	float NextShipCheckTime = 0.f;

	/** 우주선 이탈 감지 연속 카운트(최적화: 단일 틱 Falling 은 LaunchCharacter 직후 발생 가능 → 2회 연속 감지 시만 판단). */
	UPROPERTY()
	int32 FallDetectStreak = 0;
};

USTRUCT(meta = (DisplayName = "Helluna: Ship Slam Loop", Category = "Helluna|AI"))
struct HELLUNA_API FSTTask_ShipSlamLoop : public FStateTreeAITaskBase
{
	GENERATED_BODY()

	using FInstanceDataType = FSTTask_ShipSlamLoopInstanceData;

protected:
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;

	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, float DeltaTime) const override;

	virtual void ExitState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;

public:
	/** 내려찍기 GA 클래스 (기본: GA_Melee_MeleeAttack_Jump). */
	UPROPERTY(EditAnywhere, Category = "설정",
		meta = (DisplayName = "내려찍기 어빌리티 클래스"))
	TSubclassOf<UHellunaEnemyGameplayAbility> SlamAbilityClass;

	/** GA 종료 후 다음 공격까지 대기 시간(초). */
	UPROPERTY(EditAnywhere, Category = "설정",
		meta = (DisplayName = "공격 쿨다운(초)", ClampMin = "0.0"))
	float AttackCooldown = 1.2f;

	/** 상태 최초 진입 후 첫 공격 전 지연(초). 착지 직후 바로 때리지 않게 하는 여유. */
	UPROPERTY(EditAnywhere, Category = "설정",
		meta = (DisplayName = "첫 공격 전 딜레이(초)", ClampMin = "0.0"))
	float InitialAttackDelay = 0.25f;

	/** 우주선 이탈 체크 주기(초). 낮출수록 반응 빠르나 CPU 부담 증가. */
	UPROPERTY(EditAnywhere, Category = "설정",
		meta = (DisplayName = "우주선 이탈 체크 주기(초)", ClampMin = "0.1", ClampMax = "2.0"))
	float ShipCheckInterval = 0.5f;

	/** 쿨다운 중 타겟 방향 회전 속도. */
	UPROPERTY(EditAnywhere, Category = "설정",
		meta = (DisplayName = "대기 중 회전 속도", ClampMin = "0.0"))
	float RotationSpeed = 30.f;
};
