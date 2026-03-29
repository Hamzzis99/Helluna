// Gihyeon's Inventory Project

#include "Building/Actor/Inv_BuildingActor.h"
#include "Inventory.h"
#include "Components/StaticMeshComponent.h"
#include "Components/MeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"

// Sets default values
AInv_BuildingActor::AInv_BuildingActor()
{
	// Tick은 기본 비활성 — 스캔 VFX 활성 시에만 동적으로 켜짐
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	// 리플리케이션 활성화
	bReplicates = true;
	bAlwaysRelevant = true; // 모든 클라이언트에게 항상 관련성 있음

	// 건물 메시 컴포넌트 생성
	BuildingMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BuildingMesh"));
	RootComponent = BuildingMesh;

	// 충돌 설정
	BuildingMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	BuildingMesh->SetCollisionObjectType(ECollisionChannel::ECC_WorldStatic);
	BuildingMesh->SetCollisionResponseToAllChannels(ECR_Block);
}

// Called when the game starts or when spawned
void AInv_BuildingActor::BeginPlay()
{
	Super::BeginPlay();

	// 리플리케이션 설정 (BeginPlay에서 호출)
	SetReplicateMovement(true);

	// 서버에서만 OnBuildingPlaced 호출
	if (HasAuthority())
	{
		OnBuildingPlaced();
	}
}

// Called every frame
void AInv_BuildingActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// 스캔 VFX 애니메이션
	if (bScanActive)
	{
		TickPlacementScan(DeltaTime);
	}
}

void AInv_BuildingActor::OnBuildingPlaced_Implementation()
{
	// 블루프린트에서 오버라이드하여 사용
	// 예: 이펙트 재생, 사운드 재생 등
#if INV_DEBUG_BUILD
	UE_LOG(LogTemp, Warning, TEXT("Building placed: %s"), *GetName());
#endif
}

// ============================================================================
// 배치 스캔 VFX
// ============================================================================

void AInv_BuildingActor::StartPlacementScanVFX()
{
	// 머티리얼 미설정 시 스킵
	if (!PlacementScanMaterial)
	{
#if INV_DEBUG_BUILD
		UE_LOG(LogTemp, Log, TEXT("[BuildingScanVFX] PlacementScanMaterial is null — skipping scan VFX for %s"), *GetName());
#endif
		return;
	}

	// 이미 스캔 중이면 무시
	if (bScanActive)
	{
		return;
	}

	// 모든 MeshComponent 수집 (StaticMesh + DynamicMesh + SkeletalMesh)
	TArray<UMeshComponent*> AllMeshes;
	GetComponents<UMeshComponent>(AllMeshes);

	if (AllMeshes.Num() == 0)
	{
#if INV_DEBUG_BUILD
		UE_LOG(LogTemp, Warning, TEXT("[BuildingScanVFX] No MeshComponent found on %s — skipping"), *GetName());
#endif
		return;
	}

	// 액터 전체 바운딩 박스에서 바닥/꼭대기 Z 계산
	FVector BoundsOrigin, BoundsExtent;
	GetActorBounds(false, BoundsOrigin, BoundsExtent);
	ScanBottomZ = BoundsOrigin.Z - BoundsExtent.Z;
	ScanTopZ = BoundsOrigin.Z + BoundsExtent.Z;

	// 높이가 너무 작으면 스킵
	const float MeshHeight = ScanTopZ - ScanBottomZ;
	if (MeshHeight < 1.f)
	{
		return;
	}

	const float BandWidth = FMath::Max(MeshHeight * ScanBandRatio, 10.f);

	// 각 MeshComponent에 SetOverlayMaterial 적용 (메시 복제 없이 오버레이)
	for (UMeshComponent* MeshComp : AllMeshes)
	{
		if (!MeshComp || !MeshComp->IsVisible()) continue;

		UMaterialInstanceDynamic* DMI = UMaterialInstanceDynamic::Create(PlacementScanMaterial, this);
		if (!DMI) continue;

		DMI->SetScalarParameterValue(TEXT("ScanHeight"), ScanBottomZ);
		DMI->SetScalarParameterValue(TEXT("BandWidth"), BandWidth);

		MeshComp->SetOverlayMaterial(DMI);
		ScanOverlayTargets.Add(MeshComp);
		ScanDMIs.Add(DMI);
	}

	// 오버레이가 하나도 적용되지 않았으면 스킵
	if (ScanOverlayTargets.Num() == 0)
	{
		return;
	}

	// 스캔 시작
	ScanProgress = 0.f;
	bScanActive = true;
	SetActorTickEnabled(true);

#if INV_DEBUG_BUILD
	UE_LOG(LogTemp, Warning, TEXT("[BuildingScanVFX] Scan started: %s (%d meshes, Bottom=%.0f, Top=%.0f, Duration=%.1fs)"),
		*GetName(), ScanOverlayTargets.Num(), ScanBottomZ, ScanTopZ, ScanDuration);
#endif
}

void AInv_BuildingActor::TickPlacementScan(float DeltaTime)
{
	// 스캔 진행 (0→1)
	ScanProgress += DeltaTime / FMath::Max(ScanDuration, 0.1f);

	if (ScanProgress >= 1.0f)
	{
		// 스캔 완료 — 모든 오버레이 머티리얼 제거
		for (UMeshComponent* MeshComp : ScanOverlayTargets)
		{
			if (IsValid(MeshComp))
			{
				MeshComp->SetOverlayMaterial(nullptr);
			}
		}
		ScanOverlayTargets.Empty();
		ScanDMIs.Empty();
		bScanActive = false;
		SetActorTickEnabled(false);

#if INV_DEBUG_BUILD
		UE_LOG(LogTemp, Warning, TEXT("[BuildingScanVFX] Scan completed: %s"), *GetName());
#endif
		return;
	}

	// 모든 DMI의 ScanHeight 업데이트: BottomZ → TopZ로 Lerp
	const float CurrentScanZ = FMath::Lerp(ScanBottomZ, ScanTopZ, ScanProgress);
	const float MeshHeight = ScanTopZ - ScanBottomZ;
	const float BandWidth = FMath::Max(MeshHeight * ScanBandRatio, 10.f);

	for (UMaterialInstanceDynamic* DMI : ScanDMIs)
	{
		if (DMI)
		{
			DMI->SetScalarParameterValue(TEXT("ScanHeight"), CurrentScanZ);
			DMI->SetScalarParameterValue(TEXT("BandWidth"), BandWidth);
		}
	}
}

