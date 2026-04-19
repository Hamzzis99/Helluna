// Capstone Project Helluna

#pragma once

#include "CoreMinimal.h"
#include "BossEvent/BossPatternZoneBase.h"
#include "BoidPredatorSwarmZone.generated.h"

class UNiagaraSystem;
class UNiagaraComponent;
class AHellunaHeroCharacter;

/**
 * ABoidPredatorSwarmZone
 *
 * 군집 포식자 패턴 Zone Actor.
 *
 * ═══════════════════════════════════════════════════════════
 * 개요:
 *   보스가 수십 개의 소형 포식 드론(boid)을 소환한다.
 *   각 드론은 Craig Reynolds의 고전 Boids 알고리즘으로 군집을 이루고,
 *   추가로 플레이어를 사냥한다. 드론 수십 마리가 물고기떼·새떼처럼
 *   흐르며, 때때로 집단으로 플레이어를 향해 급강하(Dive).
 *
 * 핵심 알고리즘 (Craig Reynolds 1987):
 *   1. Separation (분리)
 *      - 이웃이 너무 가까우면 반대 방향으로 밀어냄
 *      - Σ (self - neighbor) / dist²
 *
 *   2. Alignment (정렬)
 *      - 이웃의 평균 속도 방향으로 정렬
 *      - Σ neighbor.velocity / count
 *
 *   3. Cohesion (응집)
 *      - 이웃의 평균 위치로 끌림
 *      - (Σ neighbor.pos / count) - self.pos
 *
 *   4. Predator Chase (추격)
 *      - 가장 가까운 플레이어 방향으로 추가 가속
 *      - 무리 중심에서 이탈 허용
 *
 *   5. Dive Mode (급강하 모드)
 *      - 주기적으로 모든 drone이 chase 비중을 극단적으로 높임
 *      - 일시적으로 군집 형태 붕괴 → 플레이어 집중 공격
 *      - 쿨다운 후 다시 무리로 복귀
 *
 * 공간 최적화:
 *   - Uniform grid spatial hash: 각 cell에 drone 인덱스 저장
 *   - 이웃 탐색 O(N) (평균 밀도 기준)
 *   - 수십 마리 규모에서 Tick 부하 최소화
 *
 * 시각적 특징:
 *   - 유기적 군집 흐름 — 완벽한 대칭이나 고정 패턴이 아닌 살아있는 움직임
 *   - 급강하 시 "포식자" 느낌
 *   - 드론 수 = 직접적 위협감
 *
 * 기술 플렉스:
 *   - Reynolds Boids (게임 업계 고전 AI)
 *   - Uniform Spatial Hash (O(N))
 *   - 다중 가중치 조합 힘 합성 (Force blending)
 * ═══════════════════════════════════════════════════════════
 */
UCLASS(Blueprintable)
class HELLUNA_API ABoidPredatorSwarmZone : public ABossPatternZoneBase
{
	GENERATED_BODY()

public:
	ABoidPredatorSwarmZone();

	virtual void ActivateZone() override;
	virtual void DeactivateZone() override;

	// =========================================================
	// 군집 설정
	// =========================================================

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "군집|기본",
		meta = (DisplayName = "드론 수", ClampMin = "5", ClampMax = "120"))
	int32 BoidCount = 40;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "군집|기본",
		meta = (DisplayName = "초기 배치 반지름 (cm)", ClampMin = "200.0", ClampMax = "3000.0"))
	float SpawnRadius = 800.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "군집|기본",
		meta = (DisplayName = "드론 기본 속도", ClampMin = "100.0", ClampMax = "2500.0"))
	float BaseSpeed = 600.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "군집|기본",
		meta = (DisplayName = "드론 최대 속도", ClampMin = "100.0", ClampMax = "3500.0"))
	float MaxSpeed = 1100.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "군집|기본",
		meta = (DisplayName = "최대 선회력 (가속도)", ClampMin = "100.0", ClampMax = "10000.0"))
	float MaxForce = 2000.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "군집|기본",
		meta = (DisplayName = "비행 높이 (cm)", ClampMin = "50.0", ClampMax = "800.0"))
	float FlightHeight = 200.f;

	// =========================================================
	// Boids 가중치
	// =========================================================

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "군집|Boids",
		meta = (DisplayName = "이웃 반경 (cm)", ClampMin = "100.0", ClampMax = "2000.0"))
	float NeighborRadius = 500.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "군집|Boids",
		meta = (DisplayName = "분리 반경 (cm)", ClampMin = "50.0", ClampMax = "500.0"))
	float SeparationRadius = 150.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "군집|Boids",
		meta = (DisplayName = "분리 가중치", ClampMin = "0.0", ClampMax = "10.0"))
	float SeparationWeight = 2.5f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "군집|Boids",
		meta = (DisplayName = "정렬 가중치", ClampMin = "0.0", ClampMax = "10.0"))
	float AlignmentWeight = 1.2f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "군집|Boids",
		meta = (DisplayName = "응집 가중치", ClampMin = "0.0", ClampMax = "10.0"))
	float CohesionWeight = 1.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "군집|Boids",
		meta = (DisplayName = "추격 가중치", ClampMin = "0.0", ClampMax = "10.0"))
	float ChaseWeight = 1.5f;

	// =========================================================
	// 급강하 (Dive) 모드
	// =========================================================

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "군집|Dive",
		meta = (DisplayName = "Dive 주기 (초)", ClampMin = "3.0", ClampMax = "20.0"))
	float DiveInterval = 8.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "군집|Dive",
		meta = (DisplayName = "Dive 지속 시간 (초)", ClampMin = "1.0", ClampMax = "8.0"))
	float DiveDuration = 3.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "군집|Dive",
		meta = (DisplayName = "Dive 중 추격 가중치", ClampMin = "1.0", ClampMax = "20.0"))
	float DiveChaseWeight = 6.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "군집|Dive",
		meta = (DisplayName = "Dive 중 최대 속도 배율", ClampMin = "1.0", ClampMax = "3.0"))
	float DiveSpeedMultiplier = 1.6f;

	// =========================================================
	// 데미지
	// =========================================================

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "군집|데미지",
		meta = (DisplayName = "접촉 반지름 (cm)", ClampMin = "30.0", ClampMax = "300.0"))
	float ContactRadius = 80.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "군집|데미지",
		meta = (DisplayName = "접촉 데미지", ClampMin = "0.0"))
	float ContactDamage = 1.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "군집|데미지",
		meta = (DisplayName = "접촉 쿨다운 (초)", ClampMin = "0.2", ClampMax = "3.0"))
	float ContactCooldown = 0.4f;

	// =========================================================
	// VFX
	// =========================================================

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "군집|VFX",
		meta = (DisplayName = "드론 VFX (각 드론마다 1개)"))
	TObjectPtr<UNiagaraSystem> BoidVFX = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "군집|VFX",
		meta = (DisplayName = "VFX 스케일", ClampMin = "0.01", ClampMax = "5.0"))
	float BoidVFXScale = 0.6f;

protected:
	virtual void Tick(float DeltaTime) override;

private:
	struct FBoid
	{
		FVector Position = FVector::ZeroVector;
		FVector Velocity = FVector::ZeroVector;
		FVector Acceleration = FVector::ZeroVector;
		TWeakObjectPtr<UNiagaraComponent> VFXComp;
	};

	TArray<FBoid> Boids;

	// Uniform grid spatial hash
	TMap<int64, TArray<int32>> SpatialHash;
	float HashCellSize = 500.f;

	bool bZoneActive = false;
	bool bInDiveMode = false;
	float DiveTimer = 0.f;
	float DiveElapsed = 0.f;

	TMap<TWeakObjectPtr<AActor>, double> LastContactTime;

	FTimerHandle PatternEndTimerHandle;

	void SpawnBoids();
	void BuildSpatialHash();
	int64 HashKey(const FVector& Pos) const;
	void GatherNeighbors(int32 BoidIdx, TArray<int32>& OutNeighbors) const;

	FVector ComputeSeparation(int32 Idx, const TArray<int32>& Neighbors) const;
	FVector ComputeAlignment(int32 Idx, const TArray<int32>& Neighbors) const;
	FVector ComputeCohesion(int32 Idx, const TArray<int32>& Neighbors) const;
	FVector ComputeChase(int32 Idx) const;

	void UpdateBoids(float DeltaTime);
	void UpdateDiveMode(float DeltaTime);
	void ProcessContactDamage();
	void UpdateVFXPositions();

	FVector LimitVector(const FVector& V, float MaxLen) const;
};
