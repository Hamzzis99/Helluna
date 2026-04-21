// Capstone Project Helluna

#pragma once

#include "CoreMinimal.h"
#include "BossEvent/BossPatternZoneBase.h"
#include "ConstellationZone.generated.h"

class UNiagaraSystem;
class AHellunaHeroCharacter;

/**
 * AConstellationZone
 *
 * 성좌의 심판 패턴 Zone Actor.
 *
 * ═══════════════════════════════════════════════════════════
 * 개요:
 *   하늘에 마법진이 그려지고, 별자리 형태로 에너지 구체(노드)가 배치.
 *   구체가 순차 활성화되면서 아래로 레이저를 쏘고,
 *   활성화된 구체 사이에 에너지 선이 생겨 데미지.
 *   모든 구체가 활성화되면 별자리 영역 내 대규모 폭발.
 *
 * 핵심 메카닉:
 *   1. 에너지 구체 (Constellation Nodes)
 *      - 원형 배치 + 랜덤 오프셋으로 별자리 형태
 *      - 순차 활성화 — 활성화 시 아래로 데미지 기둥
 *      - 활성화된 노드 근처는 지속 데미지 영역
 *
 *   2. 연결선 (Constellation Lines)
 *      - 활성화된 인접 노드 사이에 에너지 선 생성
 *      - 선분-점 거리 판정으로 선 위 데미지
 *      - 선이 늘어날수록 안전 지대 감소
 *
 *   3. 최종 폭발 (Constellation Complete)
 *      - 모든 노드 활성화 시 별자리 영역 확정
 *      - 볼록 껍질(Convex Hull) 내부에 대규모 폭발
 *      - 영역 밖으로 탈출해야 살 수 있음
 *
 * VFX 매칭:
 *   - SkyMagicCircleVFX: 하늘 마법진 (패턴 시작 시)
 *   - NodeVFX: 구체 + 마법진 (각 노드)
 *   - NodeActivateVFX: 노드 활성화 이펙트
 *   - LineVFX: 에너지 연결선
 *   - FinalExplosionVFX: 별자리 완성 폭발
 *
 * 성능:
 *   - 노드 수 5~8개로 제한 (O(N) 순회)
 *   - 선분-점 거리는 순수 수학 (물리 없음)
 *   - Point-in-Polygon은 Ray casting (O(N))
 *   - Tick에서는 활성 노드/라인만 판정
 * ═══════════════════════════════════════════════════════════
 */
UCLASS(Blueprintable)
class HELLUNA_API AConstellationZone : public ABossPatternZoneBase
{
	GENERATED_BODY()

public:
	AConstellationZone();

	virtual void ActivateZone() override;
	virtual void DeactivateZone() override;

	// =========================================================
	// 노드 배치 설정
	// =========================================================

	/** 별자리 노드 수 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "성좌|노드",
		meta = (DisplayName = "노드 수", ClampMin = "4", ClampMax = "8"))
	int32 NodeCount = 6;

	/** 노드 배치 반지름 (cm) — 보스 중심에서의 거리 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "성좌|노드",
		meta = (DisplayName = "배치 반지름 (cm)", ClampMin = "500.0", ClampMax = "4000.0"))
	float PlacementRadius = 1800.f;

	/** 배치 랜덤 오프셋 (cm) — 완벽한 원형이 아닌 자연스러운 형태 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "성좌|노드",
		meta = (DisplayName = "배치 오프셋 (cm)", ClampMin = "0.0", ClampMax = "500.0"))
	float PlacementRandomOffset = 200.f;

	/** 하늘 노드 높이 (cm) — 지면에서 얼마 위에 구체가 떠있는지 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "성좌|노드",
		meta = (DisplayName = "노드 높이 (cm)", ClampMin = "500.0", ClampMax = "3000.0"))
	float NodeHeight = 1200.f;

	/** 노드 활성화 간격 (초) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "성좌|노드",
		meta = (DisplayName = "활성화 간격 (초)", ClampMin = "0.5", ClampMax = "5.0"))
	float NodeActivateInterval = 2.0f;

	// =========================================================
	// 데미지 설정
	// =========================================================

	/** 노드 활성화 시 아래로 내려오는 기둥 데미지 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "성좌|데미지",
		meta = (DisplayName = "기둥 데미지", ClampMin = "0.0"))
	float PillarDamage = 45.f;

	/** 기둥 데미지 반지름 (cm) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "성좌|데미지",
		meta = (DisplayName = "기둥 반지름 (cm)", ClampMin = "100.0", ClampMax = "500.0"))
	float PillarRadius = 200.f;

	/** 연결선 데미지 (접촉 시 / 초) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "성좌|데미지",
		meta = (DisplayName = "연결선 DPS", ClampMin = "0.0"))
	float LineDamagePerSecond = 20.f;

	/** 연결선 폭 (cm) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "성좌|데미지",
		meta = (DisplayName = "연결선 폭 (cm)", ClampMin = "50.0", ClampMax = "400.0"))
	float LineWidth = 150.f;

	/** 최종 폭발 데미지 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "성좌|데미지",
		meta = (DisplayName = "최종 폭발 데미지", ClampMin = "0.0"))
	float FinalExplosionDamage = 120.f;

	/** 최종 폭발 전 경고 시간 (초) — 이 시간 내에 영역 밖으로 탈출해야 함 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "성좌|데미지",
		meta = (DisplayName = "폭발 경고 시간 (초)", ClampMin = "1.0", ClampMax = "5.0"))
	float FinalWarningDuration = 2.5f;

	// =========================================================
	// VFX / 사운드
	// =========================================================

	/** 하늘 마법진 VFX (패턴 시작 시) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "성좌|VFX",
		meta = (DisplayName = "하늘 마법진 VFX"))
	TObjectPtr<UNiagaraSystem> SkyMagicCircleVFX = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "성좌|VFX",
		meta = (DisplayName = "하늘 마법진 VFX 크기", ClampMin = "0.01", ClampMax = "20.0"))
	float SkyMagicCircleVFXScale = 1.f;

	/** 노드 VFX (구체 + 마법진 — 비활성 상태) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "성좌|VFX",
		meta = (DisplayName = "노드 VFX"))
	TObjectPtr<UNiagaraSystem> NodeVFX = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "성좌|VFX",
		meta = (DisplayName = "노드 VFX 크기", ClampMin = "0.01", ClampMax = "10.0"))
	float NodeVFXScale = 1.f;

	/** 노드 활성화 VFX (활성화 순간 이펙트) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "성좌|VFX",
		meta = (DisplayName = "활성화 VFX"))
	TObjectPtr<UNiagaraSystem> NodeActivateVFX = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "성좌|VFX",
		meta = (DisplayName = "활성화 VFX 크기", ClampMin = "0.01", ClampMax = "10.0"))
	float NodeActivateVFXScale = 1.f;

	/** 연결선 VFX */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "성좌|VFX",
		meta = (DisplayName = "연결선 VFX"))
	TObjectPtr<UNiagaraSystem> LineVFX = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "성좌|VFX",
		meta = (DisplayName = "연결선 VFX 크기", ClampMin = "0.01", ClampMax = "10.0"))
	float LineVFXScale = 1.f;

	/** 최종 폭발 경고 VFX */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "성좌|VFX",
		meta = (DisplayName = "폭발 경고 VFX"))
	TObjectPtr<UNiagaraSystem> FinalWarningVFX = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "성좌|VFX",
		meta = (DisplayName = "폭발 경고 VFX 크기", ClampMin = "0.01", ClampMax = "10.0"))
	float FinalWarningVFXScale = 1.f;

	/** 최종 폭발 VFX (블랙홀 폭발 권장) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "성좌|VFX",
		meta = (DisplayName = "최종 폭발 VFX"))
	TObjectPtr<UNiagaraSystem> FinalExplosionVFX = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "성좌|VFX",
		meta = (DisplayName = "최종 폭발 VFX 크기", ClampMin = "0.01", ClampMax = "20.0"))
	float FinalExplosionVFXScale = 1.f;

	/** 노드 활성화 사운드 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "성좌|사운드",
		meta = (DisplayName = "활성화 사운드"))
	TObjectPtr<USoundBase> NodeActivateSound = nullptr;

	/** 최종 폭발 사운드 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "성좌|사운드",
		meta = (DisplayName = "폭발 사운드"))
	TObjectPtr<USoundBase> FinalExplosionSound = nullptr;

	/** 경고 사운드 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "성좌|사운드",
		meta = (DisplayName = "경고 사운드"))
	TObjectPtr<USoundBase> WarningSound = nullptr;

protected:
	virtual void Tick(float DeltaTime) override;

private:
	// =========================================================
	// 런타임 구조체
	// =========================================================

	struct FConstellationNode
	{
		FVector WorldPosition;        // 지면 기준 위치 (XY)
		FVector SkyPosition;          // 하늘 구체 위치
		bool bActivated = false;
		double ActivateTime = 0.0;
	};

	struct FConstellationLine
	{
		int32 NodeA;
		int32 NodeB;
		FVector2D PointA;
		FVector2D PointB;
	};

	TArray<FConstellationNode> Nodes;
	TArray<FConstellationLine> ActiveLines;

	int32 NextNodeToActivate = 0;
	bool bZoneActive = false;
	bool bFinalWarningStarted = false;
	bool bFinalExplosionDone = false;
	double FinalWarningStartTime = 0.0;

	FTimerHandle NodeActivateTimerHandle;

	// 연결선 DPS 쿨타임 (플레이어별)
	TMap<TWeakObjectPtr<AActor>, double> LineDamageLastTime;

	// =========================================================
	// 내부 함수
	// =========================================================

	/** 노드 위치 생성 (원형 + 랜덤 오프셋) */
	void GenerateNodePositions();

	/** 다음 노드 활성화 */
	void ActivateNextNode();

	/** 연결선 갱신 (새 노드 활성화 시) */
	void UpdateLines();

	/** 연결선 데미지 처리 */
	void ProcessLineDamage(float DeltaTime);

	/** 최종 폭발 시작 (경고) */
	void BeginFinalExplosion();

	/** 최종 폭발 실행 */
	void ExecuteFinalExplosion();

	/** 볼록 껍질(Convex Hull) 계산 — Andrew's monotone chain */
	TArray<FVector2D> ComputeConvexHull(const TArray<FVector2D>& Points) const;

	/** 점이 볼록 다각형 내부에 있는지 판정 — Ray casting */
	bool IsPointInConvexHull(const FVector2D& Point, const TArray<FVector2D>& Hull) const;

	/** 점과 선분 사이의 최소 거리 */
	static float PointToSegmentDistance2D(const FVector2D& Point, const FVector2D& A, const FVector2D& B);

	/** 범위 데미지 */
	void DealAreaDamage(const FVector& Location, float Damage, float Radius);
};
