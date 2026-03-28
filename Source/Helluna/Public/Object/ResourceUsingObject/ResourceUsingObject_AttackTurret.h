// File: Source/Helluna/Public/Object/ResourceUsingObject/ResourceUsingObject_AttackTurret.h
#pragma once

#include "CoreMinimal.h"
#include "Object/ResourceUsingObject/HellunaBaseResourceUsingObject.h"
#include "ResourceUsingObject_AttackTurret.generated.h"

class AHellunaEnemyCharacter;
class USphereComponent;
class UNiagaraSystem;
class USoundBase;

/**
 * 적을 자동으로 공격하는 포탑.
 * 탐지 구체 오버랩 이벤트로 범위 내 적을 추적하고,
 * 공격 주기마다 가장 가까운 적에게 데미지를 적용한다.
 */
UCLASS()
class HELLUNA_API AResourceUsingObject_AttackTurret : public AHellunaBaseResourceUsingObject
{
	GENERATED_BODY()

public:
	AResourceUsingObject_AttackTurret();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaTime) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// =========================================================
	// 컴포넌트
	// =========================================================

	/** 터렛 루트 — 회전 기준점. DynamicMesh는 이 하위에서 방향 오프셋 조절 가능 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Turret|Components",
		meta = (DisplayName = "터렛 루트"))
	TObjectPtr<USceneComponent> TurretRoot;

	/** 적 탐지용 구체 콜리전 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Turret|Components",
		meta = (DisplayName = "탐지 구체"))
	TObjectPtr<USphereComponent> DetectionSphere;

	/** 총구 위치 — 데미지 시작점. BP에서 위치 조정 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Turret|Components",
		meta = (DisplayName = "총구 위치"))
	TObjectPtr<USceneComponent> MuzzlePoint;

	// =========================================================
	// 공격 설정
	// =========================================================

	/** 공격 간격 (초) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Turret|Attack",
		meta = (DisplayName = "공격 간격(초)", ClampMin = "0.1", ClampMax = "10.0"))
	float AttackInterval = 1.0f;

	/** 공격당 데미지 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Turret|Attack",
		meta = (DisplayName = "공격 데미지", ClampMin = "1.0"))
	float AttackDamage = 25.f;

	/** 라인트레이스 채널 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Turret|Attack",
		meta = (DisplayName = "트레이스 채널"))
	TEnumAsByte<ECollisionChannel> TraceChannel = ECC_Visibility;

	/** 공격 후 포탑이 회전을 멈추는 시간 (초). 이 시간이 지나면 다음 타겟을 추적 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Turret|Attack",
		meta = (DisplayName = "공격 후 정지 시간(초)", ClampMin = "0.0", ClampMax = "2.0"))
	float PostAttackPause = 0.2f;

	/** 포탑 회전 속도 (도/초) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Turret|Attack",
		meta = (DisplayName = "회전 속도(도/초)", ClampMin = "10.0"))
	float TurretRotationSpeed = 180.f;

	/** 타겟을 향해 이 각도(도) 이내면 발사 가능 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Turret|Attack",
		meta = (DisplayName = "발사 허용 각도(도)", ClampMin = "1.0", ClampMax = "45.0"))
	float FireAngleThreshold = 5.f;

	/** 탐지 범위를 게임 화면에 표시 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Turret|Debug",
		meta = (DisplayName = "공격 범위 표시"))
	bool bShowAttackRange = true;


	// =========================================================
	// VFX / 사운드
	// =========================================================

	/** 총구 발사 이펙트 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Turret|FX",
		meta = (DisplayName = "총구 이펙트"))
	TObjectPtr<UNiagaraSystem> MuzzleFlashFX = nullptr;

	/** 총구 이펙트 크기 배율 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Turret|FX",
		meta = (DisplayName = "총구 이펙트 크기", ClampMin = "0.01"))
	FVector MuzzleFXScale = FVector(1.f);

	/** 발사 사운드 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Turret|FX",
		meta = (DisplayName = "발사 사운드"))
	TObjectPtr<USoundBase> FireSound = nullptr;

	/** 피격 이펙트 (타겟 위치) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Turret|FX",
		meta = (DisplayName = "피격 이펙트"))
	TObjectPtr<UNiagaraSystem> ImpactFX = nullptr;

	/** 피격 이펙트 크기 배율 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Turret|FX",
		meta = (DisplayName = "피격 이펙트 크기", ClampMin = "0.01"))
	FVector ImpactFXScale = FVector(1.f);

	/** 피격 사운드 (타겟 위치) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Turret|FX",
		meta = (DisplayName = "피격 사운드"))
	TObjectPtr<USoundBase> ImpactSound = nullptr;

private:
	/** 현재 공격 대상 (서버 → 클라 복제, 회전 보간용) */
	UPROPERTY(ReplicatedUsing = OnRep_CurrentTarget)
	TObjectPtr<AActor> CurrentTarget;

	UFUNCTION()
	void OnRep_CurrentTarget();

	/** 탐지 범위 내 적 목록 (서버 전용) */
	TArray<TWeakObjectPtr<AHellunaEnemyCharacter>> EnemiesInRange;

	/** 공격 후 정지 중 */
	bool bRotationPaused = false;
	FTimerHandle PostAttackPauseTimerHandle;

	/** 마지막 공격 이후 경과 시간 */
	float TimeSinceLastAttack = 0.f;

	void OnPostAttackPauseEnd();

	/** 현재 타겟 방향을 바라보고 있는지 (FireAngleThreshold 이내) */
	bool IsFacingTarget() const;

	// =========================================================
	// 오버랩 콜백
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
	// 공격 로직
	// =========================================================

	/** 타이머에서 호출 — 현재 타겟에게 데미지 적용 */
	void PerformAttack();

	/** 현재 타겟이 유효한지 검사 */
	bool IsTargetValid() const;

	/** 범위 내 가장 가까운 적을 CurrentTarget으로 설정 */
	void SelectClosestTarget();

	/** 포탑을 타겟 방향으로 보간 회전 */
	void UpdateTurretRotation(float DeltaTime);

	// =========================================================
	// 타겟 사망 델리게이트
	// =========================================================

	void BindTargetDeathDelegate(AHellunaEnemyCharacter* Enemy);
	void UnbindTargetDeathDelegate(AHellunaEnemyCharacter* Enemy);

	UFUNCTION()
	void OnTargetDeath(AActor* DeadActor, AActor* KillerActor);

	/** 멀티캐스트 RPC: 발사/피격 FX·사운드 재생 */
	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_PlayFireFX(FVector_NetQuantize MuzzleLocation, bool bHit, FVector_NetQuantize HitLocation);
};
