// File: Source/Helluna/Public/Enemy/Guardian/HellunaGuardianTurret.h
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/EngineTypes.h"
#include "HellunaGuardianTurret.generated.h"

// 전방 선언
class USceneComponent;
class UStaticMeshComponent;
class USphereComponent;
class UHellunaHealthComponent;
class AHellunaHeroCharacter;
class AHellunaDefenseGameState;
class UNiagaraSystem;
class UNiagaraComponent;
class USoundBase;
class AHellunaGuardianProjectile;

enum class EDefensePhase : uint8;

/**
 * 가디언 상태머신
 * 설계 문서: reedme/guardian/GUARDIAN_TURRET_DESIGN.md
 *
 *   Idle → Detect → Lock → FireDelay → Fire → Cooldown → (Idle 또는 Detect)
 *                                                          ↑
 *                                                          Dead (파괴 시)
 */
UENUM(BlueprintType)
enum class EGuardianState : uint8
{
	Idle        UMETA(DisplayName = "Idle"),
	Detect      UMETA(DisplayName = "Detect"),
	Lock        UMETA(DisplayName = "Lock"),
	FireDelay   UMETA(DisplayName = "Fire Delay"),
	Fire        UMETA(DisplayName = "Fire"),
	Cooldown    UMETA(DisplayName = "Cooldown"),
	Dead        UMETA(DisplayName = "Dead")
};

/**
 * 망가진 가디언 포탑 (Guardian Turret)
 *
 * 젤다 BotW/TotK "망가진 가디언" 레퍼런스의 낮 전용 환경 위험요소.
 * 플레이어를 감지하면 5초 조준 → 2초 발사 예고 → 라인트레이스 레이저 발사.
 * 파괴 가능 (UHellunaHealthComponent). 밤에는 강제 Idle.
 *
 * AttackTurret 과 완전 별개. AHellunaBaseResourceUsingObject 상속 금지.
 */
UCLASS()
class HELLUNA_API AHellunaGuardianTurret : public AActor
{
	GENERATED_BODY()

public:
	AHellunaGuardianTurret();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaTime) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// =========================================================
	// 컴포넌트
	// =========================================================

	/** 고정 루트 — 회전하지 않는 기준점 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Guardian|Components",
		meta = (DisplayName = "고정 루트"))
	TObjectPtr<USceneComponent> TurretRoot;

	/** 몸체 메쉬 (고정) — BP 에서 SM_Guardian_body 할당 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Guardian|Components",
		meta = (DisplayName = "몸체 메쉬 (고정)"))
	TObjectPtr<UStaticMeshComponent> MeshBody;

	/** 플레이어 감지용 구체 콜리전 (고정) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Guardian|Components",
		meta = (DisplayName = "감지 구체"))
	TObjectPtr<USphereComponent> DetectionSphere;

	/** 헤드 회전 피벗 (Yaw + Pitch 2축 회전) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Guardian|Components",
		meta = (DisplayName = "헤드 회전 피벗"))
	TObjectPtr<USceneComponent> TurretHead;

	/** 헤드 메쉬 (회전) — BP 에서 SM_Guardian_head 할당 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Guardian|Components",
		meta = (DisplayName = "헤드 메쉬 (회전)"))
	TObjectPtr<UStaticMeshComponent> MeshHead;

	/** 총구 위치 — 빔 발사 원점 (회전) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Guardian|Components",
		meta = (DisplayName = "총구 위치"))
	TObjectPtr<USceneComponent> MuzzlePoint;

	/** 체력 관리 (파괴 가능) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Guardian|Components",
		meta = (DisplayName = "체력 컴포넌트"))
	TObjectPtr<UHellunaHealthComponent> HealthComponent;

	// =========================================================
	// 감지·조준 설정
	// =========================================================

	/** 감지 반경 (UU). 설계 기본값 2600, 메시 스케일 10 대응 기본 26000. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Guardian|Detection",
		meta = (DisplayName = "감지 반경 (UU)", ClampMin = "500.0", ClampMax = "50000.0"))
	float DetectionRadius = 26000.f;

	/** 헤드 회전 속도 (도/초). Yaw/Pitch 동시 적용. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Guardian|Detection",
		meta = (DisplayName = "헤드 회전 속도 (도/초)", ClampMin = "10.0", ClampMax = "360.0"))
	float HeadRotationSpeed = 75.f;

	/** 발사 허용 각도 오차 (도). 이 이내면 Lock 진입 가능. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Guardian|Detection",
		meta = (DisplayName = "발사 허용 각도 (도)", ClampMin = "1.0", ClampMax = "45.0"))
	float FireAngleThreshold = 5.f;

	/** 헤드 Pitch 상향 최대 (도). 과도한 상향 방지. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Guardian|Detection",
		meta = (DisplayName = "Pitch 최대 (상향)", ClampMin = "10.0", ClampMax = "85.0"))
	float MaxPitchAngle = 60.f;

	/** 헤드 Pitch 하향 최대 (도, 음수). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Guardian|Detection",
		meta = (DisplayName = "Pitch 최소 (하향)", ClampMin = "-85.0", ClampMax = "-1.0"))
	float MinPitchAngle = -30.f;

	/** 플레이어 루트 위쪽 조준 오프셋 (UU). 가슴 높이 조준용. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Guardian|Detection",
		meta = (DisplayName = "조준 오프셋 Z (UU)", ClampMin = "0.0", ClampMax = "200.0"))
	float TargetAimOffsetZ = 50.f;

	/** 라인트레이스 채널 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Guardian|Detection",
		meta = (DisplayName = "트레이스 채널"))
	TEnumAsByte<ECollisionChannel> TraceChannel = ECC_Visibility;

	// =========================================================
	// 상태머신 타이밍
	// =========================================================

	/** Lock 지속 시간 (초). 설계 기본값 5.0. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Guardian|Timing",
		meta = (DisplayName = "Lock 지속시간 (초)", ClampMin = "0.5", ClampMax = "20.0"))
	float LockDuration = 5.0f;

	/** FireDelay 지속 시간 (초). 설계 기본값 2.0. 회피 윈도우. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Guardian|Timing",
		meta = (DisplayName = "FireDelay 지속시간 (초)", ClampMin = "0.1", ClampMax = "10.0"))
	float FireDelayDuration = 2.0f;

	/** Fire 플래시 지속 시간 (초). 빔 시각화 유지 시간. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Guardian|Timing",
		meta = (DisplayName = "Fire 플래시 시간 (초)", ClampMin = "0.05", ClampMax = "1.0"))
	float FireFlashDuration = 0.15f;

	/** Cooldown 지속 시간 (초). 재공격 전 대기. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Guardian|Timing",
		meta = (DisplayName = "Cooldown 지속시간 (초)", ClampMin = "0.1", ClampMax = "10.0"))
	float CooldownDuration = 2.0f;

	// =========================================================
	// 전투
	// =========================================================

	/** 공격 데미지 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Guardian|Combat",
		meta = (DisplayName = "데미지", ClampMin = "1.0", ClampMax = "500.0"))
	float Damage = 25.f;

	/** 발사 히트 판정 허용 오차 (UU). LockedFireTarget 과 플레이어 위치 거리. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Guardian|Combat",
		meta = (DisplayName = "히트 허용 오차 (UU)", ClampMin = "1.0", ClampMax = "200.0"))
	float HitTolerance = 50.f;

	/** 최대 체력. HealthComponent 기본값 오버라이드용. BP 에서 오버라이드 가능. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Guardian|Combat",
		meta = (DisplayName = "최대 체력", ClampMin = "1.0", ClampMax = "10000.0"))
	float MaxHealth = 200.f;

	/** 폭발 AoE 반경 (UU). 0 이면 AoE 없음 — 단일 라인트레이스 직격만. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Guardian|Combat",
		meta = (DisplayName = "폭발 반경 (UU)", ClampMin = "0.0", ClampMax = "5000.0"))
	float ExplosionRadius = 0.f;

	/** 폭발 감쇠 여부 (true=중심→반경끝 선형감쇠, false=반경 내 균일 데미지) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Guardian|Combat",
		meta = (DisplayName = "폭발 감쇠 적용"))
	bool bExplosionFalloff = true;

	// =========================================================
	// 투사체 (BP_HeroWeapon_Launcher 패턴)
	// =========================================================

	/** 발사할 투사체 클래스. null 이면 레거시 즉시-트레이스 모드로 폴백. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Guardian|Projectile",
		meta = (DisplayName = "투사체 클래스"))
	TSubclassOf<AHellunaGuardianProjectile> ProjectileClass;

	/** 투사체 발사 속도 (UU/s). BP 에서 자유롭게 조정. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Guardian|Projectile",
		meta = (DisplayName = "투사체 속도 (UU/s)", ClampMin = "100.0", ClampMax = "50000.0"))
	float ProjectileSpeed = 6000.f;

	/** 투사체 수명 (초). 만료 시 현재 위치에서 공중 폭발. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Guardian|Projectile",
		meta = (DisplayName = "투사체 수명 (초)", ClampMin = "0.1", ClampMax = "20.0"))
	float ProjectileLifeSeconds = 5.f;

	// =========================================================
	// VFX / 사운드
	// =========================================================

	/** 조준 예고 빔 이펙트 — Lock/FireDelay 동안 머즐에 어태치로 표시 (BotW 레드라인) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Guardian|FX",
		meta = (DisplayName = "조준 빔 이펙트 (Lock/FireDelay)"))
	TObjectPtr<UNiagaraSystem> AimBeamFX = nullptr;

	/** 조준 빔 엔드포인트 Vector User Parameter 이름. 사용 NS 에셋에 맞춰 지정. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Guardian|FX",
		meta = (DisplayName = "조준 빔 엔드 파라미터명"))
	FName AimBeamEndParamName = FName(TEXT("Beam End"));

	/** 조준 빔 크기 배율 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Guardian|FX",
		meta = (DisplayName = "조준 빔 크기 배율"))
	FVector AimBeamScale = FVector(1.f);

	/** 발사 투사체/미사일 VFX — Fire 순간 머즐에서 명중 지점으로 발사 (BP 에서 NS_ThunderBolt 등 할당) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Guardian|FX",
		meta = (DisplayName = "발사 투사체 VFX (Fire)"))
	TObjectPtr<UNiagaraSystem> FireBeamFX = nullptr;

	/** 발사 투사체 VFX 크기 배율 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Guardian|FX",
		meta = (DisplayName = "발사 투사체 크기 배율"))
	FVector FireBeamScale = FVector(1.f);

	/** 피격 이펙트 (폭발/충돌 VFX — BP 에서 NS_Hit_Explosion 등 할당) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Guardian|FX",
		meta = (DisplayName = "피격/폭발 이펙트"))
	TObjectPtr<UNiagaraSystem> ImpactFX = nullptr;

	/** 피격/폭발 이펙트 크기 배율 (시각 크기, 데미지 반경과는 독립) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Guardian|FX",
		meta = (DisplayName = "폭발 이펙트 크기 배율"))
	FVector ImpactFXScale = FVector(1.f);

	/** 폭발 이펙트 반경 Float User Parameter 이름 (NS 에셋에 정의되어 있을 때만 사용. 없으면 NAME_None). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Guardian|FX",
		meta = (DisplayName = "폭발 반경 파라미터명 (Float, 선택)"))
	FName ImpactFXRadiusParamName = NAME_None;

	/** 발사 사운드 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Guardian|FX",
		meta = (DisplayName = "발사 사운드"))
	TObjectPtr<USoundBase> FireSound = nullptr;

	/** 피격 사운드 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Guardian|FX",
		meta = (DisplayName = "피격 사운드"))
	TObjectPtr<USoundBase> ImpactSound = nullptr;

	// =========================================================
	// 디버그
	// =========================================================

	/** 감지 반경·추적선 디버그 드로잉 (Shipping 자동 컴파일 아웃) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Guardian|Debug",
		meta = (DisplayName = "디버그 시각화"))
	bool bShowDebug = true;

public:
	/** 현재 상태 (Replicated) — BP 에서 읽기 전용 */
	UFUNCTION(BlueprintPure, Category = "Guardian|State")
	EGuardianState GetCurrentState() const { return CurrentState; }

	/** 현재 상태 타이머 경과 (초, 서버만 정확) */
	UFUNCTION(BlueprintPure, Category = "Guardian|State")
	float GetStateTimer() const { return StateTimer; }

private:
	// =========================================================
	// 상태
	// =========================================================

	/** 현재 상태 (서버 권한, 모든 클라에 복제) */
	UPROPERTY(ReplicatedUsing = OnRep_CurrentState)
	EGuardianState CurrentState = EGuardianState::Idle;

	/** 런타임 활성 조준 빔 컴포넌트 (캐시, 비복제 — OnRep/SetState 로 로컬 생성) */
	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> ActiveAimBeam = nullptr;

	/** 현재 상태 진입 이후 경과 시간 (초, 서버 전용) */
	float StateTimer = 0.f;

	/** 현재 타겟 (Lock 중 고수). 헤드 회전 보간용으로 클라에도 복제 */
	UPROPERTY(ReplicatedUsing = OnRep_CurrentTarget)
	TObjectPtr<AActor> CurrentTarget = nullptr;

	/** FireDelay 진입 순간 고정된 발사 지점 (Replicated — 클라 시각화) */
	UPROPERTY(Replicated)
	FVector_NetQuantize LockedFireTarget = FVector::ZeroVector;

	/** 마지막 알려진 Phase (폴링 비교용, 서버 전용) */
	EDefensePhase CachedPhase = static_cast<EDefensePhase>(0); // Day = 0

	/** 현재 낮인가 */
	bool bIsDay = true;

	/** 감지 반경 내 플레이어 목록 (서버 전용) */
	TArray<TWeakObjectPtr<AHellunaHeroCharacter>> PlayersInRange;

	/** GameState 캐시 (폴링용) */
	TWeakObjectPtr<AHellunaDefenseGameState> CachedGameState;

	/** Phase 폴링 타이머 핸들 */
	FTimerHandle PhasePollTimerHandle;

	// =========================================================
	// 상태 전이 헬퍼
	// =========================================================

	void SetState(EGuardianState NewState);

	// =========================================================
	// 감지 오버랩 콜백
	// =========================================================

	UFUNCTION()
	void OnDetectionBeginOverlap(
		UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
		bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void OnDetectionEndOverlap(
		UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

	// =========================================================
	// 감지 / 조준 로직 (서버 전용)
	// =========================================================

	/** 타겟 선정: 가장 가까운 유효 플레이어 선택. CurrentTarget 갱신. */
	void SelectClosestTarget();

	/** 타겟이 여전히 유효한지 검사 (사망·파괴·범위 체크) */
	bool IsTargetValid() const;

	/** 감지 반경 내 여부 */
	bool IsTargetInRange(const AActor* Target) const;

	/** TurretHead → AimPoint 사이에 벽이 없는지 */
	bool HasLineOfSightTo(const FVector& AimPoint) const;

	/** 플레이어 루트 위쪽으로 오프셋된 조준점 계산 */
	FVector GetAimPointFor(const AActor* Target) const;

	// =========================================================
	// 회전 보간 (서버·클라 공통)
	// =========================================================

	/** 헤드를 AimPoint 방향으로 Yaw+Pitch 보간. Roll 은 항상 0, Pitch 는 Clamp. */
	void UpdateHeadRotation(float DeltaTime, const FVector& AimPoint);

	/** 헤드 Forward 가 AimPoint 방향과 FireAngleThreshold 이내인지 */
	bool IsFacingTarget(const FVector& AimPoint) const;

	// =========================================================
	// 발사 (서버 전용)
	// =========================================================

	/** Fire 진입 순간 호출: 라인트레이스 + 데미지 + 멀티캐스트 FX */
	void PerformFire();

	// =========================================================
	// 복제 콜백
	// =========================================================

	UFUNCTION()
	void OnRep_CurrentTarget();

	UFUNCTION()
	void OnRep_CurrentState();

	// =========================================================
	// 조준 빔 제어 (서버·클라 공통, 로컬 시각화)
	// =========================================================

	/** Lock/FireDelay 진입 시 호출: 머즐에 어태치해 조준 빔 스폰 */
	void StartAimBeam();

	/** Lock/FireDelay 이탈 시 호출: 조준 빔 제거 */
	void StopAimBeam();

	/** 매 Tick 조준 빔 엔드포인트 갱신 (User Parameter 기반) */
	void UpdateAimBeamEndpoint(const FVector& Endpoint);

	/** 상태에 따라 조준 빔 시작/중지 결정 (SetState/OnRep 공용) */
	void ApplyAimBeamForState(EGuardianState State);

	// =========================================================
	// 멀티캐스트 RPC (서버 → 모든 클라 FX·사운드)
	// =========================================================

	/** 발사 FX/사운드 재생. 서버·클라 모두 실행. HitNormal 은 폭발 VFX 방향 결정 (벽면 수직). */
	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_PlayFireFX(FVector_NetQuantize Muzzle, bool bHit, FVector_NetQuantize HitLocation, FVector_NetQuantizeNormal HitNormal);

	/** 상태 변화 이벤트. BGM / 추적선 색 전환 트리거. */
	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_OnStateChanged(EGuardianState NewState);

	// =========================================================
	// 체력·파괴
	// =========================================================

	UFUNCTION()
	void OnGuardianDeath(AActor* DeadActor, AActor* KillerActor);

	// =========================================================
	// Day/Night 구독 (폴링)
	// =========================================================

	/** 주기 타이머 콜백 — Phase 변경 감지 */
	void PollDefensePhase();

	/** Phase 변경 시 호출. Night 진입 시 강제 Idle 리셋. */
	void HandlePhaseChanged(EDefensePhase NewPhase);
};
