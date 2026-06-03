// Capstone Project Helluna

#pragma once

#include "CoreMinimal.h"
#include "BossEvent/BossPatternZoneBase.h"
#include "TimeDistortionZone.generated.h"

class UNiagaraSystem;
class USphereComponent;
class UPostProcessComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class AHellunaHeroCharacter;
class ATimeDistortionOrb;
class UStaticMeshComponent;
class ULightComponent;

/**
 * ATimeDistortionZone
 *
 * 시간 왜곡 패턴의 실제 로직을 담당하는 Zone Actor.
 * GA가 이 Actor를 스폰하면, Overlap 기반으로:
 *  - 범위 진입 시 슬로우 적용
 *  - 범위 이탈 시 슬로우 해제
 *  - Orb 스폰/관리 + 파훼 처리
 *  - 파훼 실패 시 폭발 데미지
 *
 * BP에서 상속하여 VFX 에셋을 설정할 수 있다.
 */
UCLASS(Blueprintable)
class HELLUNA_API ATimeDistortionZone : public ABossPatternZoneBase
{
	GENERATED_BODY()

public:
	ATimeDistortionZone();

	// =========================================================
	// ABossPatternZoneBase 인터페이스
	// =========================================================
	virtual void ActivateZone() override;
	virtual void DeactivateZone() override;

	// =========================================================
	// 시간 왜곡 설정
	// =========================================================

	/** 슬로우 적용 시 플레이어의 시간 배율. 0.1 = 원래 속도의 10%. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "시간 왜곡|효과",
		meta = (DisplayName = "시간 감속 배율", ClampMin = "0.05", ClampMax = "0.9"))
	float TimeDilationScale = 0.3f;

	/**
	 * [BossSlowV1] 존이 활성인 동안 시전 보스(OwnerEnemy)도 함께 감속할지.
	 *   전방 발사형은 존이 보스에서 멀리 생겨 overlap 으로는 안 닿으므로, 존 활성 동안 보스에
	 *   직접 CustomTimeDilation 을 적용한다(전 클라 멀티캐스트, 클라에선 OwnerEnemy 가 Transient=null
	 *   이라 보스 액터를 멀티캐스트 인자로 넘김). 감속 배율은 TimeDilationScale 을 공유.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "시간 왜곡|효과",
		meta = (DisplayName = "시전 보스도 감속"))
	bool bSlowOwnerBoss = true;

	/** 시간 복원 시 범위 내 플레이어에게 적용할 데미지 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "시간 왜곡|효과",
		meta = (DisplayName = "폭발 데미지", ClampMin = "0.0"))
	float DetonationDamage = 50.f;

	// =========================================================
	// Orb (파훼 구체) 설정
	// =========================================================

	/** 데코 Orb BP 클래스 (VFX, 충돌 반경은 BP에서 설정) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "시간 왜곡|Orb",
		meta = (DisplayName = "데코 Orb BP 클래스"))
	TSubclassOf<ATimeDistortionOrb> DecoyOrbClass;

	/** 키 Orb BP 클래스 (파훼 대상 — 색/VFX가 다른 특수 Orb) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "시간 왜곡|Orb",
		meta = (DisplayName = "키 Orb BP 클래스"))
	TSubclassOf<ATimeDistortionOrb> KeyOrbClass;

	/** 스폰할 데코 Orb 개수 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "시간 왜곡|Orb",
		meta = (DisplayName = "데코 Orb 개수", ClampMin = "2", ClampMax = "20"))
	int32 DecoyOrbCount = 5;

	/** Orb가 Zone 중심으로부터 스폰되는 거리 (cm) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "시간 왜곡|Orb",
		meta = (DisplayName = "Orb 스폰 거리 (cm)", ClampMin = "100.0", ClampMax = "3000.0"))
	float OrbSpawnRadius = 600.f;

	/** Orb 스폰 높이 오프셋 (cm) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "시간 왜곡|Orb",
		meta = (DisplayName = "Orb 높이 오프셋 (cm)", ClampMin = "0.0", ClampMax = "500.0"))
	float OrbHeightOffset = 150.f;

	/** Orb 스폰 간격 (초) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "시간 왜곡|Orb",
		meta = (DisplayName = "Orb 스폰 간격 (초)", ClampMin = "0.05", ClampMax = "1.0"))
	float OrbSpawnInterval = 0.3f;

	/** 기본 원형 슬롯에서 좌/우로 흔들 각도 (±deg). 완전 랜덤이 아니라 슬롯 근처만 살짝 흔든다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "시간 왜곡|Orb",
		meta = (DisplayName = "Orb 각도 지터 (±도)", ClampMin = "0.0", ClampMax = "45.0"))
	float OrbAngleJitterDeg = 12.f;

	/** 스폰 반경 지터 비율 (0.15 = ±15%) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "시간 왜곡|Orb",
		meta = (DisplayName = "Orb 반경 지터 비율", ClampMin = "0.0", ClampMax = "0.5"))
	float OrbRadiusJitterRatio = 0.12f;

	/** 스폰 높이 지터 (±cm) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "시간 왜곡|Orb",
		meta = (DisplayName = "Orb 높이 지터 (±cm)", ClampMin = "0.0", ClampMax = "300.0"))
	float OrbHeightJitter = 40.f;

	/** 스폰 위치가 지오메트리에 끼는지 검사할 때 사용할 구 반경 (cm) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "시간 왜곡|Orb",
		meta = (DisplayName = "Orb 충돌 검사 반경 (cm)", ClampMin = "10.0", ClampMax = "500.0"))
	float OrbCollisionProbeRadius = 90.f;

	/** 충돌 회피를 위한 최대 지터 샘플링 시도 횟수 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "시간 왜곡|Orb",
		meta = (DisplayName = "Orb 스폰 최대 시도 횟수", ClampMin = "1", ClampMax = "16"))
	int32 OrbSpawnMaxAttempts = 8;

	/** 파훼 성공 시 재생할 사운드 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "시간 왜곡|Orb",
		meta = (DisplayName = "파훼 성공 사운드"))
	TObjectPtr<USoundBase> BreakSuccessSound = nullptr;

	// =========================================================
	// VFX 설정
	// =========================================================

	/** 슬로우 영역 지속 VFX */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "시간 왜곡|VFX",
		meta = (DisplayName = "슬로우 영역 VFX"))
	TObjectPtr<UNiagaraSystem> SlowAreaVFX = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "시간 왜곡|VFX",
		meta = (DisplayName = "슬로우 영역 VFX 크기", ClampMin = "0.01", ClampMax = "20.0"))
	float SlowAreaVFXScale = 1.f;

	/**
	 * [TimeSalvoBloomV1] 존 개화(bloom) 시간(초). 0 이면 즉시 표시(팝).
	 *   ActivateZone(=구체 착탄) 시점에 돔 메시/라이트가 숨김 상태에서 스케일 0→원본으로 ease-out 하며 "퍼지듯" 생성.
	 *   스폰 시점엔 존 비주얼을 숨겨두므로, 구체가 떨어지기 전엔 존이 보이지 않는다.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "시간 왜곡|VFX",
		meta = (DisplayName = "존 개화(bloom) 시간 (초)", ClampMin = "0.0", ClampMax = "2.0"))
	float ZoneBloomDuration = 0.4f;

	/** 패턴 종료 VFX (슬로우 해제 시 먼저 재생, 이후 폭발/파훼 VFX 재생) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "시간 왜곡|VFX",
		meta = (DisplayName = "패턴 종료 VFX"))
	TObjectPtr<UNiagaraSystem> PatternEndVFX = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "시간 왜곡|VFX",
		meta = (DisplayName = "패턴 종료 VFX 크기", ClampMin = "0.01", ClampMax = "20.0"))
	float PatternEndVFXScale = 1.f;

	/** 패턴 종료 VFX 재생 후 결과 VFX(폭발/파훼)까지의 딜레이 (초) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "시간 왜곡|VFX",
		meta = (DisplayName = "결과 VFX 딜레이 (초)", ClampMin = "0.0", ClampMax = "5.0"))
	float ResultVFXDelay = 0.5f;

	/** 폭발 VFX */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "시간 왜곡|VFX",
		meta = (DisplayName = "폭발 VFX"))
	TObjectPtr<UNiagaraSystem> DetonationVFX = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "시간 왜곡|VFX",
		meta = (DisplayName = "폭발 VFX 크기", ClampMin = "0.01", ClampMax = "20.0"))
	float DetonationVFXScale = 1.f;

	/** 파훼 성공 VFX */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "시간 왜곡|VFX",
		meta = (DisplayName = "파훼 성공 VFX"))
	TObjectPtr<UNiagaraSystem> BreakSuccessVFX = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "시간 왜곡|VFX",
		meta = (DisplayName = "파훼 성공 VFX 크기", ClampMin = "0.01", ClampMax = "20.0"))
	float BreakSuccessVFXScale = 1.f;

	// =========================================================
	// 사운드 설정
	// =========================================================

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "시간 왜곡|사운드",
		meta = (DisplayName = "슬로우 시작 사운드"))
	TObjectPtr<USoundBase> SlowStartSound = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "시간 왜곡|사운드",
		meta = (DisplayName = "폭발 사운드"))
	TObjectPtr<USoundBase> DetonationSound = nullptr;

	// =========================================================
	// 포스트프로세스 (채도 제거)
	// =========================================================

	/** 채도 제거 포스트프로세스 머티리얼 (PP_Desaturation 등) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "시간 왜곡|포스트프로세스",
		meta = (DisplayName = "채도 제거 PP 머티리얼"))
	TObjectPtr<UMaterialInterface> DesaturationPPMaterial = nullptr;

	/** 채도 제거 강도 (0=원본 색상, 1=완전 회색) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "시간 왜곡|포스트프로세스",
		meta = (DisplayName = "채도 제거 강도", ClampMin = "0.0", ClampMax = "1.0"))
	float DesaturationStrength = 1.0f;

	/** 구 경계 부드러움 (cm). 0이면 칼같은 경계, 크면 부드러운 페이드 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "시간 왜곡|포스트프로세스",
		meta = (DisplayName = "경계 부드러움 (cm)", ClampMin = "0.0", ClampMax = "2000.0"))
	float EdgeSoftness = 150.f;

protected:
	virtual void BeginPlay() override;
	virtual void Destroyed() override;

	// =========================================================
	// [TimeSalvoBloomV1] 개화 reveal — 스폰 시 숨김, 착탄(ActivateZone) 시 전 클라 reveal + bloom
	// =========================================================

	/** 스폰 시점에 돔 메시/라이트를 캐시하고 숨긴다 (BeginPlay, 전 머신). */
	void CacheAndHideZoneVisuals();

	/** ActivateZone(서버) 에서 호출 — 전 클라에서 돔/라이트 reveal + 스케일 bloom 시작.
	 *  [ZoneVisualLocFixV1] ZoneWorldLoc 으로 각 머신에서 존 위치를 강제 동기화(클라에서 보스 위치에
	 *  남는 버그 방지) 후 reveal. */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_RevealZoneVisuals(FVector ZoneWorldLoc);

	/** bloom 스케일 lerp 틱 (타이머 구동). */
	void TickBloom();

private:
	// =========================================================
	// 콜리전 컴포넌트
	// =========================================================

	UPROPERTY(VisibleAnywhere, Category = "시간 왜곡")
	TObjectPtr<USphereComponent> SlowSphere = nullptr;

	// =========================================================
	// Overlap 콜백
	// =========================================================

	UFUNCTION()
	void OnSlowSphereBeginOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
		bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void OnSlowSphereEndOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

	// =========================================================
	// 슬로우 적용/해제 (CustomTimeDilation 기반)
	// =========================================================

	/** 존 안에 들어온 액터에게 CustomTimeDilation 적용 (자기 자신과 소유 Enemy 제외) */
	void ApplySlowToActor(AActor* Actor);
	void RemoveSlowFromActor(AActor* Actor);
	void RestoreAllSlowedActors();

	/**
	 * [TDSlowReleaseFixV1] 안전망 — 패턴 종료 경로에서 맵 기반 복원으로 놓친 플레이어를
	 * Zone 반경 근처 월드 스캔으로 찾아 강제로 1.0 배율 복원한다.
	 *
	 * 증상: 파훼 성공 시 슬로우가 안 풀리는 경우가 있음.
	 * 원인: OnPatternBroken 타이밍에 SetGenerateOverlapEvents(false) 직후 엣지 케이스로
	 *       SlowedActors 맵에 플레이어가 없거나, EndOverlap 이 Apply 없이 먼저 발생한 경우.
	 * 대책: 맵과 무관하게 월드 scan 으로 복원 — 이미 맵 기반 경로가 성공했으면 이중 호출은 무해.
	 */
	void ForceRestoreNearbyPlayers();

	/** 슬로우 적용 전 원본 CustomTimeDilation 저장 */
	TMap<TWeakObjectPtr<AActor>, float> SlowedActors;

	// =========================================================
	// [BossSlowV1] 시전 보스 감속 — 존이 멀리 생겨도 보스를 직접 감속
	// =========================================================

	/** 존 활성 시 보스 감속 적용 (서버). bSlowOwnerBoss=true 일 때만. */
	void ApplyOwnerBossSlow();

	/** 패턴 종료 경로에서 보스 감속 복원 (서버). 중복 호출 무해. */
	void RestoreOwnerBossSlow();

	/** 전 클라에서 보스의 CustomTimeDilation 을 절대값으로 설정 (클라 OwnerEnemy=null 대응 인자 전달). */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_SetOwnerBossTimeDilation(AActor* Boss, float NewDilation);

	/** 보스 감속 적용 상태 (서버에서만 추적) */
	bool bOwnerBossSlowed = false;

	// =========================================================
	// 폭발 (파훼 실패)
	// =========================================================

	/** 패턴 종료 VFX 재생 후 실제 폭발 처리 */
	void BeginDetonation();
	void FinishDetonation();

	/** 패턴 종료 VFX 재생 후 실제 파훼 처리 */
	void BeginPatternBroken();
	void FinishPatternBroken();

	FTimerHandle DetonationTimerHandle;
	FTimerHandle ResultVFXTimerHandle;

	// =========================================================
	// Orb 관련
	// =========================================================

	void StartOrbSpawnSequence();
	void SpawnNextOrb();
	void DestroyAllOrbs();

	UFUNCTION()
	void OnOrbDestroyed(ATimeDistortionOrb* DestroyedOrb);

	void OnPatternBroken();

	UPROPERTY(Transient)
	TArray<TObjectPtr<ATimeDistortionOrb>> SpawnedOrbs;

	bool bPatternBroken = false;
	bool bZoneActive = false;

	int32 OrbSpawnCurrentIndex = 0;
	int32 OrbSpawnTotalCount = 0;
	int32 OrbSpawnKeyIndex = 0;
	FTimerHandle OrbSpawnTimerHandle;

	// =========================================================
	// 포스트프로세스 런타임
	// =========================================================

	UPROPERTY(VisibleAnywhere, Category = "시간 왜곡")
	TObjectPtr<UPostProcessComponent> PostProcessComp = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> DesaturationMID = nullptr;

	/** 모든 클라이언트에서 PP 활성화 + Custom Depth 설정 */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_ActivateDesaturation(FVector Center, float Radius, AActor* BossActor);

	/** 모든 클라이언트에서 PP 비활성화 + Custom Depth 해제 */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_DeactivateDesaturation();

	/** 액터의 모든 메시에 Custom Depth/Stencil 설정 (PP 제외용) */
	void SetActorCustomDepth(AActor* Actor, bool bEnable);

	/** Custom Depth가 설정된 액터 추적 (정리용) */
	TArray<TWeakObjectPtr<AActor>> CustomDepthActors;

	// =========================================================
	// [TimeSalvoBloomV1] 개화 reveal 런타임 상태
	// =========================================================

	/** 스폰 시 숨긴 돔 메시들 (BP 추가 StaticMesh — 보통 SM_Sphere_01). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> RevealMeshes;

	/** RevealMeshes 각각의 원본 RelativeScale (bloom target). */
	TArray<FVector> RevealMeshTargetScales;

	/** 스폰 시 숨긴 라이트들 (BP 추가 PointLight 등). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<ULightComponent>> RevealLights;

	FTimerHandle BloomTimerHandle;
	float BloomElapsed = 0.f;
};
