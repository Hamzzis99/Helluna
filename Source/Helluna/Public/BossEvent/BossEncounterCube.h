// Source/Helluna/Public/BossEvent/BossEncounterCube.h

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BossEncounterCube.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class UWidgetComponent;
class UHoldInteractWidget;
class AHellunaHeroController;
class UMaterialParameterCollection;
class UHellunaHealthComponent;
class UNiagaraSystem;
class UNiagaraComponent;
class UPostProcessComponent;
class UCameraComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UStaticMeshComponent;

/**
 * 중간보스 조우 큐브 — F키 홀드로 보스 소환 이벤트 시작
 *
 * PuzzleCubeActor와 유사한 3D 홀드 상호작용 패턴:
 *   1. 플레이어가 InteractionSphere 범위 안에 진입
 *   2. HoldInteractWidget(3D Screen Space) 표시
 *   3. F키 홀드 → 프로그레스 → 완료 시 Server RPC
 *
 * 퍼즐 시스템과 완전 분리 (별도 InputTag_Interaction 사용)
 */
UCLASS()
class HELLUNA_API ABossEncounterCube : public AActor
{
	GENERATED_BODY()

public:
	ABossEncounterCube();

	// =========================================================================================
	// 컴포넌트
	// =========================================================================================

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "BossEncounter|Components")
	TObjectPtr<UStaticMeshComponent> MeshComp;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "BossEncounter|Components")
	TObjectPtr<USphereComponent> InteractionSphere;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "BossEncounter|UI")
	TObjectPtr<UWidgetComponent> InteractWidgetComp;

	// =========================================================================================
	// 에디터 설정
	// =========================================================================================

	/** 3D 위젯에 사용할 위젯 클래스 (BP에서 WBP_BossEncounter_InteractWidget 할당) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BossEncounter|UI",
		meta = (DisplayName = "Interact Widget Class (상호작용 위젯 클래스)"))
	TSubclassOf<UHoldInteractWidget> InteractWidgetClass;

	/** F키 홀드 완료까지 걸리는 시간 (초) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BossEncounter",
		meta = (DisplayName = "Hold Duration (홀드 시간)", ClampMin = "0.5", ClampMax = "5.0"))
	float HoldDuration = 1.5f;

	/** 상호작용 반경 조회 */
	float GetInteractionRadius() const;

	// =========================================================================================
	// [Step 2] 보스 스폰
	// =========================================================================================

	/** 스폰할 보스 캐릭터 클래스 (BP에서 설정) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BossEncounter|Spawn",
		meta = (DisplayName = "Boss Class (보스 클래스)"))
	TSubclassOf<APawn> BossClass;

	/** 큐브 기준 보스 스폰 오프셋 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BossEncounter|Spawn",
		meta = (DisplayName = "Boss Spawn Offset (스폰 오프셋)"))
	FVector BossSpawnOffset = FVector(0.f, 0.f, 150.f);

	// =========================================================================================
	// [Step 4] Custom Depth / Stencil (플레이어·보스 컬러 유지)
	// =========================================================================================

	/**
	 * 보스 조우 전용 Stencil 값.
	 * PP Material에서 이 값을 가진 픽셀은 흑백 대신 원본 컬러를 유지한다.
	 * 다른 시스템(퍼즐 등)과 충돌하지 않도록 예약된 값.
	 */
	static constexpr int32 BossEncounterStencilValue = 1;

	// =========================================================================================
	// [Step 3] 영역 흑백 전환
	// =========================================================================================

	/**
	 * MPC_BossEncounter 에셋 참조 (BP에서 할당)
	 * DesatAmount, ColorWaveRadius, WaveOriginXYZ 파라미터를
	 * PP Material과 꽃 Material이 공유.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BossEncounter|Desaturation",
		meta = (DisplayName = "MPC Boss Encounter (MPC 에셋)"))
	TObjectPtr<UMaterialParameterCollection> MPC_BossEncounter;

	/** 흑백 전환 시간 (초) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BossEncounter|Desaturation",
		meta = (DisplayName = "Desat Transition Duration (흑백 전환 시간)", ClampMin = "0.5", ClampMax = "10.0"))
	float DesatTransitionDuration = 3.f;

	/** 보스 영역 반경 — 이 범위 안의 사물만 흑백 적용 (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BossEncounter|Area",
		meta = (DisplayName = "Boss Area Radius (영역 반경)", ClampMin = "1000", ClampMax = "50000"))
	float BossAreaRadius = 5000.f;

	/** 평시 어두움 강도 — 0=원색, 1=완전 어둡게 (Substrate Mix에 직접 사용) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BossEncounter|Area",
		meta = (DisplayName = "Darkness Amount (어두움 강도)", ClampMin = "0.0", ClampMax = "1.0"))
	float DarknessAmount = 0.85f;

	// =========================================================================================
	// [Step 5] 컬러 웨이브 (보스 처치 후)
	// =========================================================================================

	/** 컬러 웨이브 확산 속도 (cm/s) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BossEncounter|ColorWave",
		meta = (DisplayName = "Color Wave Speed (웨이브 속도)", ClampMin = "1000", ClampMax = "20000"))
	float ColorWaveSpeed = 5000.f;

	/** 웨이브 최대 반경 — 도달 시 DesatAmount=0으로 완전 컬러 복원 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BossEncounter|ColorWave",
		meta = (DisplayName = "Color Wave Max Radius (최대 반경)", ClampMin = "1000", ClampMax = "50000"))
	float ColorWaveMaxRadius = 10000.f;

	// =========================================================================================
	// [Cinematic] 보스 사망 시네마틱 연출
	// =========================================================================================

	/** 보스 폭발 나이아가라 VFX (BP에서 할당, nullptr이면 스킵) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BossEncounter|Cinematic",
		meta = (DisplayName = "Boss Death VFX (폭발 이펙트)"))
	TObjectPtr<UNiagaraSystem> BossDeathVFX;

	/** 바람 나이아가라 VFX (BP에서 할당, nullptr이면 스킵) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BossEncounter|Cinematic",
		meta = (DisplayName = "Wind VFX (바람 이펙트)"))
	TObjectPtr<UNiagaraSystem> WindVFX;

	/** 보스 사망 사운드 (BP에서 할당, nullptr이면 스킵) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BossEncounter|Cinematic",
		meta = (DisplayName = "Boss Death Sound (사망 사운드)"))
	TObjectPtr<USoundBase> BossDeathSound;

	/** 카메라 셰이크 클래스 (BP에서 할당, nullptr이면 스킵) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BossEncounter|Cinematic",
		meta = (DisplayName = "Boss Death Camera Shake (카메라 흔들림)"))
	TSubclassOf<UCameraShakeBase> BossDeathCameraShake;

	/** 슬로모션 배율 (0.3 = 30% 속도) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BossEncounter|Cinematic",
		meta = (DisplayName = "Slow Motion Scale (슬로모션 배율)", ClampMin = "0.1", ClampMax = "1.0"))
	float SlowMotionScale = 0.3f;

	/** 슬로모션 지속시간 (게임 시간 기준, 실제 체감은 더 길어짐) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BossEncounter|Cinematic",
		meta = (DisplayName = "Slow Motion Duration (슬로모션 시간)", ClampMin = "0.1", ClampMax = "2.0"))
	float SlowMotionDuration = 0.3f;

	/** FOV 펀치 크기 (현재 FOV에 더하는 값) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BossEncounter|Cinematic",
		meta = (DisplayName = "FOV Punch Amount (FOV 펀치)", ClampMin = "5", ClampMax = "40"))
	float FOVPunchAmount = 15.f;

	/** 순백 섬광 지속시간 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BossEncounter|Cinematic",
		meta = (DisplayName = "White Flash Duration (섬광 시간)", ClampMin = "0.1", ClampMax = "2.0"))
	float WhiteFlashDuration = 0.4f;

	/** 채도 오버슈트 배율 (1.3 = 30% 더 선명) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BossEncounter|Cinematic",
		meta = (DisplayName = "Saturation Overshoot (채도 오버슈트)", ClampMin = "1.0", ClampMax = "2.0"))
	float SaturationOvershootScale = 1.3f;

	// =========================================================================================
	// [Wave VFX] 컬러 웨이브 오라 스폰
	// =========================================================================================

	/** 웨이브가 사물에 닿을 때 스폰할 오라 VFX (BP에서 할당, nullptr이면 스킵) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BossEncounter|WaveVFX",
		meta = (DisplayName = "Wave Aura VFX (웨이브 오라)"))
	TObjectPtr<UNiagaraSystem> WaveAuraVFX;

	/** 오라 VFX 스케일 (기본 0.5 — 사물 크기에 맞게 조절) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BossEncounter|WaveVFX",
		meta = (DisplayName = "Wave Aura Scale (오라 크기)", ClampMin = "0.1", ClampMax = "3.0"))
	float WaveAuraScale = 0.5f;

	/** 오라 VFX 수명 (초) — 이 시간 후 강제 소멸. Loop VFX 제어용 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BossEncounter|WaveVFX",
		meta = (DisplayName = "Wave Aura Lifetime (오라 수명)", ClampMin = "0.5", ClampMax = "5.0"))
	float WaveAuraLifetime = 2.0f;

	/** 웨이브 경계에서 VFX를 스폰할 두께 (cm) — 웨이브 반경 - 이 값 ~ 반경 사이 액터 대상 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BossEncounter|WaveVFX",
		meta = (DisplayName = "Wave Edge Thickness (경계 두께)", ClampMin = "100", ClampMax = "1000"))
	float WaveEdgeThickness = 300.f;

	/** 프레임당 최대 VFX 스폰 수 — 성능 보호 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BossEncounter|WaveVFX",
		meta = (DisplayName = "Max VFX Per Frame (프레임당 최대)", ClampMin = "1", ClampMax = "10"))
	int32 MaxVFXPerFrame = 3;

	/** 폴리지 인스턴스 중 VFX를 스폰할 비율 (0.08 = 8%) — 성능 보호 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BossEncounter|WaveVFX",
		meta = (DisplayName = "Foliage Sample Rate (폴리지 샘플링)", ClampMin = "0.01", ClampMax = "1.0"))
	float FoliageSampleRate = 0.08f;

	/** [Effect A] 홀로그램 나이아가라 (NS_cosmic_Holo, BP에서 할당) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BossEncounter|WaveVFX",
		meta = (DisplayName = "Wave Holo VFX (홀로그램)"))
	TObjectPtr<UNiagaraSystem> WaveHoloVFX;

	/** [Effect C] 와이어프레임 오버레이 머티리얼 (M_Wire_Earth, BP에서 할당) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BossEncounter|WaveVFX",
		meta = (DisplayName = "Wave Wireframe Material (와이어프레임)"))
	TObjectPtr<UMaterialInterface> WaveWireframeMaterial;

	/** 스캔 라인 이동 속도 (0→1 진행 시간, 초) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BossEncounter|WaveVFX",
		meta = (DisplayName = "Scan Duration (스캔 시간)", ClampMin = "0.5", ClampMax = "5.0"))
	float ScanDuration = 2.5f;

	/** 스캔 밴드 높이 비율 (메시 전체 높이 대비, 0.2 = 20%) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BossEncounter|WaveVFX",
		meta = (DisplayName = "Scan Band Ratio (밴드 비율)", ClampMin = "0.05", ClampMax = "0.5"))
	float ScanBandRatio = 0.2f;

	// =========================================================================================
	// 리플리케이션
	// =========================================================================================

	/** 이미 활성화됨 (보스 소환 완료) */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "BossEncounter")
	bool bActivated = false;

	/** 보스 처치 완료 (컬러 웨이브 이후) — 늦은 접속 클라이언트 판별용 */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "BossEncounter")
	bool bBossDefeated = false;

	/**
	 * [Step 8] 영역 고유 식별자 — 영속 저장 시 키로 사용
	 * 레벨에 여러 보스 영역을 배치할 경우 각각 다른 ID를 설정.
	 * TODO: 향후 SQLite/SaveGame 연동 시 이 ID로 클리어 상태 저장/로드
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BossEncounter",
		meta = (DisplayName = "Encounter ID (영역 고유 ID)"))
	FName EncounterId = NAME_None;

	// =========================================================================================
	// 서버 함수 (HellunaHeroController에서 호출)
	// =========================================================================================

	/**
	 * 보스 조우 활성화 (서버 권한)
	 * 보스 스폰 + Multicast로 클라이언트에 흑백 전환 신호 전달
	 * @return 활성화 성공 여부
	 */
	bool TryActivate(AController* Activator);

	// =========================================================================================
	// Multicast RPC — 서버 → 모든 클라이언트
	// =========================================================================================

	/** 보스 조우 시작 알림 (클라이언트: 흑백 전환 시작) */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_BossEncounterStarted();

	/** [Step 5] 보스 처치 알림 (클라이언트: 컬러 웨이브 시작) */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_BossDefeated(FVector DeathLocation);

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	// =========================================================================================
	// 3D 위젯 표시/숨김
	// =========================================================================================

	/** 위젯 표시 여부 업데이트 (클라이언트 Tick) */
	void UpdateInteractWidgetVisibility();

	/** 위젯 인스턴스 캐시 */
	UPROPERTY()
	TObjectPtr<UHoldInteractWidget> InteractWidgetInstance;

	/** 위젯 현재 표시 중인지 */
	bool bInteractWidgetVisible = false;

	/** F키 홀드 프로그레스 (로컬 클라이언트) */
	float LocalHoldProgress = 0.f;

	/** 홀드 완료 플래그 (중복 RPC 방지) */
	bool bHoldCompleted = false;

	// =========================================================================================
	// [Step 2] 보스 스폰 내부
	// =========================================================================================

	/** 스폰된 보스 (서버 전용, 사망 감지용) */
	TWeakObjectPtr<APawn> SpawnedBoss;

	// =========================================================================================
	// [Step 4] Custom Depth 내부
	// =========================================================================================

	/**
	 * 현재 존재하는 모든 플레이어 캐릭터 + 보스에 Custom Depth/Stencil 활성화.
	 * Multicast(정상 흐름) 및 BeginPlay(늦은 접속) 양쪽에서 호출.
	 */
	void EnableBossEncounterCustomDepth();

	/**
	 * 액터의 모든 PrimitiveComponent + 부착된 자식 액터(무기 등)에
	 * CustomDepth/Stencil을 재귀적으로 설정한다.
	 */
	static void SetCustomDepthOnActor(AActor* Actor, bool bEnable, int32 StencilValue);

	// =========================================================================================
	// [Step 3] 흑백 전환 내부 (클라이언트)
	// =========================================================================================

	/** 흑백 전환 진행 중 */
	bool bDesatTransitioning = false;

	/** 흑백 전환 프로그레스 (0→1) */
	float DesatProgress = 0.f;

	/** 클라이언트 Tick에서 MPC DesatAmount 업데이트 */
	void TickDesaturation(float DeltaTime);

	// =========================================================================================
	// [Step 5] 컬러 웨이브 내부
	// =========================================================================================

	/** 보스 사망 시 HealthComponent::OnDeath 콜백 (서버 전용) */
	UFUNCTION()
	void OnSpawnedBossDied(AActor* DeadActor, AActor* KillerActor);

	/** 클라이언트 Tick에서 MPC ColorWaveRadius 확장 */
	void TickColorWave(float DeltaTime);

	/** 모든 플레이어·보스의 Custom Depth 비활성화 (웨이브 완료 후 정리) */
	void DisableBossEncounterCustomDepth();

	/** 컬러 웨이브 활성 중 */
	bool bColorWaveActive = false;

	/** 현재 웨이브 반경 (cm) */
	float ColorWaveRadius = 0.f;

	/** 웨이브 원점 (보스 사망 위치) */
	FVector WaveOrigin = FVector::ZeroVector;

	// =========================================================================================
	// [Wave VFX] 오라 스폰 내부
	// =========================================================================================

	/** 웨이브 경계의 사물에 오라 VFX 스폰 (TickColorWave에서 호출) */
	void SpawnWaveAuraVFX(float DeltaTime);

	/** 웨이브가 액터에 닿을 때 홀로+와이어프레임 효과 적용 */
	void ApplyWaveEffectsOnActor(AActor* Actor, UWorld* World);

	/** 와이어프레임 오버레이 스캔 애니메이션 Tick */
	void TickWireframeOverlays(float DeltaTime);

	/** 와이어프레임 오버레이 스캔 상태 */
	struct FWireframeOverlay
	{
		TWeakObjectPtr<UStaticMeshComponent> OverlayMesh;
		TObjectPtr<UMaterialInstanceDynamic> DMI;

		// 스캔 진행도: 0.0 = 바닥, 1.0 = 꼭대기
		float ScanProgress = 0.f;

		// 원본 메시 바운딩 (스캔 범위)
		float BottomZ = 0.f;
		float TopZ = 0.f;
		float OrigScaleZ = 1.f;
		FVector OrigLocation = FVector::ZeroVector;

		// 컬러 복원용 원본 액터 추적
		TWeakObjectPtr<AActor> TargetActor;
	};
	TArray<FWireframeOverlay> ActiveWireframeOverlays;

	/** 이미 오라 VFX가 스폰된 액터 추적 (중복 방지) */
	TSet<TWeakObjectPtr<AActor>> AuraVFXSpawnedActors;

	/** 이미 VFX가 스폰된 폴리지 인스턴스 위치 추적 (위치 해시 기반 중복 방지) */
	TSet<int32> AuraVFXSpawnedFoliageHashes;

	/** 폴리지 인스턴스 사전 수집 캐시 (웨이브 시작 시 한 번만 수집) */
	TArray<FVector> CachedFoliageLocations;

	/** 폴리지 캐시 완료 여부 */
	bool bFoliageCached = false;

	/** VFX 스폰 쿨다운 타이머 (초) */
	float WaveVFXCooldown = 0.f;

	// =========================================================================================
	// [Cinematic] 시네마틱 내부
	// =========================================================================================

	/** 시네마틱 시퀀스 시작 (Multicast에서 호출) */
	void StartCinematicSequence(FVector DeathLocation);

	/** Phase 0: 슬로모션 */
	void CinematicPhase0_SlowMotion();

	/**
	 * Phase 1: 폭발 임팩트
	 * 슬로모션 종료 + 카메라 셰이크 + FOV 펀치 + 크로매틱 수차 + VFX + 사운드
	 * 이 함수 내에서 나머지 Phase 타이머를 체인으로 설정
	 */
	void CinematicPhase1_Explosion();

	/** Phase 2: 순백 섬광 */
	void CinematicPhase2_WhiteFlash();

	/** Phase 4: 채도 오버슈트 (웨이브 80% 시점) */
	void CinematicPhase4_SaturationOvershoot();

	/** Phase 5: 전체 정리 — PostProcess 리셋, FOV 복원 */
	void CinematicPhase5_Cleanup();

	/** Tick에서 시네마틱 Lerp 처리 (FOV·크로마·섬광·채도) */
	void TickCinematic(float DeltaTime);

	/** 시네마틱 전용 PostProcessComponent 동적 생성 */
	void CreateCinematicPostProcess();

	/** 시네마틱용 PostProcessComponent (클라이언트에서 동적 생성, Unbound) */
	UPROPERTY()
	TObjectPtr<UPostProcessComponent> CinematicPostProcess;

	/** 보스 사망 위치 캐시 (VFX 스폰용) */
	FVector CinematicDeathLocation = FVector::ZeroVector;

	// 타이머 핸들
	FTimerHandle CinematicTimer_Phase1;
	FTimerHandle CinematicTimer_Phase2;
	FTimerHandle CinematicTimer_Phase4;
	FTimerHandle CinematicTimer_Phase5;

	// FOV Lerp
	bool bCinematicFOVActive = false;
	float CinematicOriginalFOV = 90.f;

	// 크로매틱 수차 페이드아웃
	bool bCinematicChromaActive = false;

	// 순백 섬광 페이드아웃
	bool bCinematicFlashActive = false;
	float CinematicFlashAlpha = 0.f;

	// 채도 오버슈트 Lerp
	bool bCinematicSatBoostActive = false;
};
