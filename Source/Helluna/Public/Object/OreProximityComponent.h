// [최적화] 광석 Proximity Activation 컴포넌트 V3
// BP_Test_Farming 등 StaticMesh 기반 광석 BP에 추가하면
// 거리 기반으로 콜리전/위젯/오버랩/그림자/틱/네트워크를 자동 토글합니다.
//
// V2 → V3 추가 최적화:
//  8. WidgetComponent lazy 생성/파괴 (스폰 시 가장 비싼 단일 비용 제거)
//  9. WidgetComponent UnregisterComponent 경량화 (Near 외에는 렌더링 제외)
// 10. HISM 풀 매니저 연동 준비 (OreHISMPoolComponent 연동 인터페이스)

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "OreProximityComponent.generated.h"

class UStaticMeshComponent;
class UShapeComponent;
class UWidgetComponent;
class UNiagaraComponent;
class UAudioComponent;

/** 광석 LOD 레벨 */
UENUM(BlueprintType)
enum class EOreProximityLOD : uint8
{
    Near    UMETA(DisplayName = "근거리 (풀 모드)"),
    Mid     UMETA(DisplayName = "중거리 (그림자/VFX OFF)"),
    Far     UMETA(DisplayName = "원거리 (경량 모드)")
};

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent, DisplayName = "광석 Proximity 최적화 V3"))
class HELLUNA_API UOreProximityComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UOreProximityComponent();

    /** 현재 LOD 레벨 조회 */
    UFUNCTION(BlueprintPure, Category = "Proximity 최적화")
    EOreProximityLOD GetCurrentLOD() const { return CurrentLOD; }

    /** HISM 풀에서 액터로 전환 후 호출 — 즉시 Near 모드 */
    void ForceActivateNear();

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    // ==================================================================================
    // 설정값 (에디터에서 조정 가능)
    // ==================================================================================

    /** 이 반경 안에 플레이어가 있으면 풀 모드로 전환 (cm) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Proximity 최적화|거리",
        meta = (DisplayName = "Near 반경(cm)", ClampMin = "500.0", ClampMax = "10000.0"))
    float NearRadius = 2500.f;

    /** 이 반경 안이면 Mid 모드 (그림자/VFX OFF, 콜리전 ON) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Proximity 최적화|거리",
        meta = (DisplayName = "Mid 반경(cm)", ClampMin = "1000.0", ClampMax = "20000.0"))
    float MidRadius = 5000.f;

    /** 거리 체크 주기 — Near (초) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Proximity 최적화|타이머",
        meta = (DisplayName = "Near 체크 주기(초)", ClampMin = "0.1", ClampMax = "2.0"))
    float NearCheckInterval = 0.3f;

    /** 거리 체크 주기 — Far (초). 멀리 있을수록 드물게 체크 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Proximity 최적화|타이머",
        meta = (DisplayName = "Far 체크 주기(초)", ClampMin = "0.5", ClampMax = "5.0"))
    float FarCheckInterval = 1.0f;

    /** 렌더링 컬 디스턴스 (cm, 0=사용안함) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Proximity 최적화|렌더링",
        meta = (DisplayName = "렌더 컬 디스턴스(cm)", ClampMin = "0.0"))
    float RenderCullDistance = 8000.f;

    /** 멀리 있을 때 오너 액터의 Tick을 비활성화할지 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Proximity 최적화|고급",
        meta = (DisplayName = "원거리 시 오너 Tick 비활성화"))
    bool bDisableOwnerTickWhenFar = true;

    /** 멀리 있을 때 네트워크 Dormancy를 사용할지 (서버 부하 절감) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Proximity 최적화|고급",
        meta = (DisplayName = "원거리 시 Net Dormancy 사용"))
    bool bUseNetDormancy = true;

    /** WidgetComponent를 Near에서만 lazy 생성/등록할지 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Proximity 최적화|위젯",
        meta = (DisplayName = "위젯 Lazy 생성 (Near에서만)"))
    bool bLazyWidgetCreation = true;

    // ==================================================================================
    // 내부 상태
    // ==================================================================================

    /** 현재 LOD 레벨 */
    EOreProximityLOD CurrentLOD = EOreProximityLOD::Far;

    /** 거리 체크 타이머 핸들 */
    FTimerHandle ProximityTimerHandle;

    /** 오너의 원래 틱 활성화 상태 (복원용) */
    bool bOriginalOwnerTickEnabled = true;

    /** 오너의 원래 Dormancy (복원용) */
    ENetDormancy OriginalDormancy = DORM_Awake;

    /** WidgetComponent가 현재 등록(활성) 상태인지 */
    bool bWidgetRegistered = false;

    /** 캐싱된 컴포넌트 참조 */
    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> CachedMeshComp;

    UPROPERTY()
    TArray<TObjectPtr<UShapeComponent>> CachedOverlapComps;

    UPROPERTY()
    TObjectPtr<UWidgetComponent> CachedWidgetComp;

    UPROPERTY()
    TArray<TObjectPtr<UNiagaraComponent>> CachedNiagaraComps;

    UPROPERTY()
    TArray<TObjectPtr<UAudioComponent>> CachedAudioComps;

    // ==================================================================================
    // 내부 함수
    // ==================================================================================

    /** 오너 액터에서 최적화 대상 컴포넌트들을 캐싱 */
    void CacheOwnerComponents();

    /** 주기적으로 플레이어 거리를 체크하여 모드 전환 */
    void CheckProximityToPlayers();

    /** 가장 가까운 플레이어까지의 거리 제곱을 반환 */
    float GetClosestPlayerDistanceSq() const;

    /** LOD 전환 */
    void TransitionToLOD(EOreProximityLOD NewLOD);

    /** Near 모드: 풀 활성화 */
    void ActivateNearMode();

    /** Mid 모드: 콜리전 ON, 그림자/VFX/오디오 OFF */
    void ActivateMidMode();

    /** Far 모드: 전부 경량화 */
    void ActivateFarMode();

    /** 타이머 주기를 현재 LOD에 맞게 조절 */
    void AdjustTimerInterval();

    // ── WidgetComponent Lazy 관리 ──

    /** WidgetComponent를 렌더링 시스템에 등록 (Near 진입 시) */
    void RegisterWidget();

    /** WidgetComponent를 렌더링 시스템에서 해제 (Near 이탈 시) */
    void UnregisterWidget();
};
