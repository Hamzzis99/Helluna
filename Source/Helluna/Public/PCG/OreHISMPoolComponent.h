// [PCG 최적화] 광석 HISM 풀 매니저
//
// 목적: 수천 개 광석을 AActor 대신 HISM(Hierarchical Instanced Static Mesh)로 관리.
//   - 원거리 광석: HISM 인스턴스 (드로우콜 1회, 메모리 극소)
//   - 근거리 광석: AActor로 전환 (상호작용 가능)
//   - 채굴 완료 시: HISM 인스턴스 제거
//
// 사용법:
//   1. Defense GameMode 또는 전용 관리 액터에 이 컴포넌트를 추가
//   2. RegisterOreInstance()로 광석을 HISM 풀에 등록 (스폰 대신)
//   3. 플레이어 접근 시 자동으로 PromoteToActor() 호출 → AActor 스폰
//   4. 플레이어 이탈 시 DemoteToInstance() 호출 → AActor 파괴, HISM 복원
//
// 기존 OreProximityComponent V3와 연동:
//   - PromoteToActor 후 OreProximityComponent::ForceActivateNear() 호출
//   - DemoteToInstance는 OreProximityComponent가 Far 전환 시 자동 트리거 가능

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "OreHISMPoolComponent.generated.h"

class UHierarchicalInstancedStaticMeshComponent;
class UStaticMesh;

/** HISM 풀 내 개별 광석 인스턴스 정보 */
USTRUCT()
struct FOreInstanceData
{
    GENERATED_BODY()

    /** 이 인스턴스가 속한 HISM 풀 인덱스 */
    int32 PoolIndex = INDEX_NONE;

    /** HISM 내 인스턴스 인덱스 */
    int32 InstanceIndex = INDEX_NONE;

    /** 스폰할 때 사용할 광석 BP 클래스 */
    UPROPERTY()
    TSubclassOf<AActor> OreClass;

    /** 월드 트랜스폼 */
    FTransform Transform;

    /** 광석 태그 (승격 시 AActor에 복사) */
    TArray<FName> Tags;

    /** 현재 AActor로 승격되어 있는지 */
    bool bPromotedToActor = false;

    /** 승격된 AActor 참조 */
    UPROPERTY()
    TWeakObjectPtr<AActor> PromotedActor;

    /** 채굴되어 제거되었는지 */
    bool bDestroyed = false;
};

/** 메시별 HISM 풀 */
USTRUCT()
struct FOreHISMPool
{
    GENERATED_BODY()

    UPROPERTY()
    TObjectPtr<UHierarchicalInstancedStaticMeshComponent> HISMComp;

    UPROPERTY()
    TObjectPtr<UStaticMesh> Mesh;

    /** 이 풀에 속한 인스턴스 ID 목록 (글로벌 ID) */
    TArray<int32> InstanceIDs;
};

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent, DisplayName = "광석 HISM 풀 매니저"))
class HELLUNA_API UOreHISMPoolComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UOreHISMPoolComponent();

    // ==================================================================================
    // 외부 API
    // ==================================================================================

    /**
     * 광석을 HISM 인스턴스로 등록 (AActor 스폰 없이).
     * @param Mesh        광석의 StaticMesh
     * @param OreClass    상호작용 시 스폰할 BP 클래스
     * @param Transform   월드 트랜스폼
     * @return 글로벌 인스턴스 ID (이후 Promote/Demote에 사용)
     */
    /** Tags 없이 등록 (기존 호환) */
    UFUNCTION(BlueprintCallable, Category = "HISM 풀")
    int32 RegisterOreInstance(UStaticMesh* Mesh, TSubclassOf<AActor> OreClass, const FTransform& Transform);

    /** Tags 포함 등록 — 승격 시 AActor에 Tags 복사 */
    int32 RegisterOreInstance(UStaticMesh* Mesh, TSubclassOf<AActor> OreClass, const FTransform& Transform, const TArray<FName>& InTags);

    /**
     * HISM 인스턴스 → AActor로 승격 (플레이어 접근 시).
     * HISM에서 해당 인스턴스를 숨기고, 같은 위치에 AActor를 스폰.
     * @return 스폰된 AActor (nullptr이면 이미 승격됨 또는 파괴됨)
     */
    UFUNCTION(BlueprintCallable, Category = "HISM 풀")
    AActor* PromoteToActor(int32 InstanceID);

    /**
     * AActor → HISM 인스턴스로 강등 (플레이어 이탈 시).
     * AActor를 파괴하고, HISM 인스턴스를 다시 표시.
     */
    UFUNCTION(BlueprintCallable, Category = "HISM 풀")
    void DemoteToInstance(int32 InstanceID);

    /**
     * 광석이 채굴 완료됨 — HISM 인스턴스와 AActor 모두 제거.
     */
    UFUNCTION(BlueprintCallable, Category = "HISM 풀")
    void DestroyOre(int32 InstanceID);

    /** 현재 HISM 인스턴스 수 (승격/파괴 제외) */
    UFUNCTION(BlueprintPure, Category = "HISM 풀")
    int32 GetActiveInstanceCount() const;

    /** 전체 등록 수 */
    UFUNCTION(BlueprintPure, Category = "HISM 풀")
    int32 GetTotalRegistered() const { return AllInstances.Num(); }

    /**
     * 주기적으로 호출 — 플레이어에 가까운 HISM 인스턴스를 AActor로 자동 승격.
     * GameMode의 Tick이나 타이머에서 호출.
     */
    UFUNCTION(BlueprintCallable, Category = "HISM 풀")
    void AutoPromoteNearPlayers(float PromoteRadius);

    /**
     * 주기적으로 호출 — 플레이어에서 먼 AActor를 HISM으로 자동 강등.
     */
    UFUNCTION(BlueprintCallable, Category = "HISM 풀")
    void AutoDemoteFarFromPlayers(float DemoteRadius);

protected:
    virtual void BeginPlay() override;

    // ==================================================================================
    // 설정
    // ==================================================================================

    /** HISM의 렌더링 최대 거리 (cm) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HISM 풀|렌더링",
        meta = (DisplayName = "HISM 렌더 컬 디스턴스(cm)", ClampMin = "1000.0"))
    float HISMCullDistance = 15000.f;

    /** HISM 인스턴스에 그림자 캐스팅 할지 (false 권장 — GPU 절감) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HISM 풀|렌더링",
        meta = (DisplayName = "HISM 그림자 캐스팅"))
    bool bHISMCastShadow = false;

    /** 자동 승격/강등 체크 주기 (초) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HISM 풀|자동 관리",
        meta = (DisplayName = "자동 체크 주기(초)", ClampMin = "0.1", ClampMax = "5.0"))
    float AutoCheckInterval = 0.5f;

    /** 자동 승격 반경 (cm) — 이 안에 들어오면 AActor로 전환 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HISM 풀|자동 관리",
        meta = (DisplayName = "자동 승격 반경(cm)", ClampMin = "500.0"))
    float AutoPromoteRadius = 3000.f;

    /** 자동 강등 반경 (cm) — 이 밖으로 나가면 HISM으로 전환 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HISM 풀|자동 관리",
        meta = (DisplayName = "자동 강등 반경(cm)", ClampMin = "1000.0"))
    float AutoDemoteRadius = 5000.f;

    /** 한 프레임에 최대 승격 수 (스파이크 방지) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HISM 풀|자동 관리",
        meta = (DisplayName = "프레임당 최대 승격 수", ClampMin = "1", ClampMax = "20"))
    int32 MaxPromotesPerCheck = 5;

    /** 한 프레임에 최대 강등 수 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HISM 풀|자동 관리",
        meta = (DisplayName = "프레임당 최대 강등 수", ClampMin = "1", ClampMax = "20"))
    int32 MaxDemotesPerCheck = 10;

    /** 라운드-로빈 배치 크기 — 한 체크 주기에 검사할 인스턴스 수 (전체 순회 방지) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HISM 풀|자동 관리",
        meta = (DisplayName = "배치당 검사 수", ClampMin = "100", ClampMax = "5000"))
    int32 BatchSizePerCheck = 2000;

    /** 승격된 AActor의 NetCullDistanceSquared (네트워크 리플리케이션 컬 거리²) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HISM 풀|승격 최적화",
        meta = (DisplayName = "NetCullDistSq (cm²)", ClampMin = "0.0"))
    float PromotedNetCullDistSq = 5000.f * 5000.f;

    /** 승격된 AActor의 네트워크 업데이트 빈도 (Hz). 정적 광석이라 낮은 값 권장 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HISM 풀|승격 최적화",
        meta = (DisplayName = "NetUpdateFrequency (Hz)", ClampMin = "0.1", ClampMax = "10.0"))
    float PromotedNetUpdateFrequency = 1.0f;

    /** 승격된 AActor의 Tick 비활성화 (정적 광석은 Tick 불필요) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HISM 풀|승격 최적화",
        meta = (DisplayName = "승격 시 Tick 비활성화"))
    bool bDisablePromotedActorTick = true;

private:
    // ==================================================================================
    // 내부 데이터
    // ==================================================================================

    /** 메시별 HISM 풀 */
    UPROPERTY()
    TArray<FOreHISMPool> Pools;

    /** 모든 등록된 인스턴스 (글로벌 ID = 배열 인덱스) */
    UPROPERTY()
    TArray<FOreInstanceData> AllInstances;

    /** 자동 관리 타이머 */
    FTimerHandle AutoManageTimerHandle;

    /** 라운드-로빈: 다음 체크 시작 인덱스 (승격) */
    int32 PromoteRoundRobinIndex = 0;

    /** 라운드-로빈: 다음 체크 시작 인덱스 (강등) */
    int32 DemoteRoundRobinIndex = 0;

    /** 캐싱된 플레이어 위치 (AutoManageTick 1회 호출 동안 유효) */
    TArray<FVector> CachedPlayerLocations;

    // ==================================================================================
    // 내부 함수
    // ==================================================================================

    /** 메시에 대응하는 풀 인덱스 반환 (없으면 생성) */
    int32 FindOrCreatePool(UStaticMesh* Mesh);

    /** 자동 관리 타이머 콜백 */
    void AutoManageTick();

    /** 플레이어 위치 캐시 갱신 (AutoManageTick 시작 시 1회 호출) */
    void RefreshPlayerLocationCache();

    /** 캐시된 플레이어 위치 기반 가장 가까운 거리 제곱 */
    float GetClosestPlayerDistSqCached(const FVector& Location) const;

    /** 가장 가까운 플레이어 거리 제곱 계산 (캐시 없이 직접) */
    float GetClosestPlayerDistSq(const FVector& Location) const;
};
