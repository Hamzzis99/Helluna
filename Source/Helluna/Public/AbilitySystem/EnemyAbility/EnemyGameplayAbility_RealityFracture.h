// Capstone Project Helluna

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/HellunaEnemyGameplayAbility.h"
#include "EnemyGameplayAbility_RealityFracture.generated.h"

class ABossPatternZoneBase;
class UNiagaraSystem;

/**
 * 보스 위협 패턴 — Reality Fracture (현실 균열)
 *
 * 페이즈2 + HP 50% 이하에서 사용 예정. 시간 슬로우와 다른 결의 시간 효과 —
 * "느려짐" 이 아니라 "끊김" 으로 화면이 stutter 되며 보스가 텔레포트를 반복한다.
 *
 * GA는 TimeDistortion 과 동일하게 두 가지만 담당:
 *  1. (선택) 시전 몽타주 재생
 *  2. PatternZoneClass(BP) 스폰 → 패턴 종료 시 제거
 *
 * 시퀀스/freeze/텔레포트/ghost mesh/공격 등은 ARealityFractureZone 가 전부 관리.
 */
UCLASS()
class HELLUNA_API UEnemyGameplayAbility_RealityFracture : public UHellunaEnemyGameplayAbility
{
	GENERATED_BODY()

public:
	UEnemyGameplayAbility_RealityFracture();

	// =========================================================
	// 설정
	// =========================================================

	/** 스폰할 패턴 Zone BP 클래스 (BP에서 시퀀스 파라미터 / placeholder VFX 구성) */
	UPROPERTY(EditDefaultsOnly, Category = "현실 균열",
		meta = (DisplayName = "패턴 Zone 클래스",
			ToolTip = "현실 균열 패턴의 시퀀스/텔레포트/공격을 담당하는 BP 클래스입니다. 보통 ARealityFractureZone 상속."))
	TSubclassOf<ABossPatternZoneBase> PatternZoneClass;

	/** 시전 시 재생할 몽타주 (None 이면 몽타주 없이 즉시 Zone 활성화) */
	UPROPERTY(EditDefaultsOnly, Category = "현실 균열",
		meta = (DisplayName = "시전 몽타주"))
	TObjectPtr<UAnimMontage> CastMontage = nullptr;

	/**
	 * 몽타주 시작 후 Zone 활성화까지 딜레이 (초). 몽타주 None 이면 무시되고 0 으로 처리.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "현실 균열",
		meta = (DisplayName = "Zone 활성화 딜레이 (초)", ClampMin = "0.0", ClampMax = "5.0"))
	float ZoneActivateDelay = 0.4f;

	/** 패턴 지속 시간 (초). Zone 의 시퀀스가 이 시간 이상이면 timer 가 자동 강제 종료. */
	UPROPERTY(EditDefaultsOnly, Category = "현실 균열",
		meta = (DisplayName = "패턴 지속 시간 (초)", ClampMin = "1.0", ClampMax = "20.0"))
	float PatternDuration = 9.0f;

	/** 쿨타임 (초) */
	UPROPERTY(EditDefaultsOnly, Category = "현실 균열",
		meta = (DisplayName = "쿨타임 (초)", ClampMin = "0.0", ClampMax = "120.0"))
	float CooldownDuration = 30.f;

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
