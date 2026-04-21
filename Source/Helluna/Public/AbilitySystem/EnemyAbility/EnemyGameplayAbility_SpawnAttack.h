// Capstone Project Helluna

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/HellunaEnemyGameplayAbility.h"
#include "EnemyGameplayAbility_SpawnAttack.generated.h"

class UAnimMontage;
class UNiagaraSystem;
class ABossPatternZoneBase;

/**
 * [SpawnAttackV1] 범용 BP 소환 GA.
 *
 * 시전 모션 + 시전 VFX 후 지정된 BP 액터를 보스 위치에 소환.
 * 일정 시간 후 소환 액터를 자동으로 정리한다.
 *
 * PhantomBlades GA 의 일반화 버전. 보스 패턴 Zone 외에도 일반 AActor 모두 소환 가능.
 *
 * 쿨타임은 GA 의 표준 CooldownGameplayEffect 시스템에 위임.
 *
 * ServerOnly / InstancedPerActor.
 */
UCLASS()
class HELLUNA_API UEnemyGameplayAbility_SpawnAttack : public UHellunaEnemyGameplayAbility
{
	GENERATED_BODY()

public:
	UEnemyGameplayAbility_SpawnAttack();

	/** 소환할 BP 액터 클래스. 비워두면 GA 가 즉시 종료. */
	UPROPERTY(EditDefaultsOnly, Category = "소환 공격",
		meta = (DisplayName = "소환할 BP 클래스"))
	TSubclassOf<AActor> SpawnedActorClass;

	/**
	 * [SpawnOffsetV1] 보스 로컬 좌표 기준 스폰 오프셋 (cm).
	 *   X = 전방(보스 정면), Y = 우측, Z = 수직.
	 *   예: (200, 0, -90)  → 보스 전방 2m + 발밑 90cm 지점에 스폰.
	 *   예: (0, 0, 0)      → 보스 루트 위치 그대로.
	 *   보스 Yaw 만 반영 (Pitch/Roll 은 경사 지형에서 파동이 기울어지지 않도록 무시).
	 */
	UPROPERTY(EditDefaultsOnly, Category = "소환 공격",
		meta = (DisplayName = "스폰 오프셋 (로컬 cm)"))
	FVector SpawnOffset = FVector::ZeroVector;

	/** 시전 시 재생할 보스 몬타주. 비우면 모션 없이 진행. */
	UPROPERTY(EditDefaultsOnly, Category = "소환 공격",
		meta = (DisplayName = "시전 몽타주"))
	TObjectPtr<UAnimMontage> CastMontage = nullptr;

	/** ActivateAbility 시점부터 BP 액터 스폰까지 대기 시간 (초). */
	UPROPERTY(EditDefaultsOnly, Category = "소환 공격",
		meta = (DisplayName = "BP 활성화 딜레이 (초)",
			ToolTip = "시전 모션/VFX 노출 후 이 시간이 지나면 BP 액터 스폰.",
			ClampMin = "0.0", ClampMax = "10.0"))
	float SpawnDelay = 1.f;

	/** BP 액터를 유지할 시간 (초). 0 이면 자동 정리 비활성. */
	UPROPERTY(EditDefaultsOnly, Category = "소환 공격",
		meta = (DisplayName = "BP 지속 시간 (초)",
			ToolTip = "스폰 후 이 시간이 지나면 BP 액터 Destroy + GA 종료. 0 이면 자동 정리 안 함.",
			ClampMin = "0.0", ClampMax = "120.0"))
	float SpawnedActorLifetime = 18.f;

	/** 시전 동안 보스에게 부착해 재생할 VFX. */
	UPROPERTY(EditDefaultsOnly, Category = "소환 공격|VFX",
		meta = (DisplayName = "시전 VFX"))
	TObjectPtr<UNiagaraSystem> CastVFX = nullptr;

	/** 시전 VFX 크기 배율. */
	UPROPERTY(EditDefaultsOnly, Category = "소환 공격|VFX",
		meta = (DisplayName = "시전 VFX 크기",
			ClampMin = "0.01", ClampMax = "20.0"))
	float CastVFXScale = 1.f;

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
	void HandleSpawnTimer();
	void HandleLifetimeExpired();
	void HandleAbilityFinished(bool bWasCancelled);

	UFUNCTION()
	void OnCastMontageCompleted();

	UFUNCTION()
	void OnCastMontageCancelled();

	/**
	 * 스폰된 액터가 ABossPatternZoneBase 파생일 때 패턴 종료 콜백.
	 * Zone 이 자체적으로 종료 시점을 결정하는 패턴(시간왜곡/환영검무 등)에서 사용.
	 */
	UFUNCTION()
	void OnSpawnedZonePatternFinished(bool bWasBroken);

	UPROPERTY(Transient)
	TObjectPtr<AActor> SpawnedActor = nullptr;

	FTimerHandle SpawnDelayTimerHandle;
	FTimerHandle LifetimeTimerHandle;

	bool bMontageFinished = false;
	bool bSpawnTriggered = false;
	bool bLifetimeExpired = false;
};
