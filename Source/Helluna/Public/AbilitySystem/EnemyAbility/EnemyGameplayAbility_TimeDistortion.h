// Capstone Project Helluna

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/HellunaEnemyGameplayAbility.h"
#include "EnemyGameplayAbility_TimeDistortion.generated.h"

class ABossPatternZoneBase;
class UNiagaraSystem;
class UNiagaraComponent;

/**
 * 보스 전용 시간 왜곡 GA (리팩토링 버전)
 *
 * GA는 두 가지만 담당한다:
 *  1. 시전 몽타주 재생
 *  2. PatternZoneClass(BP) 스폰 → 패턴 종료 시 제거
 *
 * 슬로우, Orb, VFX 등 패턴의 실제 로직은 Zone Actor가 전부 관리한다.
 * 이 구조로 다른 패턴도 동일한 GA 틀을 재사용할 수 있다.
 */
UCLASS()
class HELLUNA_API UEnemyGameplayAbility_TimeDistortion : public UHellunaEnemyGameplayAbility
{
	GENERATED_BODY()

public:
	UEnemyGameplayAbility_TimeDistortion();

	// =========================================================
	// 설정
	// =========================================================

	/** 스폰할 패턴 Zone BP 클래스 (BP에서 VFX/설정 구성) */
	UPROPERTY(EditDefaultsOnly, Category = "시간 왜곡",
		meta = (DisplayName = "패턴 Zone 클래스",
			ToolTip = "시간 왜곡 패턴의 로직과 VFX를 담당하는 BP 클래스입니다."))
	TSubclassOf<ABossPatternZoneBase> PatternZoneClass;

	/** 시전 시 재생할 몽타주 */
	UPROPERTY(EditDefaultsOnly, Category = "시간 왜곡",
		meta = (DisplayName = "시전 몽타주"))
	TObjectPtr<UAnimMontage> CastMontage = nullptr;

	/**
	 * 몽타주 시작 후 Zone을 활성화하기까지의 딜레이 (초).
	 * 이 시간 동안 시전 준비 VFX가 재생된다.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "시간 왜곡",
		meta = (DisplayName = "Zone 활성화 딜레이 (초)", ClampMin = "0.0", ClampMax = "5.0"))
	float ZoneActivateDelay = 1.0f;

	/** 패턴 지속 시간 (초). Zone에 전달되며 이 시간 동안 GA가 유지된다. */
	UPROPERTY(EditDefaultsOnly, Category = "시간 왜곡",
		meta = (DisplayName = "패턴 지속 시간 (초)", ClampMin = "0.5", ClampMax = "15.0"))
	float PatternDuration = 3.0f;

	/** GA 종료 후 다시 사용할 수 있기까지의 대기 시간 (초) */
	UPROPERTY(EditDefaultsOnly, Category = "시간 왜곡",
		meta = (DisplayName = "쿨타임 (초)", ClampMin = "0.0", ClampMax = "60.0"))
	float CooldownDuration = 10.f;

	/** 시전 준비 중 재생할 VFX (Zone 활성화 딜레이 동안) */
	UPROPERTY(EditDefaultsOnly, Category = "시간 왜곡|VFX",
		meta = (DisplayName = "시전 준비 VFX"))
	TObjectPtr<UNiagaraSystem> ChargingVFX = nullptr;

	UPROPERTY(EditDefaultsOnly, Category = "시간 왜곡|VFX",
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
	/** Zone 활성화 — 딜레이 후 호출 */
	void ActivateZone();

	/** Zone 패턴 종료 콜백 */
	UFUNCTION()
	void OnPatternFinished(bool bWasBroken);

	UFUNCTION()
	void OnMontageCompleted();

	UFUNCTION()
	void OnMontageCancelled();

	void HandleFinished(bool bWasCancelled);

	// =========================================================
	// 런타임 상태
	// =========================================================

	UPROPERTY(Transient)
	TObjectPtr<ABossPatternZoneBase> SpawnedZone = nullptr;

	FTimerHandle ZoneActivateTimerHandle;
	bool bPatternFinished = false;

	/** 마지막 GA 종료 시점 (월드 시간) — 쿨타임 계산용 */
	double LastAbilityEndTime = -9999.0;
};
