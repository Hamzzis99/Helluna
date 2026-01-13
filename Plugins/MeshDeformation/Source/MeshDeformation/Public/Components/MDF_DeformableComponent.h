// Gihyeon's Deformation Project (Helluna)

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MDF_DeformableComponent.generated.h"

class UDynamicMeshComponent;
class UNiagaraSystem;
class USoundBase;

/** * 타격 데이터를 임시 저장 및 네트워크 전송하기 위한 구조체 
 * (Damage 변수는 체력 감소가 아닌, 변형 강도 계산용으로 유지됩니다)
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

    // 리플리케이션 설정
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
    virtual void BeginPlay() override;

    /** 포인트 데미지 수신 및 변형 처리 (체력 감소 로직 없음) */
    UFUNCTION()
    virtual void HandlePointDamage(AActor* DamagedActor, float Damage, class AController* InstigatedBy, FVector HitLocation, class UPrimitiveComponent* FHitComponent, FName BoneName, FVector ShotFromDirection, const class UDamageType* DamageType, AActor* DamageCauser);

    /** 데이터를 모아서 처리 및 저장 요청 */
    void ProcessDeformationBatch();

    // -------------------------------------------------------------------------
    // [데이터 동기화]
    // -------------------------------------------------------------------------

    /** 서버에 누적된 타격 히스토리 (모양 변형 정보) */
    UPROPERTY(ReplicatedUsing = OnRep_HitHistory)
    TArray<FMDFHitData> HitHistory;

    /** 히스토리가 변경되면 메쉬를 변형합니다. */
    UFUNCTION()
    void OnRep_HitHistory();

    // 데이터 로드 재시도 로직
    void TryLoadDataFromGameState();

    FTimerHandle LoadRetryTimerHandle;
    int32 LoadRetryCount = 0;
    
    /** 이펙트 재생 (사운드, 나이아가라) */
    UFUNCTION(NetMulticast, Unreliable)
    void NetMulticast_PlayEffects(const TArray<FMDFHitData>& NewHits);
    
public:
    /** 원본으로 사용할 StaticMesh 에셋 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeshDeformation|설정", meta = (DisplayName = "스태틱 메쉬(StaticMesh)"))
    TObjectPtr<UStaticMesh> SourceStaticMesh;
    
    /** 에셋을 기반으로 DynamicMesh를 초기화하는 함수 */
    UFUNCTION(BlueprintCallable, Category = "MeshDeformation")
    void InitializeDynamicMesh();
    
    UFUNCTION(BlueprintCallable, Category = "MeshDeformation|수학")
    FVector ConvertWorldToLocal(FVector WorldLocation);

    UFUNCTION(BlueprintCallable, Category = "MeshDeformation|수학")
    FVector ConvertWorldDirectionToLocal(FVector WorldDirection);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeshDeformation|설정", meta = (DisplayName = "시스템 활성화"))
    bool bIsDeformationEnabled = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeshDeformation|설정", meta = (DisplayName = "변형 반경"))
    float DeformRadius = 30.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeshDeformation|설정", meta = (DisplayName = "변형 강도"))
    float DeformStrength = 5.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeshDeformation|디버그", meta = (DisplayName = "디버그 포인트 표시"))
    bool bShowDebugPoints = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeshDeformation|설정", meta = (DisplayName = "배칭 처리 대기 시간"))
    float BatchProcessDelay = 0.0f;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeshDeformation|설정", meta = (DisplayName = "파편 이펙트(Niagara)"))
    TObjectPtr<UNiagaraSystem> DebrisSystem;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeshDeformation|설정", meta = (DisplayName = "피격 사운드(3D)"))
    TObjectPtr<USoundBase> ImpactSound;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeshDeformation|설정", meta = (DisplayName = "사운드 감쇄 설정(3D Attenuation)"))
    TObjectPtr<USoundAttenuation> ImpactAttenuation;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeshDeformation|설정")
    TSubclassOf<UDamageType> RangedDamageType;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeshDeformation|설정")
    TSubclassOf<UDamageType> MeleeDamageType;

    // -------------------------------------------------------------------------
    // [월드 파티션 영속성 지원]
    // -------------------------------------------------------------------------

    /** GameState에 내 데이터를 맡길 때 사용하는 고유 ID */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MeshDeformation|설정", meta = (DisplayName = "고유 식별자(GUID)"))
    FGuid ComponentGuid;

    // -------------------------------------------------------------------------
    // [수리 시스템]
    // -------------------------------------------------------------------------

    /** 메쉬를 원상복구(수리)하고 히스토리를 초기화합니다. */ 
    UFUNCTION(BlueprintCallable, Category = "MeshDeformation|수리", meta = (DisplayName = "메시 수리(RepairMesh)"))
    void RepairMesh();
    
private:
    /** 1프레임 동안 쌓인 타격 지점 리스트 (배칭 큐) */
    TArray<FMDFHitData> HitQueue;

    /** 클라이언트가 어디까지 변형을 적용했는지 기억하는 인덱스 */
    int32 LastAppliedIndex = 0;

    /** 타이머 핸들 */
    FTimerHandle BatchTimerHandle;
};