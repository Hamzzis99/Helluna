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

	/** Phase 2 (광폭화) 일 때 dissolve MID 의 Tint — Phase 1 텍스처 위에 어둡게 깔아 cosmic 외관과 매칭. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="BossDissolve|Materials",
		meta=(DisplayName="Phase 2 Tint (광폭화 시 BaseColor 보정)"))
	FLinearColor Phase2DissolveTint = FLinearColor(0.1f, 0.05f, 0.2f, 1.f);

	/**
	 * [Phase2RetainSkinV2] Phase 2 광폭화 피부 슬롯 (갤럭시 머터리얼) 의 dissolve 처리 방식.
	 *   true  : 갤럭시 유지 + 갤럭시 머터리얼 자체의 Dissolve_Edge 파라미터를 0→1 driving.
	 *           M_ScreenUV_Galaxy 가 이미 Masked 블렌드 + Dissolve_Edge/Color_D_EdgeEmissive 파라미터를
	 *           내장하고 있어, 외부 dissolve material swap 없이 갤럭시 + 구멍 dissolve 동시 연출 가능.
	 *   false : dissolve MID 적용 + Phase2DissolveTint — 부식 효과 살아있음. 갤럭시 외관은 어두운 dissolve 로 대체.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="BossDissolve|Materials",
		meta=(DisplayName="Phase 2 피부 슬롯 dissolve 처리 (true=갤럭시 자체 dissolve, false=swap)"))
	bool bRetainPhase2SkinDuringDissolve = true;

	/**
	 * [Phase2RetainSkinV2 / ParamFix] 갤럭시(2페이즈) 피부의 dissolve 진행 scalar 파라미터 이름.
	 *   실제 2페이즈 피부 = MI_BossPhase2_Body_Galaxy → 베이스 M_Mannequin_01.
	 *   M_Mannequin_01 의 dissolve scalar 는 "Animation" (0=솔리드 → 1=완전히 사라짐) — 다른
	 *   슬롯(갑옷/총/머리) 의 DissolveAnimationParamName 과 동일. (구버전은 M_ScreenUV_Galaxy 의
	 *   "Dissolve_Edge" 로 잘못 지정돼 있어 존재하지 않는 파라미터를 driving → 피부가 안 녹았음.)
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="BossDissolve|Materials",
		meta=(DisplayName="Phase 2 갤럭시 Dissolve scalar 이름"))
	FName Phase2GalaxyDissolveParamName = FName(TEXT("Animation"));

	/** Phase 2 갤럭시 머터리얼의 edge color — M_Mannequin_01 의 벡터 파라미터 "Edge Color". */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="BossDissolve|Materials",
		meta=(DisplayName="Phase 2 갤럭시 Edge Color"))
	FName Phase2GalaxyEdgeColorParamName = FName(TEXT("Edge Color"));

	/**
	 * [Phase2RetainSkinV2] true=Animation 0→1 (정방향: 솔리드→사라짐), false=1→0 (역방향).
	 *   M_Mannequin_01 의 Animation 은 0=솔리드라 정방향이 맞음.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="BossDissolve|Materials",
		meta=(DisplayName="Phase 2 Dissolve 정방향 (true=0→1, false=1→0)"))
	bool bPhase2GalaxyDissolveForward = true;

	/**
	 * [Phase2GlowFadeV1] 2페이즈 피부 발광(M_Mannequin_01 의 "Phase2SkinGlow" 스칼라) 파라미터 이름.
	 *   dissolve 진행 중 발광을 0 으로 페이드아웃 → 피부가 녹는 모습이 발광에 묻히지 않게.
	 *
	 *   NOTE: 일부러 UPROPERTY 가 아님. UActorComponent 의 reflected 프로퍼티를 추가/제거하면
	 *   컴포넌트 레이아웃이 바뀌어, 쿡(-nocompileeditor 로 옛 에디터 바이너리가 굽는 경우) 데이터와
	 *   런타임 exe 가 불일치 → unversioned property 직렬화 크래시(MappedName assert). 고정 이름이라
	 *   에디터 노출 불필요하므로 비reflected 일반 멤버로 둔다.
	 */
	FName Phase2GalaxySkinGlowParamName = FName(TEXT("Phase2SkinGlow"));

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

	/** [Phase2RetainSkinV2] 갤럭시 자체 dissolve driving용 MID array (별도 paramName 사용). */
	UPROPERTY()
	TArray<UMaterialInstanceDynamic*> Phase2GalaxyMIDs;

	/** [Phase2GlowFadeV1] dissolve 시작 시점의 피부 발광 값 — tick 에서 (1-Alpha) 로 페이드아웃. */
	float Phase2GalaxyInitialGlow = 0.f;

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
