// Gihyeon's Deformation Project (Helluna)

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "MDF_DeformableComponent.generated.h"

class UDynamicMeshComponent;
class UNiagaraSystem;
class USoundBase;
class UMDF_DeformableComponent;

/** * [Step 6 최적화 -> Step 7-1 네트워크 확장]
 * 타격 데이터를 임시 저장 및 네트워크 전송하기 위한 구조체
 * FFastArraySerializerItem을 상속하여 델타 복제를 지원합니다.
 */
USTRUCT(BlueprintType)
struct FMDFHitData : public FFastArraySerializerItem
{
    GENERATED_BODY()

    UPROPERTY()
    FVector LocalLocation;

    UPROPERTY()
    FVector LocalDirection;

    UPROPERTY()
    float Damage;

    UPROPERTY()
    TSubclassOf<UDamageType> DamageTypeClass;

    FMDFHitData() : LocalLocation(FVector::ZeroVector), LocalDirection(FVector::ForwardVector), Damage(0.f), DamageTypeClass(nullptr) {}
    FMDFHitData(FVector Loc, FVector Dir, float Dmg, TSubclassOf<UDamageType> DmgType)
        : LocalLocation(Loc), LocalDirection(Dir), Damage(Dmg), DamageTypeClass(DmgType) {}

    /** FFastArraySerializer에서 개별 아이템 변경 시 호출 */
    void PostReplicatedAdd(const struct FMDFHitDataArray& InArraySerializer);
    void PostReplicatedChange(const struct FMDFHitDataArray& InArraySerializer);
    void PreReplicatedRemove(const struct FMDFHitDataArray& InArraySerializer);
};

USTRUCT(BlueprintType)
struct FMDFHitDataArray : public FFastArraySerializer
{
    GENERATED_BODY()

    UPROPERTY()
    TArray<FMDFHitData> Items;

    /** 소유 컴포넌트 (콜백에서 변형 적용 시 사용) */
    UPROPERTY(NotReplicated)
    TObjectPtr<UMDF_DeformableComponent> OwnerComponent = nullptr;

    bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
    {
        return FFastArraySerializer::FastArrayDeltaSerialize<FMDFHitData, FMDFHitDataArray>(Items, DeltaParms, *this);
    }
};

/** NetDeltaSerialize 활성화 매크로 (구조체 정의 직후, UCLASS 바깥에 배치) */
template<>
struct TStructOpsTypeTraits<FMDFHitDataArray> : public TStructOpsTypeTraitsBase2<FMDFHitDataArray>
{
    enum { WithNetDeltaSerializer = true };
};

UCLASS(Blueprintable, ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class MESHDEFORMATION_API UMDF_DeformableComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UMDF_DeformableComponent();

    // [Step 8] 리플리케이션(동기화) 설정을 위해 필수 오버라이드
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
    virtual void BeginPlay() override;

    /** [MeshDeformation] 포인트 데미지 수신 및 변형 데이터를 큐에 쌓음 */
    UFUNCTION()
    virtual void HandlePointDamage(AActor* DamagedActor, float Damage, class AController* InstigatedBy, FVector HitLocation, class UPrimitiveComponent* FHitComponent, FName BoneName, FVector ShotFromDirection, const class UDamageType* DamageType, AActor* DamageCauser);

    /** * [Step 6 최적화] 모인 타격 지점들을 한 프레임의 끝에서 한 번에 연산
     * (서버에서만 호출되어 RPC를 발송하는 역할로 변경 예정)
     */
    void ProcessDeformationBatch();

    /** [자식 클래스용] 배칭 타이머를 시작하는 헬퍼 함수 */
    void StartBatchTimer();

    // -------------------------------------------------------------------------
    // [Step 8 → FFastArraySerializer: 데이터 동기화]
    // -------------------------------------------------------------------------

    /** [Step 8 → FFastArraySerializer] 델타 복제되는 히트 히스토리 */
    UPROPERTY(Replicated)
    FMDFHitDataArray HitHistoryArray;

    /**
     * [Step 8 변경 - 2. 이펙트 동기화 (Track A)]
     * 기존의 ApplyDeformation을 PlayEffects로 변경합니다.
     * 총을 쏘는 그 순간에만 실행되며, 오직 사운드와 나이아가라 이펙트만 담당합니다. (모양 변형 X)
     * [최적화] Reliable -> Unreliable 변경 (기관총 연사 시 네트워크 부하 방지)
     */
    UFUNCTION(NetMulticast, Unreliable)
    void NetMulticast_PlayEffects(const TArray<FMDFHitData>& NewHits);

public:
    /** 새로 추가된 히트 데이터에 대해 변형을 적용합니다. (FFastArray 콜백에서 호출) */
    void ApplyDeformationForHits(int32 StartIndex, int32 Count);

    /** 수리 명령 시 메쉬를 리셋합니다. */
    void ResetDeformation();

    /** FFastArray 콜백용: Remove 지연 처리 플래그 */
    bool bPendingDeformationReset = false;

    /** FFastArray 콜백용: Add 배치 처리 변수 */
    int32 PendingAddStartIndex = INDEX_NONE;
    int32 PendingAddCount = 0;

    /** FFastArray 콜백용: 다음 틱 Lambda 중복 등록 방지 플래그 */
    bool bFastArrayBatchPending = false;

    /** 원본으로 사용할 StaticMesh 에셋 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeshDeformation|설정", meta = (DisplayName = "스태틱 메쉬(StaticMesh)"))
    TObjectPtr<UStaticMesh> SourceStaticMesh;

    /** 에셋을 기반으로 DynamicMesh를 초기화하는 함수 */
    UFUNCTION(BlueprintCallable, Category = "MeshDeformation")
    void InitializeDynamicMesh();

    /** 월드 좌표 -> 로컬 좌표 변환 */
    UFUNCTION(BlueprintCallable, Category = "MeshDeformation|수학")
    FVector ConvertWorldToLocal(FVector WorldLocation);

    /** 월드 방향 -> 로컬 방향 변환 */
    UFUNCTION(BlueprintCallable, Category = "MeshDeformation|수학")
    FVector ConvertWorldDirectionToLocal(FVector WorldDirection);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeshDeformation|설정", meta = (DisplayName = "시스템 활성화"))
    bool bIsDeformationEnabled = true;

    /** 타격 지점 주변의 변형 반경 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeshDeformation|설정", meta = (DisplayName = "변형 반경"))
    float DeformRadius = 100.0f;

    /** 타격 시 안으로 밀려 들어가는 강도 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeshDeformation|설정", meta = (DisplayName = "변형 강도"))
    float DeformStrength = 30.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeshDeformation|디버그", meta = (DisplayName = "디버그 포인트 표시"))
    bool bShowDebugPoints = true;

    /** [Step 6 최적화] 타격 데이터를 모으는 시간 (초) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeshDeformation|설정", meta = (DisplayName = "배칭 처리 대기 시간"))
    float BatchProcessDelay = 0.0f;

    /** [최적화] HitHistory 최대 크기. 초과 시 오래된 데이터부터 제거됩니다.
     *  이미 메시에 적용된 변형은 유지되며, 늦게 접속한 클라이언트만 영향받습니다.
     *  0 = 제한 없음 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeshDeformation|설정", meta = (DisplayName = "히스토리 최대 크기", ClampMin = "0", ClampMax = "5000"))
    int32 MaxHitHistorySize = 500;

    /** [MeshDeformation|Effect] 변형 시 발생할 나이아가라 파편 시스템 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeshDeformation|설정", meta = (DisplayName = "파편 이펙트(Niagara)"))
    TObjectPtr<UNiagaraSystem> DebrisSystem;

    /** [MeshDeformation|Effect] 피격 시 재생될 3D 사운드 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeshDeformation|설정", meta = (DisplayName = "피격 사운드(3D)"))
    TObjectPtr<USoundBase> ImpactSound;

    /** [MeshDeformation|Effect] 3D 사운드 거리 감쇄 설정 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeshDeformation|설정", meta = (DisplayName = "사운드 감쇄 설정(3D Attenuation)"))
    TObjectPtr<USoundAttenuation> ImpactAttenuation;

    /** [MeshDeformation|설정] 원거리 공격 판정용 클래스 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeshDeformation|설정")
    TSubclassOf<UDamageType> RangedDamageType;

    /** [MeshDeformation|설정] 근접 공격 판정용 클래스 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeshDeformation|설정")
    TSubclassOf<UDamageType> MeleeDamageType;

    //메시가 찌그러지지 않는다는 것을 반영하기 위한 데미지 타입
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeshDeformation|설정")
    TSubclassOf<UDamageType> BreachDamageType; // 절단 전용 타입
    // -------------------------------------------------------------------------
    // [Step 9: 월드 파티션 영속성 지원]
    // -------------------------------------------------------------------------

    /** [Step 9] GameState에 내 데이터를 맡길 때 사용하는 고유 ID (신분증) */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MeshDeformation|설정", meta = (DisplayName = "고유 식별자(GUID)"))
    FGuid ComponentGuid;

    // -------------------------------------------------------------------------
    // [Step 10: 수리 시스템]
    // -------------------------------------------------------------------------

    /** [Step 10] 메쉬를 원상복구(수리)하고 히스토리를 초기화합니다. (서버 전용) */
    UFUNCTION(BlueprintCallable, Category = "MeshDeformation|수리", meta = (DisplayName = "메시 수리(RepairMesh)"))
    void RepairMesh();

    // -------------------------------------------------------------------------
    // [디버그] 개발자 모드 - 자동 메시 복구
    // -------------------------------------------------------------------------

    /** [디버그] 개발자 모드 활성화 - 체크 시 변형 후 자동으로 메시가 복구됩니다. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeshDeformation|디버그", meta = (DisplayName = "개발자 모드 (자동 복구)"))
    bool bDevMode_AutoRepair = false;

    /** [디버그] 자동 복구까지 대기 시간 (초) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeshDeformation|디버그", meta = (DisplayName = "자동 복구 대기 시간 (초)", EditCondition = "bDevMode_AutoRepair", ClampMin = "0.5", ClampMax = "60.0"))
    float DevMode_RepairDelay = 5.0f;

protected:
    // [중요 수정] private -> protected로 변경
    // 자식 클래스(MiniGame)가 직접 데이터를 넣을 수 있게 허용합니다.

    /** [Step 6] 1프레임 동안 쌓인 타격 지점 리스트 (배칭 큐) */
    TArray<FMDFHitData> HitQueue;

    /** 타이머 핸들 (중복 호출 방지용) */
    FTimerHandle BatchTimerHandle;

    /** [디버그] 자동 복구 타이머 핸들 */
    FTimerHandle DevMode_RepairTimerHandle;
};
