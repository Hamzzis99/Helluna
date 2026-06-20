// File: Source/Helluna/Public/Object/ResourceUsingObject/HellunaTurretBase.h
#pragma once

#include "CoreMinimal.h"
#include "Object/ResourceUsingObject/HellunaBaseResourceUsingObject.h"
#include "HellunaTurretBase.generated.h"

// 전방 선언
class UHellunaHealthComponent;
class UNiagaraSystem;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UMeshComponent;

/** 포탑 파괴(HP 0) 시 브로드캐스트 — BP 추가 연출/스코어 바인딩용 (서버에서만 발화, KillerActor 전달) */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnHellunaTurretDestroyed, AActor*, KillerActor);

/**
 * 모든 포탑류(공격/회복 등)의 공통 부모.
 * 적 AI 어그로 스캔이 이 타입 하나로 모든 포탑을 일괄 잡도록 하기 위한 마커 베이스.
 *
 * ★ [TurretHP] 전투 내구도(HP) + 가디언식 "망가짐" 사망 연출을 이 베이스가 담당한다.
 *   - 적 근접 공격은 이미 HellunaEnemyCharacter::ServerApplyDamage → ApplyPointDamage 로
 *     AHellunaTurretBase 에 도달 중(STEvaluator_TargetSelector 가 터렛을 어그로 타겟으로 잡음).
 *     지금까지는 터렛에 HealthComponent 가 없어 OnTakeAnyDamage 가 버려졌을 뿐 → 컴포넌트만 붙이면
 *     적 코드 0줄 수정으로 HP 가 깎인다. 캐릭터/적/우주선과 동일한 UHellunaHealthComponent 재사용.
 *   - HP 0 → OnDeath → HandleTurretDeath → Multicast_OnTurretDeathBreak
 *     (BP_HellunaGuardianTurret 과 동일: 메시 물리 분리 + 폭발 FX + 디졸브). 게임 종료(EndGame)와 무관.
 *
 * 기능(공격/회복)은 자식 클래스에서. 자식은 OnTurretDestroyed_StopServerLogic 을 오버라이드해
 * 사망 시 자신의 타이머/타겟을 정리한다.
 */
UCLASS(Abstract)
class HELLUNA_API AHellunaTurretBase : public AHellunaBaseResourceUsingObject
{
	GENERATED_BODY()

public:
	AHellunaTurretBase();

	// =========================================================
	// 체력 조회 (BP)
	// =========================================================

	UFUNCTION(BlueprintPure, Category = "Turret|HP")
	UHellunaHealthComponent* GetTurretHealthComponent() const { return TurretHealthComponent; }

	/** 현재 포탑 HP */
	UFUNCTION(BlueprintPure, Category = "Turret|HP")
	float GetTurretHealth() const;

	/** 포탑 최대 HP */
	UFUNCTION(BlueprintPure, Category = "Turret|HP")
	float GetTurretMaxHealth() const;

	/** 포탑 HP 비율 (0~1) — HP 바 UI 용 */
	UFUNCTION(BlueprintPure, Category = "Turret|HP")
	float GetTurretHealthPercent() const;

	/** 포탑이 파괴(사망)됐는지 */
	UFUNCTION(BlueprintPure, Category = "Turret|HP")
	bool IsTurretDestroyed() const { return bTurretDestroyed; }

	/** 포탑 파괴 시 브로드캐스트 (서버) — BP 추가 연출 바인딩용 */
	UPROPERTY(BlueprintAssignable, Category = "Turret|HP")
	FOnHellunaTurretDestroyed OnTurretDestroyed;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// =========================================================
	// 체력 컴포넌트
	// =========================================================

	/** 포탑 체력 컴포넌트 (캐릭터/적/우주선과 동일 컴포넌트 재사용, 자동 복제) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Turret|HP",
		meta = (DisplayName = "체력 컴포넌트"))
	TObjectPtr<UHellunaHealthComponent> TurretHealthComponent;

	/** 포탑 최대 체력 (디자이너 튜닝). BeginPlay 서버에서 컴포넌트에 적용. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Turret|HP",
		meta = (DisplayName = "포탑 최대 체력", ClampMin = "1.0"))
	float TurretMaxHealth = 300.f;

	// =========================================================
	// 사망 연출 (메시 물리 분리)
	// =========================================================

	/** 사망 시 모든 스태틱 메시를 떼어내고 물리 시뮬 활성화. Static Mesh 에 Simple Collision 이 있어야 함. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Turret|Death",
		meta = (DisplayName = "사망 시 물리 분리"))
	bool bEnableDeathPhysicsBreak = true;

	/** 사망 시 추가되는 방사형 임펄스 크기. 0 이면 떨어지기만 함. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Turret|Death",
		meta = (DisplayName = "사망 임펄스 크기", ClampMin = "0.0"))
	float DeathBreakImpulseStrength = 60000.f;

	/** 사망 임펄스 반경 (UU). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Turret|Death",
		meta = (DisplayName = "사망 임펄스 반경", ClampMin = "1.0"))
	float DeathBreakImpulseRadius = 500.f;

	/** 사망 후 액터 자동 정리까지의 시간 (초). 0 이면 영구 유지. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Turret|Death",
		meta = (DisplayName = "사망 후 액터 유지 시간 (초)", ClampMin = "0.0"))
	float DeathActorLifetimeSeconds = 20.f;

	/** 사망 순간 스폰되는 폭발 Niagara. null/미해결이면 VFX 없이 물리 분리만. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Turret|Death",
		meta = (DisplayName = "사망 폭발 FX"))
	TSoftObjectPtr<UNiagaraSystem> DeathExplosionFX;

	/** 사망 폭발 FX 스케일. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Turret|Death",
		meta = (DisplayName = "사망 폭발 FX 스케일"))
	FVector DeathExplosionFXScale = FVector(4.f, 4.f, 4.f);

	/** 폭발 FX 스폰 위치 Z 오프셋 (UU). 포탑 중앙 높이 보정용. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Turret|Death",
		meta = (DisplayName = "폭발 FX Z 오프셋"))
	float DeathExplosionFXZOffset = 80.f;

	// =========================================================
	// 사망 디졸브 (가디언과 동일 방식)
	// =========================================================

	/** 사망 시 모든 메시를 디졸브 머티리얼로 사라지게 한다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Turret|Death|Dissolve",
		meta = (DisplayName = "사망 디졸브 활성화"))
	bool bEnableDeathDissolve = true;

	/** 사망 디졸브에 사용할 대체 머티리얼. null 이면 현재 머티리얼의 파라미터만 시도. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Turret|Death|Dissolve",
		meta = (DisplayName = "사망 디졸브 머티리얼"))
	TSoftObjectPtr<UMaterialInterface> DeathDissolveOverrideMaterial;

	/** 디졸브 진행 scalar 파라미터. M_Dissolve 계열 + 기존 적 디졸브 파라미터 함께 지원. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Turret|Death|Dissolve",
		meta = (DisplayName = "디졸브 Scalar 파라미터"))
	TArray<FName> DeathDissolveScalarParameterNames;

	/** 디졸브 컬러 vector 파라미터. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Turret|Death|Dissolve",
		meta = (DisplayName = "디졸브 Vector 파라미터"))
	TArray<FName> DeathDissolveVectorParameterNames;

	/** 디졸브 엣지/발광 컬러. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Turret|Death|Dissolve",
		meta = (DisplayName = "디졸브 엣지 컬러"))
	FLinearColor DeathDissolveEdgeColor = FLinearColor(4.5f, 1.35f, 0.25f, 1.0f);

	/** 디졸브 지속 시간. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Turret|Death|Dissolve",
		meta = (DisplayName = "디졸브 시간", ClampMin = "0.1", ClampMax = "30.0", Units = "s"))
	float DeathDissolveDuration = 3.8f;

	/** 디졸브 머티리얼 갱신 주기. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Turret|Death|Dissolve",
		meta = (DisplayName = "디졸브 Tick 주기", ClampMin = "0.016", ClampMax = "0.25", Units = "s"))
	float DeathDissolveTickInterval = 0.033f;

	/** 사망 순간부터 디졸브 시작까지 대기 시간. 0 = 즉시. (물리 분리/낙하를 보여준 뒤 디졸브) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Turret|Death|Dissolve",
		meta = (DisplayName = "디졸브 시작 지연 시간", ClampMin = "0.0", ClampMax = "10.0", Units = "s"))
	float DeathDissolveStartDelay = 0.6f;

	// =========================================================
	// 자식 훅
	// =========================================================

	/** 서버: 사망 시 자식별 기능 정지(타이머/타겟 정리). 자식이 오버라이드. */
	virtual void OnTurretDestroyed_StopServerLogic() {}

	/** 모든 머신: 사망 연출 직후 BP 추가 코스메틱(사운드/추가 FX). 데디서버에선 호출 안 됨. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Turret|Death",
		meta = (DisplayName = "On Turret Death Cosmetic (사망 코스메틱)"))
	void BP_OnTurretDeathCosmetic();

private:
	// =========================================================
	// 상태
	// =========================================================

	/** 파괴 여부 (서버 권한, 모든 클라에 복제 — late-join 동기화). */
	UPROPERTY(ReplicatedUsing = OnRep_TurretDestroyed)
	bool bTurretDestroyed = false;

	/** 사망 연출이 이 머신에서 이미 재생됐는지 (Multicast/OnRep 이중 실행 방지, 비복제). */
	bool bDeathBreakPlayedLocally = false;

	/** 런타임 사망 디졸브 대상 메시 컴포넌트. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMeshComponent>> DeathDissolveMeshComponents;

	/** 런타임 사망 디졸브 MID. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> DeathDissolveMIDs;

	/** 클라이언트 로컬 디졸브 타이머. */
	FTimerHandle DeathDissolveTimerHandle;

	/** 디졸브 시작 지연 타이머. */
	FTimerHandle DeathDissolveDelayHandle;

	/** 디졸브 시작 월드 시간. */
	float DeathDissolveStartWorldSeconds = 0.f;

	// =========================================================
	// 체력·파괴
	// =========================================================

	/** HealthComponent->OnDeath 콜백 (서버에서만 발화). 시그니처는 FOnHellunaDeath 와 일치. */
	UFUNCTION()
	void HandleTurretDeath(AActor* DeadActor, AActor* KillerActor);

	UFUNCTION()
	void OnRep_TurretDestroyed();

	/** 서버·클라 공통: 사망 연출 1회 재생 (Multicast/OnRep 공용 진입점).
	 *  bFromReplication=true 면 late-join 복제 폴백 — 연출 재생 대신 즉시 사라진 상태로 스냅. */
	void PlayDeathBreakLocal(bool bFromReplication);

	/** 사망 시 메시 물리 분리 + 폭발 FX + 디졸브. 서버·클라 동기. Reliable(1회). */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_OnTurretDeathBreak();

	// =========================================================
	// 사망 디졸브 제어 (클라이언트 로컬 시각화)
	// =========================================================

	void StartDeathDissolveVisuals();
	void ApplyDeathDissolveAmount(float Amount);
	void FinishDeathDissolveVisuals();
};
