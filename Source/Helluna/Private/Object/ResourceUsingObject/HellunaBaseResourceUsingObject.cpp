#include "Object/ResourceUsingObject/HellunaBaseResourceUsingObject.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"

// [김기현] 다이나믹 메쉬와 변형 컴포넌트 사용을 위한 헤더 추가
#include "Components/DynamicMeshComponent.h"
#include "Components/MDF_DeformableComponent.h"
#include "Engine/StaticMesh.h"

// [스캔 VFX]
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/MeshComponent.h"

AHellunaBaseResourceUsingObject::AHellunaBaseResourceUsingObject()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = false;
    
    // ==================================================================================
    // [김기현 - MDF 시스템 1단계] 루트 컴포넌트 교체 (StaticMesh -> DynamicMesh)
    // 설명: 찌그러지는 효과를 적용하기 위해 기존의 StaticMesh 대신 DynamicMesh를 사용합니다.
    // ==================================================================================
    
    // 1. [김기현] DynamicMeshComponent 생성 및 루트 설정
    DynamicMeshComponent = CreateDefaultSubobject<UDynamicMeshComponent>(TEXT("DynamicMeshComponent"));
    SetRootComponent(DynamicMeshComponent);

    if (DynamicMeshComponent)
    {
        // 2. [김기현] 충돌 프로필 설정 (BlockAll)
        // 캐릭터 이동과 총알 피격을 모두 막습니다.
        DynamicMeshComponent->SetCollisionProfileName(TEXT("BlockAll"));
        DynamicMeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);

        // 3. [김기현 핵심] 복잡한 메쉬(Poly) 그대로 충돌체로 사용 (ComplexAsSimple)
        // 이 설정이 있어야 메쉬가 찌그러졌을 때 충돌 범위도 같이 찌그러집니다.
        DynamicMeshComponent->SetComplexAsSimpleCollisionEnabled(true, true);

        // 4. [김기현] 물리 시뮬레이션 끄기 (고정된 사물이므로)
        DynamicMeshComponent->SetSimulatePhysics(false);

        // 5. [김기현 최적화] 비동기 쿠킹 활성화 (AsyncCooking)
        // 메쉬가 변형될 때 렉(Frame Drop)이 발생하지 않도록 백그라운드 스레드에서 연산합니다.
        DynamicMeshComponent->bUseAsyncCooking = true;
    }

    // ==================================================================================
    // [기존 로직 + 김기현 수정] UI 상호작용용 충돌 박스 설정
    // ==================================================================================
    
    ResouceUsingCollisionBox = CreateDefaultSubobject<UBoxComponent>(TEXT("ResouceUsingCollisionBox"));
    
    // [김기현 수정] 루트가 DynamicMesh로 변경되었으므로 여기에 부착합니다.
    ResouceUsingCollisionBox->SetupAttachment(DynamicMeshComponent); 

    // [김기현 수정 - 중요] 상호작용 버그 수정
    // 루트(DynamicMesh)가 BlockAll이라서, 자식인 박스의 오버랩(EndOverlap)이 씹히는 현상 방지.
    // 박스를 확실하게 'Trigger(감지 전용)'로 설정하여 물리 충돌 없이 이벤트만 받도록 함.
    ResouceUsingCollisionBox->SetCollisionProfileName(TEXT("Trigger"));
    ResouceUsingCollisionBox->SetGenerateOverlapEvents(true);
    
    ResouceUsingCollisionBox->OnComponentBeginOverlap.AddUniqueDynamic(this, &ThisClass::CollisionBoxBeginOverlap);
    ResouceUsingCollisionBox->OnComponentEndOverlap.AddUniqueDynamic(this, &ThisClass::CollisionBoxEndOverlap);


    // ==================================================================================
    // [김기현 - MDF 시스템 2단계] 변형 로직 컴포넌트 추가
    // 설명: 총알 피격 시 메쉬 변형 계산을 담당하는 컴포넌트입니다.
    // ==================================================================================

    // 6. [김기현] 변형 컴포넌트 생성
    DeformableComponent = CreateDefaultSubobject<UMDF_DeformableComponent>(TEXT("DeformableComponent"));

    // 7. [김기현] 네트워크 복제 설정 (전용 서버 환경 대응)
    bReplicates = true;
    SetReplicateMovement(true);
}

// [김기현 - MDF 시스템 3단계] 에디터 프리뷰 기능 구현
// 설명: 에디터에서 배치하거나 값을 바꿀 때, 즉시 메쉬 모양을 보여줍니다.
void AHellunaBaseResourceUsingObject::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);

    // [김기현] 컴포넌트 초기화 및 프리뷰 갱신
    // (블루프린트 DeformableComponent의 'Source Static Mesh'에 설정된 메쉬를 가져와서 보여줌)
    if (DeformableComponent)
    {
        DeformableComponent->InitializeDynamicMesh();
    }

    // [김기현] 변경된 메쉬 모양에 맞춰 충돌체(Collision)도 즉시 업데이트
    if (DynamicMeshComponent)
    {
        DynamicMeshComponent->UpdateCollision(true);
    }
}

void AHellunaBaseResourceUsingObject::BeginPlay()
{
    Super::BeginPlay();
    EnsureRuntimeResourceInitialization();

    UE_LOG(LogTemp, Warning, TEXT("[ScanVFX-Diag] BaseResourceUsing::BeginPlay → %s, HasAuth=%d, Material=%s"),
        *GetName(), HasAuthority(),
        PlacementScanMaterial ? *PlacementScanMaterial->GetName() : TEXT("NULL"));
    StartPlacementScanVFX();
}

void AHellunaBaseResourceUsingObject::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    if (bScanActive)
    {
        TickPlacementScan(DeltaTime);
    }
}

// [기존 로직] UI 띄우기 (유지)
void AHellunaBaseResourceUsingObject::EnsureRuntimeResourceInitialization()
{
    if (bRuntimeResourceInitialized)
    {
        return;
    }

    if (DeformableComponent)
    {
        DeformableComponent->InitializeDynamicMesh();
    }

    if (DynamicMeshComponent)
    {
        DynamicMeshComponent->SetCollisionProfileName(TEXT("BlockAll"));
        DynamicMeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        DynamicMeshComponent->SetComplexAsSimpleCollisionEnabled(true, true);
        DynamicMeshComponent->UpdateCollision(true);
    }

    if (ResouceUsingCollisionBox)
    {
        ResouceUsingCollisionBox->SetCollisionProfileName(TEXT("Trigger"));
        ResouceUsingCollisionBox->SetGenerateOverlapEvents(true);
    }

    bRuntimeResourceInitialized = true;
}

void AHellunaBaseResourceUsingObject::CollisionBoxBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{

}

// [기존 로직] UI 닫기 (유지)
void AHellunaBaseResourceUsingObject::CollisionBoxEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{

}

// ============================================================================
// 배치 스캔 VFX
// ============================================================================

void AHellunaBaseResourceUsingObject::StartPlacementScanVFX()
{
    if (!PlacementScanMaterial)
    {
        UE_LOG(LogTemp, Warning, TEXT("[ScanVFX-Diag] %s: SKIP — PlacementScanMaterial is NULL"), *GetName());
        return;
    }

    if (bScanActive)
    {
        UE_LOG(LogTemp, Warning, TEXT("[ScanVFX-Diag] %s: SKIP — bScanActive already true"), *GetName());
        return;
    }

    TArray<UMeshComponent*> AllMeshes;
    GetComponents<UMeshComponent>(AllMeshes);

    if (AllMeshes.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("[ScanVFX-Diag] %s: SKIP — no MeshComponent found"), *GetName());
        return;
    }

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
    UE_LOG(LogTemp, Warning, TEXT("[ScanVFX-Diag] %s: total meshes=%d, valid=%d"),
        *GetName(), AllMeshes.Num(), ValidMeshCount);

    if (!MeshBounds.IsValid)
    {
        UE_LOG(LogTemp, Warning, TEXT("[ScanVFX-Diag] %s: SKIP — MeshBounds invalid"), *GetName());
        return;
    }
    ScanBottomZ = MeshBounds.Min.Z;
    ScanTopZ = MeshBounds.Max.Z;

    const float MeshHeight = ScanTopZ - ScanBottomZ;
    UE_LOG(LogTemp, Warning, TEXT("[ScanVFX-Diag] %s: bounds Bottom=%.1f Top=%.1f Height=%.1f"),
        *GetName(), ScanBottomZ, ScanTopZ, MeshHeight);

    if (MeshHeight < 1.f)
    {
        UE_LOG(LogTemp, Warning, TEXT("[ScanVFX-Diag] %s: SKIP — MeshHeight %.1f < 1.0"), *GetName(), MeshHeight);
        return;
    }

    const float BandWidth = FMath::Max(MeshHeight * ScanBandRatio, 10.f);

    for (UMeshComponent* MeshComp : AllMeshes)
    {
        if (!MeshComp || !MeshComp->IsVisible()) continue;

        const int32 NumMats = MeshComp->GetNumMaterials();
        if (NumMats <= 0) continue;

        UMaterialInstanceDynamic* DMI = UMaterialInstanceDynamic::Create(PlacementScanMaterial, this);
        if (!DMI) continue;

        DMI->SetScalarParameterValue(TEXT("ScanHeight"), ScanBottomZ);
        DMI->SetScalarParameterValue(TEXT("BandWidth"), BandWidth);

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

    if (ScanSwappedMeshes.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("[ScanVFX-Diag] %s: SKIP — no meshes were swapped"), *GetName());
        return;
    }

    ScanProgress = 0.f;
    bScanActive = true;
    SetActorTickEnabled(true);

    UE_LOG(LogTemp, Warning, TEXT("[ScanVFX-Diag] %s: SCAN STARTED (%d meshes, Bottom=%.0f, Top=%.0f, Duration=%.1fs)"),
        *GetName(), ScanSwappedMeshes.Num(), ScanBottomZ, ScanTopZ, ScanDuration);
}

void AHellunaBaseResourceUsingObject::TickPlacementScan(float DeltaTime)
{
    ScanProgress += DeltaTime / FMath::Max(ScanDuration, 0.1f);

    if (ScanProgress >= 1.0f)
    {
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
        if (!ShouldKeepTickingAfterPlacementScan())
        {
            SetActorTickEnabled(false);
        }
        return;
    }

    const float CurrentScanZ = FMath::Lerp(ScanBottomZ, ScanTopZ, ScanProgress);
    const float MeshHeight = ScanTopZ - ScanBottomZ;
    const float BandWidth = FMath::Max(MeshHeight * ScanBandRatio, 10.f);

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
