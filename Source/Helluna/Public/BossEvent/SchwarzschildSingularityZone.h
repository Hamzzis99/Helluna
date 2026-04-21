// Capstone Project Helluna

#pragma once

#include "CoreMinimal.h"
#include "BossEvent/BossPatternZoneBase.h"
#include "SchwarzschildSingularityZone.generated.h"

class UNiagaraSystem;
class UNiagaraComponent;
class UStaticMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;
class URadialForceComponent;
class UPostProcessComponent;
class UPointLightComponent;
class UPrimitiveComponent;
class AController;
class UDamageType;

/**
 * ASchwarzschildSingularityZone
 *
 * ═══════════════════════════════════════════════════════════
 * 실시간 일반상대성이론 블랙홀 패턴 (Schwarzschild Metric).
 *
 * 개요:
 *   보스가 전장에 슈바르츠실트 블랙홀을 소환한다. 이 블랙홀은
 *   **실제 일반상대성이론 방정식**으로 계산된다.
 *
 * 핵심 물리:
 *   1. 슈바르츠실트 메트릭 (Schwarzschild 1916)
 *        ds² = -(1 - r_s/r)c²dt² + (1 - r_s/r)⁻¹dr² + r²dΩ²
 *      - r_s = 2GM/c²   (사건의 지평선 = 이벤트 호라이즌)
 *      - r_ph = 1.5 r_s (광자구 = 광자가 원형 궤도를 도는 반지름)
 *      - r_isco = 3 r_s (Innermost Stable Circular Orbit, 강착원반 안쪽 끝)
 *
 *   2. 광자 측지선 방정식 (적도면 equatorial plane):
 *        d²u/dφ² + u = (3/2) r_s u²    where u ≡ 1/r
 *      - 2차 비선형 ODE. 1차항은 뉴턴 중력, 2차항이 GR 보정.
 *      - **고전 RK4로 실시간 적분** → 빛이 휘는 경로를 진짜로 계산.
 *      - 결과: 광자구 위에 photon들이 불안정 원형 궤도를 돌고,
 *        영향권 밖으로 발사된 광자들은 실제로 곡선을 그리며 휜다.
 *
 *   3. 케플러 강착원반 (Accretion Disk)
 *      - r_isco ~ r_out 사이 입자들이 ω(r) = √(GM/r³) 각속도로 궤도 운동.
 *      - **도플러 편이**: 관측자 기준 접근측(blueshift) vs 후퇴측(redshift)
 *        → Niagara user param으로 입자 색을 동적으로 변경.
 *      - **중력 적색편이**: r이 r_s에 가까워질수록 파장 증가 (1-r_s/r)^(-1/2)
 *
 *   4. 플레이어 중력 (GR 보정 포함)
 *      - a(r) = GM/r² × (1 + 3 r_s/r)   // 선행 GR 보정
 *      - 뉴턴보다 강한 흡인력. ISCO 안쪽에서 급격히 발산.
 *      - r < r_ph: 조석력 DoT (1/r³ 스케일)
 *      - r < r_s : 사건의 지평선 돌파 — 즉사.
 *
 * 페이즈:
 *   Phase 1 (Formation): r_s가 0에서 목표값으로 성장. 시공간이 접혀온다.
 *   Phase 2 (Stable)   : 완전한 블랙홀. 광자구·강착원반 가동.
 *   Phase 3 (Evaporation): 호킹 복사 폭발. 마지막 순간 r_s가 0으로 수축 후 플래시.
 *
 * 기술 플렉스:
 *   - Einstein 1915 일반상대성이론 방정식의 해 (Schwarzschild 1916)
 *   - 실시간 RK4 측지선 적분 (진짜로 빛이 휨)
 *   - Kepler 궤도역학 + 상대론적 도플러 편이
 *   - 광자구의 불안정 주기 궤도 (photon sphere chaotic escape)
 * ═══════════════════════════════════════════════════════════
 */
UCLASS(Blueprintable)
class HELLUNA_API ASchwarzschildSingularityZone : public ABossPatternZoneBase
{
	GENERATED_BODY()

public:
	ASchwarzschildSingularityZone();

	virtual void ActivateZone() override;
	virtual void DeactivateZone() override;

	// =========================================================
	// 메트릭 (Schwarzschild)
	// =========================================================

	/** 사건의 지평선 반지름 r_s (cm). 게임용 단위 — 실제 물리 단위 아님. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Schwarzschild|메트릭",
		meta = (DisplayName = "사건 지평선 r_s (cm)", ClampMin = "50.0", ClampMax = "2000.0"))
	float SchwarzschildRadius = 300.f;

	/** GM 파라미터 — 플레이어 가속도 계산용 (튜닝). 물리적으로는 GM = r_s c² / 2. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Schwarzschild|메트릭",
		meta = (DisplayName = "GM 계수", ClampMin = "1000.0", ClampMax = "100000000.0"))
	float GravitationalParameter = 2000000.f;

	// =========================================================
	// 광자 (Photon Sphere)
	// =========================================================

	/** 광자구 광자 수 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Schwarzschild|광자",
		meta = (DisplayName = "광자 수", ClampMin = "4", ClampMax = "256"))
	int32 PhotonCount = 64;

	/** RK4 적분 스텝 수 (per tick, per photon) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Schwarzschild|광자",
		meta = (DisplayName = "RK4 Substeps", ClampMin = "1", ClampMax = "32"))
	int32 RK4Substeps = 8;

	/** φ 적분 속도 (rad/s) — 광자가 궤도를 도는 각속도 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Schwarzschild|광자",
		meta = (DisplayName = "광자 각속도 (rad/s)", ClampMin = "0.1", ClampMax = "20.0"))
	float PhotonPhiRate = 3.0f;

	/** 광자 초기 u = 1/r 섭동 스케일. 광자구 불안정성 때문에 시간이 지나면 탈출 or 낙하. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Schwarzschild|광자",
		meta = (DisplayName = "초기 섭동", ClampMin = "0.0", ClampMax = "0.01"))
	float PhotonInitialPerturbation = 0.0005f;

	// =========================================================
	// 강착원반 (Accretion Disk)
	// =========================================================

	/** 강착원반 입자 수 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Schwarzschild|강착원반",
		meta = (DisplayName = "원반 입자 수", ClampMin = "0", ClampMax = "256"))
	int32 DiskParticleCount = 64;

	/** 원반 바깥 반지름 (r_s 배율) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Schwarzschild|강착원반",
		meta = (DisplayName = "원반 바깥 r/r_s", ClampMin = "3.0", ClampMax = "20.0"))
	float DiskOuterRadiusRs = 8.f;

	/** 원반 두께 (cm) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Schwarzschild|강착원반",
		meta = (DisplayName = "원반 두께 (cm)", ClampMin = "0.0", ClampMax = "500.0"))
	float DiskThickness = 80.f;

	// =========================================================
	// 데미지 / 상호작용
	// =========================================================

	/** 사건 지평선 돌파 데미지 (즉사급) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Schwarzschild|데미지",
		meta = (DisplayName = "지평선 즉사 데미지", ClampMin = "0.0"))
	float HorizonKillDamage = 99999.f;

	/** 광자구 안쪽 조석력 DoT (per second, r=r_s 기준) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Schwarzschild|데미지",
		meta = (DisplayName = "조석력 DoT", ClampMin = "0.0"))
	float TidalDamagePerSecond = 40.f;

	/** 플레이어 중력 배율 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Schwarzschild|데미지",
		meta = (DisplayName = "플레이어 중력 배율", ClampMin = "0.0", ClampMax = "10.0"))
	float PlayerPullMultiplier = 1.0f;

	// =========================================================
	// 페이즈 타이밍 (비율 — PatternDuration 에 대한 비율)
	// =========================================================

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Schwarzschild|페이즈",
		meta = (DisplayName = "Formation 비율", ClampMin = "0.0", ClampMax = "1.0"))
	float FormationPhaseRatio = 0.15f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Schwarzschild|페이즈",
		meta = (DisplayName = "Evaporation 비율", ClampMin = "0.0", ClampMax = "1.0"))
	float EvaporationPhaseRatio = 0.1f;

	// =========================================================
	// VFX
	// =========================================================

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Schwarzschild|VFX",
		meta = (DisplayName = "광자 VFX"))
	TObjectPtr<UNiagaraSystem> PhotonVFX = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Schwarzschild|VFX",
		meta = (DisplayName = "광자 VFX 스케일", ClampMin = "0.01", ClampMax = "10.0"))
	float PhotonVFXScale = 0.5f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Schwarzschild|VFX",
		meta = (DisplayName = "강착원반 입자 VFX"))
	TObjectPtr<UNiagaraSystem> DiskParticleVFX = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Schwarzschild|VFX",
		meta = (DisplayName = "원반 입자 VFX 스케일", ClampMin = "0.01", ClampMax = "10.0"))
	float DiskParticleVFXScale = 0.4f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Schwarzschild|VFX",
		meta = (DisplayName = "사건 지평선 VFX (루프)"))
	TObjectPtr<UNiagaraSystem> HorizonVFX = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Schwarzschild|VFX",
		meta = (DisplayName = "Evaporation 플래시 VFX"))
	TObjectPtr<UNiagaraSystem> EvaporationFlashVFX = nullptr;

	/** 블랙홀 본체 메시 (검은 구) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Schwarzschild|VFX",
		meta = (DisplayName = "지평선 메시"))
	TObjectPtr<UStaticMeshComponent> HorizonMesh = nullptr;

	/** 렌즈링 포스트프로세스 머티리얼 (선택) — 스칼라 파라미터로 r_s 전달 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Schwarzschild|VFX",
		meta = (DisplayName = "렌즈링 PostProcess Material"))
	TObjectPtr<UMaterialInterface> LensingMaterial = nullptr;

	/** 지평선 메시에 적용할 검은 언릿 머티리얼 (선택) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Schwarzschild|VFX",
		meta = (DisplayName = "지평선 블랙 머티리얼"))
	TObjectPtr<UMaterialInterface> HorizonBlackMaterial = nullptr;

	// =========================================================
	// 고급 상호작용 플래그
	// =========================================================

	/** 플레이어에 실제 슈바르츠실트 시간 지연 √(1-r_s/r) 적용 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Schwarzschild|상호작용",
		meta = (DisplayName = "시간 지연 적용"))
	bool bApplyTimeDilation = true;

	/** 플레이어에 색수차/비네트 포스트프로세스 적용 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Schwarzschild|상호작용",
		meta = (DisplayName = "포스트프로세스 적용"))
	bool bApplyPostProcess = true;

	/** 물리 시뮬레이션 액터에 방사형 중력 적용 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Schwarzschild|상호작용",
		meta = (DisplayName = "물리 액터 인력"))
	bool bApplyPhysicsForce = true;

	/** 중력 새총 부스트 배율 (접선 속도 × 이 값만큼 부스트) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Schwarzschild|상호작용",
		meta = (DisplayName = "중력 새총 배율", ClampMin = "0.0", ClampMax = "5.0"))
	float SlingshotBoost = 1.5f;

	/** 중력 새총 트리거 최소 속도 (cm/s) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Schwarzschild|상호작용",
		meta = (DisplayName = "새총 최소 속도", ClampMin = "100.0", ClampMax = "5000.0"))
	float SlingshotMinSpeed = 800.f;

	/** 조석 분리 스케일링 (광자구 내부 물리액터 늘리기) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Schwarzschild|상호작용",
		meta = (DisplayName = "조석 분리 강도", ClampMin = "0.0", ClampMax = "10.0"))
	float TidalDisruptionStrength = 2.f;

	/** SceneCapture 해상도 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Schwarzschild|상호작용",
		meta = (DisplayName = "SceneCapture 해상도", ClampMin = "64", ClampMax = "1024"))
	int32 CaptureResolution = 256;

	// =========================================================
	// 런타임 컴포넌트
	// =========================================================

	UPROPERTY()
	TObjectPtr<USceneCaptureComponent2D> SceneCapture = nullptr;

	UPROPERTY()
	TObjectPtr<URadialForceComponent> RadialForce = nullptr;

	UPROPERTY()
	TObjectPtr<UPostProcessComponent> PostProcess = nullptr;

	UPROPERTY()
	TObjectPtr<UPointLightComponent> AccretionLight = nullptr;

	UPROPERTY()
	TObjectPtr<UTextureRenderTarget2D> CaptureRenderTarget = nullptr;

protected:
	virtual void Tick(float DeltaTime) override;

private:
	// =========================================================
	// 페이즈 상태
	// =========================================================
	enum class EPhase : uint8 { Formation, Stable, Evaporation, Done };
	EPhase CurrentPhase = EPhase::Formation;
	double PatternElapsed = 0.0;

	// 현재 유효 r_s (Formation 동안 0→SchwarzschildRadius 성장, Evaporation 동안 수축)
	double EffectiveRs = 0.0;

	// =========================================================
	// 광자 (Photon Sphere Test Particles)
	// =========================================================
	struct FPhoton
	{
		// 측지선 상태: (u = 1/r, du/dφ, φ)
		double U = 0.0;
		double DU = 0.0;
		double Phi = 0.0;
		// 적도면 각도 오프셋 (구 전체에 분포시키기 위함)
		double InclinationAxis = 0.0;
		FVector CurrentPosition = FVector::ZeroVector;
		TWeakObjectPtr<UNiagaraComponent> VFXComp;
	};
	TArray<FPhoton> Photons;

	// =========================================================
	// 강착원반 입자 (Kepler Orbits)
	// =========================================================
	struct FDiskParticle
	{
		double R = 0.0;            // 반지름 (cm)
		double Phi = 0.0;          // 궤도 각도
		double Omega = 0.0;        // 각속도 (케플러)
		double ZOffset = 0.0;      // 원반 두께 내 z 오프셋
		FVector CurrentPosition = FVector::ZeroVector;
		TWeakObjectPtr<UNiagaraComponent> VFXComp;
	};
	TArray<FDiskParticle> DiskParticles;

	// 호라이즌 VFX 핸들
	TWeakObjectPtr<UNiagaraComponent> HorizonVFXComp;

	// 포스트프로세스 MID
	TObjectPtr<UMaterialInstanceDynamic> LensingMID = nullptr;

	// 플레이어 DoT 쿨다운
	TMap<TWeakObjectPtr<AActor>, double> LastTidalTick;

	bool bZoneActive = false;
	FTimerHandle PatternEndTimerHandle;

	// =========================================================
	// 내부 로직
	// =========================================================

	void InitializePhotons();
	void InitializeDiskParticles();

	/** 고전 RK4로 측지선 ODE를 적분: d²u/dφ² + u = (3/2) r_s u² */
	void IntegratePhotonGeodesic(FPhoton& P, double DPhi);

	/** (u, φ, inclinationAxis) → 월드 좌표 변환 */
	FVector PhotonStateToWorld(const FPhoton& P) const;

	void UpdatePhotons(float DeltaTime);
	void UpdateDiskParticles(float DeltaTime);
	void ApplyPlayerGravity(float DeltaTime);
	void UpdatePhase(float DeltaTime);

	// 신규 고급 로직
	void UpdateSceneCapture();
	void ApplyPostProcessToPlayer(float DeltaTime);
	void ApplyTimeDilationToPlayer();
	void ApplyPhysicsForces();
	void TryGravitationalSlingshot();
	void ApplyTidalDisruption(float DeltaTime);

	UFUNCTION()
	void HandleDamageFeedback(AActor* DamagedActor, float Damage,
		const class UDamageType* DamageType, class AController* InstigatedBy, AActor* DamageCauser);

	void DestroyAllVFX();

	// 신규 상태
	double LastRippleTime = -1000.0;
	TMap<TWeakObjectPtr<AActor>, float> OriginalTimeDilation;
	TMap<TWeakObjectPtr<UPrimitiveComponent>, FVector> OriginalPhysicsScale;
};
