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

	/** 이 거리 이상 비행하면 충돌이 없어도 강제 Burst (벽 등에 안 닿고 허공으로 날아갈 때 cap). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "StasisSalvoOrb",
		meta = (DisplayName = "최대 비행 거리 (cm)", ClampMin = "100.0", ClampMax = "20000.0"))
	float MaxTravelDistance = 6000.f;

	/** 가장 가까운 플레이어에 이 거리 이내로 접근하면 Burst — 존이 플레이어 근처에서 "피어나게" 하기 위함.
	 *  ("총알이 플레이어한테 도착해서 존으로 커지는" 느낌 — burst 위치 ≈ 플레이어 위치) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "StasisSalvoOrb",
		meta = (DisplayName = "플레이어 근접 Burst 거리 (cm)", ClampMin = "0.0", ClampMax = "2000.0"))
	float BurstProximityToPlayer = 280.f;

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
};
