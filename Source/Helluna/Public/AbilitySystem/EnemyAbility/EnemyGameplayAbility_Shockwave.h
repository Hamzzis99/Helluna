// Capstone Project Helluna

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/HellunaEnemyGameplayAbility.h"
#include "EnemyGameplayAbility_Shockwave.generated.h"

class ABossPatternZoneBase;
class UNiagaraSystem;

/**
 * 보스 파동 패턴 GA
 *
 * 보스 주변에 파동이 퍼져나가며 닿은 플레이어에게 데미지를 주는 패턴.
 * GA는 몽타주 재생 + ShockwaveZone 스폰/관리만 담당하고,
 * 파동의 실제 로직(확장, 피격 판정, 데미지)은 Zone Actor가 처리한다.
 */
UCLASS()
class HELLUNA_API UEnemyGameplayAbility_Shockwave : public UHellunaEnemyGameplayAbility
{
	GENERATED_BODY()

public:
	UEnemyGameplayAbility_Shockwave();

	// =========================================================
	// 설정
	// =========================================================

	/** 스폰할 파동 Zone BP 클래스 */
	UPROPERTY(EditDefaultsOnly, Category = "파동",
		meta = (DisplayName = "파동 Zone 클래스"))
	TSubclassOf<ABossPatternZoneBase> PatternZoneClass;

	/** 시전 몽타주 */
	UPROPERTY(EditDefaultsOnly, Category = "파동",
		meta = (DisplayName = "시전 몽타주"))
	TObjectPtr<UAnimMontage> CastMontage = nullptr;

	/** 몽타주 시작 후 Zone 활성화까지의 딜레이 (초) */
	UPROPERTY(EditDefaultsOnly, Category = "파동",
		meta = (DisplayName = "Zone 활성화 딜레이 (초)", ClampMin = "0.0", ClampMax = "5.0"))
	float ZoneActivateDelay = 0.5f;

	/** 패턴 지속 시간 (초) — Zone에 전달 */
	UPROPERTY(EditDefaultsOnly, Category = "파동",
		meta = (DisplayName = "패턴 지속 시간 (초)", ClampMin = "0.5", ClampMax = "30.0"))
	float PatternDuration = 6.0f;

	/** 쿨타임 (초) */
	UPROPERTY(EditDefaultsOnly, Category = "파동",
		meta = (DisplayName = "쿨타임 (초)", ClampMin = "0.0", ClampMax = "60.0"))
	float CooldownDuration = 8.f;

	/** 시전 준비 VFX */
	UPROPERTY(EditDefaultsOnly, Category = "파동|VFX",
		meta = (DisplayName = "시전 준비 VFX"))
	TObjectPtr<UNiagaraSystem> ChargingVFX = nullptr;

	UPROPERTY(EditDefaultsOnly, Category = "파동|VFX",
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
