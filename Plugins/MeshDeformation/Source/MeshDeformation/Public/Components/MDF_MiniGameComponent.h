// Gihyeon's MeshDeformation Project
// File: Source/MeshDeformation/Components/MDF_MiniGameComponent.h

#pragma once

#include "CoreMinimal.h"
#include "Components/MDF_DeformableComponent.h"
#include "MDF_MiniGameComponent.generated.h"

/**
 * [Step 4] 약점 데이터 구조체
 * - 네트워크 복제를 위해 bIsBroken 상태를 감시함
 */
USTRUCT(BlueprintType)
struct FWeakSpotData
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, meta = (IgnoreForMemberInitializationTest))
    FGuid ID;

    UPROPERTY(BlueprintReadOnly)
    FBox LocalBox;

    UPROPERTY(BlueprintReadOnly)
    float CurrentHP;

    UPROPERTY(BlueprintReadOnly)
    float MaxHP;

    // [네트워크] 이 값이 서버에서 바뀌면 클라이언트의 OnRep 함수가 트리거됨
    UPROPERTY(BlueprintReadOnly)
    bool bIsBroken = false;

    FWeakSpotData() 
        : ID(FGuid::NewGuid()), LocalBox(FBox(EForceInit::ForceInit)), CurrentHP(100.f), MaxHP(100.f), bIsBroken(false) {}
};

/**
 * [MDF_MiniGameComponent]
 * - 데디케이티드 서버 환경을 지원하도록 복제 로직이 추가됨
 * - 클라이언트에서 마킹한 영역을 서버로 전송하는 RPC 포함
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class MESHDEFORMATION_API UMDF_MiniGameComponent : public UMDF_DeformableComponent
{
    GENERATED_BODY()

public:
    UMDF_MiniGameComponent();

    // [네트워크] 변수 복제 규칙을 정의하는 필수 함수
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

public:
    UFUNCTION(BlueprintCallable, Category = "MDF|MiniGame")
    void StartMarking(FVector WorldLocation);

    UFUNCTION(BlueprintCallable, Category = "MDF|MiniGame")
    void UpdateMarking(FVector WorldLocation);

    UFUNCTION(BlueprintCallable, Category = "MDF|MiniGame")
    void EndMarking(FVector WorldLocation);

    UFUNCTION(BlueprintCallable, Category = "MDF|MiniGame")
    bool TryBreach(const FHitResult& HitInfo, float DamageAmount);

protected:
    virtual void HandlePointDamage(AActor* DamagedActor, float Damage, class AController* InstigatedBy, FVector HitLocation, class UPrimitiveComponent* FHitComponent, FName BoneName, FVector ShotFromDirection, const class UDamageType* DamageType, AActor* DamageCauser) override;

protected:
    // [유틸리티 함수]
    float CalculateHPFromBox(const FBox& Box) const;
    FVector GetLocalLocationFromWorld(FVector WorldLoc) const;
    
    // [네트워크] 서버 권한으로 파괴를 확정하는 함수
    void ExecuteDestruction(int32 WeakSpotIndex);

    // [네트워크] 실제 메쉬를 깎는 "시각적 연산"만 담당 (서버/클라 공통 실행)
    void ApplyVisualMeshCut(int32 Index);

    // [네트워크] 서버에서 데이터가 넘어왔을 때 클라이언트에서 실행될 함수
    UFUNCTION()
    void OnRep_WeakSpots();

    // -------------------------------------------------------------------------
    // [NEW] 클라이언트 → 서버 RPC (마킹 영역 전송)
    // -------------------------------------------------------------------------
    
    /** 클라이언트가 마킹을 완료하면 서버에 약점 생성을 요청 */
    UFUNCTION(Server, Reliable)
    void Server_RequestCreateWeakSpot(FVector BoxMin, FVector BoxMax);

    /** 실제 약점 생성 로직 (서버 전용) */
    void Internal_CreateWeakSpot(const FBox& LocalBox);

protected:
    bool bIsMarking = false;     
    bool bIsValidCut = false;    

    FVector LocalStartPoint;          
    FBox CurrentPreviewBox;           

    // [네트워크] ReplicatedUsing을 통해 상태 변화를 감시함
    UPROPERTY(ReplicatedUsing = OnRep_WeakSpots, VisibleAnywhere, BlueprintReadOnly, Category = "MDF|MiniGame")
    TArray<FWeakSpotData> WeakSpots;

    // [네트워크] 클라이언트가 이미 깎은 인덱스를 추적하여 중복 연산 방지
    TArray<int32> LocallyProcessedIndices;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MDF|Config", meta = (DisplayName = "HP 밀도 배율"))
    float HPDensityMultiplier = 0.1f;
    
    /** Z축(높이) 보정 기준 거리 - 끝점이 바닥에서 이 거리 이내면 바닥까지 자동 확장 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MDF|Config", meta = (DisplayName = "Z축 보정 거리"))
    float YAxisSnapThreshold = 30.0f;
    
    /** 절단 시 Z축 아래 방향으로 확장하는 거리 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MDF|Config", meta = (DisplayName = "절단 Z축 확장 (아래)"))
    float CutZExpansionDown = 20.0f;
    
    /** 절단 시 Z축 위 방향으로 확장하는 거리 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MDF|Config", meta = (DisplayName = "절단 Z축 확장 (위)"))
    float CutZExpansionUp = 10.0f;
    
    /** 절단 시 X축 좌측 방향으로 확장하는 거리 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MDF|Config", meta = (DisplayName = "절단 X축 확장 (좌)"))
    float CutXExpansionLeft = 10.0f;
    
    /** 절단 시 X축 우측 방향으로 확장하는 거리 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MDF|Config", meta = (DisplayName = "절단 X축 확장 (우)"))
    float CutXExpansionRight = 10.0f; 
};