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

	/**
	 * [ShipJumpV8.Apex] 우주선 상단 위로 추가 확보할 정점 여유 높이 (cm).
	 * 값이 클수록 정점이 우주선 상단보다 높아져 측면/기울어진 날개에 걸릴 확률 감소.
	 * 기본 500 (V7 의 하드코딩 300에서 상향).
	 */
	UPROPERTY(EditDefaultsOnly, Category = "점프",
		meta = (DisplayName = "정점 여유 고도 (cm)",
			ToolTip = "우주선 상단 + 이 값 = 포물선 정점 높이. 크게 잡을수록 우주선 측면 충돌 확률↓ (체공 시간 길어짐).",
			ClampMin = "50.0", ClampMax = "2000.0"))
	float ApexClearance = 500.f;

	// =========================================================
	// [ShipJumpSpreadV1] 다중 점프 분산
	// =========================================================

	/**
	 * STTask_ShipJump 가 슬롯 매니저로부터 받은 슬롯 인덱스를 주입.
	 * -1 이면 분산 없이 정중앙 궤적 (단일 점프 호환).
	 */
	UPROPERTY(BlueprintReadWrite, Category = "Attack")
	int32 AssignedSlotIndex = INDEX_NONE;

	/** 분산 시 동시에 점프 가능한 최대 슬롯 수 (부채꼴 분할 단위). 슬롯 매니저의 MaxTopSlots 와 같게 유지. */
	UPROPERTY(EditDefaultsOnly, Category = "점프|분산",
		meta = (DisplayName = "분산 슬롯 수",
			ToolTip = "이 값으로 부채꼴을 균등 분할. 슬롯 매니저의 MaxTopSlots 와 동일하게 두기.",
			ClampMin = "1", ClampMax = "20"))
	int32 SpreadSlotCount = 10;

	/**
	 * 슬롯별 좌우 부채꼴 최대 각도 (도). 인덱스 0 은 한쪽 끝, 마지막 인덱스는 반대쪽 끝.
	 * Vxy 는 1/cos(yaw) 보정되어 forward 도달거리는 yaw 와 무관하게 보존됨 → 우주선 옆 빗나감 방지.
	 * 그래도 너무 크면 측면 거리가 ShipExtent.Y 를 초과해 옆으로 떨어질 수 있으므로 15~25 권장.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "점프|분산",
		meta = (DisplayName = "좌우 부채꼴 각도 (도)",
			ToolTip = "0 = 분산 없음. 15~25 = 안전 부채꼴. cos 보정으로 forward 거리는 보존.",
			ClampMin = "0.0", ClampMax = "60.0"))
	float SpreadFanHalfAngleDeg = 20.f;

	// =========================================================
	// [ShipJumpAirCollisionV1] 공중 충돌 해제
	// =========================================================

	/**
	 * 공중 동안 다른 Pawn 과 캡슐 충돌을 무시. 착지 시 복구.
	 * Why: 동시 점프 시 공중에서 서로 부딪혀 한 명이 못 올라가는 현상 방지.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "점프|충돌",
		meta = (DisplayName = "공중 동안 Pawn 충돌 해제",
			ToolTip = "ON: 공중에서 몬스터끼리 통과. 착지 시 자동 복구."))
	bool bIgnorePawnCollisionInAir = true;

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

	// [ShipJumpAirCollisionV1] 공중 진입 시점에 캡슐 ECC_Pawn 응답을 백업해뒀다가 착지 후 복구.
	bool bPawnCollisionOverridden = false;
	TEnumAsByte<ECollisionResponse> SavedPawnResponse = ECR_Block;

	void EnableAirCollisionPassthrough();
	void RestorePawnCollision();
};
