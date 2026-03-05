// Capstone Project Helluna

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/HellunaEnemyGameplayAbility.h"
#include "EnemyGameplayAbility_RangedAttack.generated.h"

class AHellunaProjectile_Enemy;

/**
 * 원거리 에너미 공격 GA
 *
 * 처리 순서 ─────────────────────────────────────────────────────────────────
 *  1. 공격 몽타주 재생 (PlayMontageAndWait)
 *     - bEnraged == true 이면 EnrageAttackMontagePlayRate 배율 적용
 *  2. 몽타주 길이 × LaunchDelayRatio / PlayRate 초 후 투사체 발사
 *     - 광폭화로 재생 속도가 빨라지면 발사 딜레이도 비례해서 짧아짐
 *  3. 몽타주 완료 후 AttackRecoveryDelay 대기 → 어빌리티 종료
 *
 * 네트워크 ──────────────────────────────────────────────────────────────────
 *  ServerOnly / InstancedPerActor
 *  투사체는 bReplicates = true 이므로 서버 스폰 → 클라 자동 동기화
 * ────────────────────────────────────────────────────────────────────────────
 */
UCLASS()
class HELLUNA_API UEnemyGameplayAbility_RangedAttack : public UHellunaEnemyGameplayAbility
{
	GENERATED_BODY()

public:
	UEnemyGameplayAbility_RangedAttack();

	// =========================================================
	// StateTree 에서 주입하는 값
	// =========================================================

	/** 공격 대상 액터 */
	UPROPERTY(BlueprintReadWrite, Category = "Attack")
	TWeakObjectPtr<AActor> CurrentTarget = nullptr;

	// =========================================================
	// 에디터 설정값
	// =========================================================

	/**
	 * 원거리 공격 직격 데미지.
	 * 투사체가 타겟에 명중했을 때 적용되는 기본 데미지다.
	 * 광폭화 시에는 에너미 캐릭터의 EnrageDamageMultiplier 가 곱해진다.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "공격 설정",
		meta = (DisplayName = "공격 데미지",
			ToolTip = "투사체가 타겟에 명중했을 때 적용하는 기본 데미지입니다.\n광폭화 상태에서는 에너미 캐릭터의 광폭화 데미지 배율이 곱해집니다.\n(예: 데미지 15 × 광폭화 배율 1.5 = 22.5)",
			ClampMin = "0.0"))
	float AttackDamage = 15.f;

	/**
	 * 발사할 투사체 클래스.
	 * HellunaProjectile_Enemy 를 부모로 만든 블루프린트를 설정한다.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "공격 설정",
		meta = (DisplayName = "투사체 클래스",
			ToolTip = "발사할 투사체 블루프린트를 설정합니다.\nHellunaProjectile_Enemy 를 부모로 만든 BP 를 지정하세요."))
	TSubclassOf<AHellunaProjectile_Enemy> ProjectileClass;

	/**
	 * 몽타주 전체 길이 중 투사체를 발사할 시점 비율 (0.0 ~ 1.0).
	 * 광폭화로 재생 속도가 빨라지면 실제 발사 시간도 비례하여 짧아진다.
	 * (발사 딜레이 = 몽타주 길이 / PlayRate × LaunchDelayRatio)
	 */
	UPROPERTY(EditDefaultsOnly, Category = "공격 설정",
		meta = (DisplayName = "발사 시점 비율 (0~1)",
			ToolTip = "몽타주 전체 길이 중 투사체를 발사할 시점 비율입니다.\n예) 0.3 → 몽타주 30% 시점에 발사\n광폭화로 재생 속도가 빨라지면 발사 시간도 비례해서 짧아집니다.",
			ClampMin = "0.0", ClampMax = "1.0"))
	float LaunchDelayRatio = 0.5f;

	/**
	 * 캐릭터 전방으로 띄울 발사 오프셋 (cm).
	 * 캡슐 반경보다 크게 설정해야 스폰 직후 자기 충돌을 방지할 수 있다.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "공격 설정",
		meta = (DisplayName = "전방 발사 오프셋 (cm)",
			ToolTip = "캐릭터 위치에서 앞으로 띄울 거리입니다.\n캡슐 반경보다 크게 설정해야 스폰 직후 자기 충돌을 방지할 수 있습니다.",
			ClampMin = "0.0", ClampMax = "500.0"))
	float LaunchForwardOffset = 100.f;

	/**
	 * 캐릭터 높이에서 위로 띄울 발사 오프셋 (cm).
	 * 총구/손 높이에 맞게 조정한다.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "공격 설정",
		meta = (DisplayName = "발사 높이 오프셋 (cm)",
			ToolTip = "캐릭터 위치에서 위로 띄울 높이입니다.\n몬스터의 총구나 손 높이에 맞게 조정하세요.",
			ClampMin = "0.0", ClampMax = "300.0"))
	float LaunchHeightOffset = 80.f;

	/**
	 * 투사체 비행 속도 (cm/s).
	 * 값이 클수록 타겟이 회피하기 어려워진다.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "공격 설정",
		meta = (DisplayName = "투사체 속도 (cm/s)",
			ToolTip = "투사체가 날아가는 속도입니다.\n값이 클수록 타겟이 피하기 어렵고, 작을수록 피하기 쉬워집니다.\n권장 범위: 1000 ~ 3000",
			ClampMin = "100.0", ClampMax = "10000.0"))
	float ProjectileSpeed = 1500.f;

	/**
	 * 투사체 수명 (초).
	 * 이 시간이 지나도 맞추지 못하면 소멸한다.
	 * 속도와 함께 조정해서 사거리를 결정한다. (사거리 ≈ 속도 × 수명)
	 */
	UPROPERTY(EditDefaultsOnly, Category = "공격 설정",
		meta = (DisplayName = "투사체 수명 (초)",
			ToolTip = "투사체가 맞추지 못했을 때 스스로 소멸하는 시간입니다.\n속도와 함께 사거리를 결정합니다. (사거리 ≈ 속도 × 수명)\n예) 속도 1500 × 수명 3초 = 최대 사거리 4500cm",
			ClampMin = "0.5", ClampMax = "10.0"))
	float ProjectileLifeSeconds = 3.f;

	/**
	 * 몽타주 완료 후 이동 잠금 유지 시간 (초).
	 * 이 시간이 지난 뒤 어빌리티가 종료되고 다음 행동이 가능해진다.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "공격 설정",
		meta = (DisplayName = "공격 후 딜레이 (초)",
			ToolTip = "몽타주가 끝난 후 잠시 멈추는 시간입니다.\n이 시간이 지나야 다음 이동/공격이 가능합니다.\n전체 공격 주기 = 몽타주 길이 + 이 값 + StateTree 쿨다운",
			ClampMin = "0.0", ClampMax = "2.0"))
	float AttackRecoveryDelay = 0.3f;

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
	UFUNCTION() void OnMontageCompleted();
	UFUNCTION() void OnMontageCancelled();

	void HandleAttackFinished();
	void SpawnAndLaunchProjectile();

	FTimerHandle LaunchTimerHandle;
	FTimerHandle DelayedReleaseTimerHandle;

	/** 중복 발사 방지 플래그 */
	bool bProjectileLaunched = false;
};
