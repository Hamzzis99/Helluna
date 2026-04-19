#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "NiagaraSystem.h"
#include "NiagaraComponent.h"
#include "PCG/PCGBiomeDefinition.h"
#include "PCGAmbientVFXComponent.generated.h"

/**
 * 분위기 VFX 하나에 대한 설정
 */
USTRUCT(BlueprintType)
struct FAmbientVFXEntry
{
	GENERATED_BODY()

	/** 배치할 나이아가라 시스템 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (DisplayName = "나이아가라 시스템"))
	TSoftObjectPtr<UNiagaraSystem> NiagaraSystem;

	/** 이 VFX가 적용될 바이옴 (Any = 모든 바이옴) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (DisplayName = "바이옴"))
	EPCGBiomeType BiomeType = EPCGBiomeType::Any;

	/** 선택 가중치 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (DisplayName = "가중치", ClampMin = "0.01"))
	float Weight = 1.f;

	/** 스케일 범위 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (DisplayName = "최소 크기", ClampMin = "0.01"))
	float ScaleMin = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (DisplayName = "최대 크기", ClampMin = "0.01"))
	float ScaleMax = 1.5f;

	/** 지면에서의 Z 오프셋(cm) — 0이면 지면 위, 양수면 공중에 떠있음 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (DisplayName = "Z 오프셋(cm)"))
	float ZOffset = 0.f;

	/** 밤에만 활성화할지 여부 (빛나는 버섯, 반딧불 등) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (DisplayName = "밤 전용"))
	bool bNightOnly = false;
};

/**
 * PCG 분위기 VFX 배치 컴포넌트
 *
 * 바이옴별로 분위기를 연출하는 나이아가라 시스템을 Landscape 위에 배치한다.
 * - 사막: 먼지 파티클, 열기 이펙트
 * - 대리석: 크리스탈 반짝임, 빛줄기
 * - 몽환 숲: 빛나는 버섯, 포자, 안개
 *
 * 성능 고려:
 * - 플레이어 근처만 활성화하는 거리 기반 컬링
 * - 밤/낮 자동 토글 지원
 */
UCLASS(ClassGroup=(PCG), meta=(BlueprintSpawnableComponent))
class HELLUNA_API UPCGAmbientVFXComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPCGAmbientVFXComponent();

	// ────────────────────── 설정 ──────────────────────

	/** VFX 목록 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AmbientVFX|VFX",
		meta = (DisplayName = "VFX 목록", TitleProperty = "NiagaraSystem"))
	TArray<FAmbientVFXEntry> VFXEntries;

	/** 바이옴 영역 정의 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AmbientVFX|바이옴",
		meta = (DisplayName = "바이옴 영역", TitleProperty = "BiomeType"))
	TArray<FPCGBiomeZone> BiomeZones;

	/** 배치 반경(cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AmbientVFX|영역",
		meta = (DisplayName = "배치 반경(cm)", ClampMin = "100.0"))
	float ScatterRadius = 8000.f;

	/** 배치할 VFX 수 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AmbientVFX|영역",
		meta = (DisplayName = "VFX 수", ClampMin = "1"))
	int32 VFXCount = 30;

	/** VFX 간 최소 간격(cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AmbientVFX|영역",
		meta = (DisplayName = "최소 간격(cm)", ClampMin = "0.0"))
	float MinSpacing = 500.f;

	/** 최대 경사도(도) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AmbientVFX|제외",
		meta = (DisplayName = "최대 경사도(도)", ClampMin = "0.0", ClampMax = "90.0"))
	float MaxSlopeAngle = 40.f;

	/** 플레이어로부터 이 거리 이내의 VFX만 활성화(cm) — 0이면 컬링 비활성화 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AmbientVFX|성능",
		meta = (DisplayName = "활성화 거리(cm) (0=항상)", ClampMin = "0.0"))
	float ActivationDistance = 10000.f;

	/** 컬링 체크 간격(초) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AmbientVFX|성능",
		meta = (DisplayName = "컬링 주기(초)", ClampMin = "0.1"))
	float CullingInterval = 1.f;

	/** BeginPlay 시 자동 배치 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AmbientVFX|실행",
		meta = (DisplayName = "자동 배치"))
	bool bScatterOnBeginPlay = false;

	/** 랜덤 시드 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AmbientVFX|실행",
		meta = (DisplayName = "랜덤 시드 (0=랜덤)"))
	int32 RandomSeed = 0;

	// ────────────────────── 공개 함수 ──────────────────────

	UFUNCTION(BlueprintCallable, Category = "AmbientVFX")
	void Scatter();

	UFUNCTION(BlueprintCallable, Category = "AmbientVFX")
	void Clear();

	/** 밤/낮 전환 시 호출 — 밤 전용 VFX 토글 */
	UFUNCTION(BlueprintCallable, Category = "AmbientVFX")
	void SetNightMode(bool bIsNight);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "AmbientVFX")
	int32 GetActiveVFXCount() const;

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

private:
	struct FSpawnedVFX
	{
		TWeakObjectPtr<UNiagaraComponent> Component;
		FVector WorldLocation = FVector::ZeroVector;
		bool bNightOnly = false;
		bool bCurrentlyActive = false;
	};

	TArray<FSpawnedVFX> SpawnedVFXList;

	bool bIsNight = false;
	float CullingTimer = 0.f;

	FCollisionQueryParams CachedTraceParams;
	bool bTraceParamsCached = false;
	void CacheTraceParams();

	bool TraceToGround(const FVector& InXY, float ZHigh, float ZLow,
		FVector& OutLocation, FVector& OutNormal) const;

	void UpdateCulling();
};
