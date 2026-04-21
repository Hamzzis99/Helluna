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

	/**
	 * [ParallelPatternV1] 소환 중 보스를 제자리에 잠글지 여부.
	 *   true (기본) = LockMovementAndFaceTarget 호출 → 보스는 SpawnedActor 가 끝날 때까지 고정.
	 *   false        = LockMovement 생략 → 보스는 소환 후에도 계속 이동/공격 가능.
	 *                  "백그라운드 패턴" 에 적합. StateTree 쪽에서도 해당 엔트리의
	 *                  bParallelToOtherAttacks=true 를 같이 세팅해야 다른 공격이 계속 실행됨.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "소환 공격",
		meta = (DisplayName = "소환 중 이동 잠금"))
	bool bLockMovement = true;

	/**
	 * [HoldPoseV1] 시전 몽타주를 GA 가 끝날 때까지 반복 재생해서 보스가 "채널링 자세"를 계속 유지하게 함.
	 *   true  = 몽타주 완료 시 같은 몽타주를 다시 재생 (무한 루프) — GA EndAbility 까지 유지.
	 *   false (기본) = 몽타주 1회 재생 후 idle 로 돌아감.
	 * 자세 유지가 필요한 긴 소환 패턴 (예: 시간 왜곡) 에서 사용.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "소환 공격",
		meta = (DisplayName = "시전 몽타주 루프 (자세 유지)"))
	bool bLoopCastMontage = false;

	/**
	 * [HoldPoseV1] 이 GA 가 active 인 동안 보스의 HitReact 몽타주 재생을 차단.
	 *   true  = 플레이어가 보스를 때려도 피격 애니메이션이 재생되지 않아 채널링 자세가 안 깨짐.
	 *   false (기본) = 일반 HitReact 동작.
	 * bLoopCastMontage 와 함께 쓰면 피격에도 안 흔들리는 고정 자세 유지 가능.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "소환 공격",
		meta = (DisplayName = "활성 중 HitReact 차단"))
	bool bSuppressHitReactWhileActive = false;

	/**
	 * [HoldPoseV1] 지정 보스에게 활성 상태에서 HitReact 를 차단하는 SpawnAttack 이 있는지 반환.
	 * AHellunaEnemyCharacter::Multicast_PlayHitReact_Implementation 에서 early-return 용도로 사용.
	 */
	static bool ShouldBlockHitReact(const class AHellunaEnemyCharacter* Enemy);

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

	/** [HoldPoseV1] CastMontage 1회 재생. bLoopCastMontage=true 면 OnCompleted 에서 다시 호출 → 무한 루프. */
	void StartCastMontageOnce();

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
