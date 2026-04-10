// Capstone Project Helluna

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TimeDistortionOrb.generated.h"

class UNiagaraSystem;
class UNiagaraComponent;
class USphereComponent;

/**
 * ATimeDistortionOrb
 *
 * 시간 왜곡 패턴 중 스폰되는 파괴 가능한 구체 액터.
 * 플레이어가 공격하면 파괴되며, 키 Orb를 파괴하면 시간 왜곡 패턴이 파훼된다.
 *
 * BP에서 상속하여 VFX 에셋과 충돌 반경을 직접 설정한다.
 * 에디터 뷰포트에서 콜리전 구체와 VFX를 함께 확인 가능.
 *
 * [네트워크]
 * - bReplicates = true
 * - bIsKeyOrb → Replicated
 * - DestroyVFX → Multicast RPC로 모든 클라이언트에서 재생
 */
UCLASS(Blueprintable)
class HELLUNA_API ATimeDistortionOrb : public AActor
{
	GENERATED_BODY()

public:
	ATimeDistortionOrb();

	// =========================================================
	// 델리게이트
	// =========================================================

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnOrbDestroyed, ATimeDistortionOrb*, DestroyedOrb);

	UPROPERTY(BlueprintAssignable)
	FOnOrbDestroyed OnOrbDestroyed;

	// =========================================================
	// 설정 (BP 에디터에서 세팅)
	// =========================================================

	/** 이 Orb가 파훼 키(특수 Orb)인지 여부 — Zone에서 스폰 시 설정 */
	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Orb")
	bool bIsKeyOrb = false;

	/** 충돌 감지 반경 (cm) — 에디터에서 구체 크기로 표시됨 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Orb|충돌",
		meta = (DisplayName = "충돌 반경 (cm)", ClampMin = "10.0", ClampMax = "300.0"))
	float OrbCollisionRadius = 80.f;

	/** Orb 기본 VFX (비행 중 지속 표시) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Orb|VFX",
		meta = (DisplayName = "Orb VFX"))
	TObjectPtr<UNiagaraSystem> OrbVFX = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Orb|VFX",
		meta = (DisplayName = "Orb VFX 크기", ClampMin = "0.01", ClampMax = "10.0"))
	float OrbVFXScale = 1.f;

	/** Orb 파괴 시 VFX */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Orb|VFX",
		meta = (DisplayName = "파괴 VFX"))
	TObjectPtr<UNiagaraSystem> DestroyVFX = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Orb|VFX",
		meta = (DisplayName = "파괴 VFX 크기", ClampMin = "0.01", ClampMax = "10.0"))
	float DestroyVFXScale = 1.f;

	// =========================================================
	// 초기화
	// =========================================================

	/** Zone에서 스폰 후 호출 — 키 여부만 설정 */
	void InitOrb(bool bInIsKeyOrb);

	// =========================================================
	// 데미지 수신
	// =========================================================

	virtual float TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent,
		AController* EventInstigator, AActor* DamageCauser) override;

protected:
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	UPROPERTY(VisibleAnywhere, Category = "Orb")
	TObjectPtr<USphereComponent> CollisionSphere = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> OrbVFXComp = nullptr;

	/** VFX 스폰 공통 로직 */
	void SpawnOrbVFX();

	/** 파괴 VFX 멀티캐스트 */
	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_PlayDestroyVFX(UNiagaraSystem* Effect, float Scale, FVector Location);
};
