#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "PCG/PCGBiomeDefinition.h"
#include "PCGGlowingFloraComponent.generated.h"

/**
 * 발광 식물 메시 엔트리.
 */
USTRUCT(BlueprintType)
struct FGlowingFloraMeshEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (DisplayName = "메시"))
	TSoftObjectPtr<UStaticMesh> Mesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (DisplayName = "가중치", ClampMin = "0.01"))
	float Weight = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (DisplayName = "최소 크기", ClampMin = "0.01"))
	float ScaleMin = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (DisplayName = "최대 크기", ClampMin = "0.01"))
	float ScaleMax = 1.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (DisplayName = "매몰 깊이(cm)", ClampMin = "0.0"))
	float BuryDepth = 5.f;
};

/**
 * 배치된 라이트 정보 (컬링용).
 */
struct FSpawnedFloraLight
{
	TWeakObjectPtr<UPointLightComponent> Light;
	FVector WorldLocation = FVector::ZeroVector;
	bool bCurrentlyActive = true;
};

/**
 * PCG 발광 식물 배치 컴포넌트
 *
 * 보라색 몽환 숲 바이옴에 최적화된 발광 버섯/식물 배치.
 * - HISM으로 메시 배치 (고성능)
 * - 클러스터 단위 배치: 3~8개씩 뭉쳐서 자연스러운 군락
 * - 클러스터 중심에 포인트 라이트 배치 (옵션)
 * - 거리 기반 라이트 컬링으로 GPU 부하 최소화
 * - 밤에 라이트 강도 증가 지원
 */
UCLASS(ClassGroup=(PCG), meta=(BlueprintSpawnableComponent))
class HELLUNA_API UPCGGlowingFloraComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPCGGlowingFloraComponent();

	// ────────────────────── 메시 설정 ──────────────────────

	/** 배치할 발광 메시 목록 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GlowingFlora|메시",
		meta = (DisplayName = "발광 메시 목록", TitleProperty = "Mesh"))
	TArray<FGlowingFloraMeshEntry> FloraMeshes;

	/** 머티리얼 오버라이드 (보라 발광 등) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GlowingFlora|메시",
		meta = (DisplayName = "머티리얼 오버라이드"))
	TSoftObjectPtr<UMaterialInterface> MaterialOverride;

	// ────────────────────── 배치 영역 ──────────────────────

	/** 배치 반경(cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GlowingFlora|영역",
		meta = (DisplayName = "배치 반경(cm)", ClampMin = "500.0"))
	float ScatterRadius = 8000.f;

	/** 클러스터 수 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GlowingFlora|영역",
		meta = (DisplayName = "클러스터 수", ClampMin = "1", ClampMax = "200"))
	int32 ClusterCount = 20;

	/** 클러스터당 최소 식물 수 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GlowingFlora|영역",
		meta = (DisplayName = "클러스터 최소 개수", ClampMin = "1"))
	int32 FloraPerClusterMin = 3;

	/** 클러스터당 최대 식물 수 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GlowingFlora|영역",
		meta = (DisplayName = "클러스터 최대 개수", ClampMin = "1"))
	int32 FloraPerClusterMax = 8;

	/** 클러스터 반경(cm) — 한 클러스터 내 식물들의 분산 범위 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GlowingFlora|영역",
		meta = (DisplayName = "클러스터 반경(cm)", ClampMin = "50.0"))
	float ClusterRadius = 300.f;

	/** 클러스터 간 최소 간격(cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GlowingFlora|영역",
		meta = (DisplayName = "클러스터 최소 간격(cm)", ClampMin = "0.0"))
	float ClusterMinSpacing = 600.f;

	/** 최대 배치 개수 (전체) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GlowingFlora|영역",
		meta = (DisplayName = "최대 배치 총수", ClampMin = "1"))
	int32 MaxTotalFlora = 500;

	/** 최대 경사(도) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GlowingFlora|영역",
		meta = (DisplayName = "최대 경사(도)", ClampMin = "0.0", ClampMax = "90.0"))
	float MaxSlopeAngle = 35.f;

	// ────────────────────── 라이트 설정 ──────────────────────

	/** 클러스터에 포인트 라이트 배치 여부 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GlowingFlora|라이트",
		meta = (DisplayName = "포인트 라이트 사용"))
	bool bSpawnPointLights = true;

	/** 라이트 색상 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GlowingFlora|라이트",
		meta = (DisplayName = "라이트 색상"))
	FLinearColor LightColor = FLinearColor(0.6f, 0.2f, 1.0f, 1.0f);

	/** 라이트 강도 (낮) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GlowingFlora|라이트",
		meta = (DisplayName = "낮 라이트 강도", ClampMin = "0.0"))
	float DayLightIntensity = 500.f;

	/** 라이트 강도 (밤) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GlowingFlora|라이트",
		meta = (DisplayName = "밤 라이트 강도", ClampMin = "0.0"))
	float NightLightIntensity = 2000.f;

	/** 라이트 감쇄 반경(cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GlowingFlora|라이트",
		meta = (DisplayName = "라이트 감쇄 반경(cm)", ClampMin = "50.0"))
	float LightAttenuationRadius = 500.f;

	/** 라이트를 활성화할 거리(cm) — 0이면 항상 활성 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GlowingFlora|라이트",
		meta = (DisplayName = "라이트 컬링 거리(cm)", ClampMin = "0.0"))
	float LightCullingDistance = 8000.f;

	/** 라이트 Z 오프셋(cm) — 클러스터 중심에서 위로 올림 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GlowingFlora|라이트",
		meta = (DisplayName = "라이트 Z 오프셋(cm)"))
	float LightZOffset = 80.f;

	/** HISM 최대 렌더 거리(cm) — 0이면 무제한 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GlowingFlora|성능",
		meta = (DisplayName = "최대 렌더 거리(cm)", ClampMin = "0.0"))
	float MaxDrawDistance = 0.f;

	// ────────────────────── 실행 설정 ──────────────────────

	/** BeginPlay 시 자동 배치 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GlowingFlora|실행",
		meta = (DisplayName = "자동 배치"))
	bool bScatterOnBeginPlay = false;

	/** 랜덤 시드 (0 = 매번 다름) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GlowingFlora|실행",
		meta = (DisplayName = "랜덤 시드"))
	int32 RandomSeed = 0;

	/** 라이트 컬링 체크 간격(초) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GlowingFlora|실행",
		meta = (DisplayName = "컬링 간격(초)", ClampMin = "0.1"))
	float CullingInterval = 0.5f;

	// ────────────────────── 공개 함수 ──────────────────────

	UFUNCTION(BlueprintCallable, Category = "GlowingFlora")
	void Scatter();

	UFUNCTION(BlueprintCallable, Category = "GlowingFlora")
	void Clear();

	/** 밤/낮 모드 전환 — 라이트 강도 변경 */
	UFUNCTION(BlueprintCallable, Category = "GlowingFlora")
	void SetNightMode(bool bInIsNight);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "GlowingFlora")
	int32 GetTotalFloraCount() const;

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

private:
	UPROPERTY()
	TMap<int32, UHierarchicalInstancedStaticMeshComponent*> HISMComponents;

	TArray<FSpawnedFloraLight> SpawnedLights;

	FCollisionQueryParams CachedTraceParams;
	bool bTraceParamsCached = false;
	bool bIsNight = false;
	float CullingTimer = 0.f;

	void CacheTraceParams();
	int32 SelectMeshIndex(FRandomStream& Rng) const;
	bool TraceToGround(const FVector& InXY, float ZHigh, float ZLow,
		FVector& OutLocation, FVector& OutNormal) const;
	UHierarchicalInstancedStaticMeshComponent* GetOrCreateHISM(int32 MeshIdx);
	void UpdateLightCulling();
};
