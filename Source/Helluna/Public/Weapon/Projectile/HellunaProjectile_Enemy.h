// Capstone Project Helluna

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HellunaProjectile_Enemy.generated.h"

class USphereComponent;
class UNiagaraComponent;
class UNiagaraSystem;
class UProjectileMovementComponent;
class AHellunaEnemyCharacter; // 충돌 시 HitNiagaraEffect 재생에 사용

/**
 * 원거리 에너미가 발사하는 투사체.
 *
 * 런처 투사체(HellunaProjectile_Launcher)와의 차이점:
 *  - 범위 폭발(ApplyRadialDamage) 대신 단일 직격 데미지(ApplyDamage)
 *  - 충돌 박스 대신 구체(SphereComponent) 사용
 *  - 충돌 시 에너미의 HitNiagaraEffect 를 맞은 위치에서 재생
 *
 * 네트워크:
 *  - bReplicates = true, 이동은 SetReplicateMovement 로 동기화
 *  - 데미지 판정은 서버에서만 처리
 *  - 충돌 FX 는 Multicast_SpawnHitFX 로 모든 클라이언트에 전파
 *
 * @author 김민우
 */
UCLASS()
class HELLUNA_API AHellunaProjectile_Enemy : public AActor
{
	GENERATED_BODY()

public:
	AHellunaProjectile_Enemy();

	/**
	 * 발사 직후 서버에서 호출.
	 *
	 * @param InDamage        직격 데미지
	 * @param InDirection     발사 방향 벡터 (내부에서 정규화)
	 * @param InSpeed         투사체 속도 (cm/s)
	 * @param InLifeSeconds   수명 (초)
	 * @param InOwnerEnemy    발사한 에너미 — 충돌 시 HitNiagaraEffect 재생에 사용 (선택)
	 */
	void InitProjectile(
		float InDamage,
		const FVector& InDirection,
		float InSpeed,
		float InLifeSeconds,
		AHellunaEnemyCharacter* InOwnerEnemy = nullptr
	);

protected:
	virtual void BeginPlay() override;
	virtual void LifeSpanExpired() override;

	UFUNCTION()
	void OnBeginOverlap(
		UPrimitiveComponent* OverlappedComp,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex,
		bool bFromSweep,
		const FHitResult& SweepResult
	);

	/** 충돌 처리 — 데미지 적용 + FX 재생 (서버에서만 호출) */
	void HitTarget(AActor* HitActor, const FVector& HitLocation);

	/** 충돌 FX 멀티캐스트 */
	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_SpawnHitFX(FVector_NetQuantize HitLocation);

	// =========================================================
	// 컴포넌트
	// =========================================================

	/** 충돌 감지용 구체 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Projectile|Components",
		meta = (DisplayName = "충돌(구체)"))
	TObjectPtr<USphereComponent> CollisionSphere;

	/** 투사체 이동 컴포넌트 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Projectile|Components",
		meta = (DisplayName = "이동(ProjectileMovement)"))
	TObjectPtr<UProjectileMovementComponent> MoveComp;

	/** 비행 중 트레일 FX */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Projectile|Components",
		meta = (DisplayName = "비행 트레일 FX"))
	TObjectPtr<UNiagaraComponent> TrailFX;

	// =========================================================
	// 튜닝
	// =========================================================

	/** 충돌 구체 반경 (cm) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Projectile|Tuning",
		meta = (DisplayName = "충돌 반경 (cm)", ClampMin = "1.0", ClampMax = "100.0"))
	float CollisionRadius = 15.f;

	// =========================================================
	// FX
	// =========================================================

	/**
	 * 투사체 자체에 설정하는 충돌 FX.
	 * 에너미의 HitNiagaraEffect 와 별개로 추가 재생된다.
	 * 둘 다 설정하면 두 이펙트가 모두 재생되고,
	 * 하나만 설정해도 정상 동작한다.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Projectile|FX",
		meta = (DisplayName = "충돌 FX (투사체 자체)"))
	TObjectPtr<UNiagaraSystem> HitFX = nullptr;

	/** 충돌 FX 스케일 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Projectile|FX",
		meta = (DisplayName = "충돌 FX 스케일"))
	FVector HitFXScale = FVector(1.f);

	/** 디버그 시각화 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projectile|Debug",
		meta = (DisplayName = "디버그 표시"))
	bool bDebugDraw = false;

private:
	// 서버 전용 (복제 X)
	float Damage = 0.f;
	bool  bHit   = false;

	/**
	 * 발사한 에너미 참조.
	 * 충돌 시 Multicast_SpawnHitFX 안에서 Enemy->HitNiagaraEffect 를
	 * 맞은 위치에 재생하기 위해 저장한다.
	 * TWeakObjectPtr 사용 — 에너미가 먼저 죽어도 크래시 없음.
	 */
	UPROPERTY()
	TWeakObjectPtr<AHellunaEnemyCharacter> OwnerEnemy;
};
