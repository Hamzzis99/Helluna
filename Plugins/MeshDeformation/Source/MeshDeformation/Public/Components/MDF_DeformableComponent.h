// Gihyeon's Deformation Project (Helluna)

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MDF_DeformableComponent.generated.h"

class UDynamicMeshComponent;
class UNiagaraSystem;
class USoundBase;

/** * [Step 6 최적화 -> Step 7-1 네트워크 확장] 
 * 타격 데이터를 임시 저장 및 네트워크 전송하기 위한 구조체 
 * TArray<FMDFHitData> 형태로 RPC 인자로 전달됩니다.
 */
USTRUCT(BlueprintType)
struct FMDFHitData
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

    // -------------------------------------------------------------------------
    // [Step 8 핵심: 데이터 동기화 분리]
    // -------------------------------------------------------------------------

    /** * [1. 상태 동기화 (Track B)]
     * 서버에 누적된 타격 히스토리. 늦게 들어온 유저에게 자동으로 전송됩니다.
     * 값이 변경되거나 클라이언트가 처음 접속하면 OnRep_HitHistory가 실행됩니다.
     */
    UPROPERTY(ReplicatedUsing = OnRep_HitHistory)
    TArray<FMDFHitData> HitHistory;

    /** * 클라이언트에서 히스토리 데이터가 도착/변경되었을 때 호출됩니다.
     * 여기서는 "모양 변형(Deformation)"만 수행하고 소리는 내지 않습니다.
     */
    UFUNCTION()
    void OnRep_HitHistory();

    /**
     * [Step 8 변경 - 2. 이펙트 동기화 (Track A)]
     * 기존의 ApplyDeformation을 PlayEffects로 변경합니다.
     * 총을 쏘는 그 순간에만 실행되며, 오직 사운드와 나이아가라 이펙트만 담당합니다. (모양 변형 X)
     * [최적화] Reliable -> Unreliable 변경 (기관총 연사 시 네트워크 부하 방지)
     */
    UFUNCTION(NetMulticast, Unreliable)
    void NetMulticast_PlayEffects(const TArray<FMDFHitData>& NewHits);

public:
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
    float DeformRadius = 30.0f;

    /** 타격 시 안으로 밀려 들어가는 강도 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeshDeformation|설정", meta = (DisplayName = "변형 강도"))
    float DeformStrength = 5.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeshDeformation|디버그", meta = (DisplayName = "디버그 포인트 표시"))
    bool bShowDebugPoints = true;

    /** [Step 6 최적화] 타격 데이터를 모으는 시간 (초) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeshDeformation|설정", meta = (DisplayName = "배칭 처리 대기 시간"))
    float BatchProcessDelay = 0.0f;
    
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
    
private:
    /** [Step 6] 1프레임 동안 쌓인 타격 지점 리스트 (배칭 큐) */
    TArray<FMDFHitData> HitQueue;

    /** [Step 8 최적화] 클라이언트가 어디까지 변형을 적용했는지 기억하는 인덱스 */
    int32 LastAppliedIndex = 0;

    /** 타이머 핸들 (중복 호출 방지용) */
    FTimerHandle BatchTimerHandle;
};