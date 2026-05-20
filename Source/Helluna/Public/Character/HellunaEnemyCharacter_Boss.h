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
class UMaterialInterface;
class ABossPhase2CinematicTrigger;
class ABossDeathCinematicTrigger;
class UBossDissolveComponent;

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

	/** [Phase2RefactorV1] Tick — montage section 추적 (Inertialization request) 만 보유. 카메라 lerp 는 트리거. */
	virtual void Tick(float DeltaTime) override;

	// === Hooks (베이스에서 호출) ===
	virtual bool TryInterceptDeathForPhase2(float OldHealth, float NewHealth) override;
	virtual void TriggerHitStop() override;
	virtual bool ShouldSuppressBrainRestartAfterPossess() const override { return bSuppressAutoBrainRestart; }

	/** [BossDeathCinematicV1] HP 0 도달(페이즈2 후 진짜 사망) 시점에 죽음 시네마틱 트리거 spawn. */
	virtual void OnMonsterDeath(AActor* DeadActor, AActor* KillerActor) override;

	/** [BossCinematicGateV1] 소환 시네마틱 종료 게이트.
	 *  true 면 보스 ASC 에 State.Boss.CinematicReady 태그 부여 — STEvaluator_BossTarget 가
	 *  이 태그가 있어야 타겟/패턴을 잡음. 태그 없으면 보스는 idle 유지.
	 *  BossSummonCinematicTrigger 가 시네마틱 종료(또는 skip) 시 true 로 호출. */
	void SetCinematicGateUnlocked(bool bUnlocked);

	/** [BossDissolveComponentV1] 사망 시 dissolve 효과 (mesh material swap + Niagara + slowmo) 캡슐화 컴포넌트.
	 *   GA_BossDeath 가 사망 몽타주 85% 시점에 TriggerDissolve() 호출.
	 *   디자이너가 BP CDO 에서 dissolve 머티리얼/VFX/timing 직접 set.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="BossDissolve")
	TObjectPtr<UBossDissolveComponent> DissolveComponent;

	/**
	 * [BossDeathCinematicV1] 죽음 시네마틱 동안 destroy 보류 — GA_Death 70% 시점에
	 *   DespawnMassEntityOnServer 가 호출돼도 hold 만료까지 reschedule.
	 *   덕분에 카메라가 사망 몽타주 끝까지 head 본 추적 가능.
	 */
	virtual void DespawnMassEntityOnServer(const TCHAR* Where) override;

	/** 보스 사망 시 spawn 할 시네마틱 트리거 클래스. None 이면 스킵. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|사망 시네마틱",
		meta = (DisplayName = "사망 시네마틱 트리거 클래스"))
	TSubclassOf<ABossDeathCinematicTrigger> DeathCinematicTriggerClass;

	/**
	 * 죽음 시네마틱 destroy 보류 failsafe 시간 (초).
	 *   기본 destroy 신호는 사망 몽타주 OnMontageEnded — 이 시간은 몽타주 신호가 누락될 경우의
	 *   안전망 (사망 몽타주 길이보다 충분히 길게).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|사망 시네마틱",
		meta = (DisplayName = "Destroy 보류 Failsafe (초)", ClampMin = "0.5", ClampMax = "60.0"))
	float DeathCinematicHoldDuration = 15.f;

	/**
	 * [BossDeathMeshLiftV1] 사망 몽타주 재생 동안 SkelMesh.RelativeLocation.Z 에 더할 최종 오프셋 (cm).
	 *   AM_Boss_Death (Anim_FuturisticWarrior_death3) 의 누운 자세에서 spine/hand 본이 mesh root(=바닥)
	 *   기준 약 −10cm 까지 내려가는 authored data 보정 — 콜리전 캡슐 영향 없는 시각 전용 값.
	 *   GA_BossDeath::ActivateAbility 가 Multicast_StartDeathMeshLift 를 호출하면
	 *   Tick 에서 LiftDuration 동안 0 → 이 값으로 lerp 후 유지 (보스는 dissolve 되므로 revert 없음).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|사망 시네마틱",
		meta = (DisplayName = "사망 몽타주 SkelMesh Z 리프트 (cm)", ClampMin = "0.0", ClampMax = "50.0"))
	float DeathMontageMeshZOffset = 15.f;

	/**
	 * [BossDeathMeshLiftV1] 0 → DeathMontageMeshZOffset 으로 lerp 하는 시간 (초).
	 *   기본값 1.17 = AM_Boss_Death 의 임팩트(누운 자세 진입) 시점 (frame 28 @ 24fps).
	 *   캐릭터가 서있다 쓰러지는 동안 천천히 mesh 가 올라가서 누운 자세에서 정확히 +오프셋 도달.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|사망 시네마틱",
		meta = (DisplayName = "사망 Z 리프트 보간 시간 (초)", ClampMin = "0.0", ClampMax = "5.0"))
	float DeathMontageMeshLiftDuration = 1.17f;

	/** [BossDeathMeshLiftV1] 모든 머신에서 mesh Z lift lerp 시작 — GA_BossDeath 가 server 측에서 호출. */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_StartDeathMeshLift();

private:
	/** Destroy hold 활성 플래그 (true 인 동안 DespawnMassEntityOnServer 보류). */
	bool bDeathCinematicHoldActive = false;

	/** Hold failsafe timer. */
	FTimerHandle DeathCinematicHoldTimer;

	/** [BossDeathMeshLiftV1] lerp 진행 플래그 / 누적 시간 / 시작 시점 SkelMesh.RelZ 백업. */
	bool bDeathMeshLiftActive = false;
	float DeathMeshLiftElapsed = 0.f;
	float SavedDeathMeshRelZ = 0.f;

	/** [BossDeathCinematicV1] 사망 몽타주 종료 신호 (서버 AnimInstance OnMontageEnded). */
	UFUNCTION()
	void OnBossDeathMontageEnded_ServerSignal(UAnimMontage* Montage, bool bInterrupted);

	/** [BossDeathCinematicV1] Hold 해제 + 진짜 destroy 진행. */
	void ReleaseDeathCinematicHold(const TCHAR* Reason);

public:

	// =========================================================
	// [BossPhase2V1] 보스 2페이즈 = 광폭화 시스템
	//   HP 0 도달 시 사망 대신 광폭화 진입 — MaxHP 확장 + 풀 회복 + 시네마틱 무적 +
	//   본체 메쉬 swap + 갑옷 분리 + 광폭화 피부 머터리얼 + Niagara 오라 + 진입 대사.
	//   공속/공격력 배율은 베이스(EnrageDamageMultiplier/EnrageCooldownMultiplier) 그대로 사용.
	// =========================================================

	/** 이 적이 광폭화(2페이즈)를 가지는지 (보스 BP에서 true). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|광폭화",
		meta = (DisplayName = "광폭화 활성화"))
	bool bHasPhase2 = false;

	/** 현재 광폭화 상태 (서버→클라 복제). */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Combat|광폭화")
	bool bInPhase2 = false;

	/**
	 * [BossPhase2_MaxHpV1] 광폭화 진입 시 최대 체력 배율.
	 *   기본 MaxHP × 이 값으로 MaxHP 확장 + 새 MaxHP 로 풀 회복.
	 *   → HP 바가 시각적으로 max 까지 채워지는 효과.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|광폭화",
		meta = (DisplayName = "광폭화 최대 체력 배율", ClampMin = "1.0", ClampMax = "10.0"))
	float Phase2MaxHealthMultiplier = 1.5f;

	/** 광폭화 진입 시네마틱 동안 무적 유지 시간 (초). 단계 1+2+3 합 = 이 값. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|광폭화",
		meta = (DisplayName = "광폭화 시네마틱 무적 (초)", ClampMin = "0.5", ClampMax = "20.0"))
	float Phase2InvulnerabilityDuration = 10.f;

	/**
	 * [Phase2StageV1] 광폭화 시네마틱 단계 시퀀스:
	 *   단계1 (Phase2StunDuration): 보스 정면 클로즈업 + 기절 몽타주 + 대사
	 *   단계2 (Phase2CameraRiseDuration): 카메라 위로 lerp (VFX 안 보임)
	 *   단계3 (남은 시간): 카메라 위 도달 → VFX 활성 + 본체 swap + 갑옷 분리 + 광폭화 모션
	 */

	/** 단계1 — 보스가 한쪽 무릎/충격받는 자세로 잠시 멈추는 기절 몽타주. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|광폭화",
		meta = (DisplayName = "광폭화 기절 몽타주"))
	TObjectPtr<UAnimMontage> Phase2StunMontage = nullptr;

	/** [Phase2RefactorV1] 시네마틱 트리거 클래스 — 카메라/대사/단계 시간은 트리거가 처리. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|광폭화",
		meta = (DisplayName = "광폭화 시네마틱 트리거 클래스"))
	TSubclassOf<ABossPhase2CinematicTrigger> Phase2CinematicTriggerClass;

	/**
	 * [Phase2DebugSkipV1] 페이즈2 시네마틱 트리거 spawn 자체를 skip — 디버그/테스트용.
	 *   true 시: camera/dialogue/포탈/etc 모두 skip. Stage 3 visuals (강하 VFX, 본체 swap, 갑옷 분리)
	 *   + HP fill V3 는 Stage3 timer (Phase2StunDuration 후) 로 정상 진행.
	 *   빠른 페이즈2 진입 테스트용 — Phase2StunDuration 도 짧게 두면 즉시 페이즈2 진입.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|광폭화|Debug",
		meta = (DisplayName = "Skip Phase 2 Cinematic (디버그용)"))
	bool bDebugSkipPhase2Cinematic = false;

	/**
	 * 단계1 길이 (초) — Stage3 비주얼 (광폭화/갑옷/본체 swap/VFX) 시작 timer.
	 *   트리거의 StunDuration 과 같은 값으로 set 권장 (시간 동기화).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|광폭화",
		meta = (DisplayName = "광폭화 단계1 기절 시간(초)", ClampMin = "0.5", ClampMax = "15.0"))
	float Phase2StunDuration = 6.f;

	// [Phase2HitSlowV2] 슬로우 모션 = AnimNotifyState_ActorTimeDilation (anim 자체에 drag).
	// [Phase2RefactorV1] 카메라/단계/대사 UPROPERTY 모두 트리거(BossPhase2CinematicTrigger) 로 이동.

	/** 광폭화 진입 카메라 쉐이크. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|광폭화",
		meta = (DisplayName = "광폭화 카메라 쉐이크"))
	TSubclassOf<UCameraShakeBase> Phase2TransitionShakeClass;

	/** 광폭화 진입 순간 보스 발밑에 스폰할 충격파/폭발 VFX. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|광폭화",
		meta = (DisplayName = "광폭화 진입 VFX"))
	TObjectPtr<UNiagaraSystem> Phase2TransitionVFX;

	/** 광폭화 진입 때 보스 머리 위 하늘에서 떨어지는 운석/빛 VFX. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|광폭화",
		meta = (DisplayName = "광폭화 하늘 낙하 VFX"))
	TObjectPtr<UNiagaraSystem> Phase2SkyMeteorVFX;

	/** 광폭화 하늘 낙하 VFX 스폰 개수. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|광폭화",
		meta = (DisplayName = "광폭화 하늘 VFX 개수", ClampMin = "1", ClampMax = "20"))
	int32 Phase2SkyMeteorCount = 6;

	/** 광폭화 하늘 낙하 VFX 스폰 반경 (보스 주변 XY 랜덤). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|광폭화",
		meta = (DisplayName = "광폭화 하늘 VFX 반경 (cm)", ClampMin = "100.0"))
	float Phase2SkyMeteorRadius = 1500.f;

	/** 광폭화 하늘 낙하 VFX 높이 (보스 기준 Z 오프셋). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|광폭화",
		meta = (DisplayName = "광폭화 하늘 VFX 높이 (cm)", ClampMin = "500.0"))
	float Phase2SkyMeteorHeight = 2500.f;

	/** 광폭화 보스 상시 오라 VFX (보스 루트에 attach, 광폭화 유지 중 계속 재생). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|광폭화",
		meta = (DisplayName = "광폭화 보스 오라 VFX"))
	TObjectPtr<UNiagaraSystem> Phase2AuraVFX;

	/**
	 * [Phase2DescentScaleV1] 광폭화 진입 후 'Phase2Descent' Tag NC 들의 base scale 에 곱할 시작 배율.
	 *   1.0 = 시작 시 BP 의 base scale 그대로. lerp 끝 배율로 시간 경과 따라 점점 커짐.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|광폭화",
		meta = (DisplayName = "광폭화 강하 VFX 시작 배율", ClampMin = "0.1", ClampMax = "10.0"))
	float Phase2DescentScaleStartMult = 1.0f;

	/** 광폭화 진입 후 시간 경과로 도달할 끝 배율. base × 이 값. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|광폭화",
		meta = (DisplayName = "광폭화 강하 VFX 끝 배율", ClampMin = "0.1", ClampMax = "20.0"))
	float Phase2DescentScaleEndMult = 3.0f;

	/** 시작 배율 → 끝 배율 도달까지 시간 (초). 0 또는 음수면 즉시 끝 배율. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|광폭화",
		meta = (DisplayName = "광폭화 강하 VFX 스케일 보간 시간(초)", ClampMin = "0.0", ClampMax = "30.0"))
	float Phase2DescentScaleDuration = 10.0f;

	/**
	 * [Phase2DescentLifetimeV1] 페이즈2 진입 후 'Phase2Descent' Tag NC 가 활성 유지되는 시간 (초).
	 *   만료 시 Deactivate — Loop 자산이라도 자동으로 페이드 아웃 후 사라짐.
	 *   0 = 영구 (광폭화 끝까지). 보통 시네마틱(10초) + 약간 여유 (12초) 권장.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|광폭화",
		meta = (DisplayName = "광폭화 강하 VFX 잔존 시간(초, 0=영구)", ClampMin = "0.0", ClampMax = "60.0"))
	float Phase2DescentVFXLifetime = 12.0f;

	/**
	 * [Phase2DescentSpeedV1] 광폭화 강하 VFX 시간 배율 — particle 시뮬레이션 속도.
	 *   1.0 = 원본 (Niagara 디자인 그대로)
	 *   2.0 = 2배 빠르게 떨어짐
	 *   0.5 = 반속
	 *   NiagaraComponent.SetCustomTimeDilation 으로 적용 — particle velocity, lifetime, spawn rate 모두 동시에 스케일.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|광폭화",
		meta = (DisplayName = "광폭화 강하 VFX 속도 배율 (1=원본, 2=2배 빠름)", ClampMin = "0.1", ClampMax = "10.0"))
	float Phase2DescentTimeDilation = 1.0f;

	// [Phase2RefactorV1] 카메라 위치/시간 UPROPERTY 모두 트리거(ABossPhase2CinematicTrigger)로 이동.
	//   BP_BossPhase2CinematicTrigger BP CDO 에서 직접 조정 — 시네마틱 디자인 분리.

	/**
	 * [Phase2ShakeRepeatV1] 광폭화 시네마틱 동안 카메라 쉐이크 반복 간격 (초).
	 *  0 또는 음수: 시네마틱 시작 시 1회만.
	 *  >0:        시네마틱 동안 이 간격으로 반복 → 포탈 빨려들어가는 듯한 지속 진동 연출.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|광폭화",
		meta = (DisplayName = "광폭화 쉐이크 반복 간격 (초)", ClampMin = "0.0", ClampMax = "5.0"))
	float Phase2ShakeRepeatInterval = 0.4f;

	/**
	 * [Phase2ShakeRampV1] 광폭화 쉐이크 epicenter 시작 Z offset (보스 위, cm).
	 *   하늘에서 회오리가 발생하는 거리 — 쉐이크가 멀리서 시작.
	 *   포탈(보스 약 1.5m 거리)보다 10배 이상 멀리 = 5000cm (50m) 권장.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|광폭화|Shake",
		meta = (DisplayName = "쉐이크 시작 Z (보스 위 cm)", ClampMin = "0.0", ClampMax = "20000.0"))
	float Phase2ShakeStartZ = 5000.f;

	/**
	 * [Phase2ShakeRampV1] 광폭화 쉐이크 epicenter 끝 Z offset (보스 위, cm).
	 *   레이저 임팩트 시점 — 카메라 가까이.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|광폭화|Shake",
		meta = (DisplayName = "쉐이크 끝 Z (보스 위 cm)", ClampMin = "0.0", ClampMax = "5000.0"))
	float Phase2ShakeEndZ = 200.f;

	/**
	 * [Phase2ShakeRampV1] epicenter 가 시작 Z 에서 끝 Z 로 lerp 되는 시간 (초).
	 *   회오리가 하늘에서 보스로 내려오는 시간. 강도 ramp up 도 이 시간동안.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|광폭화|Shake",
		meta = (DisplayName = "쉐이크 ramp up 시간 (초)", ClampMin = "0.5", ClampMax = "30.0"))
	float Phase2ShakeDescentDuration = 6.0f;

	/**
	 * [Phase2ShakeRampV1] OuterRadius (cm) — 이 거리 이상에서 쉐이크 falloff 0.
	 *   epicenter 가 멀 때 strength 작게 → 가까워지면 strength 큼. (Outer 5500 + 시작거리 5000 = 약 10% 강도)
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|광폭화|Shake",
		meta = (DisplayName = "쉐이크 OuterRadius (cm)", ClampMin = "100.0", ClampMax = "20000.0"))
	float Phase2ShakeOuterRadius = 5500.f;

	/** [Phase2ShakeRampV1] InnerRadius (cm) — 이 거리 이내는 항상 풀 강도. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|광폭화|Shake",
		meta = (DisplayName = "쉐이크 InnerRadius (cm)", ClampMin = "0.0"))
	float Phase2ShakeInnerRadius = 0.f;

	/** [Phase2ShakeRampV1] 거리별 falloff 지수. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|광폭화|Shake",
		meta = (DisplayName = "쉐이크 Falloff", ClampMin = "0.1", ClampMax = "10.0"))
	float Phase2ShakeFalloff = 1.f;

	// =========================================================
	// [Phase2HealthFillV2] HP 회복 타이밍 — 2-stage fill (페이즈1 max 까지 → pause → 페이즈2 max 까지 뚫기)
	// =========================================================

	/**
	 * 강하 VFX 시작 (Stage3) 후 HP Stage1 fill 시작까지 delay (초).
	 *   레이저가 떨어지기 시작한 후 잠시 뒤부터 HP 가 차오르기 시작 → 시각 임팩트.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|광폭화|HP",
		meta = (DisplayName = "HP fill 시작 delay (강하 VFX 후 초)", ClampMin = "0.0", ClampMax = "10.0"))
	float Phase2HealthFillDelay = 1.5f;

	/**
	 * Stage 1: HP 1 → 페이즈1 OldMax (예: 100) 까지 lerp 시간 (초).
	 *   "뭐야 2페이즈인가?" 라고 인식하는 단계.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|광폭화|HP",
		meta = (DisplayName = "Stage1 (1→OldMax) 시간(초)", ClampMin = "0.1", ClampMax = "10.0"))
	float Phase2HealthFillStage1Duration = 1.5f;

	/**
	 * Stage 1 후 break-through 전 pause (초).
	 *   "Old max 도달했네" → "어? 더 차오르네?" 사이의 일시 정지. 권장 0.5~1.0초.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|광폭화|HP",
		meta = (DisplayName = "Break-through pause (초)", ClampMin = "0.0", ClampMax = "5.0"))
	float Phase2HealthFillBreakthroughPause = 0.7f;

	/**
	 * Stage 2: OldMax → NewMax (페이즈2 max) 까지 lerp 시간 (초).
	 *   "체력 왜이래?" — 기존 max 칸을 뚫고 차오름.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|광폭화|HP",
		meta = (DisplayName = "Stage2 (OldMax→NewMax) 시간(초)", ClampMin = "0.1", ClampMax = "10.0"))
	float Phase2HealthFillStage2Duration = 1.0f;

	/**
	 * [BerserkGlowV1] 광폭화 진입 시 보스 머티리얼에 적용할 베르세르크 발광 색.
	 *   HDR 가능 (R/G/B > 1.0). 황금색: (4, 3, 0.5). 붉은색: (5, 0.3, 0.1).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|광폭화",
		meta = (DisplayName = "광폭화 발광 색", HideAlphaChannel))
	FLinearColor BerserkGlowColor = FLinearColor(4.0f, 3.0f, 0.5f, 1.0f);

	/** [BerserkGlowV1] 광폭화 발광 강도 multiplier (1=기본, 5=강한 발광). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|광폭화",
		meta = (DisplayName = "광폭화 발광 강도", ClampMin = "0.0", ClampMax = "20.0"))
	float BerserkGlowBoost = 5.0f;

	// =========================================================
	// [BossArmorBreakV1] 페이즈2 진입 — 갑옷 분리/폭발 연출
	//   본체 메쉬는 갑옷 입은 SK 에서 갑옷 벗겨진 SK 로 swap (같은 스켈레톤이라 애니 그대로 유지).
	//   각 갑옷 SK 메쉬는 임시 ASkeletalMeshActor 로 spawn 해서 외부+위쪽으로 폭발하듯 날린 뒤 자동 소멸.
	//   PhysicsAsset 없이 ProjectileMovement+RotatingMovement 로 단순 물리 흉내 (오픈월드 부담 최소화).
	//   Multicast 안에서 호출 → 모든 머신(서버/리슨/리모트)에서 비주얼 일관 (Dedicated 는 skip).
	// =========================================================

	/** 광폭화 진입 시 본체에 적용할 '갑옷 벗겨진' 스켈레탈 메쉬. 비어있으면 본체 메쉬 swap 안 함. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|광폭화|갑옷",
		meta = (DisplayName = "광폭화 본체 메쉬"))
	TSoftObjectPtr<USkeletalMesh> Phase2BodyMesh;

	/** 광폭화 진입 시 보스 몸에서 떨어져나갈 갑옷 SK 메쉬 목록. 비어있으면 갑옷 스폰 스킵. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|광폭화|갑옷",
		meta = (DisplayName = "광폭화 분리 갑옷 메쉬"))
	TArray<TSoftObjectPtr<USkeletalMesh>> Phase2ArmorMeshes;

	/**
	 * [BossBerserkSkinV1] 페이즈2 광폭화 피부 머터리얼.
	 *   본체 메쉬 swap (SK_body) 후 'body2' 머터리얼 슬롯에 SetMaterialByName 으로 적용.
	 *   비어있으면 SK_body default 머터리얼 그대로 사용. BP CDO 에서 dropdown 으로 swap → 시각 효과 빠르게 비교.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|광폭화|갑옷",
		meta = (DisplayName = "광폭화 피부색"))
	TSoftObjectPtr<UMaterialInterface> Phase2BerserkSkinMaterial;

	/** 피부색 적용 지연 (초) — VFX 생성 후 N초 뒤에 SetMaterialByName 호출. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|광폭화|갑옷",
		meta = (DisplayName = "광폭화 피부색 적용 지연(초)", ClampMin = "0.0", ClampMax = "10.0"))
	float Phase2BerserkSkinDelay = 2.5f;

	/**
	 * [BossArmorBreakV2] 본체 메쉬 swap 후 보스 메쉬에 다시 붙여 유지할 갑옷 SK 메쉬 목록.
	 *   본체를 SK_body 로 swap 하면 모든 갑옷이 사라져 부자연스러우므로,
	 *   하체/허리 등 시각적으로 유지돼야 하는 갑옷을 LeaderPoseComponent 로 attach.
	 *   같은 스켈레톤이라 보스 자세를 그대로 따라감 (애니메이션 비용 0 — 메인 메쉬 한 번만 평가).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|광폭화|갑옷",
		meta = (DisplayName = "광폭화 유지 갑옷 메쉬"))
	TArray<TSoftObjectPtr<USkeletalMesh>> Phase2KeepArmorMeshes;

	/** 갑옷 횡 방향(외부) 폭발 속도 (cm/s). 갑옷 별로 ±30% 랜덤 가산. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|광폭화|갑옷",
		meta = (DisplayName = "갑옷 횡 폭발 속도(cm/s)", ClampMin = "0.0", ClampMax = "5000.0"))
	float Phase2ArmorBlastSpeed = 600.f;

	/** 갑옷 위쪽 솟구치는 초기 속도 (cm/s). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|광폭화|갑옷",
		meta = (DisplayName = "갑옷 위 솟구침(cm/s)", ClampMin = "0.0", ClampMax = "5000.0"))
	float Phase2ArmorUpwardSpeed = 350.f;

	/** 갑옷 자전 최대 RPS (각 축 별 ± 랜덤, 1=초당 360도). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|광폭화|갑옷",
		meta = (DisplayName = "갑옷 자전 최대 RPS", ClampMin = "0.0", ClampMax = "20.0"))
	float Phase2ArmorMaxSpinRPS = 4.f;

	/**
	 * 갑옷 액터 잔존 시간 (초). 만료 시 자동 destroy.
	 *   0 = 영구 유지 — Phase2ArmorFallDuration 만큼 떨어진 후 ground LineTrace snap → 맵에 남아있음.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|광폭화|갑옷",
		meta = (DisplayName = "갑옷 잔존 시간(초, 0=영구)", ClampMin = "0.0", ClampMax = "30.0"))
	float Phase2ArmorLifetime = 0.f;

	/** [BossArmorPersistV1] 영구 모드일 때 떨어지는 시간 (초). 이후 PMC/RMC 정지 + ground snap. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|광폭화|갑옷",
		meta = (DisplayName = "갑옷 낙하 시간(영구 모드)", ClampMin = "0.3", ClampMax = "5.0"))
	float Phase2ArmorFallDuration = 1.5f;

	/** 갑옷 스폰 위치 Z 오프셋 (보스 발 위치 기준 cm, 80~100=허리/가슴). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|광폭화|갑옷",
		meta = (DisplayName = "갑옷 스폰 Z 오프셋(cm)", ClampMin = "0.0", ClampMax = "300.0"))
	float Phase2ArmorSpawnZOffset = 90.f;

	/** 갑옷 중력 배율 (1=일반, 2=빨리 떨어짐). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|광폭화|갑옷",
		meta = (DisplayName = "갑옷 중력 배율", ClampMin = "0.0", ClampMax = "5.0"))
	float Phase2ArmorGravityScale = 1.f;

	// [Phase2RefactorV1] 대사 UPROPERTY 모두 트리거(BossPhase2CinematicTrigger)로 이동.

	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnBossEnterPhase2);

	/** 2페이즈 진입 알림 — 모든 클라. HP 바/UI/AI 등에서 바인딩해 반응. */
	UPROPERTY(BlueprintAssignable, Category = "Combat|Phase2")
	FOnBossEnterPhase2 OnBossEnterPhase2;

	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnBossPhase2BreakThroughStart);

	/**
	 * [Phase2HealthFillV3] Stage 2 break-through 시작 알림 — HP fill state machine 이 OldMax 도달 후
	 *   pause 끝나고 Stage 2 진입할 때 broadcast. HP 바 widget 의 회색 바 width 확장 트리거.
	 */
	UPROPERTY(BlueprintAssignable, Category = "Combat|Phase2")
	FOnBossPhase2BreakThroughStart OnBossPhase2BreakThroughStart;

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
	 * [SummonMontageMeshSinkV1] SummonMontage 재생 동안 보스 SkelMesh 의 RelativeLocation.Z 에 더할 오프셋 (cm).
	 *   AM_Boss_Walk 가 살짝 공중에 떠 있는 느낌을 보정하는 시각 전용 값 — 콜리전 캡슐/CMC 는 영향 없음.
	 *   Multicast_PlaySummonMontage 에서 한 번 적용, OnSummonMontageEnded cleanup (loop 종료) 에서 복원.
	 *   음수 = 아래로 내림. 0 이면 비활성.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Animation|Combat",
		meta = (DisplayName = "소환 몽타주 SkelMesh Z 오프셋 (cm, 음수=내림)",
			ClampMin = "-50.0", ClampMax = "50.0"))
	float SummonMontageMeshZOffset = -5.f;

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

	/** [BossDissolveSpawnV1] DissolveActor 의 VFX_A08 NiagaraComponent 활성화 — 모든 머신.
	 *   서버 timer 가 호출하면 각 머신이 자기 NiagaraComponent 의 Activate() 호출.
	 *   NiagaraComponent active state 는 자동 replicate 안 되므로 명시적 Multicast 필요.
	 */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_ActivateDissolveStageTwo(AActor* DissolveActor);

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

	/** [SummonMontageMeshSinkV1] SummonMontage 적용 직전 SkelMesh.RelativeLocation.Z 백업 (복원용). */
	float SavedMeshRelativeZ = 0.f;

	/** [SummonMontageMeshSinkV1] Z 오프셋 적용됐는지 — 중복 적용/복원 방지 (loop 시 한 번만). */
	bool bSummonMontageMeshOffsetApplied = false;

	/** [SummonMontageMeshSinkV1+TickGuard] Tick guard 첫 deviation 로그 1회 한정 플래그. */
	bool bSinkTickGuardLoggedDeviation = false;

	/** [Phase2ShakeRepeatV1] 2페이즈 시네마틱 동안 카메라 쉐이크 반복용 타이머 (각 머신 로컬). */
	FTimerHandle Phase2ShakeRepeatTimer;

	/** [Phase2ShakeRampV1] 쉐이크 ramp up 시작 시각 (월드 시간 초). */
	float Phase2ShakeStartTimeSeconds = 0.f;

	/**
	 * [Phase2ShakeRampV1] 쉐이크 1회 발화 — epicenter 를 시간 따라 시작 Z → 끝 Z 로 lerp.
	 *   거리 falloff 로 자동으로 강도 ramp up 효과 (멀리서 시작 → 가까워지며 강해짐).
	 */
	void PlayPhase2RampingShake();

public:
	/**
	 * [Phase2ShakeCleanupV2] 페이즈2 쉐이크 정리 — RepeatTimer 클리어 + 진행 중 instance fade out.
	 *   Phase2InvulnerabilityTimer 콜백 + Multicast_EndCinematic 에서 호출되어 다중 보장.
	 *   public — ABossPhase2CinematicTrigger 에서 호출 가능.
	 */
	void StopPhase2Shakes();

	/**
	 * [Phase2DescentAftermathV1] 시네마틱 종료 시 호출 — 'Phase2Descent' Tag NC 를 deactivate 하지 않고
	 *   레이저 이미터를 끈 변형(NS_Loop_SkyOpen_NoLaser)으로 asset 을 교체해 회오리만 루프 유지.
	 *   변형 에셋 로드 실패 시 기존 동작(Deactivate)으로 fallback.
	 *   public — ABossPhase2CinematicTrigger 의 종료 cleanup 에서 호출.
	 */
	void SwapDescentVFXToAftermath();

private:

	/** 2페이즈 진입 때 Brain을 우리가 멈췄는지 (타이머 종료 시 재시작용). */
	bool bAIStoppedForPhase2 = false;

	/** 히트 스톱 복원용 타이머 */
	FTimerHandle HitStopTimer;

	/** 마지막 히트 스톱 발화 시각 (쿨다운 체크) */
	double LastHitStopTime = 0.0;

	/** 2페이즈 오라 VFX 인스턴스 (Destruct 시 정리). */
	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> ActivePhase2AuraComp = nullptr;

	// [Phase2RefactorV1] 카메라/대사 멤버 모두 트리거(BossPhase2CinematicTrigger)로 이동.

	/** [Phase2DescentScaleV1] 광폭화 강하 VFX 스케일 보간 — base scales 캐시 + 누적 시간. */
	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<UNiagaraComponent>> Phase2DescentScalingNCs;
	TArray<FVector> Phase2DescentBaseScales;
	float Phase2DescentScaleElapsed = 0.f;
	bool bPhase2DescentScaling = false;

public:
	/** [Phase2HealthFillV3] HP fill state machine — 0=idle, 1=Stage1 fill, 2=pause, 3=Stage2 fill, 4=done.
	 *  Replicated — client widget polling 용 (server 만 Tick 안에서 update, client 는 OnRep 받음). */
	UPROPERTY(Replicated)
	int8 Phase2HealthFillStage = 0;
	float Phase2HealthFillElapsed = 0.f;
	/** [Phase2ExtraBarV1] OldMax/NewMax 도 client widget 이 읽도록 복제 — 별도 추가 HP 바 percent 계산용. */
	UPROPERTY(Replicated)
	float Phase2HealthFillOldMax = 0.f;
	UPROPERTY(Replicated)
	float Phase2HealthFillNewMax = 0.f;
private:

	/** [InertiaTickV1] 가장 최근 추적한 montage section — 변화 시 ABP 의 Inertialization 노드에 request. */
	FName LastTrackedMontageSection = NAME_None;
	TWeakObjectPtr<UAnimMontage> LastTrackedMontage;

	/** [BossArmorBreakV2] 본체 attach 유지 갑옷 SK 컴포넌트들 (Multicast 시 동적 생성, 보스와 함께 destroy). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<USkeletalMeshComponent>> Phase2AttachedArmorComps;

	/** 히트 스톱 복원 — 타이머 콜백 */
	void RestoreTimeDilationAfterHitStop();

	/**
	 * [BossArmorBreakV1] 페이즈2 멀티캐스트 안에서 호출 — 본체 메쉬 swap + 갑옷 폭발.
	 * Dedicated 서버는 비주얼 의미 없으므로 skip (비용 0).
	 */
	void Phase2_BreakArmor();

	/** 갑옷 분리/폭발 spawn — 피부색 timer 와 같은 시점에 호출. */
	void Phase2_SpawnArmorPieces();

public:
	/**
	 * [Phase2StageV1] 시네마틱 단계 3 비주얼 묶음 — 카메라 위 도달 후 호출.
	 *   광폭화 몽타주 (서버), BerserkGlow, 발밑/메테오/강하 VFX, 본체 swap+갑옷, 오라, 광폭화 강하 NC 활성.
	 *   트리거(BossPhase2CinematicTrigger) 가 단계 3 timer 시점에 호출.
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat|광폭화")
	void Phase2_PlayStage3Visuals();
};
