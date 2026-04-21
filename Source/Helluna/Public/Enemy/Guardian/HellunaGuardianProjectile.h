// File: Source/Helluna/Public/Enemy/Guardian/HellunaGuardianProjectile.h
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/EngineTypes.h"
#include "HellunaGuardianProjectile.generated.h"

class UBoxComponent;
class UNiagaraComponent;
class UNiagaraSystem;
class UProjectileMovementComponent;

/**
 * 가디언이 발사하는 투사체.
 *
 * BP_HeroWeapon_Launcher 의 AHellunaProjectile_Launcher 패턴을 따르지만,
 * 가디언용 차이점:
 *  - Hero / Enemy 구분 없이 모든 대상에게 피해 (가디언은 만인의 적)
 *  - DamagePreventionChannel 로 벽 차폐 (얇은 벽 뒤는 안전)
 *  - HitNormal 로 폭발 VFX 를 벽면에 수직으로 정렬
 *  - 블루프린트에서 속도/수명/충돌 박스/VFX 모두 튜닝 가능
 */
UCLASS()
class HELLUNA_API AHellunaGuardianProjectile : public AActor
{
	GENERATED_BODY()

public:
	AHellunaGuardianProjectile();

	/**
	 * 서버에서 발사 직후 호출.
	 * @param InDamage       중심 데미지
	 * @param InRadius       폭발 반경 (UU). 0 이면 폭발 없이 직격 데미지만.
	 * @param bInFalloff     true=중심→반경끝 선형감쇠, false=균일 데미지
	 * @param InVelocity     발사 속도 (방향 * 스피드)
	 * @param InLifeSeconds  수명 (초). 만료 시 현재 위치에서 폭발
	 */
	void InitProjectile(
		float InDamage,
		float InRadius,
		bool bInFalloff,
		const FVector& InVelocity,
		float InLifeSeconds);

protected:
	virtual void BeginPlay() override;
	virtual void LifeSpanExpired() override;

	UFUNCTION()
	void OnBeginOverlap(
		UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
		bool bFromSweep, const FHitResult& SweepResult);

	/** 서버 전용: 폭발 처리 (데미지 + 멀티캐스트 VFX) */
	void Explode(const FVector& ExplosionLocation, const FVector& SurfaceNormal);

	/** 모든 클라에 폭발 VFX 재생 (서버 → 클라). 1회 이벤트라 Reliable — 패킷 유실 시에도 폭발 VFX 보장. */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_SpawnExplosionFX(FVector_NetQuantize ExplosionLocation, FVector_NetQuantizeNormal SurfaceNormal);

	void SetProjectileCollisionEnabled(bool bEnabled);

protected:
	// =========================================================
	// 컴포넌트
	// =========================================================

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Projectile|Components",
		meta = (DisplayName = "충돌 (박스)"))
	TObjectPtr<UBoxComponent> CollisionBox;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Projectile|Components",
		meta = (DisplayName = "이동 컴포넌트"))
	TObjectPtr<UProjectileMovementComponent> MoveComp;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Projectile|Components",
		meta = (DisplayName = "비행 트레일 FX"))
	TObjectPtr<UNiagaraComponent> TrailFX;

	// =========================================================
	// 튜닝 (BP 에서 조정)
	// =========================================================

	/** 충돌 박스 크기 (UU). 큰 메시일 경우 키우면 놓침 방지. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Projectile|Tuning",
		meta = (DisplayName = "충돌 박스 크기"))
	FVector OverlapBoxExtent = FVector(16.f, 16.f, 16.f);

	/** 벽 차폐 무시 (true 면 벽 뒤 대상도 피해 받음) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projectile|Tuning",
		meta = (DisplayName = "벽 차폐 무시"))
	bool bIgnoreWorldStatic = false;

	/** 폭발 차폐 검사용 채널 (ApplyRadialDamage 의 DamagePreventionChannel) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projectile|Tuning",
		meta = (DisplayName = "차폐 트레이스 채널"))
	TEnumAsByte<ECollisionChannel> BlockTraceChannel = ECC_Visibility;

	// =========================================================
	// FX (BP 에서 할당)
	// =========================================================

	/** 폭발 시 스폰되는 Niagara — BP 에서 NS_HIt_Explosion 등 할당 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Projectile|FX",
		meta = (DisplayName = "폭발 FX"))
	TObjectPtr<UNiagaraSystem> ExplosionFX = nullptr;

	/** 폭발 FX 스케일 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Projectile|FX",
		meta = (DisplayName = "폭발 FX 스케일"))
	FVector ExplosionFXScale = FVector(1.f);

	/** 폭발 FX 의 Float User Parameter 이름 (반경 주입용). 없으면 NAME_None. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projectile|FX",
		meta = (DisplayName = "폭발 반경 파라미터명 (Float)"))
	FName ExplosionFXRadiusParamName = NAME_None;

	/** true: 폭발 FX 를 충돌면 법선 방향으로 정렬 (벽 스플랫용). false: 월드 기본(Z-up) 재생. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projectile|FX",
		meta = (DisplayName = "폭발 FX 를 충돌면에 정렬"))
	bool bAlignExplosionToSurface = false;

	// =========================================================
	// 디버그
	// =========================================================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projectile|Debug",
		meta = (DisplayName = "디버그: 폭발 구 표시"))
	bool bDebugDrawRadialDamage = false;

private:
	// 서버 전용 (복제 X)
	float Damage = 0.f;
	float Radius = 0.f;
	bool  bFalloff = true;

	bool bExploded = false;
	bool bInitialized = false;
};
