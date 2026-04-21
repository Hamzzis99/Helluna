// Capstone Project Helluna

#pragma once

#include "CoreMinimal.h"
#include "BossEvent/BossPatternZoneBase.h"
#include "GravityVortexZone.generated.h"

class UNiagaraSystem;
class AHellunaHeroCharacter;

/**
 * AGravityVortexZone
 *
 * 중력 소용돌이 패턴 Zone Actor.
 *
 * 동작 흐름:
 *  1. 보스 위치에 중력장이 생성 (블랙홀 VFX + 마법진 권장)
 *  2. 범위 내 플레이어를 중심으로 끌어당긴다.
 *  3. 중심의 "눈"에 도달하면 큰 데미지.
 *  4. 궤도 위험물이 회전하며 닿으면 데미지. (쉴드 이펙트/구체 VFX 권장)
 *  5. 시간 경과에 따라: 흡인력 강화 + 범위 확장
 *  6. 맥동: 주기적으로 흡인 → 해제 반복 (해제 시 궤도 가속)
 *  7. 패턴 종료 시 폭발 + 넉백 (블랙홀 폭발 VFX 권장)
 *
 * 메카닉:
 *  - 이중 챌린지: 끌려가면서 궤도 위험물 회피
 *  - 맥동 해제 = 재배치 기회, but 궤도 가속 → 방심 불가
 *  - 패턴 종료 시 폭발 넉백 → 끝까지 긴장 유지
 *
 * VFX 추천:
 *  - VortexVFX: 블랙홀 이펙트 (중심 지속형)
 *  - OrbitalVFX: 쉴드 구체 / 원형 구체 이펙트 (궤도 위험물)
 *  - EndExplosionVFX: 블랙홀 폭발 이펙트
 *  - 바닥 마법진: 소용돌이 범위 표시용
 */
UCLASS(Blueprintable)
class HELLUNA_API AGravityVortexZone : public ABossPatternZoneBase
{
	GENERATED_BODY()

public:
	AGravityVortexZone();

	virtual void ActivateZone() override;
	virtual void DeactivateZone() override;

	// =========================================================
	// 중력 흡인 설정
	// =========================================================

	/** 시작 시 중력장 반지름 (cm). StartRadiusRatio로 조절. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "중력|흡인",
		meta = (DisplayName = "최대 반지름 (cm)", ClampMin = "500.0", ClampMax = "5000.0"))
	float VortexRadius = 2500.f;

	/**
	 * 시작 반지름 비율. 0.5 = 시작 시 최대 반지름의 50%에서 시작.
	 * 시간이 지나며 VortexRadius까지 확장된다.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "중력|흡인",
		meta = (DisplayName = "시작 반지름 비율", ClampMin = "0.3", ClampMax = "1.0"))
	float StartRadiusRatio = 0.5f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "중력|흡인",
		meta = (DisplayName = "시작 흡인력", ClampMin = "100.0", ClampMax = "3000.0"))
	float BasePullStrength = 500.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "중력|흡인",
		meta = (DisplayName = "최대 흡인력", ClampMin = "100.0", ClampMax = "5000.0"))
	float MaxPullStrength = 1200.f;

	// =========================================================
	// 맥동 시스템
	// =========================================================

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "중력|맥동",
		meta = (DisplayName = "맥동 주기 (초)", ClampMin = "0.0", ClampMax = "10.0"))
	float PullPulsePeriod = 3.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "중력|맥동",
		meta = (DisplayName = "해제 구간 최소 비율", ClampMin = "0.0", ClampMax = "0.9"))
	float PullPulseMinRatio = 0.1f;

	// =========================================================
	// 궤도 위험물 설정
	// =========================================================

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "중력|궤도",
		meta = (DisplayName = "궤도 위험물 수", ClampMin = "2", ClampMax = "8"))
	int32 OrbitalCount = 4;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "중력|궤도",
		meta = (DisplayName = "궤도 반지름 (cm)", ClampMin = "300.0", ClampMax = "3000.0"))
	float OrbitalRadius = 800.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "중력|궤도",
		meta = (DisplayName = "회전 속도 (도/초)", ClampMin = "30.0", ClampMax = "360.0"))
	float OrbitalSpeed = 90.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "중력|궤도",
		meta = (DisplayName = "해제 시 속도 배율", ClampMin = "1.0", ClampMax = "3.0"))
	float OrbitalReleaseSpeedMultiplier = 1.8f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "중력|궤도",
		meta = (DisplayName = "피격 반지름 (cm)", ClampMin = "50.0", ClampMax = "500.0"))
	float OrbitalHitRadius = 150.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "중력|궤도",
		meta = (DisplayName = "궤도 데미지", ClampMin = "0.0"))
	float OrbitalDamage = 25.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "중력|궤도",
		meta = (DisplayName = "궤도 높이 오프셋 (cm)", ClampMin = "0.0", ClampMax = "500.0"))
	float OrbitalHeightOffset = 100.f;

	// =========================================================
	// 중심 "눈" 데미지
	// =========================================================

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "중력|중심",
		meta = (DisplayName = "중심 반지름 (cm)", ClampMin = "100.0", ClampMax = "500.0"))
	float CenterDamageRadius = 200.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "중력|중심",
		meta = (DisplayName = "중심 데미지", ClampMin = "0.0"))
	float CenterDamage = 50.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "중력|중심",
		meta = (DisplayName = "중심 데미지 간격 (초)", ClampMin = "0.5", ClampMax = "3.0"))
	float CenterDamageInterval = 1.0f;

	// =========================================================
	// 사건의 지평선 (Event Horizon)
	// =========================================================

	/**
	 * 사건의 지평선 반지름 (cm).
	 * 이 안에 들어오면 흡인력이 EventHorizonPullMultiplier 배로 증가하고
	 * 추가 데미지를 받음. 0이면 비활성화.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "중력|지평선",
		meta = (DisplayName = "지평선 반지름 (cm)", ClampMin = "0.0", ClampMax = "1000.0"))
	float EventHorizonRadius = 500.f;

	/** 지평선 내부 흡인력 배율 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "중력|지평선",
		meta = (DisplayName = "흡인력 배율", ClampMin = "1.0", ClampMax = "5.0"))
	float EventHorizonPullMultiplier = 2.5f;

	/** 지평선 내부 DPS */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "중력|지평선",
		meta = (DisplayName = "지평선 DPS", ClampMin = "0.0"))
	float EventHorizonDPS = 30.f;

	/** 지평선 VFX (위험 경고 — 쉴드 이펙트 권장) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "중력|지평선",
		meta = (DisplayName = "지평선 VFX"))
	TObjectPtr<UNiagaraSystem> EventHorizonVFX = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "중력|지평선",
		meta = (DisplayName = "지평선 VFX 크기", ClampMin = "0.01", ClampMax = "20.0"))
	float EventHorizonVFXScale = 1.f;

	// =========================================================
	// 종료 폭발
	// =========================================================

	/** 패턴 종료 시 폭발 데미지 (범위 내 모든 플레이어) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "중력|폭발",
		meta = (DisplayName = "폭발 데미지", ClampMin = "0.0"))
	float EndExplosionDamage = 60.f;

	/** 폭발 넉백 수평 속도 (cm/s) — 중심에서 밀려남 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "중력|폭발",
		meta = (DisplayName = "넉백 수평 속도", ClampMin = "0.0", ClampMax = "3000.0"))
	float EndKnockbackStrength = 1500.f;

	/** 폭발 넉백 수직 속도 (cm/s) — 위로 띄움 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "중력|폭발",
		meta = (DisplayName = "넉백 수직 속도", ClampMin = "0.0", ClampMax = "1500.0"))
	float EndKnockbackUpward = 400.f;

	// =========================================================
	// VFX / 사운드
	// =========================================================

	/** 소용돌이 중심 지속 VFX (블랙홀 이펙트 권장) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "중력|VFX",
		meta = (DisplayName = "소용돌이 VFX"))
	TObjectPtr<UNiagaraSystem> VortexVFX = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "중력|VFX",
		meta = (DisplayName = "소용돌이 VFX 크기", ClampMin = "0.01", ClampMax = "20.0"))
	float VortexVFXScale = 1.f;

	/** 궤도 위험물 VFX (쉴드 구체/원형 구체 이펙트 권장) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "중력|VFX",
		meta = (DisplayName = "궤도 위험물 VFX"))
	TObjectPtr<UNiagaraSystem> OrbitalVFX = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "중력|VFX",
		meta = (DisplayName = "궤도 위험물 VFX 크기", ClampMin = "0.01", ClampMax = "20.0"))
	float OrbitalVFXScale = 1.f;

	/** 종료 폭발 VFX (블랙홀 폭발 이펙트 권장) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "중력|VFX",
		meta = (DisplayName = "폭발 VFX"))
	TObjectPtr<UNiagaraSystem> EndExplosionVFX = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "중력|VFX",
		meta = (DisplayName = "폭발 VFX 크기", ClampMin = "0.01", ClampMax = "20.0"))
	float EndExplosionVFXScale = 1.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "중력|사운드",
		meta = (DisplayName = "흡인 사운드"))
	TObjectPtr<USoundBase> PullSound = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "중력|사운드",
		meta = (DisplayName = "중심 데미지 사운드"))
	TObjectPtr<USoundBase> CenterDamageSound = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "중력|사운드",
		meta = (DisplayName = "폭발 사운드"))
	TObjectPtr<USoundBase> EndExplosionSound = nullptr;

protected:
	virtual void Tick(float DeltaTime) override;

private:
	struct FOrbitalHazard
	{
		float AngleDeg;
		TSet<TWeakObjectPtr<AActor>> HitActorsThisRevolution;
	};

	TArray<FOrbitalHazard> Orbitals;
	float ElapsedTime = 0.f;
	float CenterDamageAccumulator = 0.f;
	bool bZoneActive = false;
	FTimerHandle PatternEndTimerHandle;

	// 사건의 지평선 DPS 쿨타임
	TMap<TWeakObjectPtr<AActor>, double> EventHorizonDamageLastTime;
	bool bEventHorizonVFXSpawned = false;

	/** 현재 시점의 유효 반지름 (확장 중) */
	float GetCurrentVortexRadius() const;

	float CalculateCurrentPullStrength() const;
	float GetPulseFactor() const;

	void ApplyGravityPull(float DeltaTime);
	void UpdateOrbitals(float DeltaTime);
	void ProcessCenterDamage(float DeltaTime);
	void ProcessEventHorizon(float DeltaTime);

	/** 패턴 종료 — 폭발 + 넉백 + 정리 */
	void OnPatternTimeUp();
};
