// Capstone Project Helluna

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/HellunaEnemyGameplayAbility.h"
#include "EnemyGameplayAbility_BossDeath.generated.h"

class UAnimMontage;
class UAbilityTask_PlayMontageAndWait;

/**
 * [BossDeathV1] 보스 전용 사망 GA — GA_Death 와 분리.
 *
 * 차이점:
 *  - EarlyFinishTimer 없음 — 사망 몽타주 자연 종료까지 GA active 유지
 *  - StateTree 우회 — 보스 OnMonsterDeath 가 직접 활성화
 *  - 자체 lifecycle 책임: 몽타주 끝 → DespawnMassEntity → EndGame trigger
 */
UCLASS()
class HELLUNA_API UEnemyGameplayAbility_BossDeath : public UHellunaEnemyGameplayAbility
{
	GENERATED_BODY()

public:
	UEnemyGameplayAbility_BossDeath();

	virtual void ActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

protected:
	UFUNCTION()
	void OnMontageCompleted();

	UFUNCTION()
	void OnMontageCancelled();

	void HandleBossDeathFinished();

	bool bDeathHandled = false;
};
