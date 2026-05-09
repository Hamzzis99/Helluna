// Capstone Project Helluna

#pragma once

#include "CoreMinimal.h"
#include "BossEvent/BossPatternZoneBase.h"
#include "RealityFractureZone.generated.h"

class UCameraShakeBase;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UPostProcessComponent;
class UStaticMeshComponent;
class UStaticMesh;
class USphereComponent;
class UNiagaraSystem;
class UNiagaraComponent;
class USoundBase;
class UAnimationAsset;

/**
 * ARealityFractureZone — 보스 위협 패턴 (현재 디자인: Stasis Salvo)
 *
 * 시퀀스:
 *   Phase A (0 ~ 0.5s)         시전 외침 (보스 모션 유예)
 *   Phase B (0.5 ~ ~3.5s)      시간 정지 시작 (글로벌 TimeDilation 0.05).
 *                              N개 분신 (보스 SkelMesh ghost) 을 player 주변 ring 에 0.4s 간격
 *                              순차 spawn. 각 분신은 player 시작 위치 향한 aim beam 표시.
 *   Phase C (3.5 ~ 5.5s)       시간 정지 해제 (TD 1.0). plan window — beam 들 그대로 유지.
 *                              player 가 정상 속도로 회피 plan/이동.
 *   Phase D (5.5s ~ 6.0s)      일제 발사 — 각 분신 origin → aim target 으로 hitscan + damage.
 *                              그 후 NotifyPatternFinished.
 *
 * 배경:
 *   - "시간 슬로우" 와 결의 다른 시간 효과: 슬로우 = 느려짐, Stasis = 멈춤.
 *   - 보스가 기관총 손이라 검 잔상 대신 사격 line.
 *   - player 정지로 멋 살림 → plan window 로 회피 가능 (운빨 X).
 *
 * 시각 (BP CDO 에서 wiring):
 *   - StasisPostProcessMaterial: 시간 정지 동안 PostProcess 추가 (M_PP_TimeDistortion 권장)
 *   - GhostMaterial: 분신 mesh override (보스 cosmic galaxy 톤)
 *   - AimBeamVFX: 분신 → aim target line Niagara (NS_Laser_Cosmic 권장)
 *   - SpawnVFX/FireVFX: 분신 spawn / 발사 순간 burst
 */
UCLASS(Blueprintable)
class HELLUNA_API ARealityFractureZone : public ABossPatternZoneBase
{
	GENERATED_BODY()

public:
	ARealityFractureZone();

	virtual void ActivateZone() override;
	virtual void DeactivateZone() override;

	// =========================================================
	// Phase A — 시전 / 시퀀스 길이
	// =========================================================

	/** 분신 수 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Stasis|시퀀스",
		meta = (DisplayName = "분신 수", ClampMin = "1", ClampMax = "10"))
	int32 DecoyCount = 5;

	/** 분신 spawn 간격 (시간 정지 동안의 게임 시간 기준 — TD 0.05 시 wall-clock 으로 보면 매우 짧음) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Stasis|시퀀스",
		meta = (DisplayName = "분신 spawn 간격 (초)", ClampMin = "0.05", ClampMax = "2.0"))
	float DecoySpawnInterval = 0.4f;

	/** 시전 외침 → 시간 정지 시작까지 대기 (초, 정상 시간) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Stasis|시퀀스",
		meta = (DisplayName = "시전 → 시간 정지까지 (초)", ClampMin = "0.0", ClampMax = "3.0"))
	float StasisStartDelay = 0.5f;

	/** 마지막 분신 spawn 후 → 시간 정지 해제까지 추가 idle (초) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Stasis|시퀀스",
		meta = (DisplayName = "분신 spawn 끝 → 정지 해제 (초)", ClampMin = "0.0", ClampMax = "3.0"))
	float StasisHoldAfterSpawn = 0.5f;

	/** 시간 정지 해제 → 일제 발사까지 (plan window) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Stasis|시퀀스",
		meta = (DisplayName = "Plan Window (초)", ClampMin = "0.5", ClampMax = "5.0"))
	float PlanWindowDuration = 2.0f;

	/** 일제 발사 → 패턴 종료까지 (cleanup) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Stasis|시퀀스",
		meta = (DisplayName = "발사 후 종료까지 (초)", ClampMin = "0.1", ClampMax = "3.0"))
	float PostFireDelay = 0.5f;

	// =========================================================
	// Phase B — 시간 정지 (글로벌 TimeDilation)
	// =========================================================

	/** 시간 정지 글로벌 TD (0.001 = 거의 정지) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Stasis|TimeDilation",
		meta = (DisplayName = "Stasis TimeDilation", ClampMin = "0.001", ClampMax = "1.0"))
	float StasisTimeDilation = 0.001f;

	/** 시간 정지 동안 player movement input 차단 여부 (look input 은 가능) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Stasis|TimeDilation",
		meta = (DisplayName = "Player Movement 차단"))
	bool bLockPlayerMovementDuringStasis = true;

	/** 패턴 동안 player camera spring arm length 조정 여부 (회피 시야 확보) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Stasis|Camera",
		meta = (DisplayName = "패턴 동안 카메라 뒤로"))
	bool bAdjustCameraDuringPattern = true;

	/** 패턴 동안 spring arm TargetArmLength (cm) — default 250 → 1000 정도 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Stasis|Camera",
		meta = (DisplayName = "패턴 동안 카메라 거리 (cm)", ClampMin = "300.0", ClampMax = "3000.0",
			EditCondition = "bAdjustCameraDuringPattern"))
	float StasisCameraDistance = 1000.f;

	// =========================================================
	// 분신 ring 배치 + aim
	// =========================================================

	/** player 시작 위치 기준 분신 ring 반경 (cm) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Stasis|배치",
		meta = (DisplayName = "Ring 반경 (cm)", ClampMin = "200.0", ClampMax = "3000.0"))
	float DecoyRingRadius = 700.f;

	/** 분신 spawn 시 ring 각도 jitter (0 = 균등 분포, +값은 균등 ± 도) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Stasis|배치",
		meta = (DisplayName = "각도 지터 (±도)", ClampMin = "0.0", ClampMax = "30.0"))
	float DecoyAngleJitter = 8.f;

	/** 분신 ring Z offset (player 발 높이 대비 cm) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Stasis|배치",
		meta = (DisplayName = "Z offset", ClampMin = "-500.0", ClampMax = "500.0"))
	float DecoyZOffset = 0.f;

	// =========================================================
	// 발사 (Phase D)
	// =========================================================

	/** aim line 길이 (cm) — origin 부터 forward */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Stasis|발사",
		meta = (DisplayName = "Aim Line 길이 (cm)", ClampMin = "300.0", ClampMax = "5000.0"))
	float AimLineLength = 1500.f;

	/** aim line capsule sweep 반경 (cm) — 데미지 폭 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Stasis|발사",
		meta = (DisplayName = "Aim Line 폭 (cm)", ClampMin = "10.0", ClampMax = "300.0"))
	float AimLineWidth = 60.f;

	/** 분신 1발당 데미지 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Stasis|발사",
		meta = (DisplayName = "분신 당 데미지", ClampMin = "0.0"))
	float DamagePerLine = 60.f;

	/** 데미지 타입 (projectile 미사용 시 fallback capsule sweep 데미지에 사용) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Stasis|발사",
		meta = (DisplayName = "데미지 타입 (fallback)"))
	TSubclassOf<UDamageType> DamageType;

	/**
	 * 분신이 발사할 projectile BP class (BP_Boss_RangeProjectile 권장 — AHellunaProjectile_Enemy 자식).
	 *   spawn 후 InitProjectile(Damage, Direction, Speed, LifeSeconds, Owner) 자동 호출.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Stasis|발사",
		meta = (DisplayName = "Decoy Projectile Class"))
	TSubclassOf<AActor> DecoyProjectileClass;

	/** projectile 속도 (cm/s) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Stasis|발사",
		meta = (DisplayName = "Projectile Speed (cm/s)", ClampMin = "500.0", ClampMax = "10000.0"))
	float DecoyProjectileSpeed = 2500.f;

	/** projectile 수명 (초) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Stasis|발사",
		meta = (DisplayName = "Projectile 수명 (초)", ClampMin = "0.5", ClampMax = "10.0"))
	float DecoyProjectileLifeSeconds = 5.f;

	// =========================================================
	// 시각/사운드 wiring (BP CDO)
	// =========================================================

	/** 시간 정지 동안 PostProcess 추가할 머터리얼 (M_PP_TimeDistortion 권장) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Stasis|VFX",
		meta = (DisplayName = "Stasis PostProcess Material"))
	TObjectPtr<UMaterialInterface> StasisPostProcessMaterial = nullptr;

	/** 시간 정지 영역 dome mesh — 보통 SM_Sphere 빌트인 ("/Engine/BasicShapes/Sphere") */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Stasis|VFX",
		meta = (DisplayName = "Zone Dome Mesh"))
	TObjectPtr<UStaticMesh> ZoneVisualMesh = nullptr;

	/** dome material — translucent 권장 (M_ScreenUV_Galaxy 등) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Stasis|VFX",
		meta = (DisplayName = "Zone Dome Material"))
	TObjectPtr<UMaterialInterface> ZoneVisualMaterial = nullptr;

	/** dome 반경 배율 (DecoyRingRadius × 이 값). 1.15 = 분신 ring 살짝 바깥까지 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Stasis|VFX",
		meta = (DisplayName = "Zone Dome 반경 배율", ClampMin = "0.5", ClampMax = "3.0"))
	float ZoneVisualRadiusMul = 1.15f;

	/**
	 * Dome material 의 "Base_Color" 파라미터 (M_Master_ForceField_01 호환).
	 *   TimeDistortion 의 핑크 톤 (65, 10.12, 46) 같이 HDR 컬러 (>1.0) 권장 — bloom + 안개 산란으로
	 *   하늘/원거리 지면까지 색이 깔림. ZoneVisualMaterial 이 이 파라미터를 노출 안 하면 무시됨.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Stasis|VFX",
		meta = (DisplayName = "Dome Base Color (HDR)", HideAlphaChannel))
	FLinearColor DomeBaseColor = FLinearColor(65.f, 10.12f, 46.f, 1.f);

	/** 분신 mesh material override (보스 cosmic galaxy 권장 — None 이면 보스 default 머터리얼) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Stasis|VFX",
		meta = (DisplayName = "Ghost Material (선택)"))
	TObjectPtr<UMaterialInterface> GhostMaterial = nullptr;

	/**
	 * 분신이 재생할 보스 사격 애니메이션 (UAnimSequence 권장. AnimMontage 도 동작은 함).
	 *   spawn 시 첫 프레임에서 paused → 시간 정지 해제 시 재생 시작.
	 *   AM_Boss_Ranged / AM_Boss_Fire / 또는 raw AnimSequence 끼움.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Stasis|애니",
		meta = (DisplayName = "분신 사격 애니메이션"))
	TObjectPtr<UAnimationAsset> DecoyAttackAnim = nullptr;

	/** 분신 → aim target 사이 beam Niagara (NS_Laser_Cosmic 권장) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Stasis|VFX",
		meta = (DisplayName = "Aim Beam Niagara"))
	TObjectPtr<UNiagaraSystem> AimBeamVFX = nullptr;

	/** Aim line static mesh (보통 SM_Cube 빌트인 — X 축 stretch 으로 line) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Stasis|VFX",
		meta = (DisplayName = "Aim Line Mesh"))
	TObjectPtr<UStaticMesh> AimLineMesh = nullptr;

	/** Aim line material — translucent emissive (M_StasisDome 재사용 가능) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Stasis|VFX",
		meta = (DisplayName = "Aim Line Material"))
	TObjectPtr<UMaterialInterface> AimLineMaterial = nullptr;

	/** Aim line 두께 (cm). cube scale 이라 Y/Z 양쪽 적용 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Stasis|VFX",
		meta = (DisplayName = "Aim Line 두께 (cm)", ClampMin = "1.0", ClampMax = "100.0"))
	float AimLineThickness = 18.f;

	/** 분신 spawn 순간 한 방 발화 VFX (선택) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Stasis|VFX",
		meta = (DisplayName = "분신 Spawn VFX (선택)"))
	TObjectPtr<UNiagaraSystem> DecoySpawnVFX = nullptr;

	/** 일제 발사 순간 한 방 발화 VFX (선택) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Stasis|VFX",
		meta = (DisplayName = "Fire Burst VFX (선택)"))
	TObjectPtr<UNiagaraSystem> FireBurstVFX = nullptr;

	/** 시간 정지 시작 사운드 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Stasis|사운드",
		meta = (DisplayName = "Stasis 시작 사운드"))
	TObjectPtr<USoundBase> StasisStartSound = nullptr;

	/** 일제 발사 사운드 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Stasis|사운드",
		meta = (DisplayName = "Fire 사운드"))
	TObjectPtr<USoundBase> FireSound = nullptr;

	/** 카메라 쉐이크 — 발사 순간 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Stasis|사운드",
		meta = (DisplayName = "발사 카메라 쉐이크"))
	TSubclassOf<UCameraShakeBase> FireCameraShake;

	/** ghost 액터 수명 (분신 destroy 까지, 발사 후 cleanup 시 강제 destroy) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Stasis|VFX",
		meta = (DisplayName = "Ghost 수명 (초)", ClampMin = "1.0", ClampMax = "20.0"))
	float GhostLifetime = 8.f;

protected:
	virtual void BeginPlay() override;
	virtual void Destroyed() override;

private:
	// =========================================================
	// 분신 정보 — server 권한, 발사 시 사용
	// =========================================================

	struct FDecoyInfo
	{
		FVector Origin = FVector::ZeroVector;
		FVector AimTarget = FVector::ZeroVector;
		TWeakObjectPtr<AActor> GhostActor; // 클라 로컬 spawn 분신 (정리용)
	};

	/** 서버: 다음 분신 spawn timer 콜백 */
	void ServerSpawnNextDecoy();

	/** 서버: 모든 분신 spawn 완료 → idle 후 stasis 해제 */
	void ServerEndStasis();

	/** 서버: plan window 끝 → 일제 발사 + 데미지 */
	void ServerFire();

	/** 서버: 발사 후 cleanup → NotifyPatternFinished */
	void ServerFinishPattern();

	/** 가장 가까운 player pawn */
	APawn* FindClosestPlayerPawn() const;

	// =========================================================
	// Multicast — 모든 머신 시각 동기화
	// =========================================================

	UFUNCTION(NetMulticast, Reliable)
	void Multicast_StartStasis(FVector PlayerStasisCenter);

	UFUNCTION(NetMulticast, Reliable)
	void Multicast_SpawnDecoy(FVector Origin, FRotator Rot, FVector AimTarget, int32 DecoyIndex);

	UFUNCTION(NetMulticast, Reliable)
	void Multicast_EndStasis();

	UFUNCTION(NetMulticast, Reliable)
	void Multicast_Fire();

	UFUNCTION(NetMulticast, Reliable)
	void Multicast_Cleanup();

	// =========================================================
	// 로컬 헬퍼 (각 머신)
	// =========================================================

	void LocalApplyTimeDilation(float Value);
	void LocalActivatePostProcess();
	void LocalDeactivatePostProcess();
	void LocalSpawnGhostMesh(FVector WorldLoc, FRotator WorldRot, FVector AimTarget, int32 DecoyIndex);
	void LocalCleanupGhosts();

	// =========================================================
	// 상태
	// =========================================================

	int32 CurrentDecoyIndex = 0;
	bool bZoneActive = false;
	bool bStasisActive = false;

	/** 서버 측 분신 정보 — Phase D (ServerFire) 에서 hitscan damage 처리용 */
	TArray<FDecoyInfo> ServerDecoys;

	/** 클라 로컬 spawn 된 분신 actor 추적 (cleanup 용) */
	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<AActor>> LocalGhostActors;

	/** Zone Sphere — TimeDistortionZone 구조 일치. RootComponent. PostProcess 의 SetupAttachment 대상.
	 *   SetGenerateOverlapEvents=false (이 패턴은 sphere 로 overlap 검출 안 함). 시각 zone 은 PP MID 의
	 *   ZoneCenter/ZoneRadius 로 계산. radius 는 Multicast_StartStasis 에서 동적 설정.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Stasis")
	TObjectPtr<USphereComponent> ZoneSphere = nullptr;

	/** 클라 로컬 — 시간 정지 영역 PP. PostProcessComponent 동적 생성, MID 캐시. */
	UPROPERTY(Transient)
	TObjectPtr<UPostProcessComponent> PostProcessComp = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> StasisPPMID = nullptr;

	/** 클라 로컬 — zone visual dome mesh (player 중심 sphere) */
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> ZoneVisualMeshComp = nullptr;

	/** 클라 로컬 — Multicast_StartStasis 에서 받은 player 중심 (PP zone parameter 용) */
	FVector LastStasisCenter = FVector::ZeroVector;

	/** 클라 로컬 — 카메라 거리 백업 (TargetArmLength 변경 전) */
	TMap<TWeakObjectPtr<APawn>, float> SavedSpringArmLengths;

	/** 글로벌 TD scaled timer delay 보정 — stasis 동안에만 사용 */
	float StasisAdjustedDelay(float WallClockSeconds) const
	{
		return FMath::Max(WallClockSeconds * StasisTimeDilation, 0.001f);
	}

	/** 분신 → 존 끝까지 닿는 궤적 길이. (decoy → player = DecoyRingRadius) + (player → 존 외곽 = ZoneRadius). */
	float GetEffectiveAimLineLength() const
	{
		return DecoyRingRadius + DecoyRingRadius * ZoneVisualRadiusMul;
	}

	/** 서버 — 보스 freeze 백업 (복원용) */
	float SavedBossAnimRateScale = 1.f;
	bool bBossWasFrozen = false;

	/** server timer 들 */
	FTimerHandle DecoySpawnTimer;
	FTimerHandle StasisEndTimer;
	FTimerHandle FireTimer;
	FTimerHandle FinishTimer;
};
