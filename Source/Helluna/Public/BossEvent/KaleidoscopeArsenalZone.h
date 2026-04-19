// Capstone Project Helluna

#pragma once

#include "CoreMinimal.h"
#include "BossEvent/BossPatternZoneBase.h"
#include "KaleidoscopeArsenalZone.generated.h"

class UNiagaraSystem;
class AHellunaHeroCharacter;

/**
 * AKaleidoscopeArsenalZone
 *
 * 만화경 무장 패턴 Zone Actor.
 *
 * ═══════════════════════════════════════════════════════════
 * 개요:
 *   보스가 공중에 N개의 거울 평면을 펼친다.
 *   보스가 발사한 단일 "시드 빔"은 거울 사이를 재귀적으로 반사하며
 *   최종적으로 수십 개의 가상 빔으로 증식한다.
 *   전체 패턴은 만화경처럼 완벽한 회전 대칭을 그리며 천천히 회전.
 *
 * 핵심 알고리즘:
 *   1. 거울 평면 (Mirror Planes)
 *      - 보스 중심에서 방사상으로 뻗는 N개의 수직 평면
 *      - 인접 거울 각도 = 360 / (2 * MirrorCount)
 *      - 거울은 유한 반지름(MirrorRadius)까지만 유효
 *
 *   2. 재귀 반사 (Recursive Reflection)
 *      - 시드 빔 (Seed Ray): 보스에서 랜덤 방향
 *      - 빔이 진행하다가 거울과 교차하면 반사 벡터 계산
 *      - 반사 공식: D' = D - 2*(D · N)*N
 *      - 최대 BounceDepth 회 반사 후 종결
 *      - 결과: 폴리라인 (다수의 선분으로 구성된 빔 경로)
 *
 *   3. 회전 대칭 복제 (Rotational Symmetry)
 *      - 생성된 폴리라인을 보스 중심으로 360/SymmetryCount 회전하여 복제
 *      - 완벽한 만화경 대칭 (C_n 대칭군)
 *
 *   4. 데미지 판정
 *      - 각 폴리라인 세그먼트에 대해 점-선분 거리
 *      - BeamWidth 이내이면 피격
 *      - 피격 쿨다운 per beam
 *
 * 시각적 특징:
 *   - 완벽한 대칭성 → 혼란스럽지만 아름다운 위협
 *   - 전체 패턴이 천천히 회전 → 플레이어는 "회전하는 틈"을 찾아 이동
 *   - 시드 빔마다 다른 반사 경로 → 매번 새로운 대칭 문양
 *
 * 기술 플렉스:
 *   - 평면 반사 행렬 (Householder transform)
 *   - 재귀적 빔-평면 교차 계산
 *   - 회전 대칭 그룹 C_n 구현
 * ═══════════════════════════════════════════════════════════
 */
UCLASS(Blueprintable)
class HELLUNA_API AKaleidoscopeArsenalZone : public ABossPatternZoneBase
{
	GENERATED_BODY()

public:
	AKaleidoscopeArsenalZone();

	virtual void ActivateZone() override;
	virtual void DeactivateZone() override;

	// =========================================================
	// 거울 설정
	// =========================================================

	/** 거울 평면 수. 대칭 차수를 결정. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "만화경|거울",
		meta = (DisplayName = "거울 수", ClampMin = "3", ClampMax = "8"))
	int32 MirrorCount = 6;

	/** 거울 반지름 (cm) — 이 거리 이내에서만 반사 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "만화경|거울",
		meta = (DisplayName = "거울 반지름 (cm)", ClampMin = "500.0", ClampMax = "4000.0"))
	float MirrorRadius = 2000.f;

	/** 거울 전체 회전 속도 (도/초) — 만화경 자체가 회전 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "만화경|거울",
		meta = (DisplayName = "거울 회전 속도 (도/초)", ClampMin = "0.0", ClampMax = "90.0"))
	float MirrorSpinSpeed = 12.f;

	// =========================================================
	// 빔 설정
	// =========================================================

	/** 시드 빔 발사 간격 (초) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "만화경|빔",
		meta = (DisplayName = "시드 간격 (초)", ClampMin = "0.5", ClampMax = "6.0"))
	float SeedInterval = 2.0f;

	/** 시드 빔이 살아있는 시간 (초) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "만화경|빔",
		meta = (DisplayName = "빔 유지 시간 (초)", ClampMin = "0.5", ClampMax = "8.0"))
	float BeamLifetime = 3.0f;

	/** 최대 반사 깊이 — 시드 빔이 몇 번 반사될지 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "만화경|빔",
		meta = (DisplayName = "반사 깊이", ClampMin = "1", ClampMax = "8"))
	int32 BounceDepth = 4;

	/** 각 세그먼트 최대 길이 (cm) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "만화경|빔",
		meta = (DisplayName = "세그먼트 최대 길이 (cm)", ClampMin = "500.0", ClampMax = "5000.0"))
	float MaxSegmentLength = 2500.f;

	/** 빔 판정 폭 (cm) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "만화경|빔",
		meta = (DisplayName = "빔 폭 (cm)", ClampMin = "30.0", ClampMax = "300.0"))
	float BeamWidth = 100.f;

	/** 빔 데미지 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "만화경|빔",
		meta = (DisplayName = "빔 데미지", ClampMin = "0.0"))
	float BeamDamage = 1.f;

	/** 같은 빔에 다시 맞기까지 쿨다운 (초) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "만화경|빔",
		meta = (DisplayName = "피격 쿨다운 (초)", ClampMin = "0.2", ClampMax = "3.0"))
	float HitCooldown = 0.6f;

	// =========================================================
	// VFX
	// =========================================================

	/** 거울 평면 시각화 VFX (Beam 타입 권장) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "만화경|VFX",
		meta = (DisplayName = "거울 VFX"))
	TObjectPtr<UNiagaraSystem> MirrorVFX = nullptr;

	/** 빔 세그먼트 VFX (Beam 타입, Start/End 위치 파라미터) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "만화경|VFX",
		meta = (DisplayName = "빔 VFX"))
	TObjectPtr<UNiagaraSystem> BeamVFX = nullptr;

	/** 반사 지점 스파크 VFX */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "만화경|VFX",
		meta = (DisplayName = "반사 스파크 VFX"))
	TObjectPtr<UNiagaraSystem> BounceSparkVFX = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "만화경|VFX",
		meta = (DisplayName = "VFX 스케일", ClampMin = "0.01", ClampMax = "10.0"))
	float VFXScale = 1.f;

protected:
	virtual void Tick(float DeltaTime) override;

private:
	// =========================================================
	// 런타임
	// =========================================================

	/** 단일 빔 세그먼트 */
	struct FBeamSegment
	{
		FVector Start;
		FVector End;
	};

	/** 하나의 시드 빔이 생성한 폴리라인 (반사 경로) + 대칭 복제본 */
	struct FActiveBeam
	{
		TArray<FBeamSegment> Segments; // 반사 경로 세그먼트들 (대칭 복제 포함)
		double SpawnWorldTime = 0.0;
		TMap<TWeakObjectPtr<AActor>, double> LastHitTime;
	};

	TArray<FActiveBeam> ActiveBeams;

	/** 거울의 현재 회전 각도 (도) */
	float CurrentMirrorAngle = 0.f;

	float SeedAccumulator = 0.f;
	bool bZoneActive = false;

	// =========================================================
	// 내부 함수
	// =========================================================

	/**
	 * 평면에 빔 경로 반사.
	 * 시드 (Origin, Direction)에서 시작해 BounceDepth회 거울 반사를 수행하고
	 * 폴리라인을 OutSegments에 채운다.
	 */
	void TraceReflectedBeam(const FVector& Origin, const FVector& Direction,
	                        TArray<FBeamSegment>& OutSegments) const;

	/**
	 * 거울 평면 교차 검사.
	 * @return true이면 OutIntersect에 교차점, OutNormal에 거울 법선(평면 안쪽 방향)
	 */
	bool IntersectMirrors(const FVector& Origin, const FVector& Direction, float MaxLen,
	                      FVector& OutIntersect, FVector& OutNormal, int32 IgnoreMirrorIdx,
	                      int32& OutHitMirrorIdx) const;

	/** 거울 평면 하나의 법선 벡터 계산 (현재 회전 반영) */
	FVector GetMirrorNormal(int32 MirrorIdx) const;

	/** 시드 빔 발사 */
	void FireSeedBeam();

	/** 점-선분 최단거리 제곱 (2D) */
	static float PointToSegmentDistSq2D(const FVector2D& P, const FVector2D& A, const FVector2D& B);

	/** 데미지 처리 */
	void ProcessDamage();

	/** 만료된 빔 정리 */
	void CleanupBeams();

	/** 폴리라인을 중심축 기준 회전 복제 */
	void ApplySymmetryReplication(TArray<FBeamSegment>& Segments) const;
};
