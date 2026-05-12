// Capstone Project Helluna

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/HellunaEnemyGameplayAbility.h"
#include "EnemyGameplayAbility_RealityFracture.generated.h"

class ABossPatternZoneBase;
class AStasisSalvoOrb;
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
	 * [StasisSalvoV2] 몽타주 시작 → 구체(StasisSalvoOrb) 발사까지 딜레이 (초). 몽타주 None 이면 0.
	 *   (OrbClass 가 None 이면 구체 없이 이 딜레이 후 곧장 Zone 활성화 = 구버전 동작)
	 */
	UPROPERTY(EditDefaultsOnly, Category = "현실 균열",
		meta = (DisplayName = "구체 발사 딜레이 (초)", ClampMin = "0.0", ClampMax = "5.0"))
	float ZoneActivateDelay = 0.4f;

	/**
	 * [StasisSalvoV2] fire 타이밍에 보스가 발사하는 구체 클래스 (BP_StasisSalvoOrb 등).
	 *   구체가 충돌/최대거리 도달 시 시간정지 존을 발동. None 이면 구체 없이 곧장 존 활성화.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "현실 균열",
		meta = (DisplayName = "발사 구체 클래스"))
	TSubclassOf<AStasisSalvoOrb> OrbClass;

	/** 구체 발사 위치 — 보스 ActorLocation 기준 Forward 오프셋 (cm) */
	UPROPERTY(EditDefaultsOnly, Category = "현실 균열",
		meta = (DisplayName = "구체 발사 Forward 오프셋 (cm)", ClampMin = "-200.0", ClampMax = "500.0"))
	float OrbLaunchForwardOffset = 60.f;

	/** 구체 발사 위치 — 보스 ActorLocation 기준 Height 오프셋 (cm) */
	UPROPERTY(EditDefaultsOnly, Category = "현실 균열",
		meta = (DisplayName = "구체 발사 Height 오프셋 (cm)", ClampMin = "-100.0", ClampMax = "300.0"))
	float OrbLaunchHeightOffset = 60.f;

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
	/** [StasisSalvoV2] 딜레이 후 호출 — OrbClass 가 있으면 구체 발사, 없으면 곧장 Zone 활성화. */
	void LaunchOrbOrActivateZone();

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

	/** Zone 패턴이 끝났는가 (OnPatternFinished) */
	bool bPatternFinished = false;

	/** [TDEndSyncV1] 시전 몽타주가 끝났는가 (완료/취소/인터럽트, 또는 CastMontage 가 null) */
	bool bMontageFinished = false;

	/** 마지막 GA 종료 시점 (월드 시간) — 쿨타임 계산용 */
	double LastAbilityEndTime = -9999.0;
};
