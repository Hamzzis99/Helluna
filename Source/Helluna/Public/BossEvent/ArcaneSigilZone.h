// Capstone Project Helluna

#pragma once

#include "CoreMinimal.h"
#include "BossEvent/BossPatternZoneBase.h"
#include "ArcaneSigilZone.generated.h"

class UNiagaraSystem;
class AHellunaHeroCharacter;

/**
 * AArcaneSigilZone
 *
 * 마법진 봉인 패턴 Zone Actor.
 *
 * ═══════════════════════════════════════════════════════════
 * 개요:
 *   보스 주변에 거대한 동심원 마법진이 나타난다.
 *   마법진은 여러 개의 동심원 링으로 구성되며,
 *   각 링은 "안전 섹터"와 "위험 섹터"로 나뉜다.
 *   링들이 각각 다른 속도/방향으로 회전하며,
 *   주기적으로 "봉인(Lock)" 되면서 위험 섹터가 폭발한다.
 *
 * 핵심 메카닉:
 *   1. 동심원 링 (Concentric Rings)
 *      - 2~4개의 링, 각각 내경/외경 보유
 *      - 링마다 2~6개 섹터 (안전/위험 교대)
 *      - 각 링 독립적으로 회전 (방향, 속도 다름)
 *
 *   2. 섹터 판정 (Sector Detection)
 *      - 플레이어의 거리(링 판별) + 각도(섹터 판별)
 *      - 위험 섹터에 있으면 지속 데미지
 *      - 안전 섹터는 완전히 안전
 *
 *   3. 봉인 페이즈 (Lock Phase)
 *      - 주기적으로 회전이 멈추고 "봉인" 발동
 *      - 짧은 경고 후 위험 섹터에서 대규모 폭발
 *      - 폭발 후 링 구성이 변경 (섹터 수 증가, 속도 변화)
 *
 *   4. 에스컬레이션
 *      - 봉인이 발동할 때마다 안전 섹터가 줄어듦
 *      - 회전 속도 증가
 *      - 최종 봉인: 모든 섹터 폭발 → 중심만 안전
 *
 * VFX 매칭:
 *   - SigilVFX: 바닥 마법진 (전체)
 *   - RingVFX: 개별 링 이펙트
 *   - LockWarningVFX: 봉인 경고
 *   - SectorExplodeVFX: 섹터 폭발
 *   - FinalSigilVFX: 최종 봉인 폭발
 *
 * 성능:
 *   - 섹터 판정은 순수 수학 (atan2 + 거리)
 *   - TActorIterator 1회 순회
 *   - VFX는 Lock 시에만 집중 발생
 * ═══════════════════════════════════════════════════════════
 */
UCLASS(Blueprintable)
class HELLUNA_API AArcaneSigilZone : public ABossPatternZoneBase
{
	GENERATED_BODY()

public:
	AArcaneSigilZone();

	virtual void ActivateZone() override;
	virtual void DeactivateZone() override;

	// =========================================================
	// 링 구성
	// =========================================================

	/** 동심원 링 수 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "마법진|링",
		meta = (DisplayName = "링 수", ClampMin = "2", ClampMax = "4"))
	int32 RingCount = 3;

	/** 가장 안쪽 링의 내경 (cm) — 이 안쪽은 중심 안전 지대 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "마법진|링",
		meta = (DisplayName = "내부 안전 반지름 (cm)", ClampMin = "100.0", ClampMax = "500.0"))
	float InnerSafeRadius = 200.f;

	/** 가장 바깥 링의 외경 (cm) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "마법진|링",
		meta = (DisplayName = "외부 반지름 (cm)", ClampMin = "1000.0", ClampMax = "4000.0"))
	float OuterRadius = 2500.f;

	/** 기본 섹터 수 (링당). 안전+위험 교대이므로 짝수 권장. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "마법진|링",
		meta = (DisplayName = "기본 섹터 수", ClampMin = "2", ClampMax = "8"))
	int32 BaseSectorCount = 4;

	/** 기본 회전 속도 (도/초) — 각 링마다 배수로 적용 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "마법진|링",
		meta = (DisplayName = "기본 회전 속도 (도/초)", ClampMin = "10.0", ClampMax = "120.0"))
	float BaseRotationSpeed = 30.f;

	/** 안전 섹터 비율 (0~1). 0.5면 절반이 안전. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "마법진|링",
		meta = (DisplayName = "안전 비율", ClampMin = "0.2", ClampMax = "0.6"))
	float SafeSectorRatio = 0.5f;

	// =========================================================
	// 데미지
	// =========================================================

	/** 위험 섹터 지속 데미지 (DPS) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "마법진|데미지",
		meta = (DisplayName = "위험 섹터 DPS", ClampMin = "0.0"))
	float DangerDPS = 15.f;

	/** 봉인 폭발 데미지 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "마법진|데미지",
		meta = (DisplayName = "봉인 폭발 데미지", ClampMin = "0.0"))
	float LockExplosionDamage = 70.f;

	/** 최종 봉인 폭발 데미지 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "마법진|데미지",
		meta = (DisplayName = "최종 폭발 데미지", ClampMin = "0.0"))
	float FinalExplosionDamage = 150.f;

	/** 외부 영역 (마법진 밖) 데미지 — 도망 방지 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "마법진|데미지",
		meta = (DisplayName = "외부 DPS", ClampMin = "0.0"))
	float OutsideDPS = 25.f;

	// =========================================================
	// 봉인 (Lock) 설정
	// =========================================================

	/** 봉인 발동 주기 (초) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "마법진|봉인",
		meta = (DisplayName = "봉인 주기 (초)", ClampMin = "3.0", ClampMax = "15.0"))
	float LockInterval = 6.0f;

	/** 봉인 경고 시간 (초) — 회전이 멈추고 경고 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "마법진|봉인",
		meta = (DisplayName = "경고 시간 (초)", ClampMin = "0.5", ClampMax = "3.0"))
	float LockWarningDuration = 1.5f;

	/** 봉인마다 안전 비율 감소량 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "마법진|봉인",
		meta = (DisplayName = "안전 비율 감소", ClampMin = "0.0", ClampMax = "0.15"))
	float SafeRatioDecayPerLock = 0.05f;

	/** 봉인마다 회전 속도 증가 배율 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "마법진|봉인",
		meta = (DisplayName = "속도 증가 배율", ClampMin = "1.0", ClampMax = "1.5"))
	float SpeedMultiplierPerLock = 1.15f;

	/** 최종 봉인 발동까지의 봉인 횟수 (이후 패턴 종료) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "마법진|봉인",
		meta = (DisplayName = "최대 봉인 횟수", ClampMin = "2", ClampMax = "6"))
	int32 MaxLockCount = 4;

	// =========================================================
	// VFX / 사운드
	// =========================================================

	/** 전체 마법진 VFX (바닥 마법진) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "마법진|VFX",
		meta = (DisplayName = "마법진 VFX"))
	TObjectPtr<UNiagaraSystem> SigilVFX = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "마법진|VFX",
		meta = (DisplayName = "마법진 VFX 크기", ClampMin = "0.01", ClampMax = "20.0"))
	float SigilVFXScale = 1.f;

	/** 봉인 경고 VFX */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "마법진|VFX",
		meta = (DisplayName = "경고 VFX"))
	TObjectPtr<UNiagaraSystem> LockWarningVFX = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "마법진|VFX",
		meta = (DisplayName = "경고 VFX 크기", ClampMin = "0.01", ClampMax = "20.0"))
	float LockWarningVFXScale = 1.f;

	/** 섹터 폭발 VFX */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "마법진|VFX",
		meta = (DisplayName = "폭발 VFX"))
	TObjectPtr<UNiagaraSystem> SectorExplodeVFX = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "마법진|VFX",
		meta = (DisplayName = "폭발 VFX 크기", ClampMin = "0.01", ClampMax = "20.0"))
	float SectorExplodeVFXScale = 1.f;

	/** 최종 봉인 VFX */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "마법진|VFX",
		meta = (DisplayName = "최종 폭발 VFX"))
	TObjectPtr<UNiagaraSystem> FinalSigilVFX = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "마법진|VFX",
		meta = (DisplayName = "최종 폭발 VFX 크기", ClampMin = "0.01", ClampMax = "20.0"))
	float FinalSigilVFXScale = 1.f;

	/** 링 회전 사운드 (루프) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "마법진|사운드",
		meta = (DisplayName = "회전 사운드"))
	TObjectPtr<USoundBase> RotationSound = nullptr;

	/** 봉인 경고 사운드 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "마법진|사운드",
		meta = (DisplayName = "경고 사운드"))
	TObjectPtr<USoundBase> LockWarningSound = nullptr;

	/** 폭발 사운드 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "마법진|사운드",
		meta = (DisplayName = "폭발 사운드"))
	TObjectPtr<USoundBase> ExplosionSound = nullptr;

	// =========================================================
	// 블루프린트 읽기 전용 — 현재 상태 (디버그/UI용)
	// =========================================================

	/** 현재 봉인 횟수 */
	UPROPERTY(BlueprintReadOnly, Category = "마법진|상태")
	int32 CurrentLockCount = 0;

	/** 봉인 경고 중인지 */
	UPROPERTY(BlueprintReadOnly, Category = "마법진|상태")
	bool bIsLockWarning = false;

protected:
	virtual void Tick(float DeltaTime) override;

private:
	// =========================================================
	// 링 런타임 구조체
	// =========================================================

	/** 개별 링 상태 */
	struct FRingState
	{
		float InnerRadius;
		float OuterRadius;
		int32 SectorCount;         // 총 섹터 수
		float CurrentAngleOffset;  // 현재 회전 각도 (도)
		float RotationSpeed;       // 도/초 (음수면 역방향)
		float CurrentSafeRatio;    // 현재 안전 섹터 비율
	};

	TArray<FRingState> Rings;

	bool bZoneActive = false;
	float ElapsedTime = 0.f;
	float TimeSinceLastLock = 0.f;
	bool bLockWarningActive = false;
	double LockWarningStartTime = 0.0;

	// DPS 적용 쿨타임
	TMap<TWeakObjectPtr<AActor>, double> DangerDamageLastTime;
	TMap<TWeakObjectPtr<AActor>, double> OutsideDamageLastTime;

	// =========================================================
	// 내부 함수
	// =========================================================

	/** 링 초기화 */
	void InitializeRings();

	/** 링 회전 업데이트 */
	void UpdateRotation(float DeltaTime);

	/** 섹터 기반 데미지 처리 */
	void ProcessSectorDamage(float DeltaTime);

	/** 봉인 경고 시작 */
	void BeginLockWarning();

	/** 봉인 폭발 실행 */
	void ExecuteLock();

	/** 최종 봉인 */
	void ExecuteFinalLock();

	/** 봉인 후 링 재구성 (에스컬레이션) */
	void EscalateRings();

	/**
	 * 주어진 위치가 어떤 링의 어떤 섹터에 있는지 판정.
	 * @return true: 위험 섹터, false: 안전 섹터 또는 링 밖
	 */
	bool IsInDangerSector(const FVector& WorldPos, int32& OutRingIndex) const;

	/** 주어진 위치가 마법진 전체 영역 밖인지 */
	bool IsOutsideSigil(const FVector& WorldPos) const;

	/** 특정 링의 위험 섹터 중심 위치들 반환 (VFX 배치용) */
	TArray<FVector> GetDangerSectorCenters(int32 RingIndex) const;

	/** 각도 정규화 (0 ~ 360) */
	static float NormalizeAngle(float Angle);
};
