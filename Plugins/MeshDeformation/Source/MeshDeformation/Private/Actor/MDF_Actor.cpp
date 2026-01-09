// Gihyeon's Deformation Project (Helluna)

#include "Actor/MDF_Actor.h"
#include "Components/DynamicMeshComponent.h"
#include "Components/MDF_DeformableComponent.h"

AMDF_Actor::AMDF_Actor()
{
    PrimaryActorTick.bCanEverTick = false;
    
    // 1. 다이나믹 메시 컴포넌트 생성 및 루트 설정
    DynamicMeshComponent = CreateDefaultSubobject<UDynamicMeshComponent>(TEXT("다이나믹메시컴포넌트 (DynamicMeshComponent)"));
    SetRootComponent(DynamicMeshComponent);

    // 2. 충돌(Collision) 및 물리 설정 자동화
    if (DynamicMeshComponent)
    {
        // 모든 채널을 차단(BlockAll)하여 캐릭터와 탄환이 통과하지 못하게 합니다.
        DynamicMeshComponent->SetCollisionProfileName(TEXT("BlockAll"));
        DynamicMeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);

        // [해결책] ECollisionComplexity 심볼 에러를 피하기 위해 전용 함수를 사용합니다.
        // 이 함수는 내부적으로 ComplexAsSimple 설정을 안전하게 처리합니다.
        DynamicMeshComponent->SetComplexAsSimpleCollisionEnabled(true, true);
        
        // 정적 구조물로서의 역할을 위해 물리 시뮬레이션(중력 등)은 끕니다.
        DynamicMeshComponent->SetSimulatePhysics(false);
        
        // [Step 6 최적화] 이 한 줄을 추가하는 것이 맞습니다.
        // 찌그러질 때마다 발생하는 물리 계산을 백그라운드 스레드로 던져서 렉(Hitch)을 방지합니다.
        DynamicMeshComponent->bUseAsyncCooking = true;
    }

    // 3. 변형 로직 컴포넌트 생성
    DeformableComponent = CreateDefaultSubobject<UMDF_DeformableComponent>(TEXT("변형컴포넌트 (DeformableComponent)"));

    // [Step 7 대비] 전용 서버 환경을 위한 네트워크 복제 설정
    bReplicates = true;
    SetReplicateMovement(true);
}

void AMDF_Actor::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);

    // [Step 6 핵심] 에디터 프리뷰 구현
    // 배치 직후나 수치 변경 시 즉시 메시를 초기화하여 뷰포트에 보여줍니다.
    if (DeformableComponent)
    {
        DeformableComponent->InitializeDynamicMesh();
    }

    // 충돌 상태가 뷰포트에서 즉시 갱신되도록 합니다.
    if (DynamicMeshComponent)
    {
        DynamicMeshComponent->UpdateCollision(true);
    }
}

void AMDF_Actor::BeginPlay()
{
    Super::BeginPlay();
}