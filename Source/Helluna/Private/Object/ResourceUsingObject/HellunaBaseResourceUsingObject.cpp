#include "Object/ResourceUsingObject/HellunaBaseResourceUsingObject.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h" 
// [김기현] 다이나믹 메쉬와 변형 컴포넌트 헤더
#include "Components/DynamicMeshComponent.h"
#include "Components/MDF_DeformableComponent.h"
#include "Engine/StaticMesh.h" 

AHellunaBaseResourceUsingObject::AHellunaBaseResourceUsingObject()
{
    PrimaryActorTick.bCanEverTick = false;
    
    // ==================================================================================
    // [김기현 - MDF 시스템 1단계] 메쉬 컴포넌트 교체 및 설정
    // ==================================================================================
    
    // 1. DynamicMeshComponent 생성 (루트)
    DynamicMeshComponent = CreateDefaultSubobject<UDynamicMeshComponent>(TEXT("DynamicMeshComponent"));
    SetRootComponent(DynamicMeshComponent);

    if (DynamicMeshComponent)
    {
        DynamicMeshComponent->SetCollisionProfileName(TEXT("BlockAll"));
        DynamicMeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        
        // 정밀한 충돌 처리
        DynamicMeshComponent->SetComplexAsSimpleCollisionEnabled(true, true);
        
        // 물리 시뮬레이션 끄기
        DynamicMeshComponent->SetSimulatePhysics(false);
        
        // 비동기 쿠킹 (렉 방지)
        DynamicMeshComponent->bUseAsyncCooking = true;
    }

    // ==================================================================================
    // [기존 로직 - UI 상호작용] 충돌 박스 설정
    // ==================================================================================
    
    ResouceUsingCollisionBox = CreateDefaultSubobject<UBoxComponent>(TEXT("ResouceUsingCollisionBox"));
    ResouceUsingCollisionBox->SetupAttachment(DynamicMeshComponent); // 루트에 부착
    
    ResouceUsingCollisionBox->OnComponentBeginOverlap.AddUniqueDynamic(this, &ThisClass::CollisionBoxBeginOverlap);
    ResouceUsingCollisionBox->OnComponentEndOverlap.AddUniqueDynamic(this, &ThisClass::CollisionBoxEndOverlap);

    // ==================================================================================
    // [김기현 - MDF 시스템 2단계] 변형 로직 컴포넌트
    // ==================================================================================

    // 6. 변형 컴포넌트 생성
    DeformableComponent = CreateDefaultSubobject<UMDF_DeformableComponent>(TEXT("DeformableComponent"));

    // 7. 네트워크 복제 설정
    bReplicates = true;
    SetReplicateMovement(true);
}

// [김기현 - MDF 시스템] 에디터 프리뷰 기능
void AHellunaBaseResourceUsingObject::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);

    // [수정됨] 액터에서 변수를 넘겨줄 필요 없이, 컴포넌트 갱신 함수만 호출하면 됩니다.
    // (사용자가 블루프린트의 DeformableComponent에서 직접 Mesh를 설정했다고 가정)
    if (DeformableComponent)
    {
        DeformableComponent->InitializeDynamicMesh();
    }

    // 충돌체 업데이트
    if (DynamicMeshComponent)
    {
        DynamicMeshComponent->UpdateCollision(true);
    }
}

void AHellunaBaseResourceUsingObject::BeginPlay()
{
    Super::BeginPlay();
}

void AHellunaBaseResourceUsingObject::CollisionBoxBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
    // 기존 UI 로직
}

void AHellunaBaseResourceUsingObject::CollisionBoxEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
    // 기존 UI 로직
}