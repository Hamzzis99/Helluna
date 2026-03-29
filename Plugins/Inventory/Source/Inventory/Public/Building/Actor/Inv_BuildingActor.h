// Gihyeon's Inventory Project

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Inv_BuildingActor.generated.h"

class UMaterialInterface;
class UMaterialInstanceDynamic;
class UMeshComponent;

/**
 * 리플리케이션이 가능한 건물 베이스 액터
 */
UCLASS(Blueprintable)
class INVENTORY_API AInv_BuildingActor : public AActor
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	AInv_BuildingActor();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

	// 건물 메시 컴포넌트
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "건설", meta = (DisplayName = "건물 메시"))
	TObjectPtr<UStaticMeshComponent> BuildingMesh;

public:
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	// 건물이 배치되었을 때 호출 (블루프린트에서 오버라이드 가능)
	UFUNCTION(BlueprintNativeEvent, Category = "건설", meta = (DisplayName = "건물 배치됨"))
	void OnBuildingPlaced();
	virtual void OnBuildingPlaced_Implementation();

	// =========================================================================================
	// 배치 스캔 VFX (아래→위 메시 스캔 이펙트)
	// =========================================================================================

	/** 배치 스캔 VFX 시작 — Multicast_OnBuildingPlaced에서 호출 */
	UFUNCTION(BlueprintCallable, Category = "건설|VFX", meta = (DisplayName = "Start Placement Scan VFX (배치 스캔 VFX 시작)"))
	void StartPlacementScanVFX();

	/** 스캔 VFX에 사용할 와이어프레임 머티리얼 (M_ScanBand_Wireframe_V2 할당) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "건설|VFX",
		meta = (DisplayName = "Placement Scan Material (배치 스캔 머티리얼)", Tooltip = "건물 배치 시 아래→위로 스캔되는 와이어프레임 머티리얼입니다."))
	TObjectPtr<UMaterialInterface> PlacementScanMaterial;

	/** 스캔 완료까지 걸리는 시간 (초) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "건설|VFX",
		meta = (DisplayName = "Scan Duration (스캔 시간)", ClampMin = "0.3", ClampMax = "5.0"))
	float ScanDuration = 1.5f;

	/** 스캔 밴드 높이 비율 (메시 전체 높이 대비, 0.2 = 20%) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "건설|VFX",
		meta = (DisplayName = "Scan Band Ratio (밴드 비율)", ClampMin = "0.05", ClampMax = "0.5"))
	float ScanBandRatio = 0.2f;

	/** 오버레이 메시 스케일 오프셋 (원본보다 약간 크게 — Z-fighting 방지) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "건설|VFX",
		meta = (DisplayName = "Overlay Scale Offset (오버레이 스케일)", ClampMin = "1.0", ClampMax = "1.1"))
	float OverlayScaleOffset = 1.02f;

private:
	// === 스캔 오버레이 내부 상태 ===

	/** SetOverlayMaterial이 적용된 메시 컴포넌트 배열 (Static/Dynamic/Skeletal 모두 대상) */
	UPROPERTY()
	TArray<TObjectPtr<UMeshComponent>> ScanOverlayTargets;

	/** 스캔 머티리얼 다이내믹 인스턴스 배열 (ScanOverlayTargets와 1:1 대응) */
	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> ScanDMIs;

	/** 스캔 진행도 (0.0=바닥, 1.0=꼭대기) */
	float ScanProgress = 0.f;

	/** 메시 바운딩 바닥 Z */
	float ScanBottomZ = 0.f;

	/** 메시 바운딩 꼭대기 Z */
	float ScanTopZ = 0.f;

	/** 스캔 애니메이션 활성 여부 */
	bool bScanActive = false;

	/** 스캔 Tick 처리 */
	void TickPlacementScan(float DeltaTime);
};

