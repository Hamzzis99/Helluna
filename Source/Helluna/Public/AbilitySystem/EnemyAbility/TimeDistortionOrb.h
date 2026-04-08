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
 * 플레이어가 공격하면 파괴되며, 특수 Orb(색이 다른 1개)를 파괴하면
 * 시간 왜곡 패턴이 파훼된다.
 *
 * - SphereComponent(ECC_Pawn) → 플레이어 근접/원거리 공격 트레이스에 감지
 * - TakeDamage() → 피격 시 파괴 + VFX + 델리게이트 발동
 *
 * [네트워크]
 * - bReplicates = true → 클라이언트에 액터 자동 복제
 * - OrbVFXSystem/OrbVFXScale → ReplicatedUsing=OnRep → 클라이언트에서 VFX 스폰
 * - DestroyVFX → Multicast RPC로 모든 클라이언트에서 파괴 이펙트 재생
 */
UCLASS()
class HELLUNA_API ATimeDistortionOrb : public AActor
{
	GENERATED_BODY()

public:
	ATimeDistortionOrb();

	// =========================================================
	// 델리게이트
	// =========================================================

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnOrbDestroyed, ATimeDistortionOrb*, DestroyedOrb);

	/** Orb가 플레이어 공격으로 파괴될 때 브로드캐스트 */
	UPROPERTY(BlueprintAssignable)
	FOnOrbDestroyed OnOrbDestroyed;

	// =========================================================
	// 설정
	// =========================================================

	/** 이 Orb가 파훼 키(색이 다른 특수 Orb)인지 여부 */
	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "TimeDistortion|Orb")
	bool bIsKeyOrb = false;

	/** Orb 기본 VFX 컴포넌트 */
	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> OrbVFXComp = nullptr;

	// =========================================================
	// 초기화
	// =========================================================

	/**
	 * 어빌리티에서 스폰 후 호출하여 VFX 및 키 여부를 설정한다.
	 * 서버에서 호출 → Replicated 프로퍼티가 클라이언트에 복제되어 OnRep에서 VFX 스폰.
	 * @param InVFX         - Orb에 표시할 나이아가라 시스템
	 * @param InVFXScale    - VFX 크기 배율
	 * @param bInIsKeyOrb   - true이면 파훼 키 Orb
	 */
	void InitOrb(UNiagaraSystem* InVFX, float InVFXScale, bool bInIsKeyOrb);

	/** Orb 파괴 시 재생할 VFX (외부에서 세팅) */
	UPROPERTY(Transient)
	TObjectPtr<UNiagaraSystem> DestroyVFX = nullptr;

	/** 파괴 VFX 크기 배율 */
	float DestroyVFXScale = 1.f;

	// =========================================================
	// 데미지 수신
	// =========================================================

	virtual float TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent,
		AController* EventInstigator, AActor* DamageCauser) override;

protected:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	UPROPERTY(VisibleAnywhere, Category = "TimeDistortion|Orb")
	TObjectPtr<USphereComponent> CollisionSphere = nullptr;

	// =========================================================
	// VFX 복제용 프로퍼티
	// =========================================================

	/** 서버에서 설정 → 클라이언트에 복제 → OnRep에서 VFX 스폰 */
	UPROPERTY(ReplicatedUsing = OnRep_OrbVFXData)
	TObjectPtr<UNiagaraSystem> RepOrbVFXSystem = nullptr;

	UPROPERTY(Replicated)
	float RepOrbVFXScale = 1.f;

	UFUNCTION()
	void OnRep_OrbVFXData();

	/** VFX 스폰 공통 로직 (서버/클라이언트 양쪽) */
	void SpawnOrbVFX();

	/** 파괴 VFX 멀티캐스트 */
	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_PlayDestroyVFX(UNiagaraSystem* Effect, float Scale, FVector Location);
};
