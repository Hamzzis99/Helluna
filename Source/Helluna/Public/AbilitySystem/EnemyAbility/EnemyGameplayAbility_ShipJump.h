/**
 * EnemyGameplayAbility_ShipJump.h
 *
 * [ShipJumpV1] 테스트용 GA — 몬스터가 우주선 어택존에 닿은 순간 발동되면
 * 우주선 상단으로 포물선 점프해 착지. 중력 기반으로 착지 정확도를 보장.
 *
 * 처리 순서
 *  1. CurrentTarget(우주선)의 ComponentsBoundingBox로 상단 Z 계산
 *  2. Apex 높이 = ShipTop + OvershootHeight
 *  3. 중력 기반 LaunchVelocity 계산 → LaunchCharacter
 *  4. 착지 감지(IsMovingOnGround) → AttackRecoveryDelay 후 EndAbility
 *
 *  ServerOnly / InstancedPerActor
 */

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/HellunaEnemyGameplayAbility.h"
#include "EnemyGameplayAbility_ShipJump.generated.h"

UCLASS()
class HELLUNA_API UEnemyGameplayAbility_ShipJump : public UHellunaEnemyGameplayAbility
{
	GENERATED_BODY()

public:
	UEnemyGameplayAbility_ShipJump();

	/** STTask_AttackTarget이 주입하는 공격 대상(우주선). */
	UPROPERTY(BlueprintReadWrite, Category = "Attack")
	TWeakObjectPtr<AActor> CurrentTarget = nullptr;

	/** 우주선 상단을 기준으로 얼마나 더 높이 정점까지 올라갈지 (cm). */
	UPROPERTY(EditDefaultsOnly, Category = "점프",
		meta = (DisplayName = "오버슛 높이 (cm)",
			ToolTip = "우주선 최상단 위로 추가로 올라가는 정점 높이. 값이 클수록 높게 점프.",
			ClampMin = "20.0", ClampMax = "2000.0"))
	float OvershootHeight = 400.f;

	/** 타겟 없을 때 사용할 기본 수평 속도 (cm/s). */
	UPROPERTY(EditDefaultsOnly, Category = "점프",
		meta = (DisplayName = "폴백 수평 속도 (cm/s)",
			ClampMin = "100.0", ClampMax = "3000.0"))
	float FallbackHorizontalSpeed = 800.f;

	/**
	 * 우주선 중심 조준 시 수평 속도 상한 (cm/s).
	 * Vxy = Distance / TimeToApex 가 이 값을 초과하면 clamp — 너무 멀 때 초고속 비행 방지.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "점프",
		meta = (DisplayName = "최대 수평 속도 (cm/s)",
			ClampMin = "200.0", ClampMax = "5000.0"))
	float MaxHorizontalSpeed = 1500.f;

	/** 타겟 없을 때 사용할 기본 수직 속도 (cm/s). */
	UPROPERTY(EditDefaultsOnly, Category = "점프",
		meta = (DisplayName = "폴백 수직 속도 (cm/s)",
			ClampMin = "100.0", ClampMax = "2000.0"))
	float FallbackVerticalSpeed = 900.f;

	/** 착지 후 종료 대기 시간 (초). */
	UPROPERTY(EditDefaultsOnly, Category = "점프",
		meta = (DisplayName = "착지 후 딜레이 (초)",
			ClampMin = "0.0", ClampMax = "3.0"))
	float AttackRecoveryDelay = 0.8f;

	/** 안전장치: 착지 감지가 이 시간 내 실패하면 강제 종료 (초). */
	UPROPERTY(EditDefaultsOnly, Category = "점프",
		meta = (DisplayName = "최대 체공 시간 (초)",
			ClampMin = "1.0", ClampMax = "15.0"))
	float MaxAirborneTime = 6.f;

protected:
	virtual void ActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	virtual void EndAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility,
		bool bWasCancelled) override;

private:
	void PerformJump();
	void OnLanded();
	void HandleAttackFinished();
	FVector ComputeLaunchVelocityToShipTop(const class AHellunaEnemyCharacter* Enemy, AActor* Ship) const;

	FTimerHandle LandingCheckTimerHandle;
	FTimerHandle DelayedReleaseTimerHandle;
	FTimerHandle AirborneFailsafeHandle;
	bool bHasLanded = false;
};
