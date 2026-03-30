// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HellunaBaseResourceUsingObject.generated.h"

// [김기현 - MDF 시스템] 전방 선언
class UDynamicMeshComponent;
class UMDF_DeformableComponent;
class UStaticMesh;

// [기존 로직] UI 상호작용
class UBoxComponent;

// 스캔 VFX
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UMeshComponent;

UCLASS()
class HELLUNA_API AHellunaBaseResourceUsingObject : public AActor
{
    GENERATED_BODY()
    
public:
    AHellunaBaseResourceUsingObject();

    // 에디터에서 값 변경 시 프리뷰 업데이트
    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void Tick(float DeltaTime) override;

protected:
    virtual void BeginPlay() override;

protected:
    // ==================================================================================
    // [김기현 - MDF 시스템 영역] 
    // ==================================================================================

    // [김기현] 기존 StaticMeshComponent를 대체하는 변형 가능한 메쉬 컴포넌트
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MDF | Components", meta = (AllowPrivateAccess = "true"))
    UDynamicMeshComponent* DynamicMeshComponent;

    // [김기현] 변형 로직 및 스태틱 메쉬 정보를 담고 있는 컴포넌트
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MDF | Components", meta = (AllowPrivateAccess = "true"))
    UMDF_DeformableComponent* DeformableComponent;


    // ==================================================================================
    // [기존 로직 - UI 상호작용 영역]
    // ==================================================================================

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ResouceUsing")
    UBoxComponent* ResouceUsingCollisionBox;

    UFUNCTION()
    virtual void CollisionBoxBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

    UFUNCTION()
    virtual void CollisionBoxEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

    // ==================================================================================
    // 배치 스캔 VFX (바닥→위 물질화 효과)
    // AInv_BuildingActor와 동일한 BeginPlay 기반 — 모든 클라이언트에서 즉시 시작
    // ==================================================================================
public:
    /** 배치 스캔 VFX 시작 */
    UFUNCTION(BlueprintCallable, Category = "건설|VFX",
        meta = (DisplayName = "Start Placement Scan VFX (배치 스캔 VFX 시작)"))
    void StartPlacementScanVFX();

    /** 스캔 VFX 머티리얼 (M_BuildingScan 할당) — BP에서 설정 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "건설|VFX",
        meta = (DisplayName = "Placement Scan Material (배치 스캔 머티리얼)"))
    TObjectPtr<UMaterialInterface> PlacementScanMaterial;

    /** 스캔 완료까지 걸리는 시간 (초) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "건설|VFX",
        meta = (DisplayName = "Scan Duration (스캔 시간)", ClampMin = "0.3", ClampMax = "5.0"))
    float ScanDuration = 1.5f;

    /** 스캔 밴드 높이 비율 (메시 전체 높이 대비) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "건설|VFX",
        meta = (DisplayName = "Scan Band Ratio (밴드 비율)", ClampMin = "0.05", ClampMax = "0.5"))
    float ScanBandRatio = 0.2f;

private:
    // === 스캔 내부 상태 ===
    UPROPERTY()
    TArray<TObjectPtr<UMeshComponent>> ScanSwappedMeshes;

    UPROPERTY()
    TArray<TObjectPtr<UMaterialInterface>> ScanOriginalMaterials;

    TArray<int32> ScanSwappedSlotCounts;

    UPROPERTY()
    TArray<TObjectPtr<UMaterialInstanceDynamic>> ScanDMIs;

    float ScanProgress = 0.f;
    float ScanBottomZ = 0.f;
    float ScanTopZ = 0.f;
    bool bScanActive = false;

    void TickPlacementScan(float DeltaTime);
};