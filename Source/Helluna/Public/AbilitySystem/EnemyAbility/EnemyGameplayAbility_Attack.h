// Capstone Project Helluna

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/HellunaEnemyGameplayAbility.h"
#include "EnemyGameplayAbility_Attack.generated.h"

/**
 * 몬스터 공격 GA
 *
 * 역할:
 *   1. 공격 몽타주 재생 (PlayMontageAndWait)
 *   2. 몽타주 완료 시 타겟에게 데미지 적용
 *   3. StateTree AttackTask에 완료 알림
 *
 * 발동 시점:
 *   StateTree AttackTask::Tick 에서 쿨다운 완료 시 TryActivateAbilityByClass로 발동
 *
 * 주의:
 *   - NetExecutionPolicy: ServerOnly (서버에서만 실행, 몽타주는 ASC 자동 동기화)
 *   - InstancingPolicy: InstancedPerActor (몬스터당 1개 인스턴스)
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

	/** 공격 완료 후 이동 재개까지 대기 시간 (초) */
	UPROPERTY(EditDefaultsOnly, Category = "Attack",
		meta = (DisplayName = "공격 후 대기 시간 (초)",
			ToolTip = "공격 몽타주가 끝난 후 이동을 재개하기까지 대기할 시간입니다.\n0 = 즉시 재개, 0.2~0.5 = 자연스러운 여운",
			ClampMin = "0.0", ClampMax = "2.0"))
	float AttackRecoveryDelay = 0.2f;

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

	/** 몽타주 완료 후 딜레이 태그 제거 타이머 */
	FTimerHandle DelayedReleaseTimerHandle;
};
