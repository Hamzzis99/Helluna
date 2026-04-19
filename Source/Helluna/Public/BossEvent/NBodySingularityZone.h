// Capstone Project Helluna

#pragma once

#include "CoreMinimal.h"
#include "BossEvent/BossPatternZoneBase.h"
#include "NBodySingularityZone.generated.h"

class UNiagaraSystem;
class UNiagaraComponent;
class AHellunaHeroCharacter;

/**
 * ANBodySingularityZone
 *
 * N-Body 특이점 패턴 Zone Actor.
 *
 * ═══════════════════════════════════════════════════════════
 * 개요:
 *   보스가 공간에 여러 개의 소형 블랙홀(특이점)을 소환한다.
 *   각 블랙홀은 **서로에게** 뉴턴 중력을 행사하며, 동시에
 *   플레이어도 끌어당긴다. N-body 시뮬레이션(O(N²)).
 *   특이점들은 카오스적으로 춤추듯 궤도를 돌며,
 *   가끔 서로 가까워져 **중력 합쳐짐(Merger)**이 발생 → 대폭발.
 *
 * 핵심 알고리즘:
 *   1. N-Body Gravitation (O(N²))
 *      - 각 특이점 쌍 (i, j)마다:
 *        F_ij = G * m_i * m_j / r² * (p_j - p_i) / r
 *      - Verlet / semi-implicit Euler 적분
 *      - 수치 안정화: 소프트닝 (r² + eps²)
 *      - 포텐셜 발산 방지: MaxAcceleration 클램프
 *
 *   2. 플레이어 흡인
 *      - 각 특이점이 플레이어에 개별 중력 적용
 *      - 총합 가속도 = Σ F_i / distance²
 *      - 플레이어 이동에 LaunchCharacter로 가속 반영
 *
 *   3. 머저 (Merger) 이벤트
 *      - 두 특이점이 MergerDistance 이내로 접근하면
 *      - 탄성 충돌 / 합체 선택 (합체 시 질량 합산, 대폭발 VFX)
 *      - 남은 특이점의 궤도가 더 카오스적이 됨
 *
 *   4. 궤도 경계 반사
 *      - 특이점이 MaxOrbitRadius를 벗어나면 중심 방향으로 속도 반전
 *      - 무한 탈출 방지 (boundary reflection)
 *
 * 시각적 특징:
 *   - 3~5개의 블랙홀이 서로를 중심으로 나선형으로 춤춘다
 *   - 예측 불가능한 카오스 궤도 (진정한 n-body)
 *   - 머저 시 거대 폭발 → 극적인 순간
 *
 * 기술 플렉스:
 *   - 실제 뉴턴 중력 방정식 (F = Gm₁m₂/r²)
 *   - Semi-implicit Euler 수치 적분
 *   - O(N²) 쌍별 상호작용
 *   - 충돌 감지 + 합체 동역학
 * ═══════════════════════════════════════════════════════════
 */
UCLASS(Blueprintable)
class HELLUNA_API ANBodySingularityZone : public ABossPatternZoneBase
{
	GENERATED_BODY()

public:
	ANBodySingularityZone();

	virtual void ActivateZone() override;
	virtual void DeactivateZone() override;

	// =========================================================
	// 특이점 설정
	// =========================================================

	/** 초기 특이점 수 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "N-Body|초기",
		meta = (DisplayName = "특이점 수", ClampMin = "2", ClampMax = "6"))
	int32 SingularityCount = 4;

	/** 특이점 질량 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "N-Body|초기",
		meta = (DisplayName = "질량", ClampMin = "1.0", ClampMax = "1000.0"))
	float Mass = 100.f;

	/** 초기 배치 반지름 (cm) — 보스 중심에서 이 거리에 원형 배치 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "N-Body|초기",
		meta = (DisplayName = "초기 반지름 (cm)", ClampMin = "500.0", ClampMax = "3000.0"))
	float InitialRadius = 1200.f;

	/** 초기 접선 속도 (cm/s) — 안정 궤도를 위한 접선 방향 속도 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "N-Body|초기",
		meta = (DisplayName = "초기 접선 속도", ClampMin = "0.0", ClampMax = "2000.0"))
	float InitialTangentialSpeed = 500.f;

	// =========================================================
	// 물리 설정
	// =========================================================

	/** 중력 상수 (튜닝용 — 실제 G 아님) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "N-Body|물리",
		meta = (DisplayName = "중력 상수", ClampMin = "1.0", ClampMax = "1000000.0"))
	float GravityConstant = 50000.f;

	/** 소프트닝 파라미터 (cm) — r² + eps² 로 발산 방지 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "N-Body|물리",
		meta = (DisplayName = "소프트닝 (cm)", ClampMin = "10.0", ClampMax = "500.0"))
	float Softening = 100.f;

	/** 최대 가속도 클램프 (cm/s²) — 수치 안정화 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "N-Body|물리",
		meta = (DisplayName = "최대 가속도", ClampMin = "100.0", ClampMax = "50000.0"))
	float MaxAcceleration = 5000.f;

	/** 궤도 경계 반지름 (cm) — 이 거리 벗어나면 반사 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "N-Body|물리",
		meta = (DisplayName = "궤도 경계 (cm)", ClampMin = "1000.0", ClampMax = "6000.0"))
	float MaxOrbitRadius = 3000.f;

	// =========================================================
	// 머저 설정
	// =========================================================

	/** 머저 발생 거리 (cm) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "N-Body|머저",
		meta = (DisplayName = "머저 거리 (cm)", ClampMin = "50.0", ClampMax = "500.0"))
	float MergerDistance = 150.f;

	/** 머저 시 대폭발 데미지 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "N-Body|머저",
		meta = (DisplayName = "머저 폭발 데미지", ClampMin = "0.0"))
	float MergerExplosionDamage = 1.f;

	/** 머저 폭발 반지름 (cm) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "N-Body|머저",
		meta = (DisplayName = "머저 폭발 반지름", ClampMin = "100.0", ClampMax = "2000.0"))
	float MergerExplosionRadius = 600.f;

	// =========================================================
	// 플레이어 상호작용
	// =========================================================

	/** 플레이어에게 적용되는 중력 배율 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "N-Body|플레이어",
		meta = (DisplayName = "플레이어 중력 배율", ClampMin = "0.0", ClampMax = "10.0"))
	float PlayerPullMultiplier = 0.3f;

	/** 특이점 접촉 데미지 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "N-Body|플레이어",
		meta = (DisplayName = "접촉 데미지", ClampMin = "0.0"))
	float ContactDamage = 1.f;

	/** 특이점 접촉 반지름 (cm) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "N-Body|플레이어",
		meta = (DisplayName = "접촉 반지름 (cm)", ClampMin = "30.0", ClampMax = "300.0"))
	float ContactRadius = 120.f;

	/** 접촉 데미지 쿨다운 (초) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "N-Body|플레이어",
		meta = (DisplayName = "접촉 쿨다운 (초)", ClampMin = "0.2", ClampMax = "3.0"))
	float ContactCooldown = 0.5f;

	// =========================================================
	// VFX
	// =========================================================

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "N-Body|VFX",
		meta = (DisplayName = "특이점 VFX"))
	TObjectPtr<UNiagaraSystem> SingularityVFX = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "N-Body|VFX",
		meta = (DisplayName = "특이점 VFX 스케일", ClampMin = "0.01", ClampMax = "10.0"))
	float SingularityVFXScale = 1.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "N-Body|VFX",
		meta = (DisplayName = "머저 폭발 VFX"))
	TObjectPtr<UNiagaraSystem> MergerExplosionVFX = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "N-Body|VFX",
		meta = (DisplayName = "머저 폭발 VFX 스케일", ClampMin = "0.01", ClampMax = "10.0"))
	float MergerExplosionVFXScale = 1.f;

protected:
	virtual void Tick(float DeltaTime) override;

private:
	struct FSingularity
	{
		FVector Position = FVector::ZeroVector;
		FVector Velocity = FVector::ZeroVector;
		FVector Acceleration = FVector::ZeroVector;
		float Mass = 100.f;
		bool bAlive = true;
		TWeakObjectPtr<UNiagaraComponent> VFXComp;
	};

	TArray<FSingularity> Singularities;

	bool bZoneActive = false;

	// 접촉 쿨다운 per player
	TMap<TWeakObjectPtr<AActor>, double> LastContactTime;

	FTimerHandle PatternEndTimerHandle;

	void InitializeSingularities();
	void ComputeForces();
	void IntegrateMotion(float DeltaTime);
	void HandleMergers();
	void EnforceBoundaryReflection();
	void ApplyGravityToPlayers(float DeltaTime);
	void ProcessContactDamage();
	void UpdateVFXPositions();
	void SpawnMergerExplosion(const FVector& Location);
};
