// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Character/HellunaEnemyCharacter.h"
#include "HellunaEnemyCharacter_Boss.generated.h"

class UNiagaraSystem;
class UNiagaraComponent;
class UAnimMontage;
class UCameraShakeBase;
class UHellunaEnemyGameplayAbility;

/**
 * 보스 전용 몬스터 베이스 클래스.
 *
 * 일반 몬스터(근거리/원거리/Super 미니언)와 분리된 보스 전용 기능 보관:
 *   - 2페이즈 시스템 (HP 0 가로채기 + 시네마틱 + Enrage 재활용)
 *   - 히트 스톱 (피격 순간 일시정지)
 *   - 보스 소환 시네마틱 몽타주
 *   - 보스 패턴별 쿨다운 맵 (BossAttackCooldowns)
 *
 * 베이스 OnMonsterHealthChanged 가 virtual TryInterceptDeathForPhase2 / TriggerHitStop 을
 * 호출하므로 일반 몬스터는 no-op (비용 0), 보스만 override 로 동작.
 */
UCLASS()
class HELLUNA_API AHellunaEnemyCharacter_Boss : public AHellunaEnemyCharacter
{
	GENERATED_BODY()

public:
	AHellunaEnemyCharacter_Boss();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// === Hooks (베이스에서 호출) ===
	virtual bool TryInterceptDeathForPhase2(float OldHealth, float NewHealth) override;
	virtual void TriggerHitStop() override;
	virtual bool ShouldSuppressBrainRestartAfterPossess() const override { return bSuppressAutoBrainRestart; }

	// =========================================================
	// [BossPhase2V1] 보스 2페이즈 시스템 — HP 0 도달 시 사망 대신 2페이즈 전환
	// =========================================================

	/** 이 적이 2페이즈를 가지는지 (보스 BP에서 true로 설정). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Phase2",
		meta = (DisplayName = "2페이즈 활성화"))
	bool bHasPhase2 = false;

	/** 현재 2페이즈 상태 (서버→클라 복제). */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Combat|Phase2")
	bool bInPhase2 = false;

	/** 2페이즈 진입 시 HP 회복 비율 (1.0 = 풀 회복). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Phase2",
		meta = (DisplayName = "2페이즈 HP 회복 비율", ClampMin = "0.1", ClampMax = "1.0"))
	float Phase2HealthRestoreRatio = 1.f;

	/** 2페이즈 진입 시네마틱 동안 무적 유지 시간 (초). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Phase2",
		meta = (DisplayName = "2페이즈 시네마틱 무적 (초)", ClampMin = "0.5", ClampMax = "10.0"))
	float Phase2InvulnerabilityDuration = 5.5f;

	/** 2페이즈 공격력 배율 (기본 1.5). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Phase2",
		meta = (DisplayName = "2페이즈 공격력 배율", ClampMin = "1.0", ClampMax = "5.0"))
	float Phase2AttackMultiplier = 1.6f;

	/** 2페이즈 공격 속도 배율 (0.7 = 1/0.7배 빨라짐). 낮을수록 공격 쿨 짧아짐. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Phase2",
		meta = (DisplayName = "2페이즈 공속 배율 (쿨다운)", ClampMin = "0.2", ClampMax = "1.0"))
	float Phase2CooldownMultiplier = 0.65f;

	/** 2페이즈 진입 카메라 쉐이크. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Phase2",
		meta = (DisplayName = "2페이즈 카메라 쉐이크"))
	TSubclassOf<UCameraShakeBase> Phase2TransitionShakeClass;

	/** 2페이즈 진입 순간 보스 발밑에 스폰할 충격파/폭발 VFX. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Phase2",
		meta = (DisplayName = "2페이즈 진입 VFX"))
	TObjectPtr<UNiagaraSystem> Phase2TransitionVFX;

	/** 2페이즈 진입 때 보스 머리 위 하늘에서 떨어지는 운석/빛 VFX. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Phase2",
		meta = (DisplayName = "2페이즈 하늘 낙하 VFX"))
	TObjectPtr<UNiagaraSystem> Phase2SkyMeteorVFX;

	/** 하늘 낙하 VFX 스폰 개수. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Phase2",
		meta = (DisplayName = "하늘 VFX 개수", ClampMin = "1", ClampMax = "20"))
	int32 Phase2SkyMeteorCount = 6;

	/** 하늘 낙하 VFX 스폰 반경 (보스 주변 XY 랜덤). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Phase2",
		meta = (DisplayName = "하늘 VFX 반경 (cm)", ClampMin = "100.0"))
	float Phase2SkyMeteorRadius = 1500.f;

	/** 하늘 낙하 VFX 높이 (보스 기준 Z 오프셋). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Phase2",
		meta = (DisplayName = "하늘 VFX 높이 (cm)", ClampMin = "500.0"))
	float Phase2SkyMeteorHeight = 2500.f;

	/** 2페이즈 보스 상시 오라 VFX (보스 루트에 attach, 페이즈 유지 중 계속 재생). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Phase2",
		meta = (DisplayName = "2페이즈 보스 오라 VFX"))
	TObjectPtr<UNiagaraSystem> Phase2AuraVFX;

	/** 2페이즈 시네마틱 카메라 블렌드 인 시간 (초). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Phase2",
		meta = (DisplayName = "시네마틱 블렌드 인 (초)", ClampMin = "0.0", ClampMax = "3.0"))
	float Phase2CameraBlendIn = 0.5f;

	/** 2페이즈 시네마틱 카메라 블렌드 아웃 시간 (초). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Phase2",
		meta = (DisplayName = "시네마틱 블렌드 아웃 (초)", ClampMin = "0.0", ClampMax = "3.0"))
	float Phase2CameraBlendOut = 0.45f;

	/**
	 * 2페이즈 시네마틱 카메라 오프셋 (보스 로컬 좌표):
	 *   X = 보스 정면 거리 (양수=앞, 멀수록 전신이 보임)
	 *   Y = 보스 우측 거리 (3/4 각도)
	 *   Z = 보스 위쪽 높이 (살짝 위에서 내려다보기)
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Phase2",
		meta = (DisplayName = "시네마틱 카메라 오프셋 (보스 로컬)"))
	FVector Phase2CameraOffset = FVector(550.f, 250.f, 150.f);

	/** 카메라 LookAt 기준 보스 Z 오프셋 (0=발밑, 80=허리). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Phase2",
		meta = (DisplayName = "시네마틱 LookAt 높이 (cm)", ClampMin = "0.0", ClampMax = "300.0"))
	float Phase2CameraLookHeight = 90.f;

	/**
	 * [Phase2ShakeRepeatV1] 2페이즈 시네마틱 동안 Phase2TransitionShakeClass 를 반복 재생할 간격 (초).
	 *  0 또는 음수: 시네마틱 시작 시 1회만 (기존 동작).
	 *  >0:        시네마틱 동안 이 간격으로 반복 → 포탈 빨려들어가는 듯한 지속 진동 연출.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Phase2",
		meta = (DisplayName = "쉐이크 반복 간격 (초)", ClampMin = "0.0", ClampMax = "5.0"))
	float Phase2ShakeRepeatInterval = 0.4f;

	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnBossEnterPhase2);

	/** 2페이즈 진입 알림 — 모든 클라. HP 바/UI/AI 등에서 바인딩해 반응. */
	UPROPERTY(BlueprintAssignable, Category = "Combat|Phase2")
	FOnBossEnterPhase2 OnBossEnterPhase2;

	/** 2페이즈 진입 시 서버에서 호출 — HP 회복 + Enrage 스탯 적용 + Multicast 연출. */
	void EnterBossPhase2();

	/** 2페이즈 전환 멀티캐스트 — 모든 클라에서 VFX/쉐이크/오라 재생 + 델리게이트 브로드캐스트. */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_PlayBossPhase2Transition();

	// =========================================================
	// [HitStopV1] 히트 스톱 — 피격 순간 보스 시간 일시 정지
	// =========================================================

	/** 히트 스톱 지속시간 (초). 0이면 비활성. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|HitStop",
		meta = (DisplayName = "히트 스톱 시간 (초)", ClampMin = "0.0", ClampMax = "0.5"))
	float HitStopDuration = 0.08f;

	/** 히트 스톱 동안 보스 CustomTimeDilation 값 (0에 가까울수록 강한 정지). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|HitStop",
		meta = (DisplayName = "히트 스톱 시간 배율", ClampMin = "0.01", ClampMax = "1.0"))
	float HitStopTimeDilation = 0.05f;

	/** 히트 스톱 최소 쿨다운 (중첩 시 남용 방지). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|HitStop",
		meta = (DisplayName = "히트 스톱 쿨다운 (초)", ClampMin = "0.0", ClampMax = "2.0"))
	float HitStopCooldown = 0.12f;

	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_TriggerHitStop(float Duration, float Dilation);

	// =========================================================
	// 보스 소환 시네마틱 — 몽타주 + AI 억제 플래그
	// =========================================================

	/**
	 * 보스 소환 몽타주.
	 * BossEncounterCube::TryActivate에서 보스 스폰 직후 Multicast로 재생한다.
	 * 시네마틱 동안 플레이어 입력은 전면 잠기고 보스는 무적.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Animation|Combat",
		meta = (DisplayName = "보스 소환 몽타주",
			ToolTip = "보스가 스폰될 때 재생할 애니메이션 몽타주입니다.\nBossEncounterCube가 스폰 직후 Multicast로 재생합니다."))
	TObjectPtr<UAnimMontage> SummonMontage = nullptr;

	/**
	 * 소환 시네마틱 진입 플래그 (서버 전용).
	 * true면 PossessedBy의 NextTick StartLogic을 건너뛴다 — 소환 몽타주 중 AI가 공격을 시도하지 않도록 함.
	 * BossEncounterCube::TryActivate에서 스폰 직후 true로 설정하고,
	 * OnBossSummonCompletedServer에서 false로 되돌린 뒤 수동으로 StartLogic 호출.
	 */
	UPROPERTY(Transient)
	bool bSuppressAutoBrainRestart = false;

	/**
	 * [SummonMontageLoopV1] true 면 OnSummonMontageEnded 에서 cleanup 대신 다시 Montage_Play.
	 *   시네마틱 동안 짧은 AM_Boss_Walk 가 한 번 재생 후 idle 로 빠지지 않게 유지.
	 *   BossSummonCinematicTrigger 가 TryActivate 에서 true, 종료 시 false 로 토글.
	 */
	UPROPERTY(Transient)
	bool bShouldLoopSummonMontage = false;

	/** 소환 몽타주 멀티캐스트 재생 (서버 → 전체 클라) */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_PlaySummonMontage();

	/** 소환 몽타주 완료 델리게이트 — BossEncounterCube가 바인딩 */
	DECLARE_DELEGATE(FOnSummonMontageFinished)
	FOnSummonMontageFinished OnSummonMontageFinished;

	/** 소환 몽타주 종료 콜백 (서버에서만 바인딩 — AnimInstance OnMontageEnded) */
	UFUNCTION()
	void OnSummonMontageEnded(UAnimMontage* Montage, bool bInterrupted);

	// =========================================================
	// [BossAttackCooldownPersistV1] 보스 공격(GA)별 남은 쿨다운(초).
	//   STTask_BossChooseAttack 의 FBossAttackEntry.Cooldown 이 Attack 상태 재진입에도 살아남도록
	//   보스 캐릭터 자체에 저장한다 (기존엔 ActiveInstanceData 에 있어서 상태 Exit 시 리셋되는 버그).
	//
	//   Tick 마다 Task 가 Decrement 하고, 엔트리 발동 시 Entry.Cooldown 로 재설정.
	//   멀티플레이어: 서버 AI 만 돌므로 복제 불필요 (Transient).
	// =========================================================
	UPROPERTY(Transient)
	TMap<TSubclassOf<UHellunaEnemyGameplayAbility>, float> BossAttackCooldowns;

private:
	/** 2페이즈 시네마틱 무적 복원용 타이머 */
	FTimerHandle Phase2InvulnerabilityTimer;

	/** [Phase2ShakeRepeatV1] 2페이즈 시네마틱 동안 카메라 쉐이크 반복용 타이머 (각 머신 로컬). */
	FTimerHandle Phase2ShakeRepeatTimer;

	/** 2페이즈 진입 때 Brain을 우리가 멈췄는지 (타이머 종료 시 재시작용). */
	bool bAIStoppedForPhase2 = false;

	/** 히트 스톱 복원용 타이머 */
	FTimerHandle HitStopTimer;

	/** 마지막 히트 스톱 발화 시각 (쿨다운 체크) */
	double LastHitStopTime = 0.0;

	/** 2페이즈 오라 VFX 인스턴스 (Destruct 시 정리). */
	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> ActivePhase2AuraComp = nullptr;

	/** 히트 스톱 복원 — 타이머 콜백 */
	void RestoreTimeDilationAfterHitStop();
};
