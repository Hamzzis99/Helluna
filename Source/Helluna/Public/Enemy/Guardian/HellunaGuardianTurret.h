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
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UMeshComponent;
class USoundBase;
class USoundAttenuation;
class USoundConcurrency;
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

	/** 빔 끝점 표면 오프셋 (UU). 양수=몸에서 더 떨어짐 / 음수=몸 안쪽으로 더 들어감. 0=캡슐 표면 정확히. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Guardian|Detection",
		meta = (DisplayName = "빔 표면 오프셋 (UU)", ClampMin = "-100.0", ClampMax = "100.0"))
	float BeamSurfaceOffset = 0.f;

	/** 라인트레이스 채널 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Guardian|Detection",
		meta = (DisplayName = "트레이스 채널"))
	TEnumAsByte<ECollisionChannel> TraceChannel = ECC_Visibility;

	/** LoS 라인트레이스 시작점 Z 오프셋 (UU). TurretHead 가 WP 랜드스케이프에 파묻히는 케이스 보정용. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Guardian|Detection",
		meta = (DisplayName = "LoS 시작 Z 오프셋 (UU)", ClampMin = "0.0", ClampMax = "500.0"))
	float LoSStartZOffset = 250.f;

	// =========================================================
	// 디버그 진단 (원격 테스터용)
	// =========================================================

	/** 진단 로그 활성화. 상태/LoS/Facing/InRange 를 주기적으로 UE_LOG 에 출력. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Guardian|Debug",
		meta = (DisplayName = "진단 로그 활성화"))
	bool bDebugLogEnabled = false;

	/** 진단 로그 출력 주기 (초) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Guardian|Debug",
		meta = (DisplayName = "진단 로그 주기 (초)", ClampMin = "0.1", ClampMax = "5.0"))
	float DebugLogInterval = 1.0f;

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
	// 사망 연출 (메시 분리 + 물리)
	// =========================================================

	/** 사망 시 머리/몸체를 떼어내고 물리 시뮬 활성화. Static Mesh 에 Simple Collision 이 있어야 함. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Guardian|Death",
		meta = (DisplayName = "사망 시 물리 분리"))
	bool bEnableDeathPhysicsBreak = true;

	/** 사망 시 추가되는 방사형 임펄스 크기. 0 이면 떨어지기만 함. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Guardian|Death",
		meta = (DisplayName = "사망 임펄스 크기", ClampMin = "0.0"))
	float DeathBreakImpulseStrength = 60000.f;

	/** 사망 임펄스 반경 (UU). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Guardian|Death",
		meta = (DisplayName = "사망 임펄스 반경", ClampMin = "1.0"))
	float DeathBreakImpulseRadius = 500.f;

	/** 사망 후 액터 자동 정리까지의 시간 (초). 0 이면 영구 유지. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Guardian|Death",
		meta = (DisplayName = "사망 후 액터 유지 시간 (초)", ClampMin = "0.0"))
	float DeathActorLifetimeSeconds = 20.f;

	/** 사망 순간 스폰되는 폭발 Niagara. null 이면 VFX 없이 물리 분리만. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Guardian|Death",
		meta = (DisplayName = "사망 폭발 FX"))
	TObjectPtr<UNiagaraSystem> DeathExplosionFX = nullptr;

	/** 사망 폭발 FX 스케일. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Guardian|Death",
		meta = (DisplayName = "사망 폭발 FX 스케일"))
	FVector DeathExplosionFXScale = FVector(5.f, 5.f, 5.f);

	/** 사망 시 분리된 모든 메시 컴포넌트를 디졸브 머티리얼로 사라지게 한다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Guardian|Death|Dissolve",
		meta = (DisplayName = "사망 디졸브 활성화"))
	bool bEnableDeathDissolve = true;

	/** 사망 디졸브에 사용할 대체 머티리얼. null 이면 현재 머티리얼의 파라미터만 시도한다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Guardian|Death|Dissolve",
		meta = (DisplayName = "사망 디졸브 머티리얼"))
	TSoftObjectPtr<UMaterialInterface> DeathDissolveOverrideMaterial;

	/** 디졸브 진행 scalar 파라미터. M_Dissolve 계열의 Animation 과 기존 적 디졸브 파라미터를 함께 지원한다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Guardian|Death|Dissolve",
		meta = (DisplayName = "디졸브 Scalar 파라미터"))
	TArray<FName> DeathDissolveScalarParameterNames;

	/** 디졸브 컬러 vector 파라미터. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Guardian|Death|Dissolve",
		meta = (DisplayName = "디졸브 Vector 파라미터"))
	TArray<FName> DeathDissolveVectorParameterNames;

	/** 디졸브 엣지/발광 컬러. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Guardian|Death|Dissolve",
		meta = (DisplayName = "디졸브 엣지 컬러"))
	FLinearColor DeathDissolveEdgeColor = FLinearColor(4.5f, 1.35f, 0.25f, 1.0f);

	/** 가디언 사망 디졸브 지속 시간. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Guardian|Death|Dissolve",
		meta = (DisplayName = "디졸브 시간", ClampMin = "0.1", ClampMax = "30.0", Units = "s"))
	float DeathDissolveDuration = 3.8f;

	/** 디졸브 머티리얼 갱신 주기. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Guardian|Death|Dissolve",
		meta = (DisplayName = "디졸브 Tick 주기", ClampMin = "0.016", ClampMax = "0.25", Units = "s"))
	float DeathDissolveTickInterval = 0.033f;

	/** 사망 순간부터 디졸브 시작까지 대기 시간. 0 = 즉시. (메시 분리/낙하 모션을 보여준 뒤 디졸브) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Guardian|Death|Dissolve",
		meta = (DisplayName = "디졸브 시작 지연 시간", ClampMin = "0.0", ClampMax = "10.0", Units = "s"))
	float DeathDissolveStartDelay = 0.f;

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
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Guardian|Sound",
		meta = (DisplayName = "발사 사운드"))
	TObjectPtr<USoundBase> FireSound = nullptr;

	/** 발사 사운드 볼륨 배율 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Guardian|Sound",
		meta = (DisplayName = "발사 사운드 볼륨", ClampMin = "0.0", ClampMax = "5.0"))
	float FireSoundVolume = 1.0f;

	/** 발사 사운드 피치 배율 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Guardian|Sound",
		meta = (DisplayName = "발사 사운드 피치", ClampMin = "0.1", ClampMax = "4.0"))
	float FireSoundPitch = 1.0f;

	/** 피격 사운드 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Guardian|Sound",
		meta = (DisplayName = "피격 사운드 (레거시 즉시 트레이스)"))
	TObjectPtr<USoundBase> ImpactSound = nullptr;

	/** 피격 사운드 볼륨 배율 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Guardian|Sound",
		meta = (DisplayName = "피격 사운드 볼륨", ClampMin = "0.0", ClampMax = "5.0"))
	float ImpactSoundVolume = 1.0f;

	/** 피격 사운드 피치 배율 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Guardian|Sound",
		meta = (DisplayName = "피격 사운드 피치", ClampMin = "0.1", ClampMax = "4.0"))
	float ImpactSoundPitch = 1.0f;

	/** Lock/FireDelay 동안 반복 재생할 기본 경고 beep. 짧은 one-shot 권장. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Guardian|Sound|Warning",
		meta = (DisplayName = "경고 Beep 사운드"))
	TObjectPtr<USoundBase> WarningBeepSound = nullptr;

	/** 마지막 FireDelay 구간에서 쓸 급박 경고 beep. 비워두면 WarningBeepSound 를 피치만 올려 사용. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Guardian|Sound|Warning",
		meta = (DisplayName = "급박 경고 Beep 사운드"))
	TObjectPtr<USoundBase> CriticalWarningBeepSound = nullptr;

	/** 경고 beep 볼륨 배율 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Guardian|Sound|Warning",
		meta = (DisplayName = "경고 Beep 볼륨", ClampMin = "0.0", ClampMax = "5.0"))
	float WarningBeepVolume = 1.0f;

	/** Lock 시작부의 가장 느린 beep 간격 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Guardian|Sound|Warning",
		meta = (DisplayName = "경고 Beep 느린 간격", ClampMin = "0.05", ClampMax = "3.0"))
	float WarningBeepSlowInterval = 0.85f;

	/** FireDelay 끝부분의 가장 빠른 beep 간격 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Guardian|Sound|Warning",
		meta = (DisplayName = "경고 Beep 빠른 간격", ClampMin = "0.05", ClampMax = "1.0"))
	float WarningBeepFastInterval = 0.12f;

	/** Lock 시작부의 경고 beep 피치 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Guardian|Sound|Warning",
		meta = (DisplayName = "경고 Beep 최소 피치", ClampMin = "0.1", ClampMax = "4.0"))
	float WarningBeepMinPitch = 0.95f;

	/** FireDelay 끝부분의 경고 beep 피치 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Guardian|Sound|Warning",
		meta = (DisplayName = "경고 Beep 최대 피치", ClampMin = "0.1", ClampMax = "4.0"))
	float WarningBeepMaxPitch = 1.45f;

	/** 가디언 3D 사운드 공통 감쇠 설정. null 이면 사운드 에셋 기본값 사용. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Guardian|Sound",
		meta = (DisplayName = "가디언 사운드 감쇠"))
	TObjectPtr<USoundAttenuation> GuardianSoundAttenuation = nullptr;

	/** 가디언 3D 사운드 공통 동시재생 제한. null 이면 사운드 에셋 기본값 사용. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Guardian|Sound",
		meta = (DisplayName = "가디언 사운드 동시재생"))
	TObjectPtr<USoundConcurrency> GuardianSoundConcurrency = nullptr;

	/** 조준당한 플레이어 본인에게만 재생되는 2D 긴박 BGM. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Guardian|Sound|TargetedBGM",
		meta = (DisplayName = "타겟 전용 2D BGM"))
	TObjectPtr<USoundBase> TargetedBgmSound = nullptr;

	/** 타겟 전용 BGM 기본 볼륨. 거리/위협도 알파가 곱해진다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Guardian|Sound|TargetedBGM",
		meta = (DisplayName = "타겟 BGM 기본 볼륨", ClampMin = "0.0", ClampMax = "5.0"))
	float TargetedBgmVolume = 1.0f;

	/** Lock 상태에서의 BGM 위협도 볼륨 배율. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Guardian|Sound|TargetedBGM",
		meta = (DisplayName = "Lock BGM 볼륨 배율", ClampMin = "0.0", ClampMax = "5.0"))
	float TargetedBgmLockVolumeScale = 0.75f;

	/** FireDelay 상태에서의 BGM 위협도 볼륨 배율. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Guardian|Sound|TargetedBGM",
		meta = (DisplayName = "FireDelay BGM 볼륨 배율", ClampMin = "0.0", ClampMax = "5.0"))
	float TargetedBgmFireDelayVolumeScale = 1.0f;

	/** 감지 반경 중 이 비율 안쪽은 BGM 풀 볼륨, 바깥쪽은 반경 끝까지 부드럽게 감소. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Guardian|Sound|TargetedBGM",
		meta = (DisplayName = "BGM 풀 볼륨 반경 비율", ClampMin = "0.0", ClampMax = "1.0"))
	float TargetedBgmFullVolumeRadiusRatio = 0.75f;

	/** 타겟 전용 BGM 페이드 인 시간. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Guardian|Sound|TargetedBGM",
		meta = (DisplayName = "타겟 BGM 페이드 인", ClampMin = "0.0", ClampMax = "10.0"))
	float TargetedBgmFadeInDuration = 0.5f;

	/** 타겟 해제 시 BGM 페이드 아웃 시간. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Guardian|Sound|TargetedBGM",
		meta = (DisplayName = "타겟 BGM 페이드 아웃", ClampMin = "0.0", ClampMax = "10.0"))
	float TargetedBgmFadeOutDuration = 1.8f;

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

	/** 런타임 사망 디졸브 대상 메시 컴포넌트. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMeshComponent>> DeathDissolveMeshComponents;

	/** 런타임 사망 디졸브 MID. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> DeathDissolveMIDs;

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

	/** 클라이언트 로컬 디졸브 타이머. */
	FTimerHandle DeathDissolveTimerHandle;

	/** 디졸브 시작 월드 시간. */
	float DeathDissolveStartWorldSeconds = 0.f;

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

	/** 진단 로그 출력 (서버 전용, DebugLogInterval 주기). */
	void TickDebugDiagnostics(float DeltaTime);

	/** 진단 로그 누적 타이머 */
	float DebugLogAccumulator = 0.f;

	/** 서버 전용: 다음 경고 beep 까지 누적된 시간 */
	float WarningBeepAccumulator = 0.f;

	void TickWarningBeep(float DeltaTime);
	float GetWarningBeepProgress() const;
	float GetWarningBeepInterval() const;
	float GetWarningBeepPitch() const;
	bool IsWarningBeepCritical() const;

	/** 서버 전용: 조준당한 플레이어의 로컬 2D BGM 세션 시작/갱신/종료. */
	TWeakObjectPtr<AHellunaHeroCharacter> ActiveTargetedBgmHero;
	void UpdateTargetedBgmSession();
	void StopTargetedBgmSession();
	void SendTargetedBgmStartOrUpdate(AHellunaHeroCharacter* TargetHero, float ThreatVolumeScale);

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
	// 사망 디졸브 제어 (클라이언트 로컬 시각화)
	// =========================================================

	void StartDeathDissolveVisuals();
	void ApplyDeathDissolveAmount(float Amount);
	void FinishDeathDissolveVisuals();

	// =========================================================
	// 멀티캐스트 RPC (서버 → 모든 클라 FX·사운드)
	// =========================================================

	/** 발사 FX/사운드 재생. SoundLocation 은 팀원이 타겟된 플레이어 주변에서 위험을 듣도록 타겟 조준점 기준으로 전달. */
	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_PlayFireFX(
		FVector_NetQuantize Muzzle,
		bool bHit,
		FVector_NetQuantize HitLocation,
		FVector_NetQuantizeNormal HitNormal,
		FVector_NetQuantize SoundLocation);

	/** 상태 변화 이벤트. BGM / 추적선 색 전환 트리거. */
	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_OnStateChanged(EGuardianState NewState);

	/** Lock/FireDelay 경고 beep 재생. 서버가 박자만 결정하고 각 클라가 3D 월드 사운드로 재생한다. */
	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_PlayWarningBeep(FVector_NetQuantize SoundLocation, bool bCritical, float PitchMultiplier);

	/** 사망 시 머리/몸체 분리 + 물리 시뮬 + 임펄스. 서버·클라 동기. Reliable (한번만 발생). */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_OnDeathBreak();

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
