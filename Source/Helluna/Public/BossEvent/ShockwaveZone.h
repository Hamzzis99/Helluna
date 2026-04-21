// Capstone Project Helluna

#pragma once

#include "CoreMinimal.h"
#include "BossEvent/BossPatternZoneBase.h"
#include "ShockwaveZone.generated.h"

class UNiagaraSystem;
class AHellunaHeroCharacter;

/**
 * AShockwaveZone
 *
 * 파동 패턴 Zone Actor.
 * 보스 위치에서 원형 파동이 일정 간격으로 퍼져나가며,
 * 파동 링에 닿은 플레이어에게 데미지를 준다.
 *
 * 파동은 Tick 기반으로 반지름이 확장되며,
 * 매 프레임 SphereOverlap으로 링 범위 안의 플레이어를 검출한다.
 *
 * 점프 회피:
 *  bCanJumpDodge가 true이면 공중에 있는 플레이어는 파동을 회피할 수 있다.
 *  점프 타이밍에 따른 스킬 표현이 가능.
 *
 * VFX 추천:
 *  - 돌이 솟아오르는 이펙트: 파동 발사 시 지면이 솟구치는 연출
 *  - 마법진: 파동 발사 직전 바닥에 표시
 */
UCLASS(Blueprintable)
class HELLUNA_API AShockwaveZone : public ABossPatternZoneBase
{
	GENERATED_BODY()

public:
	AShockwaveZone();

	virtual void ActivateZone() override;
	virtual void DeactivateZone() override;

	// =========================================================
	// 파동 설정
	// =========================================================

	/** 총 발사할 파동 횟수 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "파동|기본",
		meta = (DisplayName = "파동 횟수", ClampMin = "1", ClampMax = "20"))
	int32 WaveCount = 3;

	/** 파동 간 발사 간격 (초) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "파동|기본",
		meta = (DisplayName = "파동 간격 (초)", ClampMin = "0.5", ClampMax = "5.0"))
	float WaveInterval = 1.5f;

	/** 파동 확장 속도 (cm/s) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "파동|기본",
		meta = (DisplayName = "파동 속도 (cm/s)", ClampMin = "100.0", ClampMax = "5000.0"))
	float WaveSpeed = 800.f;

	/** 파동 최대 반지름 (cm) — 이 거리에 도달하면 파동 소멸 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "파동|기본",
		meta = (DisplayName = "최대 반지름 (cm)", ClampMin = "500.0", ClampMax = "10000.0"))
	float MaxRadius = 3000.f;

	/** 파동 링의 두께 (cm) — 이 범위 안에 있으면 피격 판정 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "파동|기본",
		meta = (DisplayName = "파동 두께 (cm)", ClampMin = "50.0", ClampMax = "500.0"))
	float WaveThickness = 150.f;

	/** 파동 높이 범위 (cm) — 중심 Z ± 이 값 안에 있어야 피격 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "파동|기본",
		meta = (DisplayName = "파동 높이 범위 (cm)", ClampMin = "50.0", ClampMax = "1000.0"))
	float WaveHeight = 300.f;

	/** 파동 한 번 피격 시 데미지 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "파동|기본",
		meta = (DisplayName = "파동 데미지", ClampMin = "0.0"))
	float WaveDamage = 30.f;

	/**
	 * 점프로 파동을 피할 수 있는지 여부.
	 * true면 공중에 있는 플레이어(점프/낙하)는 파동이 지나갈 때 피격되지 않는다.
	 * 플레이어가 타이밍에 맞춰 점프하면 회피 가능 → 스킬 표현.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "파동|기본",
		meta = (DisplayName = "점프 회피 가능"))
	bool bCanJumpDodge = true;

	/** 점프 회피에 필요한 최소 공중 높이 (cm). 발밑이 이 높이 이상 올라와야 회피. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "파동|기본",
		meta = (DisplayName = "점프 회피 최소 높이 (cm)", ClampMin = "10.0", ClampMax = "300.0",
			EditCondition = "bCanJumpDodge"))
	float JumpDodgeMinHeight = 80.f;

	// =========================================================
	// VFX / 사운드
	// =========================================================

	/** 각 파동 발사 시 스폰할 Niagara VFX (중심에서 확장되는 링 등) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "파동|VFX",
		meta = (DisplayName = "파동 VFX"))
	TObjectPtr<UNiagaraSystem> WaveVFX = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "파동|VFX",
		meta = (DisplayName = "파동 VFX 크기", ClampMin = "0.01", ClampMax = "20.0"))
	float WaveVFXScale = 1.f;

	/** 파동 발사 시 사운드 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "파동|사운드",
		meta = (DisplayName = "파동 사운드"))
	TObjectPtr<USoundBase> WaveSound = nullptr;

protected:
	virtual void Tick(float DeltaTime) override;

private:
	/** 개별 파동의 런타임 상태 */
	struct FActiveWave
	{
		float CurrentRadius = 0.f;
		TSet<AActor*> AlreadyHitActors;
	};

	/** 현재 진행 중인 파동들 */
	TArray<FActiveWave> ActiveWaves;

	/** 다음 파동 발사 타이머 */
	FTimerHandle WaveSpawnTimerHandle;

	/** 발사된 파동 수 */
	int32 WavesFired = 0;

	bool bZoneActive = false;

	/** 새 파동 발사 */
	void FireNextWave();

	/** 파동 링과 플레이어 충돌 검사 및 데미지 처리 */
	void ProcessWaveHits(FActiveWave& Wave);

	/** 모든 파동이 끝났는지 확인 → 패턴 종료 */
	void CheckPatternComplete();
};
