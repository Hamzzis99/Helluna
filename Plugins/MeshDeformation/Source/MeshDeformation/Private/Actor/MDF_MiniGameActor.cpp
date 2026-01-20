// Gihyeon's MeshDeformation Project
#include "Actor/MDF_MiniGameActor.h"
#include "Components/DynamicMeshComponent.h"
#include "Components/MDF_MiniGameComponent.h"
#include "Components/SceneComponent.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

AMDF_MiniGameActor::AMDF_MiniGameActor()
{
    PrimaryActorTick.bCanEverTick = true;

    // 1. 루트 컴포넌트 설정
    DefaultSceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("DefaultSceneRoot"));
    SetRootComponent(DefaultSceneRoot);

    // 2. 다이나믹 메시 컴포넌트 생성 및 설정
    DynamicMeshComponent = CreateDefaultSubobject<UDynamicMeshComponent>(TEXT("DynamicMeshComponent"));
    if (DynamicMeshComponent)
    {
       DynamicMeshComponent->SetupAttachment(DefaultSceneRoot); 
       DynamicMeshComponent->SetMobility(EComponentMobility::Movable);

       // [최적화] 바운드 정상화 (1.2f로 성능 확보)
       DynamicMeshComponent->SetBoundsScale(1.2f); 

       // [충돌] 정밀 충돌 설정 (단면 물리 판정 활성화)
       DynamicMeshComponent->SetCollisionProfileName(TEXT("BlockAll"));
       DynamicMeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
       DynamicMeshComponent->SetComplexAsSimpleCollisionEnabled(true, true);
       DynamicMeshComponent->SetSimulatePhysics(false);
       
       // [부하 제거] 렌더링 및 라이팅 최적화
       DynamicMeshComponent->bCastDynamicShadow = true; 
       DynamicMeshComponent->bCastStaticShadow = false; 
       DynamicMeshComponent->SetAffectDistanceFieldLighting(false);
       DynamicMeshComponent->bAffectDynamicIndirectLighting = false;
       DynamicMeshComponent->SetCanEverAffectNavigation(false);
       DynamicMeshComponent->bUseAsyncCooking = false; 

       // [머티리얼] 오버레이 버그 방어 및 기본값 할당
       DynamicMeshComponent->SetOverlayMaterial(nullptr);
       
       static ConstructorHelpers::FObjectFinder<UMaterialInterface> DefaultMat(TEXT("/Engine/EngineMaterials/WorldGridMaterial"));
       if (DefaultMat.Succeeded())
       {
           DynamicMeshComponent->SetMaterial(0, DefaultMat.Object);
       }
    }

    MiniGameComponent = CreateDefaultSubobject<UMDF_MiniGameComponent>(TEXT("MiniGameComponent"));

    bReplicates = true;
    SetReplicateMovement(true);
}

void AMDF_MiniGameActor::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);

    if (DynamicMeshComponent)
    {
        // 런타임/에디터 갱신 로직 유지
        DynamicMeshComponent->UpdateCollision(true);
        DynamicMeshComponent->NotifyMeshUpdated();

        // 오버레이 머티리얼이 자동으로 들어오는 경우를 대비한 상시 제거
        if (DynamicMeshComponent->GetOverlayMaterial() != nullptr)
        {
            DynamicMeshComponent->SetOverlayMaterial(nullptr);
        }
    }

    if (MiniGameComponent)
    {
       MiniGameComponent->InitializeDynamicMesh();
    }
}

void AMDF_MiniGameActor::BeginPlay()
{
    Super::BeginPlay();
    
    if (DynamicMeshComponent)
    {
        DynamicMeshComponent->SetMobility(EComponentMobility::Movable);
        
        // 최종 방어선 (렌더링 상태 리셋)
        if (DynamicMeshComponent->GetOverlayMaterial() != nullptr)
        {
            DynamicMeshComponent->SetOverlayMaterial(nullptr);
            DynamicMeshComponent->MarkRenderStateDirty();
        }
    }
}