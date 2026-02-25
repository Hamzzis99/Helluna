// Capstone Project Helluna

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/HellunaEnemyGameplayAbility.h"
#include "EnemyGameplayAbility_Attack.generated.h"

/**
 * 몬스터 공격 GA
 *
 * ─── 역할 ─────────────────────────────────────────────────────
 *  1. 공격 몽타주 재생 (PlayMontageAndWait)
 *  2. 몽타주 완료 후 AttackRecoveryDelay 동안 이동 잠금 유지
 *     (이 시간 동안 PostAttackRotationRate로 타겟 방향 빠르게 회전)
 *  3. 딜레이 완료 후 이동 잠금 해제 → StateTree Chase 재개
 *
 * ─── 공격 주기에서 이 GA가 차지하는 시간 ────────────────────
 *  몽타주 길이 + AttackRecoveryDelay
 *  이 시간이 끝나야 StateTree의 AttackCooldown이 새로 시작된다.
 *
 * ─── 네트워크 ─────────────────────────────────────────────────
 *  ServerOnly: 서버에서만 실행, 몽타주는 ASC가 클라이언트에 자동 동기화
 *  InstancingPolicy: InstancedPerActor (몬스터당 1개 인스턴스)
 * ─────────────────────────────────────────────────────────────
 */
UCLASS()
class HELLUNA_API UEnemyGameplayAbility_Attack : public UHellunaEnemyGameplayAbility
{
	GENERATED_BODY()

public:
	UEnemyGameplayAbility_Attack();

	/** StateTree AttackTask에서 설정할 타겟 정보 */
	UPROPERTY(BlueprintReadWrite, Category = "Attack")
	TWeakObjectPtr<AActor> CurrentTarget = nullptr;

	/** StateTree AttackTask에서 설정할 데미지 */
	UPROPERTY(BlueprintReadWrite, Category = "Attack")
	float AttackDamage = 10.f;

	/** StateTree AttackTask에서 설정할 데미지 판정 범위 */
	UPROPERTY(BlueprintReadWrite, Category = "Attack")
	float DamageRange = 150.f;

	/**
	 * 몽타주 완료 후 이동 잠금을 유지하는 시간 (초).
	 * 이 시간 동안 공격 후 회전 속도로 타겟 방향 회전.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "공격 설정",
		meta = (DisplayName = "공격 후 경직 시간 (초)",
			ToolTip = "공격 모션이 끝난 후 이동이 잠기는 시간입니다.\n이 시간 동안 빠르게 타겟 방향으로 회전합니다.\n체감 공격 주기 = 공격 모션 길이 + 이 값 + 쿨다운(StateTree 설정)",
			ClampMin = "0.0", ClampMax = "2.0"))
	float AttackRecoveryDelay = 0.5f;

	/**
	 * 경직 시간 동안 적용할 회전 속도 (도/초).
	 * 0 = 평소 회전 속도 그대로.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "공격 설정",
		meta = (DisplayName = "공격 후 회전 속도 (도/초)",
			ToolTip = "공격 후 경직 시간 동안 타겟 방향으로 회전하는 속도입니다.\n0으로 설정하면 평소 이동 회전 속도와 동일하게 동작합니다.\n권장값: 360 ~ 720",
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
	/** 몽타주 완료 시 호출 */
	UFUNCTION()
	void OnMontageCompleted();

	UFUNCTION()
	void OnMontageCancelled();

	/** 공격 완료 후처리 - 데미지 적용 */
	void HandleAttackFinished();

	/** 몽타주 완료 후 딜레이 타이머 */
	FTimerHandle DelayedReleaseTimerHandle;

	/** 공격 후 회전 적용 전 원래 RotationRate 저장값 */
	FRotator SavedRotationRate = FRotator(0.f, 180.f, 0.f);
};
