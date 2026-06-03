// Capstone Project Helluna

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "StasisSalvoOrb.generated.h"

class USphereComponent;
class UProjectileMovementComponent;
class UNiagaraComponent;
class UNiagaraSystem;
class USoundBase;
class UStaticMesh;
class UStaticMeshComponent;
class UMaterialInterface;
class AHellunaEnemyCharacter;
class ABossPatternZoneBase;

/**
 * [StasisSalvoV2] 보스 분신패턴 — 보스가 fire 몽타주 중 "발사"하는 시간정지 구체.
 *
 *  흐름:
 *   1) RealityFracture GA 가 fire 타이밍에 이 구체를 보스 → 플레이어 방향으로 스폰. 동시에
 *      미리 만들어둔 (deactivated) ARealityFractureZone 을 TargetZone 으로 넘겨받음.
 *   2) 구체는 짧은 거리만 비행 — 무언가에 부딪히거나 MaxTravelDistance 에 도달하면 Burst().
 *      ("총알은 멀리 가면 안 됨")
 *   3) Burst(): 원형 burst VFX/사운드 멀티캐스트 + 서버에서 TargetZone->ActivateZone() 으로
 *      시간정지 존 발동(존이 그 자리로 이동·확장) + self destroy.
 *
 *  멀티: 서버 권위 + bReplicates(+replicate movement) — 모든 클라가 구체 비행을 봄. 존 발동은
 *  서버에서만(존 자체가 replicate 되고 내부에서 Multicast 로 stasis 동기화).
 */
UCLASS()
class HELLUNA_API AStasisSalvoOrb : public AActor
{
	GENERATED_BODY()

public:
	AStasisSalvoOrb();

	/** GA 가 스폰 직후 1회 호출 — 비행 방향/속도 + Burst 시 발동할 zone 지정. */
	void Init(AHellunaEnemyCharacter* InOwnerEnemy, ABossPatternZoneBase* InTargetZone, const FVector& InDirection);

	// =========================================================
	// 튜닝
	// =========================================================

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "StasisSalvoOrb",
		meta = (DisplayName = "비행 속도 (cm/s)", ClampMin = "100.0", ClampMax = "6000.0"))
	float Speed = 1400.f;

	// =========================================================
	// [SlowDescentV1] 비행 중 점진 감속 — 하늘 낙하 분신 구체용 (내려갈수록 느려짐)
	// =========================================================

	/** true 면 매 틱 속도를 지수 감쇠시켜 "내려갈수록(시간이 갈수록) 점점 느려진다". 전방 발사형은 false 권장. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "StasisSalvoOrb|감속",
		meta = (DisplayName = "비행 중 감속(ease-out)"))
	bool bDecelerateWhileMoving = false;

	/** 초당 속도 유지율 (0.4 = 1초마다 속도의 40%만 남김 = 60% 감속). 작을수록 빨리 느려진다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "StasisSalvoOrb|감속",
		meta = (DisplayName = "초당 속도 유지율", ClampMin = "0.05", ClampMax = "0.99",
			EditCondition = "bDecelerateWhileMoving"))
	float DecelRetainPerSecond = 0.4f;

	/** 감속 하한 속도 (cm/s) — 이 이하로는 안 느려져서 끝까지 낙하·도달 보장(허공 정지 방지). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "StasisSalvoOrb|감속",
		meta = (DisplayName = "최소 속도 (cm/s)", ClampMin = "20.0", ClampMax = "2000.0",
			EditCondition = "bDecelerateWhileMoving"))
	float MinMoveSpeed = 150.f;

	/** [GroundDecelV1] 이 높이(지면 위 cm) 아래로 내려와야 감속 시작 — 그 위에선 Speed 유지(빠름).
	 *  지면(BurstHeightAboveGround)에 가까워질수록 MinMoveSpeed 로 선형 감속 → "위에선 빠르고 바닥서만 느려짐". */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "StasisSalvoOrb|감속",
		meta = (DisplayName = "감속 시작 높이 (지면 위 cm)", ClampMin = "50.0", ClampMax = "3000.0",
			EditCondition = "bDecelerateWhileMoving"))
	float DecelStartHeightAboveGround = 200.f;

	/** [GroundHoverV1] 지면(BurstHeightAboveGround) 도달 시 완전히 멈춘 뒤 이만큼(초) 정지해 있다가 Burst (0 이면 즉시 Burst). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "StasisSalvoOrb|감속",
		meta = (DisplayName = "Burst 전 정지 시간 (초)", ClampMin = "0.0", ClampMax = "3.0"))
	float HoverBeforeBurstSeconds = 1.0f;

	/** [GroundDecelV1] 감속 곡선 지수 (1=선형, >1=감속 시작 후 더 급격히 느려짐 = "확 브레이크"). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "StasisSalvoOrb|감속",
		meta = (DisplayName = "감속 곡선 지수(>1=급감속)", ClampMin = "1.0", ClampMax = "6.0",
			EditCondition = "bDecelerateWhileMoving"))
	float DecelExponent = 2.5f;

	/** 이 거리 이상 비행하면 충돌이 없어도 강제 Burst (벽 등에 안 닿고 허공으로 날아갈 때 cap). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "StasisSalvoOrb",
		meta = (DisplayName = "최대 비행 거리 (cm)", ClampMin = "100.0", ClampMax = "20000.0"))
	float MaxTravelDistance = 6000.f;

	/** 가장 가까운 플레이어에 이 거리 이내로 접근하면 Burst — 존이 플레이어 근처에서 "피어나게" 하기 위함.
	 *  ("총알이 플레이어한테 도착해서 존으로 커지는" 느낌 — burst 위치 ≈ 플레이어 위치) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "StasisSalvoOrb",
		meta = (DisplayName = "플레이어 근접 Burst 거리 (cm)", ClampMin = "0.0", ClampMax = "2000.0"))
	float BurstProximityToPlayer = 280.f;

	/** [BurstHeightV1] 지면 위 이 높이(cm) 이내로 내려오면 Burst — 바닥까지 안 가고 공중에서 터지게(하늘 낙하용).
	 *  0 이면 비활성(충돌/근접/최대거리로만 burst). 예: 90 ≈ 플레이어 키 절반 높이. 매 틱 아래로 짧은 트레이스. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "StasisSalvoOrb",
		meta = (DisplayName = "지면 위 Burst 높이 (cm, 0=off)", ClampMin = "0.0", ClampMax = "1000.0"))
	float BurstHeightAboveGround = 0.f;

	/** 안전망 — 이 시간이 지나면 거리/근접 무관 강제 Burst (구체가 영원히 안 사라지는 것 방지). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "StasisSalvoOrb",
		meta = (DisplayName = "최대 수명 (초)", ClampMin = "0.2", ClampMax = "10.0"))
	float MaxLifeSeconds = 3.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "StasisSalvoOrb",
		meta = (DisplayName = "충돌 구체 반경 (cm)", ClampMin = "5.0", ClampMax = "200.0"))
	float CollisionRadius = 35.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "StasisSalvoOrb",
		meta = (DisplayName = "비행 트레일 VFX (선택)"))
	TObjectPtr<UNiagaraSystem> TrailVFX = nullptr;

	/** 구체 메시 — 존의 ZoneVisualMesh(SM_Shield_Octagon_01) 를 그대로 쓰면 "축소한 돔" 모양. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "StasisSalvoOrb",
		meta = (DisplayName = "구체 메시 (선택)"))
	TObjectPtr<UStaticMesh> OrbMesh = nullptr;

	/** 구체 메시 머티리얼 (slot 0 override) — 존의 ZoneVisualMaterial 를 그대로 쓰면 색감 일치. None 이면 메시 기본 머티리얼. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "StasisSalvoOrb",
		meta = (DisplayName = "구체 메시 머티리얼 (선택)"))
	TObjectPtr<UMaterialInterface> OrbMeshMaterial = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "StasisSalvoOrb",
		meta = (DisplayName = "구체 메시 크기 배율", ClampMin = "0.001", ClampMax = "10.0"))
	float OrbMeshScale = 0.5f;

	// =========================================================
	// [OrbColorMatchV1] 구체 색을 존 돔 색과 맞추기 — 같은 메시/머티리얼이라 Base_Color 만 맞추면 동일
	// =========================================================

	/** true 면 메시 머티리얼에 MID 로 OrbBaseColor 를 입혀 존 돔(DomeBaseColor)과 색감 통일. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "StasisSalvoOrb|색감",
		meta = (DisplayName = "구체 색 직접 지정"))
	bool bApplyOrbBaseColor = false;

	/** 구체 색 (HDR). 존의 DomeBaseColor 와 같은 값을 넣으면 색감 일치 (기본=RF 돔 시안-블루). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "StasisSalvoOrb|색감",
		meta = (DisplayName = "구체 색 (HDR)", EditCondition = "bApplyOrbBaseColor"))
	FLinearColor OrbBaseColor = FLinearColor(0.5f, 1.5f, 2.0f, 1.f);

	/** 메시 머티리얼의 색 파라미터 이름 (M_Master_Shield_Octagon_01 = "Base_Color"). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "StasisSalvoOrb|색감",
		meta = (DisplayName = "메시 색 파라미터명", EditCondition = "bApplyOrbBaseColor"))
	FName OrbBaseColorParamName = FName(TEXT("Base_Color"));

	/** 트레일 Niagara 의 색 user 파라미터 이름 (있으면 OrbBaseColor 로 tint). 비우면 트레일 색 미변경. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "StasisSalvoOrb|색감",
		meta = (DisplayName = "트레일 색 파라미터명(선택)", EditCondition = "bApplyOrbBaseColor"))
	FName OrbTrailColorParamName = NAME_None;

	/** [ChargeV1] 지면 도달 후 Burst 전 정지(hover) 동안 재생할 "차징" VFX (선택) — 터지기 직전 모이는 느낌. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "StasisSalvoOrb",
		meta = (DisplayName = "Burst 전 차징 VFX (선택)"))
	TObjectPtr<UNiagaraSystem> ChargeVFX = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "StasisSalvoOrb",
		meta = (DisplayName = "차징 VFX 크기 배율", ClampMin = "0.01", ClampMax = "20.0"))
	float ChargeVFXScale = 1.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "StasisSalvoOrb",
		meta = (DisplayName = "Burst 원형 VFX (선택)"))
	TObjectPtr<UNiagaraSystem> BurstVFX = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "StasisSalvoOrb",
		meta = (DisplayName = "Burst VFX 크기 배율", ClampMin = "0.01", ClampMax = "20.0"))
	float BurstVFXScale = 1.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "StasisSalvoOrb",
		meta = (DisplayName = "Burst 사운드 (선택)"))
	TObjectPtr<USoundBase> BurstSound = nullptr;

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION()
	void OnSphereBeginOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& Sweep);

private:
	/** Burst — 1회만. 멀티캐스트 VFX/사운드 + 서버: TargetZone->ActivateZone() + self destroy. */
	void Burst();

	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_PlayBurst(FVector BurstLocation);

	/** [ChargeV1] 지면 도달 후 hover 동안 차징 VFX 재생 (전 머신). */
	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_PlayCharge(FVector ChargeLocation);

	UPROPERTY(VisibleAnywhere, Category = "StasisSalvoOrb")
	TObjectPtr<USphereComponent> CollisionSphere = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "StasisSalvoOrb")
	TObjectPtr<UProjectileMovementComponent> MoveComp = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "StasisSalvoOrb")
	TObjectPtr<UNiagaraComponent> TrailComp = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "StasisSalvoOrb")
	TObjectPtr<UStaticMeshComponent> MeshComp = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<AHellunaEnemyCharacter> OwnerEnemy = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<ABossPatternZoneBase> TargetZone = nullptr;

	FVector StartLocation = FVector::ZeroVector;
	bool bBursted = false;

	/** [GroundHoverV1] 지면 도달 후 Burst 직전 정지(hover) 중 플래그 — Tick early-return + HoverBurstTimer 가 Burst 호출. */
	bool bHovering = false;
	FTimerHandle HoverBurstTimer;
};
