// [최적화] 광석 Proximity Activation 컴포넌트
// BP_Test_Farming 등 StaticMesh 기반 광석 BP에 추가하면
// 거리 기반으로 콜리전/위젯/오버랩을 자동 토글합니다.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "OreProximityComponent.generated.h"

class UStaticMeshComponent;
class UShapeComponent;
class UWidgetComponent;

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent, DisplayName = "광석 Proximity 최적화"))
class HELLUNA_API UOreProximityComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UOreProximityComponent();

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    // ==================================================================================
    // 설정값 (에디터에서 조정 가능)
    // ==================================================================================

    /** 이 반경 안에 플레이어가 있으면 풀 모드로 전환 (cm) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Proximity 최적화",
        meta = (DisplayName = "활성화 반경(cm)", ClampMin = "500.0", ClampMax = "10000.0"))
    float ActivationRadius = 2500.f;

    /** 거리 체크 주기 (초) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Proximity 최적화",
        meta = (DisplayName = "체크 주기(초)", ClampMin = "0.1", ClampMax = "5.0"))
    float CheckInterval = 0.5f;

    /** 렌더링 컬 디스턴스 (cm, 0=사용안함) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Proximity 최적화",
        meta = (DisplayName = "렌더 컬 디스턴스(cm)", ClampMin = "0.0"))
    float RenderCullDistance = 8000.f;

    // ==================================================================================
    // 내부 상태
    // ==================================================================================

    /** 현재 풀 모드인지 여부 */
    bool bFullModeActive = false;

    /** 거리 체크 타이머 핸들 */
    FTimerHandle ProximityTimerHandle;

    /** 캐싱된 컴포넌트 참조 */
    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> CachedMeshComp;

    UPROPERTY()
    TArray<TObjectPtr<UShapeComponent>> CachedOverlapComps;

    UPROPERTY()
    TObjectPtr<UWidgetComponent> CachedWidgetComp;

    // ==================================================================================
    // 내부 함수
    // ==================================================================================

    /** 오너 액터에서 최적화 대상 컴포넌트들을 캐싱 */
    void CacheOwnerComponents();

    /** 주기적으로 플레이어 거리를 체크하여 모드 전환 */
    void CheckProximityToPlayers();

    /** 경량 모드 → 풀 모드 전환 */
    void ActivateFullMode();

    /** 풀 모드 → 경량 모드 복귀 */
    void DeactivateToLightMode();
};
