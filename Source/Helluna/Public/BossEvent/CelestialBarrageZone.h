// Capstone Project Helluna

#pragma once

#include "CoreMinimal.h"
#include "BossEvent/BossPatternZoneBase.h"
#include "CelestialBarrageZone.generated.h"

class UNiagaraSystem;
class AHellunaHeroCharacter;

/**
 * ACelestialBarrageZone
 *
 * 천벌 패턴 Zone Actor.
 *
 * ═══════════════════════════════════════════════════════════
 * 개요:
 *   하늘에 포탈이 열리고, 그 포탈에서 회전하는 레이저 빔이 내려와
 *   지면을 쓸어가며 데미지를 준다. 동시에 번개가 랜덤 위치에
 *   내리꽂히며 보조 공격을 가한다.
 *
 * 핵심 메카닉:
 *   1. 회전 레이저 (Sweeping Beam)
 *      - 보스 중심에서 직선으로 뻗어나가는 레이저가 회전
 *      - 가감속 + 방향 전환으로 예측을 어렵게 함
 *      - 레이저 수 증가 (1개→2개→4개)
 *      - 직선(Line) 판정 — 원형이 아닌 새로운 충돌 방식
 *
 *   2. 번개 낙뢰 (Lightning Strikes)
 *      - 패턴 중 랜덤 위치에 번개가 내리꽂힘
 *      - 짧은 경고 후 즉발 데미지
 *      - 레이저를 피하면서 번개도 피해야 하는 이중 위협
 *
 *   3. 페이즈 시스템
 *      - Phase 1: 레이저 1개, 느린 회전, 번개 없음
 *      - Phase 2: 레이저 2개 (180도 대칭), 번개 시작
 *      - Phase 3: 레이저 4개 (90도 간격), 빠른 회전, 번개 밀도 증가
 *
 * VFX 매칭:
 *   - PortalVFX: 하늘에 열리는 포탈 / 하늘 마법진
 *   - BeamVFX: 하늘에서 떨어지는 레이저 VFX
 *   - LightningWarningVFX: 바닥 경고 원 / 마법진
 *   - LightningStrikeVFX: 번개 낙하 VFX
 *
 * 성능:
 *   - 레이저 판정은 Line Trace가 아닌 수학적 직선-원 거리 계산
 *   - TActorIterator로 플레이어 1회 순회
 *   - 번개는 타이머 기반 (Tick 부하 최소)
 * ═══════════════════════════════════════════════════════════
 */
UCLASS(Blueprintable)
class HELLUNA_API ACelestialBarrageZone : public ABossPatternZoneBase
{
	GENERATED_BODY()

public:
	ACelestialBarrageZone();

	virtual void ActivateZone() override;
	virtual void DeactivateZone() override;

	// =========================================================
	// 레이저 빔 설정
	// =========================================================

	/** 레이저 길이 (cm) — 중심에서 끝까지의 거리 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "천벌|레이저",
		meta = (DisplayName = "레이저 길이 (cm)", ClampMin = "500.0", ClampMax = "5000.0"))
	float BeamLength = 2500.f;

	/** 레이저 폭 (cm) — 이 범위 안에 있으면 피격 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "천벌|레이저",
		meta = (DisplayName = "레이저 폭 (cm)", ClampMin = "50.0", ClampMax = "500.0"))
	float BeamWidth = 200.f;

	/** 레이저 피격 데미지 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "천벌|레이저",
		meta = (DisplayName = "레이저 데미지", ClampMin = "0.0"))
	float BeamDamage = 35.f;

	/** 레이저 피격 후 같은 레이저에 다시 맞기까지의 쿨타임 (초) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "천벌|레이저",
		meta = (DisplayName = "피격 쿨타임 (초)", ClampMin = "0.3", ClampMax = "3.0"))
	float BeamHitCooldown = 0.8f;

	/** Phase 1 회전 속도 (도/초) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "천벌|레이저",
		meta = (DisplayName = "Phase 1 회전 속도 (도/초)", ClampMin = "20.0", ClampMax = "180.0"))
	float Phase1RotationSpeed = 45.f;

	/** Phase 2 회전 속도 (도/초) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "천벌|레이저",
		meta = (DisplayName = "Phase 2 회전 속도 (도/초)", ClampMin = "30.0", ClampMax = "240.0"))
	float Phase2RotationSpeed = 70.f;

	/** Phase 3 회전 속도 (도/초) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "천벌|레이저",
		meta = (DisplayName = "Phase 3 회전 속도 (도/초)", ClampMin = "40.0", ClampMax = "360.0"))
	float Phase3RotationSpeed = 110.f;

	/** 방향 전환 주기 (초). 이 주기마다 회전 방향이 바뀜. 0이면 항상 같은 방향. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "천벌|레이저",
		meta = (DisplayName = "방향 전환 주기 (초)", ClampMin = "0.0", ClampMax = "10.0"))
	float DirectionChangePeriod = 4.0f;

	/**
	 * 레이저가 시작되지 않는 중심 영역 반지름 (cm).
	 * 보스 근처가 안전한 "사각지대"가 됨 → 위험 감수형 플레이 보상.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "천벌|레이저",
		meta = (DisplayName = "중심 안전 반지름 (cm)", ClampMin = "0.0", ClampMax = "500.0"))
	float BeamInnerRadius = 150.f;

	/** 빔 교차점 폭발 데미지 (Phase 2+에서 빔이 교차하는 지점에 폭발 발생) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "천벌|레이저",
		meta = (DisplayName = "교차점 폭발 데미지", ClampMin = "0.0"))
	float IntersectionDamage = 50.f;

	/** 교차점 폭발 반지름 (cm) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "천벌|레이저",
		meta = (DisplayName = "교차점 폭발 반지름 (cm)", ClampMin = "100.0", ClampMax = "500.0"))
	float IntersectionRadius = 300.f;

	/** 교차점 폭발 간격 (초) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "천벌|레이저",
		meta = (DisplayName = "교차점 폭발 간격 (초)", ClampMin = "0.5", ClampMax = "5.0"))
	float IntersectionInterval = 2.0f;

	/** 교차점 VFX */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "천벌|VFX",
		meta = (DisplayName = "교차점 VFX"))
	TObjectPtr<UNiagaraSystem> IntersectionVFX = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "천벌|VFX",
		meta = (DisplayName = "교차점 VFX 크기", ClampMin = "0.01", ClampMax = "10.0"))
	float IntersectionVFXScale = 1.f;

	// =========================================================
	// 페이즈 타이밍
	// =========================================================

	/** Phase 1 지속 시간 (초). 이후 Phase 2로 전환. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "천벌|페이즈",
		meta = (DisplayName = "Phase 1 시간 (초)", ClampMin = "1.0", ClampMax = "20.0"))
	float Phase1Duration = 4.0f;

	/** Phase 2 지속 시간 (초). 이후 Phase 3으로 전환. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "천벌|페이즈",
		meta = (DisplayName = "Phase 2 시간 (초)", ClampMin = "1.0", ClampMax = "20.0"))
	float Phase2Duration = 5.0f;

	// Phase 3는 PatternDuration까지 지속

	// =========================================================
	// 번개 설정
	// =========================================================

	/** 번개 데미지 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "천벌|번개",
		meta = (DisplayName = "번개 데미지", ClampMin = "0.0"))
	float LightningDamage = 50.f;

	/** 번개 피해 반지름 (cm) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "천벌|번개",
		meta = (DisplayName = "번개 반지름 (cm)", ClampMin = "100.0", ClampMax = "500.0"))
	float LightningRadius = 250.f;

	/** 번개 경고 시간 (초) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "천벌|번개",
		meta = (DisplayName = "경고 시간 (초)", ClampMin = "0.5", ClampMax = "3.0"))
	float LightningWarningDuration = 1.0f;

	/** Phase 2에서 번개 간격 (초) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "천벌|번개",
		meta = (DisplayName = "Phase 2 번개 간격 (초)", ClampMin = "0.5", ClampMax = "5.0"))
	float Phase2LightningInterval = 2.0f;

	/** Phase 3에서 번개 간격 (초) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "천벌|번개",
		meta = (DisplayName = "Phase 3 번개 간격 (초)", ClampMin = "0.3", ClampMax = "3.0"))
	float Phase3LightningInterval = 1.0f;

	/** 번개가 떨어지는 범위 반지름 (cm) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "천벌|번개",
		meta = (DisplayName = "번개 범위 (cm)", ClampMin = "500.0", ClampMax = "5000.0"))
	float LightningPatternRadius = 2000.f;

	/** 번개가 플레이어를 추적하는 비율 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "천벌|번개",
		meta = (DisplayName = "플레이어 추적 비율", ClampMin = "0.0", ClampMax = "1.0"))
	float LightningPlayerTargetRatio = 0.5f;

	// =========================================================
	// VFX / 사운드
	// =========================================================

	/** 하늘 포탈 VFX (패턴 시작 시 1회 — 하늘 포탈/마법진 권장) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "천벌|VFX",
		meta = (DisplayName = "포탈 VFX"))
	TObjectPtr<UNiagaraSystem> PortalVFX = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "천벌|VFX",
		meta = (DisplayName = "포탈 VFX 크기", ClampMin = "0.01", ClampMax = "20.0"))
	float PortalVFXScale = 1.f;

	/** 레이저 빔 VFX (하늘에서 떨어지는 레이저 권장). Phase 전환 시 갱신. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "천벌|VFX",
		meta = (DisplayName = "레이저 VFX"))
	TObjectPtr<UNiagaraSystem> BeamVFX = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "천벌|VFX",
		meta = (DisplayName = "레이저 VFX 크기", ClampMin = "0.01", ClampMax = "20.0"))
	float BeamVFXScale = 1.f;

	/** 번개 경고 VFX (바닥 마법진/원형 경고 권장) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "천벌|VFX",
		meta = (DisplayName = "번개 경고 VFX"))
	TObjectPtr<UNiagaraSystem> LightningWarningVFX = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "천벌|VFX",
		meta = (DisplayName = "번개 경고 VFX 크기", ClampMin = "0.01", ClampMax = "20.0"))
	float LightningWarningVFXScale = 1.f;

	/** 번개 낙하 VFX (번개 VFX 권장) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "천벌|VFX",
		meta = (DisplayName = "번개 VFX"))
	TObjectPtr<UNiagaraSystem> LightningStrikeVFX = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "천벌|VFX",
		meta = (DisplayName = "번개 VFX 크기", ClampMin = "0.01", ClampMax = "20.0"))
	float LightningStrikeVFXScale = 1.f;

	/** 레이저 사운드 (지속 재생) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "천벌|사운드",
		meta = (DisplayName = "레이저 사운드"))
	TObjectPtr<USoundBase> BeamSound = nullptr;

	/** 번개 사운드 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "천벌|사운드",
		meta = (DisplayName = "번개 사운드"))
	TObjectPtr<USoundBase> LightningSound = nullptr;

	/** 페이즈 전환 사운드 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "천벌|사운드",
		meta = (DisplayName = "페이즈 전환 사운드"))
	TObjectPtr<USoundBase> PhaseTransitionSound = nullptr;

protected:
	virtual void Tick(float DeltaTime) override;

private:
	// =========================================================
	// 레이저 런타임
	// =========================================================

	/** 개별 레이저 빔 상태 */
	struct FBeamState
	{
		float AngleDeg;
		/** 플레이어별 마지막 피격 시간 (쿨다운용) */
		TMap<TWeakObjectPtr<AActor>, double> LastHitTime;
	};

	/** 번개 낙뢰 대기 */
	struct FPendingLightning
	{
		FVector Location;
		double StrikeWorldTime;
	};

	TArray<FBeamState> Beams;
	TArray<FPendingLightning> PendingLightnings;

	float ElapsedTime = 0.f;
	int32 CurrentPhase = 0; // 1, 2, 3
	float CurrentRotationSpeed = 0.f;
	float RotationDirection = 1.f; // 1 = CW, -1 = CCW
	bool bZoneActive = false;

	FTimerHandle LightningTimerHandle;
	FTimerHandle PatternEndTimerHandle;

	// =========================================================
	// 내부 함수
	// =========================================================

	/** 페이즈 전환 체크 및 처리 */
	void UpdatePhase();

	/** 레이저 회전 + 방향 전환 */
	void UpdateBeamRotation(float DeltaTime);

	/** 레이저-플레이어 충돌 판정 (직선-점 거리 기반) */
	void ProcessBeamHits();

	/** 빔 교차점 폭발 처리 (Phase 2+) */
	void ProcessBeamIntersections();

	/** 번개 스폰 */
	void SpawnLightning();

	/** 번개 착탄 처리 */
	void ProcessPendingLightnings();

	/** 주어진 위치가 레이저 빔 내에 있는지 판정 */
	bool IsPointInBeam(const FVector& Point, const FBeamState& Beam) const;

	/** 패턴 종료 */
	void OnPatternTimeUp();

	/** 교차점 폭발 쿨타임 */
	double LastIntersectionTime = -9999.0;
};
