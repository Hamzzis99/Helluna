// Capstone Project Helluna

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/HellunaEnemyGameplayAbility.h"
#include "EnemyGameplayAbility_PhantomBlades.generated.h"

class ABossPatternZoneBase;
class UNiagaraSystem;

/**
 * 보스 환영 검무 패턴 GA
 *
 * 리사주 곡선을 따라 비행하는 팬텀 검 + 잔상 트레일.
 * GA는 몽타주 + Zone 스폰/관리만 담당.
 */
UCLASS()
class HELLUNA_API UEnemyGameplayAbility_PhantomBlades : public UHellunaEnemyGameplayAbility
{
	GENERATED_BODY()

public:
	UEnemyGameplayAbility_PhantomBlades();

	UPROPERTY(EditDefaultsOnly, Category = "환영 검무",
		meta = (DisplayName = "패턴 Zone 클래스"))
	TSubclassOf<ABossPatternZoneBase> PatternZoneClass;

	UPROPERTY(EditDefaultsOnly, Category = "환영 검무",
		meta = (DisplayName = "시전 몽타주"))
	TObjectPtr<UAnimMontage> CastMontage = nullptr;

	UPROPERTY(EditDefaultsOnly, Category = "환영 검무",
		meta = (DisplayName = "Zone 활성화 딜레이 (초)", ClampMin = "0.0", ClampMax = "5.0"))
	float ZoneActivateDelay = 1.0f;

	UPROPERTY(EditDefaultsOnly, Category = "환영 검무",
		meta = (DisplayName = "패턴 지속 시간 (초)", ClampMin = "1.0", ClampMax = "60.0"))
	float PatternDuration = 18.0f;

	UPROPERTY(EditDefaultsOnly, Category = "환영 검무",
		meta = (DisplayName = "쿨타임 (초)", ClampMin = "0.0", ClampMax = "60.0"))
	float CooldownDuration = 15.f;

	UPROPERTY(EditDefaultsOnly, Category = "환영 검무|VFX",
		meta = (DisplayName = "시전 준비 VFX"))
	TObjectPtr<UNiagaraSystem> ChargingVFX = nullptr;

	UPROPERTY(EditDefaultsOnly, Category = "환영 검무|VFX",
		meta = (DisplayName = "시전 준비 VFX 크기", ClampMin = "0.01", ClampMax = "20.0"))
	float ChargingVFXScale = 1.f;

protected:
	virtual bool CanActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags = nullptr,
		const FGameplayTagContainer* TargetTags = nullptr,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;

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
	void ActivateZone();

	UFUNCTION()
	void OnPatternFinished(bool bWasBroken);

	UFUNCTION()
	void OnMontageCompleted();

	UFUNCTION()
	void OnMontageCancelled();

	void HandleFinished(bool bWasCancelled);

	UPROPERTY(Transient)
	TObjectPtr<ABossPatternZoneBase> SpawnedZone = nullptr;

	FTimerHandle ZoneActivateTimerHandle;
	bool bPatternFinished = false;
	double LastAbilityEndTime = -9999.0;
};
