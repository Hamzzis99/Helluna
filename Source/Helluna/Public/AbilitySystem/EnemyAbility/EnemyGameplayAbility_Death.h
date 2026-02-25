// Capstone Project Helluna

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/HellunaEnemyGameplayAbility.h"
#include "EnemyGameplayAbility_Death.generated.h"

/**
 * 몬스터 사망 처리 GA
 *
 * 역할:
 *   1. 사망 몽타주 재생 (PlayMontageAndWait)
 *   2. 몽타주 완료 시 StateTree DeathTask에 콜백 전달 (bMontageFinished = true)
 *   3. DeathTask Tick에서 Entity 제거 + Actor 삭제
 *
 * 발동 시점:
 *   StateTree DeathTask::EnterState 에서 TryActivateAbilityByClass 로 발동
 *
 * 주의:
 *   - NetExecutionPolicy: ServerOnly (서버에서만 실행, 몽타주는 Multicast로 전파)
 *   - InstancingPolicy: InstancedPerActor (몬스터당 1개 인스턴스)
 */
UCLASS()
class HELLUNA_API UEnemyGameplayAbility_Death : public UHellunaEnemyGameplayAbility
{
	GENERATED_BODY()

public:
	UEnemyGameplayAbility_Death();

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
	/** 몽타주 재생이 완료(또는 중단)됐을 때 호출 */
	UFUNCTION()
	void OnMontageCompleted();

	UFUNCTION()
	void OnMontageCancelled();

	/** 몽타주 완료 후 사망 후처리 공통 로직 */
	void HandleDeathFinished();

	/** 후처리가 이미 실행됐는지 체크 (중복 방지) */
	bool bDeathHandled = false;
};
