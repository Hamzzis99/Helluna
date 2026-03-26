// Capstone Project Helluna

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/HellunaEnemyGameplayAbility.h"
#include "EnemyGameplayAbility_TimeDistortion.generated.h"

class UNiagaraSystem;
class UNiagaraComponent;
class AHellunaHeroCharacter;

/**
 * 보스 전용 시간 왜곡 GA
 *
 * 처리 순서 ─────────────────────────────────────────────────────────────────
 *  1. 발동 → 이동 잠금 + 시전 몽타주 재생
 *  2. SlowStartDelay 후 → 범위(DistortionRadius) 내 플레이어의 시간을
 *     CustomTimeDilation으로 감속 + 슬로우 VFX 스폰
 *  3. SlowDuration 동안 유지
 *  4. 지속 시간 종료 → 시간 복원 + 범위 내 플레이어에게 데미지 적용
 *     + 폭발 VFX 재생
 *  5. 몽타주 완료 후 어빌리티 종료
 *
 * 네트워크 ──────────────────────────────────────────────────────────────────
 *  ServerOnly: 서버에서만 실행
 *  CustomTimeDilation은 서버에서 설정하면 자동으로 클라이언트에 복제된다.
 *  VFX는 Multicast RPC로 모든 클라이언트에서 재생한다.
 * ────────────────────────────────────────────────────────────────────────────
 */
UCLASS()
class HELLUNA_API UEnemyGameplayAbility_TimeDistortion : public UHellunaEnemyGameplayAbility
{
	GENERATED_BODY()

public:
	UEnemyGameplayAbility_TimeDistortion();

	// =========================================================
	// 시간 왜곡 설정
	// =========================================================

	/**
	 * 시간 왜곡 영향 범위 (cm).
	 * 보스 위치 기준 이 반경 내 플레이어가 슬로우 + 데미지 대상이 된다.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "시간 왜곡|범위",
		meta = (DisplayName = "왜곡 범위 (cm)",
			ToolTip = "보스 중심으로 시간 왜곡이 적용되는 반경입니다.\n이 범위 내 플레이어가 슬로우 및 데미지 대상이 됩니다.",
			ClampMin = "100.0", ClampMax = "5000.0"))
	float DistortionRadius = 1500.f;

	/**
	 * 몽타주 시작 후 슬로우가 적용되기까지의 딜레이 (초).
	 * 이 시간 동안 시전 모션/준비 VFX가 재생된다.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "시간 왜곡|타이밍",
		meta = (DisplayName = "슬로우 시작 딜레이 (초)",
			ToolTip = "몽타주 시작 후 실제로 시간 감속이 적용되기까지의 대기 시간입니다.",
			ClampMin = "0.0", ClampMax = "5.0"))
	float SlowStartDelay = 1.0f;

	/**
	 * 슬로우 유지 시간 (초).
	 * 이 시간이 끝나면 시간이 복원되면서 데미지가 적용된다.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "시간 왜곡|타이밍",
		meta = (DisplayName = "슬로우 지속 시간 (초)",
			ToolTip = "플레이어의 시간이 느려진 상태로 유지되는 시간입니다.\n이 시간이 끝나면 시간이 복원되며 데미지가 적용됩니다.",
			ClampMin = "0.5", ClampMax = "10.0"))
	float SlowDuration = 3.0f;

	/**
	 * 슬로우 적용 시 플레이어의 시간 배율.
	 * 0.3 = 원래 속도의 30%, 0.1 = 10%.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "시간 왜곡|효과",
		meta = (DisplayName = "시간 감속 배율",
			ToolTip = "슬로우 적용 시 플레이어의 시간 배율입니다.\n0.3 = 원래 속도의 30%로 감속됩니다.",
			ClampMin = "0.05", ClampMax = "0.9"))
	float TimeDilationScale = 0.3f;

	/**
	 * 시간 복원 시 범위 내 플레이어에게 적용할 데미지.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "시간 왜곡|효과",
		meta = (DisplayName = "폭발 데미지",
			ToolTip = "슬로우가 끝나며 시간이 복원될 때 범위 내 플레이어에게 적용되는 데미지입니다.",
			ClampMin = "0.0"))
	float DetonationDamage = 50.f;

	// =========================================================
	// 몽타주
	// =========================================================

	/** 시간 왜곡 시전 시 재생할 몽타주 */
	UPROPERTY(EditDefaultsOnly, Category = "시간 왜곡|애니메이션",
		meta = (DisplayName = "시전 몽타주",
			ToolTip = "시간 왜곡 발동 시 재생할 애니메이션 몽타주입니다."))
	TObjectPtr<UAnimMontage> CastMontage = nullptr;

	// =========================================================
	// VFX 설정
	// =========================================================

	/** 슬로우 시작 시 보스 주변에 재생할 나이아가라 이펙트 (지속형) */
	UPROPERTY(EditDefaultsOnly, Category = "시간 왜곡|VFX",
		meta = (DisplayName = "슬로우 영역 VFX",
			ToolTip = "시간 감속 중 보스 주변에 재생되는 지속형 VFX입니다.\n슬로우가 끝나면 자동으로 꺼집니다."))
	TObjectPtr<UNiagaraSystem> SlowAreaVFX = nullptr;

	/** 슬로우 영역 VFX 크기 배율 */
	UPROPERTY(EditDefaultsOnly, Category = "시간 왜곡|VFX",
		meta = (DisplayName = "슬로우 영역 VFX 크기", ClampMin = "0.01", ClampMax = "20.0"))
	float SlowAreaVFXScale = 1.f;

	/** 시간 복원 + 데미지 적용 시 재생할 폭발 VFX */
	UPROPERTY(EditDefaultsOnly, Category = "시간 왜곡|VFX",
		meta = (DisplayName = "폭발 VFX",
			ToolTip = "슬로우가 끝나고 시간이 복원될 때 재생되는 일회성 폭발 이펙트입니다."))
	TObjectPtr<UNiagaraSystem> DetonationVFX = nullptr;

	/** 폭발 VFX 크기 배율 */
	UPROPERTY(EditDefaultsOnly, Category = "시간 왜곡|VFX",
		meta = (DisplayName = "폭발 VFX 크기", ClampMin = "0.01", ClampMax = "20.0"))
	float DetonationVFXScale = 1.f;

	/** 시전 준비 중 재생할 VFX (슬로우 시작 딜레이 동안) */
	UPROPERTY(EditDefaultsOnly, Category = "시간 왜곡|VFX",
		meta = (DisplayName = "시전 준비 VFX",
			ToolTip = "슬로우가 시작되기 전 시전 모션 중 재생되는 준비 이펙트입니다."))
	TObjectPtr<UNiagaraSystem> ChargingVFX = nullptr;

	/** 시전 준비 VFX 크기 배율 */
	UPROPERTY(EditDefaultsOnly, Category = "시간 왜곡|VFX",
		meta = (DisplayName = "시전 준비 VFX 크기", ClampMin = "0.01", ClampMax = "20.0"))
	float ChargingVFXScale = 1.f;

	// =========================================================
	// 사운드 설정
	// =========================================================

	/** 슬로우 시작 시 재생할 사운드 */
	UPROPERTY(EditDefaultsOnly, Category = "시간 왜곡|사운드",
		meta = (DisplayName = "슬로우 시작 사운드"))
	TObjectPtr<USoundBase> SlowStartSound = nullptr;

	/** 폭발 시 재생할 사운드 */
	UPROPERTY(EditDefaultsOnly, Category = "시간 왜곡|사운드",
		meta = (DisplayName = "폭발 사운드"))
	TObjectPtr<USoundBase> DetonationSound = nullptr;

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
	// =========================================================
	// 내부 처리 함수
	// =========================================================

	/** 슬로우 시작 — SlowStartDelay 후 호출 */
	void ApplyTimeSlow();

	/** 슬로우 종료 + 데미지 적용 — SlowDuration 후 호출 */
	void DetonateAndRestore();

	/**
	 * 보스 위치 기준 DistortionRadius 내의 플레이어 목록을 수집한다.
	 * @param OutPlayers - 결과를 담을 배열
	 */
	void GatherPlayersInRadius(TArray<AHellunaHeroCharacter*>& OutPlayers) const;

	/** 슬로우에 걸린 플레이어들의 CustomTimeDilation을 원래 값으로 복원 */
	void RestoreAllTimeDilation();

	/** 몽타주 완료 콜백 */
	UFUNCTION()
	void OnMontageCompleted();

	/** 몽타주 취소/인터럽트 콜백 */
	UFUNCTION()
	void OnMontageCancelled();

	/** 어빌리티 종료 공통 처리 */
	void HandleFinished(bool bWasCancelled);

	// =========================================================
	// 타이머 핸들
	// =========================================================
	FTimerHandle SlowStartTimerHandle;
	FTimerHandle DetonationTimerHandle;

	// =========================================================
	// 슬로우 중인 플레이어 + 원래 TimeDilation 저장
	// =========================================================

	/** 슬로우가 적용된 플레이어와 원래 CustomTimeDilation 값 */
	TMap<TWeakObjectPtr<AHellunaHeroCharacter>, float> SlowedPlayers;

	// =========================================================
	// 활성 VFX 캐싱 (종료 시 비활성화)
	// =========================================================
	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> ActiveSlowAreaVFXComp = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> ActiveChargingVFXComp = nullptr;
};
