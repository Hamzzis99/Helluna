#include "PCG/PCGGlowingFloraComponent.h"
#include "PCG/PCGScatterUtils.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Character.h"

UPCGGlowingFloraComponent::UPCGGlowingFloraComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.5f;
}

void UPCGGlowingFloraComponent::BeginPlay()
{
	Super::BeginPlay();

	if (bScatterOnBeginPlay)
	{
		Scatter();
	}
}

void UPCGGlowingFloraComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (LightCullingDistance <= 0.f || SpawnedLights.Num() == 0) return;

	CullingTimer += DeltaTime;
	if (CullingTimer >= CullingInterval)
	{
		CullingTimer = 0.f;
		UpdateLightCulling();
	}
}

// ════════════════════════════════════════════════════════════════
// 유틸리티
// ════════════════════════════════════════════════════════════════

void UPCGGlowingFloraComponent::CacheTraceParams()
{
	if (bTraceParamsCached) return;
	CachedTraceParams = PCGScatterUtils::BuildFoliageIgnoreParams(GetWorld(), FName(TEXT("GlowingFloraGround")));
	bTraceParamsCached = true;
}

int32 UPCGGlowingFloraComponent::SelectMeshIndex(FRandomStream& Rng) const
{
	float TotalWeight = 0.f;
	for (const FGlowingFloraMeshEntry& E : FloraMeshes)
		TotalWeight += E.Weight;

	float Roll = Rng.FRandRange(0.f, TotalWeight);
	for (int32 i = 0; i < FloraMeshes.Num(); ++i)
	{
		Roll -= FloraMeshes[i].Weight;
		if (Roll <= 0.f) return i;
	}
	return FloraMeshes.Num() - 1;
}

bool UPCGGlowingFloraComponent::TraceToGround(const FVector& InXY, float ZHigh, float ZLow,
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

UHierarchicalInstancedStaticMeshComponent* UPCGGlowingFloraComponent::GetOrCreateHISM(int32 MeshIdx)
{
	if (auto* Found = HISMComponents.Find(MeshIdx))
		return *Found;

	UStaticMesh* LoadedMesh = FloraMeshes[MeshIdx].Mesh.LoadSynchronous();
	if (!LoadedMesh) return nullptr;

	auto* HISM = NewObject<UHierarchicalInstancedStaticMeshComponent>(GetOwner());
	HISM->SetStaticMesh(LoadedMesh);
	HISM->SetMobility(EComponentMobility::Static);
	HISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HISM->SetCastShadow(false); // 발광체는 그림자 불필요

	// 머티리얼 오버라이드
	if (UMaterialInterface* Mat = MaterialOverride.LoadSynchronous())
	{
		for (int32 i = 0; i < LoadedMesh->GetStaticMaterials().Num(); ++i)
		{
			HISM->SetMaterial(i, Mat);
		}
	}

	if (MaxDrawDistance > 0.f)
	{
		HISM->SetCullDistances(0, MaxDrawDistance);
	}

	HISM->RegisterComponent();
	HISM->AttachToComponent(GetOwner()->GetRootComponent(),
		FAttachmentTransformRules::KeepWorldTransform);

	HISMComponents.Add(MeshIdx, HISM);
	return HISM;
}

// ════════════════════════════════════════════════════════════════
// Scatter — 클러스터 기반 배치
// ════════════════════════════════════════════════════════════════

void UPCGGlowingFloraComponent::Scatter()
{
	Clear();

	if (FloraMeshes.Num() == 0 || ClusterCount <= 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[GlowingFlora] 메시 또는 클러스터 설정 없음 -- 스킵"));
		return;
	}

	CacheTraceParams();

	const FVector Origin = GetOwner()->GetActorLocation();
	FRandomStream Rng(RandomSeed != 0 ? RandomSeed : FMath::Rand());

	const float MaxSlopeCos = FMath::Cos(FMath::DegreesToRadians(MaxSlopeAngle));
	const float ClusterSpacingSq = ClusterMinSpacing * ClusterMinSpacing;

	TArray<FVector> ClusterCenters;
	ClusterCenters.Reserve(ClusterCount);

	int32 TotalPlaced = 0;
	int32 ClusterAttempts = 0;
	const int32 MaxClusterAttempts = ClusterCount * 4;

	// 1단계: 클러스터 중심 배치
	while (ClusterCenters.Num() < ClusterCount && ClusterAttempts < MaxClusterAttempts)
	{
		ClusterAttempts++;

		const FVector RandXY = PCGScatterUtils::RandomPointInCircle(Rng, Origin, ScatterRadius);

		FVector GroundLoc, GroundNormal;
		if (!TraceToGround(RandXY, Origin.Z + 5000.f, Origin.Z - 5000.f, GroundLoc, GroundNormal))
			continue;

		// 경사 체크
		if (FVector::DotProduct(GroundNormal, FVector::UpVector) < MaxSlopeCos)
			continue;

		// 클러스터 간격 체크
		if (!PCGScatterUtils::CheckMinSpacing(GroundLoc, ClusterCenters, ClusterSpacingSq))
			continue;

		ClusterCenters.Add(GroundLoc);
	}

	// 2단계: 각 클러스터에 식물 배치
	for (const FVector& Center : ClusterCenters)
	{
		if (TotalPlaced >= MaxTotalFlora) break;

		const int32 FloraCount = FMath::Min(
			Rng.RandRange(FloraPerClusterMin, FloraPerClusterMax),
			MaxTotalFlora - TotalPlaced);

		int32 ClusterPlaced = 0;

		for (int32 Attempt = 0; Attempt < FloraCount * 3 && ClusterPlaced < FloraCount; ++Attempt)
		{
			// 클러스터 중심에서 랜덤 오프셋
			const FVector FloraXY = PCGScatterUtils::RandomPointInCircle(Rng, Center, ClusterRadius);

			FVector GroundLoc, GroundNormal;
			if (!TraceToGround(FloraXY, Center.Z + 1000.f, Center.Z - 1000.f, GroundLoc, GroundNormal))
				continue;

			if (FVector::DotProduct(GroundNormal, FVector::UpVector) < MaxSlopeCos)
				continue;

			const int32 MeshIdx = SelectMeshIndex(Rng);
			auto* HISM = GetOrCreateHISM(MeshIdx);
			if (!HISM) continue;

			const FGlowingFloraMeshEntry& Entry = FloraMeshes[MeshIdx];
			const float Scale = Rng.FRandRange(Entry.ScaleMin, Entry.ScaleMax);

			// 회전: Yaw 랜덤, 약간의 기울기
			const FRotator Rot(
				Rng.FRandRange(-8.f, 8.f),
				Rng.FRandRange(0.f, 360.f),
				Rng.FRandRange(-8.f, 8.f));

			FVector FinalLoc = GroundLoc;
			FinalLoc.Z -= Entry.BuryDepth * Scale;

			HISM->AddInstance(FTransform(Rot, FinalLoc, FVector(Scale)), true);
			ClusterPlaced++;
			TotalPlaced++;
		}

		// 클러스터 중심에 포인트 라이트 배치
		if (bSpawnPointLights && ClusterPlaced > 0)
		{
			auto* Light = NewObject<UPointLightComponent>(GetOwner());
			Light->SetMobility(EComponentMobility::Movable);
			Light->SetLightColor(LightColor);
			Light->SetIntensity(bIsNight ? NightLightIntensity : DayLightIntensity);
			Light->SetAttenuationRadius(LightAttenuationRadius);
			Light->SetCastShadows(false);
			Light->SetWorldLocation(Center + FVector(0.f, 0.f, LightZOffset));
			Light->RegisterComponent();
			Light->AttachToComponent(GetOwner()->GetRootComponent(),
				FAttachmentTransformRules::KeepWorldTransform);

			FSpawnedFloraLight LightData;
			LightData.Light = Light;
			LightData.WorldLocation = Center;
			LightData.bCurrentlyActive = true;
			SpawnedLights.Add(LightData);
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("[GlowingFlora] 배치 완료 | 클러스터: %d | 식물: %d | 라이트: %d"),
		ClusterCenters.Num(), TotalPlaced, SpawnedLights.Num());
}

// ════════════════════════════════════════════════════════════════
// 밤/낮 토글
// ════════════════════════════════════════════════════════════════

void UPCGGlowingFloraComponent::SetNightMode(bool bInIsNight)
{
	bIsNight = bInIsNight;
	const float TargetIntensity = bIsNight ? NightLightIntensity : DayLightIntensity;

	for (FSpawnedFloraLight& LightData : SpawnedLights)
	{
		if (LightData.Light.IsValid() && LightData.bCurrentlyActive)
		{
			LightData.Light->SetIntensity(TargetIntensity);
		}
	}
}

// ════════════════════════════════════════════════════════════════
// 라이트 컬링
// ════════════════════════════════════════════════════════════════

void UPCGGlowingFloraComponent::UpdateLightCulling()
{
	ACharacter* Player = UGameplayStatics::GetPlayerCharacter(GetWorld(), 0);
	if (!Player) return;

	const FVector PlayerLoc = Player->GetActorLocation();
	const float DistSq = LightCullingDistance * LightCullingDistance;
	const float TargetIntensity = bIsNight ? NightLightIntensity : DayLightIntensity;

	for (FSpawnedFloraLight& LightData : SpawnedLights)
	{
		if (!LightData.Light.IsValid()) continue;

		const bool bInRange = FVector::DistSquared(PlayerLoc, LightData.WorldLocation) <= DistSq;

		if (bInRange && !LightData.bCurrentlyActive)
		{
			LightData.Light->SetVisibility(true);
			LightData.Light->SetIntensity(TargetIntensity);
			LightData.bCurrentlyActive = true;
		}
		else if (!bInRange && LightData.bCurrentlyActive)
		{
			LightData.Light->SetVisibility(false);
			LightData.bCurrentlyActive = false;
		}
	}
}

// ════════════════════════════════════════════════════════════════
// Clear
// ════════════════════════════════════════════════════════════════

void UPCGGlowingFloraComponent::Clear()
{
	for (auto& Pair : HISMComponents)
	{
		if (IsValid(Pair.Value))
		{
			Pair.Value->ClearInstances();
			Pair.Value->DestroyComponent();
		}
	}
	HISMComponents.Empty();

	for (FSpawnedFloraLight& LightData : SpawnedLights)
	{
		if (LightData.Light.IsValid())
		{
			LightData.Light->DestroyComponent();
		}
	}
	SpawnedLights.Empty();
	bTraceParamsCached = false;
}

int32 UPCGGlowingFloraComponent::GetTotalFloraCount() const
{
	int32 Total = 0;
	for (const auto& Pair : HISMComponents)
	{
		if (IsValid(Pair.Value))
			Total += Pair.Value->GetInstanceCount();
	}
	return Total;
}
