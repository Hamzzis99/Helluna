// Source/Helluna/Public/Conponent/Outline/HellunaTeamOutlineComponent.h
// L4D식 아군 외곽선 — CustomDepth + Stencil 기반 클라이언트 시각 효과
// 데디서버 NM_DedicatedServer 환경에서는 Tick 비활성

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "HellunaTeamOutlineComponent.generated.h"

class USkeletalMeshComponent;
class UMaterialInterface;
class UPostProcessComponent;

UENUM(BlueprintType)
enum class EHellunaOutlineState : uint8
{
	None        UMETA(DisplayName = "None (없음)"),
	Ally        UMETA(DisplayName = "Ally (아군)"),
	AllyDowned  UMETA(DisplayName = "Ally Downed (아군 다운)"),
};

/**
 * 팀 외곽선 컴포넌트 (Team Outline Component).
 * - 로컬 클라이언트가 자신을 제외한 다른 Hero의 거리를 평가하여
 *   본인 메시의 RenderCustomDepth/StencilValue를 토글한다.
 * - 외곽선 렌더 자체는 PostProcess 머티리얼이 SceneTexture:CustomDepth/CustomStencil
 *   을 샘플링해 처리(2단계 작업).
 * - 순수 시각 효과 — Replication 없음.
 */
UCLASS(ClassGroup = (Helluna), meta = (BlueprintSpawnableComponent,
	DisplayName = "Helluna Team Outline (팀 외곽선)"))
class HELLUNA_API UHellunaTeamOutlineComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UHellunaTeamOutlineComponent();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

	/** 외곽선 활성 최대 거리 (cm). 이 거리 안이면 CustomDepth 활성. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outline|Distance",
		meta = (DisplayName = "Max Outline Distance (외곽선 최대 거리, cm)",
			ClampMin = "100.0"))
	float MaxOutlineDistance = 12000.f;

	/**
	 * 외곽선 활성 최소 거리 (cm). 이 거리 안에 있으면 외곽선 표시 안 함.
	 * 가까이 있으면 굳이 외곽선 필요 없음 — 직접 보임.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outline|Distance",
		meta = (DisplayName = "Min Show Distance (외곽선 표시 시작 거리, cm)",
			ClampMin = "0.0"))
	float MinShowDistance = 500.f;

	/**
	 * 페이드 시작 거리 (cm). 이 거리부터 머티리얼 알파가 점차 감소하여
	 * MaxOutlineDistance 에서 완전히 사라진다.
	 * (실제 페이드 곡선은 PostProcess Material 에서 적용)
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outline|Distance",
		meta = (DisplayName = "Fade Start Distance (페이드 시작 거리, cm)",
			ClampMin = "100.0"))
	float FadeStartDistance = 6000.f;

	/** 거리 평가 주기 (초). 매 틱이 아닌 이 간격으로 검사. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outline|Performance",
		meta = (DisplayName = "Evaluation Interval (평가 주기, 초)",
			ClampMin = "0.05", ClampMax = "1.0"))
	float EvaluationInterval = 0.2f;

	/** Stencil Value: 일반 아군 (PostProcess Material 분기 키) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outline|Stencil",
		meta = (DisplayName = "Ally Stencil (아군 스텐실 값)",
			ClampMin = "1", ClampMax = "255"))
	int32 AllyStencilValue = 250;

	/** Stencil Value: 다운된 아군 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outline|Stencil",
		meta = (DisplayName = "Ally Downed Stencil (다운 아군 스텐실 값)",
			ClampMin = "1", ClampMax = "255"))
	int32 AllyDownedStencilValue = 251;

	/** 디버그/치트로 강제 비활성화 */
	UPROPERTY(BlueprintReadWrite, Category = "Outline|Debug")
	bool bDisabled = false;

	/**
	 * PostProcess 머티리얼 (M_TeamOutline_PP).
	 * BP에서 할당. 미할당 시 BeginPlay에서 기본 경로 LoadObject 시도.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Outline|Material",
		meta = (DisplayName = "외곽선 PP 머티리얼"))
	TObjectPtr<UMaterialInterface> OutlineMaterial;

	/** PP Blendable 가중치 (1.0 = 풀 적용) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Outline|Material",
		meta = (DisplayName = "PP 가중치", ClampMin = "0.0", ClampMax = "1.0"))
	float OutlineWeight = 1.0f;

	/** 외부에서 다운 상태 알리기 (HealthComponent 의 OnDowned 등에서 호출 가능) */
	UFUNCTION(BlueprintCallable, Category = "Outline")
	void SetDownedHint(bool bInDowned);

private:
	float LastEvaluationTime = 0.f;
	EHellunaOutlineState CurrentState = EHellunaOutlineState::None;
	bool bDownedHint = false;

	TWeakObjectPtr<USkeletalMeshComponent> CachedMeshComponent;

	void EvaluateAndApply();
	void ApplyOutlineState(EHellunaOutlineState NewState);
	APawn* GetLocalPlayerPawn() const;

	/** 로컬 컨트롤 Hero일 때 동적 PostProcessComponent 부착 (Unbound=true) */
	void TryRegisterPostProcessOnLocalCamera();
	bool bPostProcessRegistered = false;

	UPROPERTY()
	TObjectPtr<UPostProcessComponent> SpawnedOutlinePP = nullptr;
};
