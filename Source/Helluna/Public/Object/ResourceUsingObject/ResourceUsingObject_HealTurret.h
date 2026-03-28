// File: Source/Helluna/Public/Object/ResourceUsingObject/ResourceUsingObject_HealTurret.h
#pragma once

#include "CoreMinimal.h"
#include "Object/ResourceUsingObject/HellunaBaseResourceUsingObject.h"
#include "ResourceUsingObject_HealTurret.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class UNiagaraComponent;
class UNiagaraSystem;
class AHellunaHeroCharacter;

/**
 * 범위 내 아군 플레이어를 주기적으로 회복하는 포탑.
 * 서버 권위 기반으로 힐을 적용하며, 힐 시 Niagara VFX를 멀티캐스트로 재생한다.
 */
UCLASS()
class HELLUNA_API AResourceUsingObject_HealTurret : public AHellunaBaseResourceUsingObject
{
	GENERATED_BODY()

public:
	AResourceUsingObject_HealTurret();

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// =========================================================
	// 힐 범위
	// =========================================================

	/** 힐 범위 탐지용 구체 콜리전 — 디테일 패널에서 반경을 직접 설정 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Turret|Detection")
	TObjectPtr<USphereComponent> HealRangeSphere;

	// =========================================================
	// 메쉬 파트 (4분할)
	// =========================================================

	/** 파트2 메쉬 (회전) — SpinRoot 하위, BP에서 스태틱메쉬 지정 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Turret|Components",
		meta = (DisplayName = "파트2 메쉬 (회전)"))
	TObjectPtr<UStaticMeshComponent> MeshPart2;

	/** 파트3 메쉬 (회전) — SpinRoot 하위, BP에서 스태틱메쉬 지정 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Turret|Components",
		meta = (DisplayName = "파트3 메쉬 (회전)"))
	TObjectPtr<UStaticMeshComponent> MeshPart3;

	/** 회전 피벗 — 이 하위의 메쉬가 빙글빙글 회전합니다 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Turret|Components",
		meta = (DisplayName = "회전 피벗"))
	TObjectPtr<USceneComponent> SpinRoot;

	/** 회전 파트 메쉬 — SpinRoot 하위에서 회전 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Turret|Components",
		meta = (DisplayName = "회전 파트 메쉬"))
	TObjectPtr<UStaticMeshComponent> MeshSpin;

	// =========================================================
	// 회전 설정
	// =========================================================

	/** 회전 속도 (도/초) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Turret|Spin",
		meta = (DisplayName = "회전 속도 (도/초)", ClampMin = "0.0", ClampMax = "720.0"))
	float SpinSpeed = 90.f;

	// =========================================================
	// 힐 설정
	// =========================================================

	/** 힐 간격 (초) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Turret|Heal",
		meta = (DisplayName = "힐 간격(초)", ClampMin = "0.5", ClampMax = "30.0"))
	float HealInterval = 3.0f;

	/** 틱당 힐량 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Turret|Heal",
		meta = (DisplayName = "틱당 힐량", ClampMin = "1.0"))
	float HealAmount = 15.f;

	// =========================================================
	// VFX
	// =========================================================

	/** 힐 시 플레이어 위치에 재생할 나이아가라 이펙트 (에디터에서 설정) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Turret|VFX",
		meta = (DisplayName = "힐 이펙트"))
	TObjectPtr<UNiagaraSystem> HealNiagaraEffect = nullptr;

	/** 힐 이펙트 크기 배율 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Turret|VFX",
		meta = (DisplayName = "힐 이펙트 크기", ClampMin = "0.01", ClampMax = "10.0"))
	float HealEffectScale = 1.f;

	/** 힐 시 재생할 사운드 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Turret|VFX",
		meta = (DisplayName = "힐 사운드"))
	TObjectPtr<USoundBase> HealSound = nullptr;

	// =========================================================
	// 범위 표시 VFX
	// =========================================================

	/** 터렛 주변 힐 범위를 표시하는 나이아가라 컴포넌트 — 에디터에서 에셋·크기 직접 조절 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Turret|RangeVFX",
		meta = (DisplayName = "범위 표시 컴포넌트"))
	TObjectPtr<UNiagaraComponent> RangeIndicatorComponent;

	// =========================================================
	// 디버그
	// =========================================================

	/** 체력이 풀일 때도 힐 이펙트를 재생할지 여부 (디버그/테스트용) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Turret|Debug",
		meta = (DisplayName = "풀피일 때도 VFX 재생"))
	bool bPlayVFXWhenFull = false;

private:
	/** 힐 타이머 핸들 */
	FTimerHandle HealTimerHandle;

	// =========================================================
	// 힐 로직 (서버)
	// =========================================================

	/** 타이머에서 호출 — 범위 내 모든 플레이어 캐릭터에게 힐 적용 */
	void PerformHeal();

	/** 멀티캐스트 RPC: 힐 VFX 재생 — 캐릭터에 Attach되어 이동을 따라감 */
	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_PlayHealEffect(const TArray<AHellunaHeroCharacter*>& HealedTargets);

};
