// Capstone Project Helluna

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/HellunaEnemyGameplayAbility.h"
#include "EnemyGameplayAbility_TimeDistortion.generated.h"

class UNiagaraSystem;
class UNiagaraComponent;
class AHellunaHeroCharacter;
class ATimeDistortionOrb;

/**
 * 보스 전용 시간 왜곡 GA
 *
 * 처리 순서 ─────────────────────────────────────────────────────────────────
 *  1. 발동 → 이동 잠금 + 시전 몽타주 재생
 *  2. SlowStartDelay 후 → 범위(DistortionRadius) 내 플레이어의 시간을
 *     CustomTimeDilation으로 감속 + 슬로우 VFX 스폰
 *  3. 검은색 데코 Orb N개 + 색 다른 키 Orb 1개 스폰
 *     → 키 Orb를 플레이어가 공격하면 패턴 파훼
 *  4-A. [파훼 성공] 시간 복원 + Orb 전부 제거 + 파훼 VFX → 데미지 없음
 *  4-B. [파훼 실패] SlowDuration 만료 → 시간 복원 + 범위 데미지 + 폭발 VFX
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
	// Orb (파훼 구체) 설정
	// =========================================================

	/** 스폰할 데코(검은색) Orb 개수 */
	UPROPERTY(EditDefaultsOnly, Category = "시간 왜곡|Orb",
		meta = (DisplayName = "데코 Orb 개수",
			ToolTip = "슬로우 시작 시 보스 주변에 스폰되는 검은색 데코 Orb 수입니다.\n이 중 1개가 색이 다른 키 Orb로 대체됩니다.",
			ClampMin = "2", ClampMax = "20"))
	int32 DecoyOrbCount = 5;

	/** Orb가 보스로부터 스폰되는 거리 (cm) */
	UPROPERTY(EditDefaultsOnly, Category = "시간 왜곡|Orb",
		meta = (DisplayName = "Orb 스폰 거리 (cm)",
			ToolTip = "보스 위치 기준으로 Orb가 배치되는 반경입니다.",
			ClampMin = "100.0", ClampMax = "3000.0"))
	float OrbSpawnRadius = 600.f;

	/** Orb 스폰 높이 오프셋 (cm) */
	UPROPERTY(EditDefaultsOnly, Category = "시간 왜곡|Orb",
		meta = (DisplayName = "Orb 높이 오프셋 (cm)",
			ToolTip = "Orb가 보스 위치에서 얼마나 위에 스폰되는지 설정합니다.",
			ClampMin = "0.0", ClampMax = "500.0"))
	float OrbHeightOffset = 150.f;

	/** Orb 스폰 간격 (초) — Orb가 하나씩 순서대로 나타남 */
	UPROPERTY(EditDefaultsOnly, Category = "시간 왜곡|Orb",
		meta = (DisplayName = "Orb 스폰 간격 (초)",
			ToolTip = "각 Orb가 스폰되는 시간 간격입니다.\n0.3이면 0.3초마다 하나씩 나타납니다.",
			ClampMin = "0.05", ClampMax = "1.0"))
	float OrbSpawnInterval = 0.3f;

	/** 데코(일반) Orb VFX — 검은색 이펙트 */
	UPROPERTY(EditDefaultsOnly, Category = "시간 왜곡|Orb",
		meta = (DisplayName = "데코 Orb VFX",
			ToolTip = "슬로우 중 보스 주변에 떠다니는 검은색 구체 이펙트입니다."))
	TObjectPtr<UNiagaraSystem> DecoyOrbVFX = nullptr;

	/** 데코 Orb VFX 크기 배율 */
	UPROPERTY(EditDefaultsOnly, Category = "시간 왜곡|Orb",
		meta = (DisplayName = "데코 Orb VFX 크기", ClampMin = "0.01", ClampMax = "10.0"))
	float DecoyOrbVFXScale = 1.f;

	/** 키(파훼 대상) Orb VFX — 색이 다른 이펙트 */
	UPROPERTY(EditDefaultsOnly, Category = "시간 왜곡|Orb",
		meta = (DisplayName = "키 Orb VFX",
			ToolTip = "플레이어가 공격해야 하는 색이 다른 특수 구체 이펙트입니다."))
	TObjectPtr<UNiagaraSystem> KeyOrbVFX = nullptr;

	/** 키 Orb VFX 크기 배율 */
	UPROPERTY(EditDefaultsOnly, Category = "시간 왜곡|Orb",
		meta = (DisplayName = "키 Orb VFX 크기", ClampMin = "0.01", ClampMax = "10.0"))
	float KeyOrbVFXScale = 1.f;

	/** 데코 Orb 파괴 시 VFX (검은 Orb가 깨질 때) */
	UPROPERTY(EditDefaultsOnly, Category = "시간 왜곡|Orb",
		meta = (DisplayName = "데코 Orb 파괴 VFX",
			ToolTip = "일반 검은색 Orb가 공격으로 깨질 때 재생되는 이펙트입니다."))
	TObjectPtr<UNiagaraSystem> DecoyOrbDestroyVFX = nullptr;

	/** 데코 Orb 파괴 VFX 크기 배율 */
	UPROPERTY(EditDefaultsOnly, Category = "시간 왜곡|Orb",
		meta = (DisplayName = "데코 Orb 파괴 VFX 크기", ClampMin = "0.01", ClampMax = "10.0"))
	float DecoyOrbDestroyVFXScale = 1.f;

	/** 키 Orb 파괴(파훼 성공) 시 VFX */
	UPROPERTY(EditDefaultsOnly, Category = "시간 왜곡|Orb",
		meta = (DisplayName = "키 Orb 파괴 VFX",
			ToolTip = "색이 다른 키 Orb를 공격하여 패턴을 파훼했을 때 재생되는 이펙트입니다."))
	TObjectPtr<UNiagaraSystem> KeyOrbDestroyVFX = nullptr;

	/** 키 Orb 파괴 VFX 크기 배율 */
	UPROPERTY(EditDefaultsOnly, Category = "시간 왜곡|Orb",
		meta = (DisplayName = "키 Orb 파괴 VFX 크기", ClampMin = "0.01", ClampMax = "10.0"))
	float KeyOrbDestroyVFXScale = 1.f;

	/** 파훼 성공 시 재생할 사운드 */
	UPROPERTY(EditDefaultsOnly, Category = "시간 왜곡|Orb",
		meta = (DisplayName = "파훼 성공 사운드"))
	TObjectPtr<USoundBase> BreakSuccessSound = nullptr;

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

	/** 파훼 성공 시 보스 위치에 재생할 VFX (시간 풀리는 연출) */
	UPROPERTY(EditDefaultsOnly, Category = "시간 왜곡|VFX",
		meta = (DisplayName = "파훼 성공 VFX",
			ToolTip = "키 Orb 파괴로 패턴이 파훼되었을 때 보스 위치에 재생되는 이펙트입니다."))
	TObjectPtr<UNiagaraSystem> BreakSuccessVFX = nullptr;

	/** 파훼 성공 VFX 크기 배율 */
	UPROPERTY(EditDefaultsOnly, Category = "시간 왜곡|VFX",
		meta = (DisplayName = "파훼 성공 VFX 크기", ClampMin = "0.01", ClampMax = "20.0"))
	float BreakSuccessVFXScale = 1.f;

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

	/** 슬로우 종료 + 데미지 적용 — SlowDuration 후 호출 (파훼 실패) */
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
	// Orb 관련
	// =========================================================

	/** Orb 순차 스폰 시작 (타이머 기반) */
	void StartOrbSpawnSequence();

	/** 타이머 콜백: 다음 Orb 하나를 스폰 */
	void SpawnNextOrb();

	/** 모든 Orb를 제거한다 */
	void DestroyAllOrbs();

	/** Orb 파괴 콜백 — 키 Orb가 파괴되면 패턴 파훼 */
	UFUNCTION()
	void OnOrbDestroyed(ATimeDistortionOrb* DestroyedOrb);

	/** 패턴 파훼 처리 */
	void OnPatternBroken();

	/** 스폰된 Orb 목록 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<ATimeDistortionOrb>> SpawnedOrbs;

	/** 파훼 완료 여부 (중복 호출 방지) */
	bool bPatternBroken = false;

	// =========================================================
	// Orb 순차 스폰 상태
	// =========================================================

	/** 현재 스폰할 Orb 인덱스 */
	int32 OrbSpawnCurrentIndex = 0;

	/** 총 Orb 수 (데코 + 키 1개) */
	int32 OrbSpawnTotalCount = 0;

	/** 키 Orb가 배치될 인덱스 (랜덤) */
	int32 OrbSpawnKeyIndex = 0;

	/** Orb 순차 스폰 타이머 */
	FTimerHandle OrbSpawnTimerHandle;

	// =========================================================
	// 타이머 핸들
	// =========================================================
	FTimerHandle SlowStartTimerHandle;
	FTimerHandle DetonationTimerHandle;

	// =========================================================
	// 슬로우 중인 플레이어 + 원래 상태 저장
	// =========================================================

	/** 슬로우 적용 전 플레이어 원본 상태 */
	struct FPlayerSlowState
	{
		float OriginalTimeDilation = 1.f;
		float OriginalAnimPlayRate = 1.f;
	};

	/** 슬로우가 적용된 플레이어와 원래 상태 */
	TMap<TWeakObjectPtr<AHellunaHeroCharacter>, FPlayerSlowState> SlowedPlayers;

	// =========================================================
	// 활성 VFX 캐싱 (종료 시 비활성화)
	// =========================================================
	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> ActiveSlowAreaVFXComp = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> ActiveChargingVFXComp = nullptr;
};
