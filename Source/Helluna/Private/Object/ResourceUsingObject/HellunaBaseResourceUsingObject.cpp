#include "Object/ResourceUsingObject/HellunaBaseResourceUsingObject.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h" 

// [김기현] 다이나믹 메쉬와 변형 컴포넌트 사용을 위한 헤더 추가
#include "Components/DynamicMeshComponent.h"
#include "Components/MDF_DeformableComponent.h"
#include "Engine/StaticMesh.h" 

AHellunaBaseResourceUsingObject::AHellunaBaseResourceUsingObject()
{
    PrimaryActorTick.bCanEverTick = false;
    
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
}

// [기존 로직] UI 띄우기 (유지)
void AHellunaBaseResourceUsingObject::CollisionBoxBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{

}

// [기존 로직] UI 닫기 (유지)
void AHellunaBaseResourceUsingObject::CollisionBoxEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{

}