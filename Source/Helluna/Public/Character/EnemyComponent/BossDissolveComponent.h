// Capstone Project Helluna

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "BossDissolveComponent.generated.h"

class UMaterialInterface;
class UMaterialInstanceDynamic;
class UNiagaraSystem;
class UNiagaraComponent;
class USkeletalMeshComponent;
class ACameraActor;

/**
 * [BossDissolveComponentV1] 보스 사망 시 dissolve 효과 캡슐화 컴포넌트.
 *
 * 책임:
 *  - 보스 mesh 머티리얼을 dissolve 지원 머티리얼로 swap
 *  - Animation scalar parameter 0→1 진행 (Tick)
 *  - Stage1/Stage2 Niagara VFX attach + 활성
 *  - 사용자 정의 시점에 actor 슬로우 모션
 *  - 완료 시 보스 actor destroy
 *
 * 멀티플레이 안전 — Multicast RPC 로 server/client 동기화.
 */
UCLASS(ClassGroup=(Helluna), meta=(BlueprintSpawnableComponent))
class HELLUNA_API UBossDissolveComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UBossDissolveComponent();

	// ─── 디자이너 노출 파라미터 ─────────────────────────────────────

	/** 보스 mesh 의 슬롯별 dissolve 머티리얼. 슬롯 수와 일치해야 함. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="BossDissolve|Materials",
		meta=(DisplayName="Dissolve 머티리얼 (슬롯별)"))
	TArray<UMaterialInterface*> DissolveMaterialOverrides;

	/** Master material 의 Animation scalar parameter 이름 (0=보임, 1=완전 사라짐). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="BossDissolve|Materials",
		meta=(DisplayName="Animation 파라미터 이름"))
	FName DissolveAnimationParamName = FName(TEXT("Animation"));

	/** Edge Color (HDR). 검정+보라 default. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="BossDissolve|Materials",
		meta=(DisplayName="Edge Color (보라톤)"))
	FLinearColor DissolveEdgeColor = FLinearColor(11.45f, 0.f, 50.f, 1.f);

	/** Stage 1 VFX (가루) — TriggerDissolve 시점에 즉시 attach. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="BossDissolve|VFX",
		meta=(DisplayName="Stage 1 VFX (가루)"))
	TObjectPtr<UNiagaraSystem> VFX_Stage1Asset = nullptr;

	/** Stage 2 VFX (구멍/녹는) — Stage2Delay 후 attach. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="BossDissolve|VFX",
		meta=(DisplayName="Stage 2 VFX (구멍/녹는)"))
	TObjectPtr<UNiagaraSystem> VFX_Stage2Asset = nullptr;

	/** dissolve animation 진행 시간 (wall clock 초). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="BossDissolve|Timing",
		meta=(DisplayName="Dissolve 총 시간 (wall, 초)", ClampMin="0.5", ClampMax="60.0"))
	float DissolveDuration = 5.0f;

	/** Stage 2 VFX activate delay (wall clock 초). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="BossDissolve|Timing",
		meta=(DisplayName="Stage 2 Delay (wall, 초)", ClampMin="0.0", ClampMax="60.0"))
	float Stage2DelaySeconds = 3.0f;

	/** dissolve 시 actor TimeDilation. anim/niagara 시각만 슬로우, dissolve material progression 은 wall 기준. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="BossDissolve|Timing",
		meta=(DisplayName="슬로우 모션 (CustomTimeDilation)", ClampMin="0.05", ClampMax="1.0"))
	float SlowMotionDilation = 0.2f;

	// ─── Top-down 카메라 ──────────────────────────────────────────

	/** dissolve 시작 시 보스 위에서 아래 (top-down) 카메라로 view target 전환. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="BossDissolve|Camera",
		meta=(DisplayName="Top-down 카메라 활성"))
	bool bUseTopDownCamera = true;

	/** 보스 mesh location 기준 카메라 offset (보통 위쪽 z+500). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="BossDissolve|Camera",
		meta=(DisplayName="Top-down 카메라 오프셋 (보스 mesh 기준)"))
	FVector TopDownCameraOffset = FVector(0.f, 0.f, 500.f);

	/** 카메라 회전 (보통 -90 pitch = 정면 아래). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="BossDissolve|Camera",
		meta=(DisplayName="Top-down 카메라 회전"))
	FRotator TopDownCameraRotation = FRotator(-90.f, 0.f, 0.f);

	/** 카메라 블렌드 인 시간 (초). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="BossDissolve|Camera",
		meta=(DisplayName="카메라 블렌드 인 (초)", ClampMin="0.0", ClampMax="3.0"))
	float TopDownCameraBlendIn = 0.4f;

	// ─── 공개 API ──────────────────────────────────────────────────

	/** 사망 시점에 호출 — 모든 dissolve 효과 시작. server 에서만 호출. */
	UFUNCTION(BlueprintCallable, Category="BossDissolve")
	void TriggerDissolve();

	// ─── Multicast RPC ─────────────────────────────────────────────

	UFUNCTION(NetMulticast, Reliable)
	void Multicast_StartDissolve();

	UFUNCTION(NetMulticast, Reliable)
	void Multicast_ActivateStage2();

protected:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

private:
	void StartDissolveLocal();
	void ActivateStage2Local();
	void CompleteAndDestroy();

	UPROPERTY()
	TArray<UMaterialInstanceDynamic*> DissolveMIDs;

	UPROPERTY()
	TObjectPtr<UNiagaraComponent> SpawnedVFX_Stage1 = nullptr;

	UPROPERTY()
	TObjectPtr<UNiagaraComponent> SpawnedVFX_Stage2 = nullptr;

	UPROPERTY()
	TWeakObjectPtr<ACameraActor> TopDownCamera;

	bool bDissolveActive = false;
	bool bStage2Activated = false;
	double DissolveStartWallTime = 0.0;

	FTimerHandle Stage2Timer;
	FTimerHandle CompleteTimer;
};
