// Capstone Project Helluna

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HellunaProjectile_Launcher.generated.h"

class UBoxComponent;
class UNiagaraComponent;
class UNiagaraSystem;
class UProjectileMovementComponent;

UCLASS()
class HELLUNA_API AHellunaProjectile_Launcher : public AActor
{
	GENERATED_BODY()

public:
	AHellunaProjectile_Launcher();

	// - 데미지는 서버에서만 적용
	// - FX는 모든 클라에 브로드캐스트
	void InitProjectile(
		float InDamage,
		float InRadius,
		const FVector& InVelocity,
		float InLifeSeconds
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

	void Explode(const FVector& ExplosionLocation);

	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_SpawnExplosionFX(FVector_NetQuantize ExplosionLocation);

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Projectile|Components",
		meta = (DisplayName = "충돌(박스)"))
	TObjectPtr<UBoxComponent> CollisionBox;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Projectile|Components",
		meta = (DisplayName = "이동(ProjectileMovement)"))
	TObjectPtr<UProjectileMovementComponent> MoveComp;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Projectile|Components",
		meta = (DisplayName = "비행 FX(나이아가라)"))
	TObjectPtr<UNiagaraComponent> TrailFX;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Projectile|Tuning",
		meta = (DisplayName = "오버랩 박스 크기"))
	FVector OverlapBoxExtent = FVector(8.f, 8.f, 8.f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Projectile|FX",
		meta = (DisplayName = "폭발 FX(나이아가라)"))
	TObjectPtr<UNiagaraSystem> ExplosionFX = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Projectile|FX",
		meta = (DisplayName = "폭발 FX 스케일"))
	FVector ExplosionFXScale = FVector(1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projectile|FX", meta = (DisplayName = "디버그 표시"))
	bool bDebugDrawRadialDamage = false;

private:
	// 서버에서만 사용(복제 X)
	float Damage = 0.f;
	float Radius = 0.f;

	bool bExploded = false;
};