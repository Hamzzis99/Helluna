#include "Object/OreProximityComponent.h"

#include "Components/StaticMeshComponent.h"
#include "Components/ShapeComponent.h"
#include "Components/WidgetComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogOreProximity, Log, All);

UOreProximityComponent::UOreProximityComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void UOreProximityComponent::BeginPlay()
{
    Super::BeginPlay();

    CacheOwnerComponents();

    // 경량 모드로 시작 — 콜리전/오버랩/위젯 비활성화
    bFullModeActive = false;

    // 렌더 컬 디스턴스 설정
    if (CachedMeshComp && RenderCullDistance > 0.f)
    {
        CachedMeshComp->LDMaxDrawDistance = RenderCullDistance;
        CachedMeshComp->MarkRenderStateDirty();
    }

    // 경량 모드 초기 상태: 콜리전을 QueryOnly, 오버랩 OFF, 위젯 OFF
    if (CachedMeshComp)
    {
        CachedMeshComp->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    }

    for (UShapeComponent* Shape : CachedOverlapComps)
    {
        if (Shape)
        {
            Shape->SetGenerateOverlapEvents(false);
        }
    }

    if (CachedWidgetComp)
    {
        CachedWidgetComp->SetHiddenInGame(true);
        CachedWidgetComp->SetVisibility(false, true);
    }

    // 거리 체크 타이머 시작 (랜덤 오프셋으로 동시 실행 분산)
    if (UWorld* World = GetWorld())
    {
        const float RandomOffset = FMath::FRandRange(0.f, CheckInterval);
        World->GetTimerManager().SetTimer(
            ProximityTimerHandle,
            this,
            &UOreProximityComponent::CheckProximityToPlayers,
            CheckInterval,
            true,
            RandomOffset
        );
    }

    UE_LOG(LogOreProximity, Log, TEXT("[%s] 경량 모드로 시작 (반경: %.0fcm, 주기: %.1fs, 컬: %.0fcm | 메쉬:%s 오버랩:%d 위젯:%s)"),
        *GetOwner()->GetName(), ActivationRadius, CheckInterval, RenderCullDistance,
        CachedMeshComp ? TEXT("O") : TEXT("X"),
        CachedOverlapComps.Num(),
        CachedWidgetComp ? TEXT("O") : TEXT("X"));
}

void UOreProximityComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(ProximityTimerHandle);
    }

    Super::EndPlay(EndPlayReason);
}

void UOreProximityComponent::CacheOwnerComponents()
{
    AActor* Owner = GetOwner();
    if (!Owner) return;

    // StaticMeshComponent (루트 또는 첫 번째)
    CachedMeshComp = Owner->FindComponentByClass<UStaticMeshComponent>();

    // 모든 ShapeComponent (BoxComponent, SphereComponent 등 — 오버랩용)
    TArray<UShapeComponent*> Shapes;
    Owner->GetComponents<UShapeComponent>(Shapes);
    for (UShapeComponent* Shape : Shapes)
    {
        if (Shape && Shape != Owner->GetRootComponent())
        {
            CachedOverlapComps.Add(Shape);
        }
    }

    // WidgetComponent
    CachedWidgetComp = Owner->FindComponentByClass<UWidgetComponent>();
}

void UOreProximityComponent::CheckProximityToPlayers()
{
    UWorld* World = GetWorld();
    if (!World) return;

    AActor* Owner = GetOwner();
    if (!Owner) return;

    const FVector MyLocation = Owner->GetActorLocation();
    const float RadiusSq = ActivationRadius * ActivationRadius;
    bool bAnyPlayerNear = false;

    for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
    {
        const APlayerController* PC = It->Get();
        if (!PC) continue;

        const APawn* Pawn = PC->GetPawn();
        if (!Pawn) continue;

        const float DistSq = FVector::DistSquared(MyLocation, Pawn->GetActorLocation());
        if (DistSq <= RadiusSq)
        {
            bAnyPlayerNear = true;
            break;
        }
    }

    if (bAnyPlayerNear && !bFullModeActive)
    {
        ActivateFullMode();
    }
    else if (!bAnyPlayerNear && bFullModeActive)
    {
        DeactivateToLightMode();
    }
}

void UOreProximityComponent::ActivateFullMode()
{
    bFullModeActive = true;

    // 1) 콜리전 풀 활성화
    if (CachedMeshComp)
    {
        CachedMeshComp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    }

    // 2) 오버랩 이벤트 활성화
    for (UShapeComponent* Shape : CachedOverlapComps)
    {
        if (Shape)
        {
            Shape->SetGenerateOverlapEvents(true);
        }
    }

    // 3) 위젯은 FindResourceComponent가 제어하므로 여기서는 건드리지 않음

    UE_LOG(LogOreProximity, Log, TEXT("[%s] ▶ 풀 모드 활성화 — 콜리전 ON, 오버랩 ON"),
        *GetOwner()->GetName());
}

void UOreProximityComponent::DeactivateToLightMode()
{
    bFullModeActive = false;

    // 1) 콜리전 경량화
    if (CachedMeshComp)
    {
        CachedMeshComp->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    }

    // 2) 오버랩 이벤트 비활성화
    for (UShapeComponent* Shape : CachedOverlapComps)
    {
        if (Shape)
        {
            Shape->SetGenerateOverlapEvents(false);
        }
    }

    // 3) 위젯 숨기기 (멀리 있는데 떠있는 것 방지)
    if (CachedWidgetComp)
    {
        CachedWidgetComp->SetHiddenInGame(true);
        CachedWidgetComp->SetVisibility(false, true);
    }

    UE_LOG(LogOreProximity, Log, TEXT("[%s] ◀ 경량 모드 복귀 — 콜리전 QueryOnly, 오버랩 OFF"),
        *GetOwner()->GetName());
}
