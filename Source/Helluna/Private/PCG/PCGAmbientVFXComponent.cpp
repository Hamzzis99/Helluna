#include "PCG/PCGAmbientVFXComponent.h"
#include "PCG/PCGScatterUtils.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "NiagaraFunctionLibrary.h"
#include "Components/InstancedStaticMeshComponent.h"

UPCGAmbientVFXComponent::UPCGAmbientVFXComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.5f; // 컬링용 저빈도 틱
}

void UPCGAmbientVFXComponent::BeginPlay()
{
	Super::BeginPlay();

	if (bScatterOnBeginPlay)
	{
		Scatter();
	}
}

void UPCGAmbientVFXComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (ActivationDistance <= 0.f || SpawnedVFXList.Num() == 0) return;

	CullingTimer += DeltaTime;
	if (CullingTimer >= CullingInterval)
	{
		CullingTimer = 0.f;
		UpdateCulling();
	}
}

// ══════════════════════════════════════════��═════════════════════
// 캐싱
// ═══════════════════════════════════════════���════════════════════

void UPCGAmbientVFXComponent::CacheTraceParams()
{
	if (bTraceParamsCached) return;
	CachedTraceParams = PCGScatterUtils::BuildFoliageIgnoreParams(GetWorld(), FName(TEXT("AmbientVFXGround")));
	bTraceParamsCached = true;
}

bool UPCGAmbientVFXComponent::TraceToGround(const FVector& InXY, float ZHigh, float ZLow,
	FVector& OutLocation, FVector& OutNormal) const
{
	UWorld* World = GetWorld();
	if (!World) return false;

	FHitResult Hit;
	if (World->LineTraceSingleByChannel(Hit,
		FVector(InXY.X, InXY.Y, ZHigh),
		FVector(InXY.X, InXY.Y, ZLow),
		ECC_Visibility, CachedTraceParams))
	{
		OutLocation = Hit.ImpactPoint;
		OutNormal = Hit.ImpactNormal;
		return true;
	}
	return false;
}

// ════════════════════════════════════════════════════════════════
// Scatter
// ══════════════════════════════════════════════��═════════════════

void UPCGAmbientVFXComponent::Scatter()
{
	Clear();

	if (VFXEntries.Num() == 0 || VFXCount <= 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[AmbientVFX] VFX 목록 없음 — 스킵"));
		return;
	}

	CacheTraceParams();

	const FVector Origin = GetOwner()->GetActorLocation();
	FRandomStream Rng(RandomSeed != 0 ? RandomSeed : FMath::Rand());

	const float MinSpacingSq = MinSpacing * MinSpacing;
	const float MaxSlopeCos = FMath::Cos(FMath::DegreesToRadians(MaxSlopeAngle));
	const int32 MaxAttempts = VFXCount * 4;
	const bool bUseBiome = BiomeZones.Num() > 0;

	TArray<FVector> PlacedLocs;
	PlacedLocs.Reserve(VFXCount);

	int32 Placed = 0;
	int32 Attempts = 0;

	while (Placed < VFXCount && Attempts < MaxAttempts)
	{
		Attempts++;

		const float Angle = Rng.FRandRange(0.f, 360.f);
		const float Dist = FMath::Sqrt(Rng.FRandRange(0.f, 1.f)) * ScatterRadius;
		const FVector RandXY = Origin + FVector(
			FMath::Cos(FMath::DegreesToRadians(Angle)) * Dist,
			FMath::Sin(FMath::DegreesToRadians(Angle)) * Dist,
			0.f);

		FVector GroundLoc, GroundNormal;
		if (!TraceToGround(RandXY, Origin.Z + 5000.f, Origin.Z - 5000.f, GroundLoc, GroundNormal))
			continue;

		if (FVector::DotProduct(GroundNormal, FVector::UpVector) < MaxSlopeCos)
			continue;

		bool bTooClose = false;
		for (const FVector& Prev : PlacedLocs)
		{
			if (FVector::DistSquared(GroundLoc, Prev) < MinSpacingSq)
			{
				bTooClose = true;
				break;
			}
		}
		if (bTooClose) continue;

		// 바이옴 판정
		EPCGBiomeType Biome = bUseBiome ? DetermineBiome(GroundLoc, BiomeZones) : EPCGBiomeType::Any;

		// 이 바이옴에 맞는 VFX 후보 필터링
		TArray<int32> ValidIndices;
		float TotalWeight = 0.f;
		for (int32 i = 0; i < VFXEntries.Num(); ++i)
		{
			const FAmbientVFXEntry& E = VFXEntries[i];
			if (E.BiomeType == EPCGBiomeType::Any || E.BiomeType == Biome)
			{
				ValidIndices.Add(i);
				TotalWeight += E.Weight;
			}
		}
		if (ValidIndices.Num() == 0) continue;

		// 가중치 선택
		float Roll = Rng.FRandRange(0.f, TotalWeight);
		int32 SelectedIdx = ValidIndices.Last();
		for (int32 VI : ValidIndices)
		{
			Roll -= VFXEntries[VI].Weight;
			if (Roll <= 0.f) { SelectedIdx = VI; break; }
		}

		const FAmbientVFXEntry& Entry = VFXEntries[SelectedIdx];
		UNiagaraSystem* NS = Entry.NiagaraSystem.LoadSynchronous();
		if (!NS) continue;

		const float Scale = Rng.FRandRange(Entry.ScaleMin, Entry.ScaleMax);
		FVector SpawnLoc = GroundLoc;
		SpawnLoc.Z += Entry.ZOffset;

		UNiagaraComponent* NComp = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			GetWorld(), NS, SpawnLoc,
			FRotator(0.f, Rng.FRandRange(0.f, 360.f), 0.f),
			FVector(Scale),
			/*bAutoDestroy=*/false,
			/*bAutoActivate=*/true);

		if (!NComp) continue;

		// 밤 전용이면 현재 낮이므로 비활성화
		if (Entry.bNightOnly && !bIsNight)
		{
			NComp->Deactivate();
		}

		FSpawnedVFX VFXData;
		VFXData.Component = NComp;
		VFXData.WorldLocation = SpawnLoc;
		VFXData.bNightOnly = Entry.bNightOnly;
		VFXData.bCurrentlyActive = !Entry.bNightOnly || bIsNight;
		SpawnedVFXList.Add(VFXData);

		PlacedLocs.Add(GroundLoc);
		Placed++;
	}

	UE_LOG(LogTemp, Warning, TEXT("[AmbientVFX] 배치 완료 | 요청: %d | 실제: %d"), VFXCount, Placed);
}

// ════════════════════════════════════════════��═══════════════════
// 밤/낮 토글
// ══════════════════════════════════════════════��═════════════════

void UPCGAmbientVFXComponent::SetNightMode(bool bInIsNight)
{
	bIsNight = bInIsNight;

	for (FSpawnedVFX& VFX : SpawnedVFXList)
	{
		if (!VFX.Component.IsValid()) continue;

		if (VFX.bNightOnly)
		{
			if (bIsNight && !VFX.bCurrentlyActive)
			{
				VFX.Component->Activate(true);
				VFX.bCurrentlyActive = true;
			}
			else if (!bIsNight && VFX.bCurrentlyActive)
			{
				VFX.Component->Deactivate();
				VFX.bCurrentlyActive = false;
			}
		}
	}
}

// ═══════════════════════════��════════════════════════════════════
// 거리 기반 컬링
// ══════════════════════════════���══════════════════════��══════════

void UPCGAmbientVFXComponent::UpdateCulling()
{
	ACharacter* Player = UGameplayStatics::GetPlayerCharacter(GetWorld(), 0);
	if (!Player) return;

	const FVector PlayerLoc = Player->GetActorLocation();
	const float DistSq = ActivationDistance * ActivationDistance;

	for (FSpawnedVFX& VFX : SpawnedVFXList)
	{
		if (!VFX.Component.IsValid()) continue;

		// 밤 전용인데 낮이면 무조건 비활성화
		if (VFX.bNightOnly && !bIsNight)
		{
			if (VFX.bCurrentlyActive)
			{
				VFX.Component->Deactivate();
				VFX.bCurrentlyActive = false;
			}
			continue;
		}

		const bool bInRange = FVector::DistSquared(PlayerLoc, VFX.WorldLocation) <= DistSq;

		if (bInRange && !VFX.bCurrentlyActive)
		{
			VFX.Component->Activate(true);
			VFX.bCurrentlyActive = true;
		}
		else if (!bInRange && VFX.bCurrentlyActive)
		{
			VFX.Component->Deactivate();
			VFX.bCurrentlyActive = false;
		}
	}
}

// ═══════════════════════════════════════════════���════════════════
// Clear
// ═════════════════════════════════════════════════��══════════════

void UPCGAmbientVFXComponent::Clear()
{
	for (FSpawnedVFX& VFX : SpawnedVFXList)
	{
		if (VFX.Component.IsValid())
		{
			VFX.Component->DestroyComponent();
		}
	}
	SpawnedVFXList.Empty();
	bTraceParamsCached = false;
}

int32 UPCGAmbientVFXComponent::GetActiveVFXCount() const
{
	int32 Count = 0;
	for (const FSpawnedVFX& VFX : SpawnedVFXList)
	{
		if (VFX.bCurrentlyActive && VFX.Component.IsValid()) Count++;
	}
	return Count;
}
