#include "PCG/OreHISMPoolComponent.h"

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Engine/Engine.h"
#include "TimerManager.h"
#include "HAL/IConsoleManager.h"
#include "Object/OreProximityComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogOreHISMPool, Log, All);

// [OreHISMDiagV1] 진단용 CVar — 패키징/PIE 모두에서 콘솔로 토글 가능.
//   Helluna.OreHISM.ForceFallback 1  → GameMode 가 HISM 우회하고 모든 광석을 직접 AActor 로 스폰.
//                                      "HISM 시스템 문제 vs 생성 자체 문제" 구분에 사용.
//   Helluna.OreHISM.ShowStats     1  → OreHISMPool 의 통계를 매 AutoManageTick 마다 화면 출력.
static TAutoConsoleVariable<int32> CVarOreHISMForceFallback(
    TEXT("Helluna.OreHISM.ForceFallback"),
    0,
    TEXT("1 = GameMode 의 HISM 풀 사용을 우회하고 광석을 직접 AActor 로 스폰. ")
    TEXT("HISM 최적화 시스템 문제와 생성 자체 문제를 구분하기 위한 진단 토글."),
    ECVF_Default);

static TAutoConsoleVariable<int32> CVarOreHISMShowStats(
    TEXT("Helluna.OreHISM.ShowStats"),
    0,
    TEXT("1 = OreHISMPool 통계 (등록/활성/풀/승격 수) 를 매 AutoManageTick 화면에 출력."),
    ECVF_Default);

UOreHISMPoolComponent::UOreHISMPoolComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void UOreHISMPoolComponent::BeginPlay()
{
    Super::BeginPlay();

    // 자동 관리 타이머 시작
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().SetTimer(
            AutoManageTimerHandle,
            this,
            &UOreHISMPoolComponent::AutoManageTick,
            AutoCheckInterval,
            true,
            FMath::FRandRange(0.f, AutoCheckInterval)
        );
    }

    UE_LOG(LogOreHISMPool, Log, TEXT("[OreHISMPool] 초기화 완료 — 자동 관리 주기: %.1fs, 승격: %.0fcm, 강등: %.0fcm, 배치: %d"),
        AutoCheckInterval, AutoPromoteRadius, AutoDemoteRadius, BatchSizePerCheck);
    UE_LOG(LogOreHISMPool, Log, TEXT("[OreHISMPool] 승격 최적화 — NetCullDistSq=%.0f, NetUpdateFreq=%.1fHz, TickOff=%s"),
        PromotedNetCullDistSq, PromotedNetUpdateFrequency, bDisablePromotedActorTick ? TEXT("Y") : TEXT("N"));
}

// ============================================================================
// RegisterOreInstance — 광석을 HISM 인스턴스로 등록
// ============================================================================
int32 UOreHISMPoolComponent::RegisterOreInstance(UStaticMesh* Mesh, TSubclassOf<AActor> OreClass, const FTransform& Transform)
{
    TArray<FName> EmptyTags;
    return RegisterOreInstance(Mesh, OreClass, Transform, EmptyTags);
}

int32 UOreHISMPoolComponent::RegisterOreInstance(UStaticMesh* Mesh, TSubclassOf<AActor> OreClass, const FTransform& Transform, const TArray<FName>& InTags)
{
    if (!Mesh)
    {
        UE_LOG(LogOreHISMPool, Warning, TEXT("RegisterOreInstance: Mesh is null"));
        return INDEX_NONE;
    }

    const int32 PoolIdx = FindOrCreatePool(Mesh);
    FOreHISMPool& Pool = Pools[PoolIdx];

    // HISM에 인스턴스 추가
    const int32 HISMIdx = Pool.HISMComp->AddInstance(Transform, /*bWorldSpace=*/ true);

    // 글로벌 인스턴스 데이터 생성
    const int32 GlobalID = AllInstances.Num();
    FOreInstanceData& Data = AllInstances.AddDefaulted_GetRef();
    Data.PoolIndex = PoolIdx;
    Data.InstanceIndex = HISMIdx;
    Data.OreClass = OreClass;
    Data.Transform = Transform;
    Data.Tags = InTags;

    Pool.InstanceIDs.Add(GlobalID);

    return GlobalID;
}

// ============================================================================
// PromoteToActor — HISM → AActor 승격
// ============================================================================
AActor* UOreHISMPoolComponent::PromoteToActor(int32 InstanceID)
{
    if (!AllInstances.IsValidIndex(InstanceID)) return nullptr;

    FOreInstanceData& Data = AllInstances[InstanceID];
    if (Data.bPromotedToActor || Data.bDestroyed) return nullptr;
    if (!Data.OreClass) return nullptr;

    UWorld* World = GetWorld();
    if (!World) return nullptr;

    // 1) HISM 인스턴스 숨기기 (제거하면 인덱스 변동 → 스케일 0으로 숨김)
    FOreHISMPool& Pool = Pools[Data.PoolIndex];
    if (Pool.HISMComp && Data.InstanceIndex != INDEX_NONE)
    {
        FTransform HiddenTransform = Data.Transform;
        HiddenTransform.SetScale3D(FVector::ZeroVector);
        Pool.HISMComp->UpdateInstanceTransform(Data.InstanceIndex, HiddenTransform, /*bWorldSpace=*/ true, /*bMarkRenderStateDirty=*/ true);
    }

    // 2) AActor 스폰
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    AActor* SpawnedActor = World->SpawnActor(Data.OreClass, &Data.Transform, Params);

    if (SpawnedActor)
    {
        Data.bPromotedToActor = true;
        Data.PromotedActor = SpawnedActor;

        // Tags 복원 (HISM 모드에서는 AActor가 없어 태그가 유실되므로 승격 시 복사)
        if (Data.Tags.Num() > 0)
        {
            SpawnedActor->Tags = Data.Tags;
        }

        // 스케일 명시 적용 (SpawnActor가 Transform의 Scale을 무시할 수 있음)
        const FVector Scale = Data.Transform.GetScale3D();
        if (!Scale.Equals(FVector::OneVector))
        {
            SpawnedActor->SetActorScale3D(Scale);
        }

        // ── 승격 AActor 최적화 ──────────────────────────────────
        // [최적화1] NetCullDistanceSquared — 네트워크 리플리케이션 컬 거리 설정
        SpawnedActor->SetNetCullDistanceSquared(PromotedNetCullDistSq);
        UE_LOG(LogOreHISMPool, Log, TEXT("[OreHISMPool][최적화] ID=%d NetCullDistSq=%.0f 적용"),
            InstanceID, PromotedNetCullDistSq);

        // [최적화2] NetUpdateFrequency — 정적 광석은 낮은 빈도로 충분
        SpawnedActor->NetUpdateFrequency = PromotedNetUpdateFrequency;
        SpawnedActor->MinNetUpdateFrequency = PromotedNetUpdateFrequency * 0.5f;
        UE_LOG(LogOreHISMPool, Log, TEXT("[OreHISMPool][최적화] ID=%d NetUpdateFreq=%.1fHz 적용"),
            InstanceID, PromotedNetUpdateFrequency);

        // [최적화3] Actor Tick 비활성화 — 정적 광석에 Tick 불필요
        if (bDisablePromotedActorTick)
        {
            SpawnedActor->SetActorTickEnabled(false);
            SpawnedActor->PrimaryActorTick.bStartWithTickEnabled = false;
            UE_LOG(LogOreHISMPool, Log, TEXT("[OreHISMPool][최적화] ID=%d Actor Tick 비활성화"),
                InstanceID);
        }

        // OreProximityComponent가 있으면 즉시 Near 모드 활성화
        if (UOreProximityComponent* ProxComp = SpawnedActor->FindComponentByClass<UOreProximityComponent>())
        {
            ProxComp->ForceActivateNear();
        }

        UE_LOG(LogOreHISMPool, Verbose, TEXT("[OreHISMPool] 승격: ID=%d → %s (Tags=%d, Scale=%.2f)"),
            InstanceID, *SpawnedActor->GetName(), Data.Tags.Num(), Scale.X);
    }

    return SpawnedActor;
}

// ============================================================================
// DemoteToInstance — AActor → HISM 강등
// ============================================================================
void UOreHISMPoolComponent::DemoteToInstance(int32 InstanceID)
{
    if (!AllInstances.IsValidIndex(InstanceID)) return;

    FOreInstanceData& Data = AllInstances[InstanceID];
    if (!Data.bPromotedToActor || Data.bDestroyed) return;

    // 1) AActor 파괴
    if (AActor* Actor = Data.PromotedActor.Get())
    {
        Actor->Destroy();
    }

    Data.bPromotedToActor = false;
    Data.PromotedActor = nullptr;

    // 2) HISM 인스턴스 복원 (스케일 원복)
    FOreHISMPool& Pool = Pools[Data.PoolIndex];
    if (Pool.HISMComp && Data.InstanceIndex != INDEX_NONE)
    {
        Pool.HISMComp->UpdateInstanceTransform(Data.InstanceIndex, Data.Transform, /*bWorldSpace=*/ true, /*bMarkRenderStateDirty=*/ true);
    }

    UE_LOG(LogOreHISMPool, Log, TEXT("[OreHISMPool] 강등: ID=%d → HISM (풀=%d)"), InstanceID, Data.PoolIndex);
}

// ============================================================================
// DestroyOre — 채굴 완료: HISM + AActor 모두 제거
// ============================================================================
void UOreHISMPoolComponent::DestroyOre(int32 InstanceID)
{
    if (!AllInstances.IsValidIndex(InstanceID)) return;

    FOreInstanceData& Data = AllInstances[InstanceID];
    if (Data.bDestroyed) return;

    // AActor가 있으면 파괴
    if (Data.bPromotedToActor)
    {
        if (AActor* Actor = Data.PromotedActor.Get())
        {
            Actor->Destroy();
        }
        Data.bPromotedToActor = false;
        Data.PromotedActor = nullptr;
    }

    // HISM 인스턴스 숨기기 (스케일 0)
    FOreHISMPool& Pool = Pools[Data.PoolIndex];
    if (Pool.HISMComp && Data.InstanceIndex != INDEX_NONE)
    {
        FTransform HiddenTransform = Data.Transform;
        HiddenTransform.SetScale3D(FVector::ZeroVector);
        Pool.HISMComp->UpdateInstanceTransform(Data.InstanceIndex, HiddenTransform, /*bWorldSpace=*/ true, /*bMarkRenderStateDirty=*/ true);
    }

    Data.bDestroyed = true;

    UE_LOG(LogOreHISMPool, Log, TEXT("[OreHISMPool] 파괴: ID=%d (풀=%d, 인스턴스=%d)"),
        InstanceID, Data.PoolIndex, Data.InstanceIndex);
}

// ============================================================================
// GetActiveInstanceCount
// ============================================================================
int32 UOreHISMPoolComponent::GetActiveInstanceCount() const
{
    int32 Count = 0;
    for (const FOreInstanceData& Data : AllInstances)
    {
        if (!Data.bDestroyed && !Data.bPromotedToActor)
        {
            ++Count;
        }
    }
    return Count;
}

// ============================================================================
// AutoPromoteNearPlayers — 자동 승격 (라운드-로빈 배치)
// ============================================================================
void UOreHISMPoolComponent::AutoPromoteNearPlayers(float PromoteRadius)
{
    const int32 TotalCount = AllInstances.Num();
    if (TotalCount == 0) return;

    const float RadiusSq = PromoteRadius * PromoteRadius;
    int32 PromotedThisFrame = 0;
    int32 CheckedThisBatch = 0;

    // 라운드-로빈 인덱스 보정
    if (PromoteRoundRobinIndex >= TotalCount)
    {
        PromoteRoundRobinIndex = 0;
    }

    const int32 StartIdx = PromoteRoundRobinIndex;

    for (int32 c = 0; c < TotalCount && CheckedThisBatch < BatchSizePerCheck && PromotedThisFrame < MaxPromotesPerCheck; ++c)
    {
        const int32 i = (StartIdx + c) % TotalCount;
        const FOreInstanceData& Data = AllInstances[i];
        if (Data.bPromotedToActor || Data.bDestroyed)
        {
            ++CheckedThisBatch;
            continue;
        }

        const float DistSq = GetClosestPlayerDistSqCached(Data.Transform.GetLocation());
        if (DistSq <= RadiusSq)
        {
            PromoteToActor(i);
            ++PromotedThisFrame;
        }
        ++CheckedThisBatch;
    }

    PromoteRoundRobinIndex = (StartIdx + CheckedThisBatch) % FMath::Max(TotalCount, 1);

    UE_LOG(LogOreHISMPool, Log, TEXT("[OreHISMPool][배치] 승격 스캔: 배치=%d/%d, 승격=%d, 다음시작=%d"),
        CheckedThisBatch, TotalCount, PromotedThisFrame, PromoteRoundRobinIndex);
}

// ============================================================================
// AutoDemoteFarFromPlayers — 자동 강등 (승격된 것만 순회 — 전체 순회 불필요)
// ============================================================================
void UOreHISMPoolComponent::AutoDemoteFarFromPlayers(float DemoteRadius)
{
    const float RadiusSq = DemoteRadius * DemoteRadius;
    int32 DemotedThisFrame = 0;
    int32 CleanedInvalid = 0;

    // 강등은 승격된 AActor만 대상이므로 전체를 돌되, 승격 수는 보통 50~200개라 부담 없음
    for (int32 i = 0; i < AllInstances.Num() && DemotedThisFrame < MaxDemotesPerCheck; ++i)
    {
        FOreInstanceData& Data = AllInstances[i];
        if (!Data.bPromotedToActor || Data.bDestroyed) continue;

        // 승격된 AActor가 유효한지 확인
        if (!Data.PromotedActor.IsValid())
        {
            // AActor가 이미 파괴됨 (채굴 등) — 바로 파괴 마킹
            Data.bDestroyed = true;
            Data.bPromotedToActor = false;

            // HISM도 숨김
            FOreHISMPool& Pool = Pools[Data.PoolIndex];
            if (Pool.HISMComp && Data.InstanceIndex != INDEX_NONE)
            {
                FTransform HiddenTransform = Data.Transform;
                HiddenTransform.SetScale3D(FVector::ZeroVector);
                Pool.HISMComp->UpdateInstanceTransform(Data.InstanceIndex, HiddenTransform, true, true);
            }
            ++CleanedInvalid;
            continue;
        }

        const float DistSq = GetClosestPlayerDistSqCached(Data.Transform.GetLocation());
        if (DistSq > RadiusSq)
        {
            DemoteToInstance(i);
            ++DemotedThisFrame;
        }
    }

    if (DemotedThisFrame > 0 || CleanedInvalid > 0)
    {
        UE_LOG(LogOreHISMPool, Log, TEXT("[OreHISMPool][배치] 강등 스캔: 강등=%d, 무효정리=%d"),
            DemotedThisFrame, CleanedInvalid);
    }
}

// ============================================================================
// 자동 관리 타이머 콜백
// ============================================================================
void UOreHISMPoolComponent::AutoManageTick()
{
    // [최적화4] 플레이어 위치 캐싱 — 이번 틱 동안 1회만 조회
    RefreshPlayerLocationCache();
    UE_LOG(LogOreHISMPool, Verbose, TEXT("[OreHISMPool][캐시] 플레이어 %d명 위치 캐싱 완료"),
        CachedPlayerLocations.Num());

    AutoDemoteFarFromPlayers(AutoDemoteRadius);
    AutoPromoteNearPlayers(AutoPromoteRadius);

    // [OreHISMDiagV1] 진단용 화면 출력 — Helluna.OreHISM.ShowStats 1.
    if (CVarOreHISMShowStats.GetValueOnGameThread() != 0 && GEngine)
    {
        int32 PromotedCount = 0;
        int32 DestroyedCount = 0;
        for (const FOreInstanceData& Data : AllInstances)
        {
            if (Data.bDestroyed) ++DestroyedCount;
            else if (Data.bPromotedToActor) ++PromotedCount;
        }
        const int32 ActiveHISM = AllInstances.Num() - DestroyedCount - PromotedCount;

        const FString Msg = FString::Printf(
            TEXT("[OreHISM] 등록=%d 활성HISM=%d 승격액터=%d 파괴=%d 풀=%d 플레이어=%d"),
            AllInstances.Num(), ActiveHISM, PromotedCount, DestroyedCount,
            Pools.Num(), CachedPlayerLocations.Num());

        // 키 = 컴포넌트 포인터 → 같은 메시지 덮어쓰기
        const uint64 Key = reinterpret_cast<uint64>(this);
        GEngine->AddOnScreenDebugMessage(
            static_cast<int32>(Key & 0x7FFFFFFF), AutoCheckInterval + 0.5f,
            FColor::Yellow, Msg);

        UE_LOG(LogOreHISMPool, Warning, TEXT("%s"), *Msg);
    }
}

// ============================================================================
// 내부: 메시별 풀 찾기/생성
// ============================================================================
int32 UOreHISMPoolComponent::FindOrCreatePool(UStaticMesh* Mesh)
{
    // 기존 풀 검색
    for (int32 i = 0; i < Pools.Num(); ++i)
    {
        if (Pools[i].Mesh == Mesh)
        {
            return i;
        }
    }

    // 새 풀 생성
    AActor* Owner = GetOwner();
    if (!Owner) return INDEX_NONE;

    FOreHISMPool& NewPool = Pools.AddDefaulted_GetRef();
    NewPool.Mesh = Mesh;

    // HISM 컴포넌트 생성
    const FString CompName = FString::Printf(TEXT("OreHISM_%s"), *Mesh->GetName());
    UHierarchicalInstancedStaticMeshComponent* HISM = NewObject<UHierarchicalInstancedStaticMeshComponent>(
        Owner, *CompName);

    HISM->SetStaticMesh(Mesh);
    HISM->SetCastShadow(bHISMCastShadow);
    HISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);  // HISM은 시각만 담당
    HISM->SetMobility(EComponentMobility::Static);
    HISM->SetCullDistances(0.f, HISMCullDistance);
    HISM->NumCustomDataFloats = 0;

    HISM->RegisterComponent();

    // [OreHISMHostFixV1] Owner(GameMode 등) 에 RootComponent 가 없으면 attach 가 실패해
    // HISM 의 ComponentToWorld 가 unset → bounds 0 → frustum culling 으로 안 보일 수 있음.
    // 해결: RootComponent 가 있으면 attach, 없으면 명시적으로 World 0 에 위치 설정.
    USceneComponent* OwnerRoot = Owner->GetRootComponent();
    if (OwnerRoot)
    {
        HISM->AttachToComponent(OwnerRoot, FAttachmentTransformRules::KeepWorldTransform);
        UE_LOG(LogOreHISMPool, Log, TEXT("[OreHISMHostFixV1] HISM attach OK to RootComponent=%s"),
            *OwnerRoot->GetName());
    }
    else
    {
        HISM->SetWorldLocationAndRotation(FVector::ZeroVector, FRotator::ZeroRotator);
        HISM->SetWorldScale3D(FVector::OneVector);
        UE_LOG(LogOreHISMPool, Warning,
            TEXT("[OreHISMHostFixV1] Owner=%s 에 RootComponent 없음 → HISM 을 World (0,0,0) 에 명시 위치"),
            *Owner->GetName());
    }

    NewPool.HISMComp = HISM;

    UE_LOG(LogOreHISMPool, Log, TEXT("[OreHISMPool] 새 풀 생성: %s (풀 인덱스: %d)"),
        *Mesh->GetName(), Pools.Num() - 1);

    return Pools.Num() - 1;
}

// ============================================================================
// 내부: 플레이어 위치 캐시 갱신
// ============================================================================
void UOreHISMPoolComponent::RefreshPlayerLocationCache()
{
    CachedPlayerLocations.Reset();

    UWorld* World = GetWorld();
    if (!World) return;

    for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
    {
        const APlayerController* PC = It->Get();
        if (!PC) continue;

        const APawn* Pawn = PC->GetPawn();
        if (!Pawn) continue;

        CachedPlayerLocations.Add(Pawn->GetActorLocation());
    }
}

// ============================================================================
// 내부: 캐시 기반 가장 가까운 플레이어 거리
// ============================================================================
float UOreHISMPoolComponent::GetClosestPlayerDistSqCached(const FVector& Location) const
{
    float ClosestDistSq = MAX_FLT;

    for (const FVector& PlayerLoc : CachedPlayerLocations)
    {
        const float DistSq = FVector::DistSquared(Location, PlayerLoc);
        if (DistSq < ClosestDistSq)
        {
            ClosestDistSq = DistSq;
        }
    }

    return ClosestDistSq;
}

// ============================================================================
// 내부: 가장 가까운 플레이어 거리 (캐시 없이 — 외부 직접 호출용)
// ============================================================================
float UOreHISMPoolComponent::GetClosestPlayerDistSq(const FVector& Location) const
{
    UWorld* World = GetWorld();
    if (!World) return MAX_FLT;

    float ClosestDistSq = MAX_FLT;

    for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
    {
        const APlayerController* PC = It->Get();
        if (!PC) continue;

        const APawn* Pawn = PC->GetPawn();
        if (!Pawn) continue;

        const float DistSq = FVector::DistSquared(Location, Pawn->GetActorLocation());
        if (DistSq < ClosestDistSq)
        {
            ClosestDistSq = DistSq;
        }
    }

    return ClosestDistSq;
}
