// Gihyeon's Inventory Project

#include "Building/Actor/Inv_BuildingActor.h"
#include "Inventory.h"
#include "Components/StaticMeshComponent.h"
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

	// 건물 메시가 없으면 스킵
	if (!IsValid(BuildingMesh) || !BuildingMesh->GetStaticMesh())
	{
#if INV_DEBUG_BUILD
		UE_LOG(LogTemp, Warning, TEXT("[BuildingScanVFX] BuildingMesh or StaticMesh is invalid — skipping"));
#endif
		return;
	}

	// 이미 스캔 중이면 무시
	if (bScanActive)
	{
		return;
	}

	// 바운딩 박스에서 바닥/꼭대기 Z 계산
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

	// 오버레이 메시 컴포넌트 생성 (BuildingMesh와 동일한 스태틱메시 사용)
	ScanOverlayMesh = NewObject<UStaticMeshComponent>(this);
	if (!ScanOverlayMesh)
	{
		return;
	}

	ScanOverlayMesh->SetStaticMesh(BuildingMesh->GetStaticMesh());
	ScanOverlayMesh->SetWorldTransform(BuildingMesh->GetComponentTransform());
	ScanOverlayMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// DMI 생성 및 초기 파라미터 설정
	ScanDMI = UMaterialInstanceDynamic::Create(PlacementScanMaterial, this);
	if (!ScanDMI)
	{
		ScanOverlayMesh = nullptr;
		return;
	}

	ScanDMI->SetScalarParameterValue(TEXT("ScanHeight"), ScanBottomZ);
	const float BandWidth = FMath::Max(MeshHeight * ScanBandRatio, 10.f);
	ScanDMI->SetScalarParameterValue(TEXT("BandWidth"), BandWidth);

	// 모든 머티리얼 슬롯을 스캔 DMI로 교체
	const int32 NumMats = BuildingMesh->GetNumMaterials();
	for (int32 i = 0; i < NumMats; ++i)
	{
		ScanOverlayMesh->SetMaterial(i, ScanDMI);
	}

	// Z-fighting 방지: 원본보다 약간 크게
	const FVector OrigScale = BuildingMesh->GetComponentScale();
	ScanOverlayMesh->SetWorldScale3D(OrigScale * OverlayScaleOffset);
	ScanOverlayMesh->SetVisibility(true);
	ScanOverlayMesh->RegisterComponent();

	// 건물 메시에 부착 (이동/회전 동기화)
	if (GetRootComponent())
	{
		ScanOverlayMesh->AttachToComponent(
			GetRootComponent(),
			FAttachmentTransformRules::KeepWorldTransform);
	}

	// 스캔 시작
	ScanProgress = 0.f;
	bScanActive = true;
	SetActorTickEnabled(true);

#if INV_DEBUG_BUILD
	UE_LOG(LogTemp, Warning, TEXT("[BuildingScanVFX] Scan started: %s (Bottom=%.0f, Top=%.0f, Duration=%.1fs)"),
		*GetName(), ScanBottomZ, ScanTopZ, ScanDuration);
#endif
}

void AInv_BuildingActor::TickPlacementScan(float DeltaTime)
{
	if (!IsValid(ScanOverlayMesh))
	{
		// 오버레이가 무효화됨 — 정리
		bScanActive = false;
		SetActorTickEnabled(false);
		return;
	}

	// 스캔 진행 (0→1)
	ScanProgress += DeltaTime / FMath::Max(ScanDuration, 0.1f);

	if (ScanProgress >= 1.0f)
	{
		// 스캔 완료 — 오버레이 제거
		ScanOverlayMesh->DestroyComponent();
		ScanOverlayMesh = nullptr;
		ScanDMI = nullptr;
		bScanActive = false;
		SetActorTickEnabled(false);

#if INV_DEBUG_BUILD
		UE_LOG(LogTemp, Warning, TEXT("[BuildingScanVFX] Scan completed: %s"), *GetName());
#endif
		return;
	}

	// ScanHeight 업데이트: BottomZ → TopZ로 Lerp
	if (ScanDMI)
	{
		const float CurrentScanZ = FMath::Lerp(ScanBottomZ, ScanTopZ, ScanProgress);
		ScanDMI->SetScalarParameterValue(TEXT("ScanHeight"), CurrentScanZ);

		// 밴드 두께도 매 프레임 갱신 (에디터에서 실시간 조절 가능)
		const float MeshHeight = ScanTopZ - ScanBottomZ;
		const float BandWidth = FMath::Max(MeshHeight * ScanBandRatio, 10.f);
		ScanDMI->SetScalarParameterValue(TEXT("BandWidth"), BandWidth);
	}
}

