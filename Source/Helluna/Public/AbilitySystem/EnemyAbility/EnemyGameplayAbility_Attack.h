// Capstone Project Helluna

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/HellunaEnemyGameplayAbility.h"
#include "EnemyGameplayAbility_Attack.generated.h"

/**
 * 적 공격 GA
 *
 * 처리 순서 ─────────────────────────────────────────────────────────────────
 *  1. 공격 몽타주 재생 (PlayMontageAndWait)
 *     - bEnraged == true이면 AttackMontageEnragedPlayRate 배율 적용
 *  2. 몽타주 완료 후 AttackRecoveryDelay 동안 이동 잠금 유지
 *     (이 시간 동안 PostAttackRotationRate로 타겟 방향으로 회전)
 *  3. 딜레이 완료 후 이동 잠금 해제 → StateTree Chase 재개
 *
 * 공격 주기에 영향을 주는 시간 ──────────────────────────────────────────────
 *  몽타주 길이 + AttackRecoveryDelay
 *  이 시간이 지나야 StateTree의 AttackCooldown이 재개된다.
 *
 * 네트워크 ──────────────────────────────────────────────────────────────────
 *  ServerOnly: 서버에서만 실행, 몽타주는 ASC가 클라이언트로 자동 동기화
 *  InstancingPolicy: InstancedPerActor (몬스터마다 1개 인스턴스)
 * ────────────────────────────────────────────────────────────────────────────
 */
UCLASS()
class HELLUNA_API UEnemyGameplayAbility_Attack : public UHellunaEnemyGameplayAbility
{
	GENERATED_BODY()

public:
	UEnemyGameplayAbility_Attack();

	/** StateTree AttackTask에서 설정하는 타겟 액터 */
	UPROPERTY(BlueprintReadWrite, Category = "Attack")
	TWeakObjectPtr<AActor> CurrentTarget = nullptr;

	/**
	 * 근거리 공격 직격 데미지.
	 * StateTree 의 STTask_AttackTarget 이 이 GA 를 발동할 때 사용된다.
	 * 광폭화 시에는 EnrageDamageMultiplier 가 곱해져서 최종 데미지가 결정된다.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "공격 설정",
		meta = (DisplayName = "공격 데미지",
			ToolTip = "근거리 공격이 타겟에게 적용하는 기본 데미지입니다.\n광폭화 상태에서는 에너미 캐릭터의 광폭화 데미지 배율이 곱해집니다.\n(예: 데미지 20 × 광폭화 배율 1.5 = 30)",
			ClampMin = "0.0"))
	float AttackDamage = 10.f;

	/** StateTree AttackTask에서 설정하는 데미지 판정 범위 */
	UPROPERTY(BlueprintReadWrite, Category = "Attack")
	float DamageRange = 150.f;

	/**
	 * 몽타주 완료 후 이동 잠금을 유지하는 시간 (초).
	 * 이 시간 동안 회전 후 회전 속도를 타겟 방향으로 회전.
	 * 전체 공격 주기 = 몽타주 길이 + 이 값 + 쿨다운(StateTree 설정)
	 */
	UPROPERTY(EditDefaultsOnly, Category = "공격 설정",
		meta = (DisplayName = "공격 후 딜레이 시간 (초)",
			ToolTip = "공격 몽타주가 끝난 후 이동을 잠금하는 시간입니다.\n이 시간 동안 캐릭터는 타겟 방향으로 회전합니다.\n전체 공격 주기 = 몽타주 길이 + 이 값 + 쿨다운(StateTree 설정)",
			ClampMin = "0.0", ClampMax = "2.0"))
	float AttackRecoveryDelay = 0.5f;

	/**
	 * 딜레이 시간 동안 타겟을 향해 회전하는 속도 (도/초).
	 * 0 = 기본 회전 속도 그대로.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "공격 설정",
		meta = (DisplayName = "공격 후 회전 속도 (도/초)",
			ToolTip = "공격 후 딜레이 시간 동안 타겟 방향으로 회전하는 속도입니다.\n0으로 설정하면 기본 이동 회전 속도를 그대로 사용합니다.\n권장값: 360 ~ 720",
			ClampMin = "0.0", ClampMax = "3600.0"))
	float PostAttackRotationRate = 720.f;

protected:
	//~ Begin UGameplayAbility Interface
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
	//~ End UGameplayAbility Interface

private:
	/** 몽타주 완료 후 호출 */
	UFUNCTION()
	void OnMontageCompleted();

	UFUNCTION()
	void OnMontageCancelled();

	/** 공격 완료 후처리 - 어빌리티 종료 */
	void HandleAttackFinished();

	/** 몽타주 완료 후 딜레이용 타이머 */
	FTimerHandle DelayedReleaseTimerHandle;

	/** 공격 후 회전 적용 전 원래 RotationRate 저장값 */
	FRotator SavedRotationRate = FRotator(0.f, 180.f, 0.f);
};
