// Gihyeon's Inventory Project

#include "Building/Actor/Inv_BuildingActor.h"
#include "Inventory.h"
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
	// [Lag-Fix5] bAlwaysRelevant=true 제거 — 거리 기반 relevancy로 전환
	// 모든 건물이 모든 클라이언트에 항상 전송되면 O(건물수*플레이어수) 리플리케이션 부하
	bAlwaysRelevant = false;
	NetCullDistanceSquared = 150000.f * 150000.f; // 1500m

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

	// 모든 머신에서 즉시 스캔 VFX 시작 (Multicast 리플리케이션 대기 불필요)
	// PlacementScanMaterial은 블루프린트 CDO에서 로드되므로 서버/클라이언트 모두 유효
	// bScanActive 가드로 Multicast 도착 시 이중 적용 방지
	UE_LOG(LogTemp, Warning, TEXT("[ScanVFX-Diag] AInv_BuildingActor::BeginPlay → %s, HasAuth=%d, Material=%s"),
		*GetName(), HasAuthority(),
		PlacementScanMaterial ? *PlacementScanMaterial->GetName() : TEXT("NULL"));
	StartPlacementScanVFX();
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
	// [Lag-Fix3] 데디서버에서는 스캔 VFX 불필요 (렌더링 없음, DMI 생성/Tick 비용 제거)
	if (IsRunningDedicatedServer())
		return;

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
		UE_LOG(LogTemp, Warning, TEXT("[ScanVFX-Diag] %s: SKIP — bScanActive already true"), *GetName());
		return;
	}

	// 모든 MeshComponent 수집 (StaticMesh + DynamicMesh + SkeletalMesh)
	TArray<UMeshComponent*> AllMeshes;
	GetComponents<UMeshComponent>(AllMeshes);

	if (AllMeshes.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ScanVFX-Diag] %s: SKIP — no MeshComponent found"), *GetName());
		return;
	}

	// MeshComponent만의 바운딩 박스 합산 (DetectionSphere 등 비렌더링 컴포넌트 제외)
	FBox MeshBounds(ForceInit);
	int32 ValidMeshCount = 0;
	for (UMeshComponent* MeshComp : AllMeshes)
	{
		if (MeshComp && MeshComp->IsVisible() && MeshComp->GetNumMaterials() > 0)
		{
			MeshBounds += MeshComp->Bounds.GetBox();
			++ValidMeshCount;
		}
		else if (MeshComp)
		{
			UE_LOG(LogTemp, Warning, TEXT("[ScanVFX-Diag] %s: mesh '%s' skipped (Visible=%d, NumMats=%d)"),
				*GetName(), *MeshComp->GetName(), MeshComp->IsVisible(), MeshComp->GetNumMaterials());
		}
	}
	UE_LOG(LogTemp, Warning, TEXT("[ScanVFX-Diag] %s: total meshes=%d, valid (visible+hasMats)=%d"),
		*GetName(), AllMeshes.Num(), ValidMeshCount);

	if (!MeshBounds.IsValid)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ScanVFX-Diag] %s: SKIP — MeshBounds invalid (no visible mesh with materials)"), *GetName());
		return;
	}
	ScanBottomZ = MeshBounds.Min.Z;
	ScanTopZ = MeshBounds.Max.Z;

	// 높이가 너무 작으면 스킵
	const float MeshHeight = ScanTopZ - ScanBottomZ;
	UE_LOG(LogTemp, Warning, TEXT("[ScanVFX-Diag] %s: bounds Bottom=%.1f Top=%.1f Height=%.1f"),
		*GetName(), ScanBottomZ, ScanTopZ, MeshHeight);
	if (MeshHeight < 1.f)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ScanVFX-Diag] %s: SKIP — MeshHeight %.1f < 1.0"), *GetName(), MeshHeight);
		return;
	}

	const float BandWidth = FMath::Max(MeshHeight * ScanBandRatio, 10.f);

	// 모든 MeshComponent에 머티리얼 교체 (리빌 마스크로 바닥부터 물질화)
	for (UMeshComponent* MeshComp : AllMeshes)
	{
		if (!MeshComp || !MeshComp->IsVisible()) continue;

		const int32 NumMats = MeshComp->GetNumMaterials();
		if (NumMats <= 0) continue;

		UMaterialInstanceDynamic* DMI = UMaterialInstanceDynamic::Create(PlacementScanMaterial, this);
		if (!DMI) continue;

		DMI->SetScalarParameterValue(TEXT("ScanHeight"), ScanBottomZ);
		DMI->SetScalarParameterValue(TEXT("BandWidth"), BandWidth);

		// 원본 머티리얼 저장 후 스캔 머티리얼로 교체
		for (int32 i = 0; i < NumMats; ++i)
		{
			ScanOriginalMaterials.Add(MeshComp->GetMaterial(i));
			MeshComp->SetMaterial(i, DMI);
		}
		ScanSwappedMeshes.Add(MeshComp);
		ScanSwappedSlotCounts.Add(NumMats);
		ScanDMIs.Add(DMI);

		UE_LOG(LogTemp, Warning, TEXT("[ScanVFX-Diag] %s: SWAPPED mesh '%s' (Class=%s, NumMats=%d)"),
			*GetName(), *MeshComp->GetName(), *MeshComp->GetClass()->GetName(), NumMats);
	}

	// 아무것도 적용되지 않았으면 스킵
	if (ScanSwappedMeshes.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ScanVFX-Diag] %s: SKIP — no meshes were swapped (all DMI creation failed?)"), *GetName());
		return;
	}

	// 스캔 시작
	ScanProgress = 0.f;
	bScanActive = true;
	SetActorTickEnabled(true);

	UE_LOG(LogTemp, Warning, TEXT("[ScanVFX-Diag] %s: SCAN STARTED (%d meshes, Bottom=%.0f, Top=%.0f, Duration=%.1fs)"),
		*GetName(), ScanSwappedMeshes.Num(), ScanBottomZ, ScanTopZ, ScanDuration);
}

void AInv_BuildingActor::TickPlacementScan(float DeltaTime)
{
	// 스캔 진행 (0→1)
	ScanProgress += DeltaTime / FMath::Max(ScanDuration, 0.1f);

	if (ScanProgress >= 1.0f)
	{
		// 스캔 완료 — 머티리얼 교체된 메시 원본 복원
		int32 MatOffset = 0;
		for (int32 MeshIdx = 0; MeshIdx < ScanSwappedMeshes.Num(); ++MeshIdx)
		{
			UMeshComponent* MeshComp = ScanSwappedMeshes[MeshIdx];
			const int32 SlotCount = ScanSwappedSlotCounts.IsValidIndex(MeshIdx) ? ScanSwappedSlotCounts[MeshIdx] : 0;
			if (IsValid(MeshComp))
			{
				for (int32 i = 0; i < SlotCount; ++i)
				{
					if (ScanOriginalMaterials.IsValidIndex(MatOffset + i))
					{
						MeshComp->SetMaterial(i, ScanOriginalMaterials[MatOffset + i]);
					}
				}
			}
			MatOffset += SlotCount;
		}
		ScanSwappedMeshes.Empty();
		ScanOriginalMaterials.Empty();
		ScanSwappedSlotCounts.Empty();

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

	// 첫 프레임 + 중간 + 마지막 진단 로그
	if (ScanProgress < 0.02f || (ScanProgress > 0.49f && ScanProgress < 0.52f) || ScanProgress > 0.98f)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ScanVFX-Diag] %s TICK: Progress=%.2f, ScanHeight=%.1f, DMIs=%d"),
			*GetName(), ScanProgress, CurrentScanZ, ScanDMIs.Num());
	}

	for (UMaterialInstanceDynamic* DMI : ScanDMIs)
	{
		if (DMI)
		{
			DMI->SetScalarParameterValue(TEXT("ScanHeight"), CurrentScanZ);
			DMI->SetScalarParameterValue(TEXT("BandWidth"), BandWidth);
		}
	}
}

