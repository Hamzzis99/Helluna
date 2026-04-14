// Capstone Project Helluna

#pragma once

#include "CoreMinimal.h"
#include "BossEvent/BossPatternZoneBase.h"
#include "PhantomBladesZone.generated.h"

class UNiagaraSystem;
class UPostProcessComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class AHellunaHeroCharacter;
class APhantomBladeActor;

/**
 * APhantomBladesZone
 *
 * 환영 검무 패턴 Zone Actor.
 *
 * ═══════════════════════════════════════════════════════════
 * 개요:
 *   보스가 팬텀 검(에너지 구체)을 소환한다.
 *   검들이 리사주(Lissajous) 곡선을 따라 수학적으로 아름다운
 *   궤적을 그리며 비행하고, 지나간 자리에 잔상(트레일)이
 *   일정 시간 남아 데미지를 준다.
 *
 * 핵심 메카닉:
 *   1. 리사주 궤적 (Lissajous Trajectories)
 *      - x = A*sin(a*t + delta), y = B*sin(b*t)
 *      - 주파수비(a:b)에 따라 완전히 다른 패턴
 *      - 여러 검이 서로 다른 위상(delta)으로 비행
 *      - Phase 전환 시 주파수비 변경 → 궤적 변화
 *
 *   2. 잔상 트레일 (Phantom Trail)
 *      - 검이 지나간 위치를 배열에 저장
 *      - 트레일 위에 있으면 데미지
 *      - 시간이 지나면 트레일 소멸
 *      - 갈수록 트레일이 촘촘해짐 → 안전 공간 감소
 *
 *   3. 검 직격 (Direct Hit)
 *      - 검 자체에 닿으면 큰 데미지
 *      - 트레일보다 훨씬 높은 데미지 → 궤적 예측이 중요
 *
 * 기술적 특징:
 *   - Lissajous 곡선: 파라메트릭 방정식 기반
 *   - 트레일: 고정 크기 환형 버퍼 (성능)
 *   - 거리 기반 판정 (물리 없음)
 *
 * VFX 매칭:
 *   - BladeVFX: 구체 + 마법진 (검/에너지 구체)
 *   - TrailVFX: 잔상 이펙트 (마법진 / 쉴드 이펙트)
 *   - PhaseChangeVFX: 궤적 변화 시 이펙트
 * ═══════════════════════════════════════════════════════════
 */
UCLASS(Blueprintable)
class HELLUNA_API APhantomBladesZone : public ABossPatternZoneBase
{
	GENERATED_BODY()

public:
	APhantomBladesZone();

	virtual void ActivateZone() override;
	virtual void DeactivateZone() override;

	// =========================================================
	// 검 설정
	// =========================================================

	/** 스폰할 검 액터 BP 클래스 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "환영|검",
		meta = (DisplayName = "검 액터 클래스"))
	TSubclassOf<APhantomBladeActor> BladeActorClass;

	/** 팬텀 검 수 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "환영|검",
		meta = (DisplayName = "검 수", ClampMin = "1", ClampMax = "4"))
	int32 BladeCount = 2;

	/** 검 직격 데미지 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "환영|검",
		meta = (DisplayName = "직격 데미지", ClampMin = "0.0"))
	float BladeDirectDamage = 60.f;

	/** 검 피격 반지름 (cm) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "환영|검",
		meta = (DisplayName = "피격 반지름 (cm)", ClampMin = "50.0", ClampMax = "300.0"))
	float BladeHitRadius = 120.f;

	/** 검 피격 쿨타임 (초) — 같은 검에 연속 피격 방지 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "환영|검",
		meta = (DisplayName = "피격 쿨타임 (초)", ClampMin = "0.3", ClampMax = "3.0"))
	float BladeHitCooldown = 0.5f;

	// =========================================================
	// 궤적 설정
	// =========================================================

	/** 궤적의 X축 반지름 (cm) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "환영|궤적",
		meta = (DisplayName = "X 반지름 (cm)", ClampMin = "500.0", ClampMax = "3000.0"))
	float TrajectoryRadiusX = 1500.f;

	/** 궤적의 Y축 반지름 (cm) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "환영|궤적",
		meta = (DisplayName = "Y 반지름 (cm)", ClampMin = "500.0", ClampMax = "3000.0"))
	float TrajectoryRadiusY = 1200.f;

	/** 기본 궤적 속도 (radian/초) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "환영|궤적",
		meta = (DisplayName = "기본 속도", ClampMin = "0.5", ClampMax = "5.0"))
	float BaseSpeed = 1.5f;

	/** 검 비행 높이 (cm) — 지면에서 검까지 높이 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "환영|궤적",
		meta = (DisplayName = "비행 높이 (cm)", ClampMin = "50.0", ClampMax = "300.0"))
	float BladeHeight = 100.f;

	// =========================================================
	// 페이즈 설정 (주파수비 변화)
	// =========================================================

	/** Phase 1 지속 시간 (초) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "환영|페이즈",
		meta = (DisplayName = "Phase 1 시간 (초)", ClampMin = "2.0", ClampMax = "15.0"))
	float Phase1Duration = 5.0f;

	/** Phase 2 지속 시간 (초) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "환영|페이즈",
		meta = (DisplayName = "Phase 2 시간 (초)", ClampMin = "2.0", ClampMax = "15.0"))
	float Phase2Duration = 5.0f;

	// Phase 3는 패턴 종료까지

	/**
	 * Phase 1 주파수비 (X:Y).
	 * 1:2 = 8자형, 3:2 = 매듭형, 3:4 = 복잡한 루프.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "환영|페이즈",
		meta = (DisplayName = "Phase 1 X 주파수", ClampMin = "1", ClampMax = "5"))
	int32 Phase1FreqX = 1;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "환영|페이즈",
		meta = (DisplayName = "Phase 1 Y 주파수", ClampMin = "1", ClampMax = "5"))
	int32 Phase1FreqY = 2;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "환영|페이즈",
		meta = (DisplayName = "Phase 2 X 주파수", ClampMin = "1", ClampMax = "5"))
	int32 Phase2FreqX = 3;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "환영|페이즈",
		meta = (DisplayName = "Phase 2 Y 주파수", ClampMin = "1", ClampMax = "5"))
	int32 Phase2FreqY = 2;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "환영|페이즈",
		meta = (DisplayName = "Phase 3 X 주파수", ClampMin = "1", ClampMax = "5"))
	int32 Phase3FreqX = 3;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "환영|페이즈",
		meta = (DisplayName = "Phase 3 Y 주파수", ClampMin = "1", ClampMax = "5"))
	int32 Phase3FreqY = 4;

	/** 페이즈마다 속도 증가 배율 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "환영|페이즈",
		meta = (DisplayName = "속도 증가 배율", ClampMin = "1.0", ClampMax = "2.0"))
	float SpeedEscalation = 1.2f;

	// =========================================================
	// 트레일 설정
	// =========================================================

	/** 트레일 데미지 (접촉 시 1회) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "환영|트레일",
		meta = (DisplayName = "트레일 데미지", ClampMin = "0.0"))
	float TrailDamage = 20.f;

	/** 트레일 판정 반지름 (cm) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "환영|트레일",
		meta = (DisplayName = "트레일 반지름 (cm)", ClampMin = "30.0", ClampMax = "200.0"))
	float TrailHitRadius = 80.f;

	/** 트레일 유지 시간 (초) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "환영|트레일",
		meta = (DisplayName = "트레일 유지 시간 (초)", ClampMin = "1.0", ClampMax = "10.0"))
	float TrailLifetime = 4.0f;

	/** 트레일 생성 간격 (초) — 이 간격마다 검 위치에 트레일 노드 생성 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "환영|트레일",
		meta = (DisplayName = "트레일 간격 (초)", ClampMin = "0.05", ClampMax = "0.5"))
	float TrailSpawnInterval = 0.15f;

	/** 최대 트레일 노드 수 (성능) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "환영|트레일",
		meta = (DisplayName = "최대 트레일 수", ClampMin = "50", ClampMax = "500"))
	int32 MaxTrailNodes = 200;

	/** 트레일 피격 쿨타임 (초) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "환영|트레일",
		meta = (DisplayName = "트레일 피격 쿨타임 (초)", ClampMin = "0.3", ClampMax = "2.0"))
	float TrailHitCooldown = 0.5f;

	// =========================================================
	// VFX / 사운드
	// =========================================================

	/** 트레일 VFX */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "환영|VFX",
		meta = (DisplayName = "트레일 VFX"))
	TObjectPtr<UNiagaraSystem> TrailVFX = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "환영|VFX",
		meta = (DisplayName = "트레일 VFX 크기", ClampMin = "0.01", ClampMax = "10.0"))
	float TrailVFXScale = 0.5f;

	/** 페이즈 전환 VFX */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "환영|VFX",
		meta = (DisplayName = "페이즈 전환 VFX"))
	TObjectPtr<UNiagaraSystem> PhaseChangeVFX = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "환영|VFX",
		meta = (DisplayName = "페이즈 전환 VFX 크기", ClampMin = "0.01", ClampMax = "10.0"))
	float PhaseChangeVFXScale = 1.f;

	/** 검 비행 사운드 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "환영|사운드",
		meta = (DisplayName = "비행 사운드"))
	TObjectPtr<USoundBase> BladeSound = nullptr;

	/** 페이즈 전환 사운드 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "환영|사운드",
		meta = (DisplayName = "전환 사운드"))
	TObjectPtr<USoundBase> PhaseChangeSound = nullptr;

	// =========================================================
	// 공간 왜곡 (포스트프로세스)
	// =========================================================

	/**
	 * 공간 왜곡 PP 머티리얼.
	 * 필요한 파라미터:
	 *  - Vector  PhantomCenter  (검 중심의 월드 좌표, W=1 활성)
	 *  - Scalar  DistortionRadius (영향 반경 cm)
	 *  - Scalar  DistortionStrength (0~1)
	 *  - Scalar  ElapsedTime (애니메이션용)
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "환영|포스트프로세스",
		meta = (DisplayName = "공간 왜곡 PP 머티리얼"))
	TObjectPtr<UMaterialInterface> DistortionPPMaterial = nullptr;

	/** 공간 왜곡 영향 반경 (cm) — 검 주변 왜곡 범위 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "환영|포스트프로세스",
		meta = (DisplayName = "왜곡 반경 (cm)", ClampMin = "100.0", ClampMax = "5000.0"))
	float DistortionRadius = 800.f;

	/** 공간 왜곡 강도 (0=원본, 1=최대 왜곡) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "환영|포스트프로세스",
		meta = (DisplayName = "왜곡 강도", ClampMin = "0.0", ClampMax = "1.0"))
	float DistortionStrength = 0.75f;

	/** 입력 왜곡 반경 (cm) — 이 안에 들어오면 플레이어 입력에 노이즈 적용 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "환영|포스트프로세스",
		meta = (DisplayName = "입력 왜곡 반경 (cm)", ClampMin = "0.0", ClampMax = "3000.0"))
	float InputDistortionRadius = 500.f;

	/** 입력 왜곡 최대 회전 (도/초) — 검에 가까울수록 이 값이 커짐 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "환영|포스트프로세스",
		meta = (DisplayName = "입력 왜곡 회전 (도/초)", ClampMin = "0.0", ClampMax = "120.0"))
	float InputDistortionYawRate = 45.f;

protected:
	virtual void Tick(float DeltaTime) override;

private:
	// =========================================================
	// 런타임 구조체
	// =========================================================

	struct FBladeState
	{
		float PhaseDelta;    // 리사주 위상 오프셋 (라디안)
		float TimeParam;     // 현재 시간 파라미터
		FVector CurrentPosition;
		TMap<TWeakObjectPtr<AActor>, double> LastHitTime; // 직격 쿨타임
		TWeakObjectPtr<APhantomBladeActor> BladeActor;   // 검 VFX 액터
	};

	struct FTrailNode
	{
		FVector Position;
		double SpawnWorldTime;
	};

	TArray<FBladeState> Blades;
	TArray<FTrailNode> TrailNodes;

	float ElapsedTime = 0.f;
	int32 CurrentPhase = 0;
	int32 CurrentFreqX = 1;
	int32 CurrentFreqY = 2;
	float CurrentSpeed = 1.f;
	float TrailSpawnAccumulator = 0.f;
	float DistortionElapsedTime = 0.f;
	bool bZoneActive = false;

	// 트레일 피격 쿨타임
	TMap<TWeakObjectPtr<AActor>, double> TrailDamageLastTime;

	FTimerHandle PatternEndTimerHandle;

	// =========================================================
	// 내부 함수
	// =========================================================

	/** 리사주 곡선 위치 계산 */
	FVector CalculateLissajousPosition(float T, float PhaseDelta) const;

	/** 페이즈 전환 체크 */
	void UpdatePhase();

	/** 검 위치 업데이트 */
	void UpdateBlades(float DeltaTime);

	/** 검 직격 판정 */
	void ProcessBladeHits();

	/** 트레일 생성 */
	void SpawnTrailNodes();

	/** 트레일 판정 + 소멸 */
	void ProcessTrails();

	/** 패턴 종료 */
	void OnPatternTimeUp();

	// =========================================================
	// 공간 왜곡 런타임
	// =========================================================

	UPROPERTY(VisibleAnywhere, Category = "환영")
	TObjectPtr<USceneComponent> SceneRoot = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "환영")
	TObjectPtr<UPostProcessComponent> PostProcessComp = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> DistortionMID = nullptr;

	/** 모든 클라이언트에서 공간 왜곡 PP 활성화 */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_ActivateDistortion();

	UFUNCTION(NetMulticast, Reliable)
	void Multicast_DeactivateDistortion();

	/** 검 위치 변경 시 PP 머티리얼 파라미터 업데이트 (모든 클라이언트) */
	void UpdateDistortionMaterial(float DeltaTime);

	/** 입력 왜곡 — 서버 권한에서 플레이어 시점 회전 적용 */
	void ApplyInputDistortion(float DeltaTime);
};
