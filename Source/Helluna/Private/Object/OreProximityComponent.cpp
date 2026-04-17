#include "Object/OreProximityComponent.h"

#include "Components/StaticMeshComponent.h"
#include "Components/ShapeComponent.h"
#include "Components/WidgetComponent.h"
#include "Components/AudioComponent.h"
#include "NiagaraComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "TimerManager.h"
#include "Net/UnrealNetwork.h"

DEFINE_LOG_CATEGORY_STATIC(LogOreProximity, Log, All);

UOreProximityComponent::UOreProximityComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void UOreProximityComponent::BeginPlay()
{
    Super::BeginPlay();

    CacheOwnerComponents();

    AActor* Owner = GetOwner();
    if (Owner)
    {
        bOriginalOwnerTickEnabled = Owner->PrimaryActorTick.bCanEverTick;
        OriginalDormancy = Owner->NetDormancy;
    }

    // 렌더 컬 디스턴스 설정
    if (CachedMeshComp && RenderCullDistance > 0.f)
    {
        CachedMeshComp->LDMaxDrawDistance = RenderCullDistance;
        CachedMeshComp->MarkRenderStateDirty();
    }

    // ── WidgetComponent lazy 처리: BeginPlay 직후 즉시 해제 ──
    if (bLazyWidgetCreation && CachedWidgetComp)
    {
        UnregisterWidget();
    }

    // Far 모드로 시작 — 모든 비용 최소화
    CurrentLOD = EOreProximityLOD::Far;
    ActivateFarMode();

    // 거리 체크 타이머 시작 (랜덤 오프셋으로 동시 실행 분산)
    if (UWorld* World = GetWorld())
    {
        const float RandomOffset = FMath::FRandRange(0.f, FarCheckInterval);
        World->GetTimerManager().SetTimer(
            ProximityTimerHandle,
            this,
            &UOreProximityComponent::CheckProximityToPlayers,
            FarCheckInterval,
            true,
            RandomOffset
        );
    }

    UE_LOG(LogOreProximity, Log,
        TEXT("[%s] V3 경량 모드 시작 (Near:%.0f Mid:%.0f 컬:%.0f | 메쉬:%s 오버랩:%d 위젯:%s(%s) VFX:%d 오디오:%d)"),
        Owner ? *Owner->GetName() : TEXT("?"),
        NearRadius, MidRadius, RenderCullDistance,
        CachedMeshComp ? TEXT("O") : TEXT("X"),
        CachedOverlapComps.Num(),
        CachedWidgetComp ? TEXT("O") : TEXT("X"),
        bLazyWidgetCreation ? TEXT("lazy") : TEXT("eager"),
        CachedNiagaraComps.Num(),
        CachedAudioComps.Num());
}

void UOreProximityComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(ProximityTimerHandle);
    }

    // 오너 틱 상태 복원
    if (AActor* Owner = GetOwner())
    {
        if (bDisableOwnerTickWhenFar)
        {
            Owner->SetActorTickEnabled(bOriginalOwnerTickEnabled);
        }
    }

    // Widget 복원 (해제 상태로 끝나면 GC 문제 방지)
    if (CachedWidgetComp && !bWidgetRegistered)
    {
        RegisterWidget();
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

    // NiagaraComponent (VFX)
    TArray<UNiagaraComponent*> Niagaras;
    Owner->GetComponents<UNiagaraComponent>(Niagaras);
    for (UNiagaraComponent* NC : Niagaras)
    {
        if (NC)
        {
            CachedNiagaraComps.Add(NC);
        }
    }

    // AudioComponent
    TArray<UAudioComponent*> Audios;
    Owner->GetComponents<UAudioComponent>(Audios);
    for (UAudioComponent* AC : Audios)
    {
        if (AC)
        {
            CachedAudioComps.Add(AC);
        }
    }
}

float UOreProximityComponent::GetClosestPlayerDistanceSq() const
{
    UWorld* World = GetWorld();
    if (!World) return MAX_FLT;

    AActor* Owner = GetOwner();
    if (!Owner) return MAX_FLT;

    const FVector MyLocation = Owner->GetActorLocation();
    float ClosestDistSq = MAX_FLT;

    for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
    {
        const APlayerController* PC = It->Get();
        if (!PC) continue;

        const APawn* Pawn = PC->GetPawn();
        if (!Pawn) continue;

        const float DistSq = FVector::DistSquared(MyLocation, Pawn->GetActorLocation());
        if (DistSq < ClosestDistSq)
        {
            ClosestDistSq = DistSq;
        }
    }

    return ClosestDistSq;
}

void UOreProximityComponent::CheckProximityToPlayers()
{
    const float DistSq = GetClosestPlayerDistanceSq();
    const float NearSq = NearRadius * NearRadius;
    const float MidSq = MidRadius * MidRadius;

    EOreProximityLOD DesiredLOD;
    if (DistSq <= NearSq)
    {
        DesiredLOD = EOreProximityLOD::Near;
    }
    else if (DistSq <= MidSq)
    {
        DesiredLOD = EOreProximityLOD::Mid;
    }
    else
    {
        DesiredLOD = EOreProximityLOD::Far;
    }

    if (DesiredLOD != CurrentLOD)
    {
        TransitionToLOD(DesiredLOD);
    }
}

void UOreProximityComponent::TransitionToLOD(EOreProximityLOD NewLOD)
{
    const EOreProximityLOD OldLOD = CurrentLOD;
    CurrentLOD = NewLOD;

    switch (NewLOD)
    {
    case EOreProximityLOD::Near:
        ActivateNearMode();
        break;
    case EOreProximityLOD::Mid:
        ActivateMidMode();
        break;
    case EOreProximityLOD::Far:
        ActivateFarMode();
        break;
    }

    AdjustTimerInterval();

    UE_LOG(LogOreProximity, Verbose, TEXT("[%s] LOD 전환: %d → %d"),
        GetOwner() ? *GetOwner()->GetName() : TEXT("?"),
        (int32)OldLOD, (int32)NewLOD);
}

void UOreProximityComponent::ForceActivateNear()
{
    TransitionToLOD(EOreProximityLOD::Near);
}

// ============================================================================
// Near 모드: 풀 활성화 — 플레이어가 상호작용 가능 거리
// ============================================================================
void UOreProximityComponent::ActivateNearMode()
{
    // 1) 콜리전 풀 활성화
    if (CachedMeshComp)
    {
        CachedMeshComp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        CachedMeshComp->SetCastShadow(true);
    }

    // 2) 오버랩 이벤트 활성화
    for (UShapeComponent* Shape : CachedOverlapComps)
    {
        if (Shape)
        {
            Shape->SetGenerateOverlapEvents(true);
        }
    }

    // 3) VFX 활성화
    for (UNiagaraComponent* NC : CachedNiagaraComps)
    {
        if (NC)
        {
            NC->SetVisibility(true);
            NC->Activate(true);
        }
    }

    // 4) 오디오 활성화
    for (UAudioComponent* AC : CachedAudioComps)
    {
        if (AC)
        {
            AC->SetActive(true);
        }
    }

    // 5) WidgetComponent 등록 (lazy 모드)
    RegisterWidget();

    // 6) 오너 틱 복원
    if (bDisableOwnerTickWhenFar)
    {
        if (AActor* Owner = GetOwner())
        {
            Owner->SetActorTickEnabled(bOriginalOwnerTickEnabled);
        }
    }

    // 7) 네트워크 Dormancy 해제
    if (bUseNetDormancy)
    {
        if (AActor* Owner = GetOwner())
        {
            Owner->SetNetDormancy(DORM_Awake);
            Owner->FlushNetDormancy();
        }
    }
}

// ============================================================================
// Mid 모드: 보이지만 상호작용 불가 — 그림자/VFX/오디오 비활성화
// ============================================================================
void UOreProximityComponent::ActivateMidMode()
{
    // 1) 콜리전 QueryOnly (레이캐스트는 됨, 피직스 OFF)
    if (CachedMeshComp)
    {
        CachedMeshComp->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        CachedMeshComp->SetCastShadow(false);
    }

    // 2) 오버랩 이벤트 비활성화
    for (UShapeComponent* Shape : CachedOverlapComps)
    {
        if (Shape)
        {
            Shape->SetGenerateOverlapEvents(false);
        }
    }

    // 3) VFX 비활성화
    for (UNiagaraComponent* NC : CachedNiagaraComps)
    {
        if (NC)
        {
            NC->Deactivate();
            NC->SetVisibility(false);
        }
    }

    // 4) 오디오 비활성화
    for (UAudioComponent* AC : CachedAudioComps)
    {
        if (AC)
        {
            AC->SetActive(false);
        }
    }

    // 5) WidgetComponent 해제 (Mid에서는 상호작용 불가)
    UnregisterWidget();

    // 6) 오너 틱은 유지 (Mid에서는 애니메이션 등 가능)
    if (bDisableOwnerTickWhenFar)
    {
        if (AActor* Owner = GetOwner())
        {
            Owner->SetActorTickEnabled(bOriginalOwnerTickEnabled);
        }
    }

    // 7) 네트워크 Dormancy 부분 설정
    if (bUseNetDormancy)
    {
        if (AActor* Owner = GetOwner())
        {
            Owner->SetNetDormancy(DORM_DormantPartial);
        }
    }
}

// ============================================================================
// Far 모드: 완전 경량화 — 렌더 컬 밖, 모든 비용 최소화
// ============================================================================
void UOreProximityComponent::ActivateFarMode()
{
    // 1) 콜리전 최소화
    if (CachedMeshComp)
    {
        CachedMeshComp->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        CachedMeshComp->SetCastShadow(false);
    }

    // 2) 오버랩 이벤트 비활성화
    for (UShapeComponent* Shape : CachedOverlapComps)
    {
        if (Shape)
        {
            Shape->SetGenerateOverlapEvents(false);
        }
    }

    // 3) WidgetComponent 해제
    UnregisterWidget();

    // 4) VFX 완전 비활성화
    for (UNiagaraComponent* NC : CachedNiagaraComps)
    {
        if (NC)
        {
            NC->Deactivate();
            NC->SetVisibility(false);
        }
    }

    // 5) 오디오 비활성화
    for (UAudioComponent* AC : CachedAudioComps)
    {
        if (AC)
        {
            AC->SetActive(false);
        }
    }

    // 6) 오너 틱 비활성화 (서버 CPU 절감)
    if (bDisableOwnerTickWhenFar)
    {
        if (AActor* Owner = GetOwner())
        {
            Owner->SetActorTickEnabled(false);
        }
    }

    // 7) 네트워크 Dormancy 풀 설정 (리플리케이션 완전 중단)
    if (bUseNetDormancy)
    {
        if (AActor* Owner = GetOwner())
        {
            Owner->SetNetDormancy(DORM_DormantAll);
        }
    }
}

// ============================================================================
// 적응형 타이머 — LOD에 따라 체크 주기 조절
// ============================================================================
void UOreProximityComponent::AdjustTimerInterval()
{
    UWorld* World = GetWorld();
    if (!World) return;

    float NewInterval;
    switch (CurrentLOD)
    {
    case EOreProximityLOD::Near:
        NewInterval = NearCheckInterval;
        break;
    case EOreProximityLOD::Mid:
        NewInterval = FMath::Lerp(NearCheckInterval, FarCheckInterval, 0.5f);
        break;
    case EOreProximityLOD::Far:
    default:
        NewInterval = FarCheckInterval;
        break;
    }

    // 기존 타이머 교체
    World->GetTimerManager().ClearTimer(ProximityTimerHandle);
    World->GetTimerManager().SetTimer(
        ProximityTimerHandle,
        this,
        &UOreProximityComponent::CheckProximityToPlayers,
        NewInterval,
        true
    );
}

// ============================================================================
// WidgetComponent Lazy 관리
// UnregisterComponent()는 렌더링/틱 시스템에서 완전 제거 → 비용 0
// RegisterComponent()로 필요 시 복원
// 이 방식은 Destroy+Recreate보다 안전하고
// FindResourceComponent의 FindComponentByClass에도 영향 없음
// (Unregistered 컴포넌트도 GetComponents에 여전히 존재)
// ============================================================================
void UOreProximityComponent::RegisterWidget()
{
    if (!bLazyWidgetCreation) return;
    if (!CachedWidgetComp) return;
    if (bWidgetRegistered) return;

    if (!CachedWidgetComp->IsRegistered())
    {
        CachedWidgetComp->RegisterComponent();
    }

    // Hidden 상태로 등록 — FindResourceComponent가 SetPromptVisible로 제어
    CachedWidgetComp->SetHiddenInGame(true);
    CachedWidgetComp->SetVisibility(false, true);

    bWidgetRegistered = true;

    UE_LOG(LogOreProximity, Verbose, TEXT("[%s] WidgetComponent 등록 (Near 진입)"),
        GetOwner() ? *GetOwner()->GetName() : TEXT("?"));
}

void UOreProximityComponent::UnregisterWidget()
{
    if (!bLazyWidgetCreation) return;
    if (!CachedWidgetComp) return;
    if (!bWidgetRegistered) return;

    // 먼저 숨기고
    CachedWidgetComp->SetHiddenInGame(true);
    CachedWidgetComp->SetVisibility(false, true);

    // 렌더링/틱 시스템에서 완전 제거
    if (CachedWidgetComp->IsRegistered())
    {
        CachedWidgetComp->UnregisterComponent();
    }

    bWidgetRegistered = false;

    UE_LOG(LogOreProximity, Verbose, TEXT("[%s] WidgetComponent 해제 (Near 이탈)"),
        GetOwner() ? *GetOwner()->GetName() : TEXT("?"));
}
