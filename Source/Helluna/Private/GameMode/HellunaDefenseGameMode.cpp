#include "GameMode/HellunaDefenseGameMode.h"
#include "GameMode/HellunaDefenseGameState.h"
#include "Object/ResourceUsingObject/ResourceUsingObject_SpaceShip.h"
#include "ECS/Spawner/HellunaEnemyMassSpawner.h"
#include "Character/HellunaEnemyCharacter.h"
#include "Engine/TargetPoint.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "debughelper.h"
#include "AIController.h"
#include "Components/StateTreeComponent.h"
#include "PCGComponent.h"
#include "PCG/PCGScoreComponent.h"
#include "PCGGraph.h"
#include "Data/PCGBasePointData.h"
#include "PCGWorldActor.h"
#include "Grid/PCGLandscapeCache.h"
#include "LandscapeProxy.h"
#include "LandscapeInfo.h"
#include "LandscapeComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "ActorFolder.h"

// Phase 7 게임 종료 + 결과 반영
#include "Lobby/Database/HellunaSQLiteSubsystem.h"
#include "Player/HellunaPlayerState.h"
#include "Controller/HellunaHeroController.h"
#include "Character/HellunaHeroCharacter.h"
#include "Character/EnemyComponent/HellunaHealthComponent.h"
#include "InventoryManagement/Components/Inv_InventoryComponent.h"
#include "Player/Inv_PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Chat/HellunaChatTypes.h"
#include "Login/Controller/HellunaLoginController.h"  // [Fix50]
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace HellunaPCGInternal
{
    /**
     * 광석 배치용 2D 균일 격자(공간 해시).
     * 셀 크기 = 최소 간격으로 잡으면 임의 점의 충돌 후보는 인접 3×3 셀 내부에만 존재한다.
     * Add/AnyWithinSq 모두 평균 O(1) — 이전의 O(N²) 선형 스캔을 대체.
     * Z는 SnapToGround 이후 거의 동일 평면이므로 2D만 사용해도 충분하다.
     */
    struct FOreReservedGrid2D
    {
        TMap<int64, TArray<FVector>> Cells;
        double InvCellSize = 1.0;

        void Init(double CellSize, int32 ReserveCells)
        {
            InvCellSize = 1.0 / FMath::Max(CellSize, 1.0);
            Cells.Empty(ReserveCells);
        }

        static FORCEINLINE int64 PackKey(int32 X, int32 Y)
        {
            return (static_cast<int64>(X) << 32) | static_cast<int64>(static_cast<uint32>(Y));
        }

        FORCEINLINE int64 KeyOf(const FVector& V) const
        {
            return PackKey(
                FMath::FloorToInt(V.X * InvCellSize),
                FMath::FloorToInt(V.Y * InvCellSize));
        }

        FORCEINLINE void Add(const FVector& V)
        {
            Cells.FindOrAdd(KeyOf(V)).Add(V);
        }

        bool AnyWithinSq(const FVector& V, double RadiusSq) const
        {
            const int32 Cx = FMath::FloorToInt(V.X * InvCellSize);
            const int32 Cy = FMath::FloorToInt(V.Y * InvCellSize);
            for (int32 dx = -1; dx <= 1; ++dx)
            {
                for (int32 dy = -1; dy <= 1; ++dy)
                {
                    if (const TArray<FVector>* Bucket = Cells.Find(PackKey(Cx + dx, Cy + dy)))
                    {
                        for (const FVector& P : *Bucket)
                        {
                            if (FVector::DistSquared(P, V) <= RadiusSq)
                            {
                                return true;
                            }
                        }
                    }
                }
            }
            return false;
        }

        /** 셀 크기 ≥ 검색 반경이라는 전제로 인접 3×3 셀만 검사 → 평균 O(1). */
        int32 CountWithinSq(const FVector& V, double RadiusSq) const
        {
            const int32 Cx = FMath::FloorToInt(V.X * InvCellSize);
            const int32 Cy = FMath::FloorToInt(V.Y * InvCellSize);
            int32 Count = 0;
            for (int32 dx = -1; dx <= 1; ++dx)
            {
                for (int32 dy = -1; dy <= 1; ++dy)
                {
                    if (const TArray<FVector>* Bucket = Cells.Find(PackKey(Cx + dx, Cy + dy)))
                    {
                        for (const FVector& P : *Bucket)
                        {
                            if (FVector::DistSquared(P, V) <= RadiusSq)
                            {
                                ++Count;
                            }
                        }
                    }
                }
            }
            return Count;
        }
    };
}
#include "Dom/JsonObject.h"

AHellunaDefenseGameMode::AHellunaDefenseGameMode()
{
#if HELLUNA_DEBUG_DEFENSE
    UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] Constructor"));
    UE_LOG(LogTemp, Warning, TEXT("  PlayerControllerClass: %s"), PlayerControllerClass ? *PlayerControllerClass->GetName() : TEXT("nullptr"));
    UE_LOG(LogTemp, Warning, TEXT("  DefaultPawnClass: %s"),      DefaultPawnClass      ? *DefaultPawnClass->GetName()      : TEXT("nullptr"));
#endif
}

void AHellunaDefenseGameMode::BeginPlay()
{
    Super::BeginPlay();
    if (!HasAuthority()) return;

    // 커맨드라인 LobbyURL 오버라이드
    FString CmdLobbyURL;
    if (FParse::Value(FCommandLine::Get(), TEXT("-LobbyURL="), CmdLobbyURL))
    {
        LobbyServerURL = CmdLobbyURL;
        UE_LOG(LogHelluna, Warning, TEXT("[DefenseGameMode] 커맨드라인에서 LobbyServerURL 설정: %s"), *LobbyServerURL);
    }
    UE_LOG(LogHelluna, Warning, TEXT("[DefenseGameMode] LobbyServerURL = '%s'"), *LobbyServerURL);

    // Phase 12b: 서버 레지스트리 초기화
    {
        const FString RegistryDir = GetRegistryDirectoryPath();
        IFileManager::Get().MakeDirectory(*RegistryDir, true);
        WriteRegistryFile(TEXT("empty"), 0);
        UE_LOG(LogHelluna, Log, TEXT("[DefenseGameMode] 서버 레지스트리 초기화 | Port=%d | Path=%s"), GetServerPort(), *GetRegistryFilePath());

        // 30초마다 하트비트
        if (UWorld* W = GetWorld())
        {
            W->GetTimerManager().SetTimer(RegistryHeartbeatTimer, [this]()
            {
                const FString Status = CurrentPlayerCount > 0 ? TEXT("playing") : TEXT("empty");
                WriteRegistryFile(Status, CurrentPlayerCount);
            }, 30.0f, true);
        }
    }

    CacheBossSpawnPoints();
    CacheMeleeSpawnPoints();
    CacheRangeSpawnPoints();
    CacheNightPCGComponents();

    // [Phase 16] 유휴 자동 종료 타이머 (접속자 0이면 IdleShutdownSeconds 후 자동 종료)
    if (IdleShutdownSeconds > 0.f)
    {
        if (UWorld* W = GetWorld())
        {
            W->GetTimerManager().SetTimer(IdleShutdownTimer, this,
                &AHellunaDefenseGameMode::CheckIdleShutdown, IdleShutdownSeconds, false);
            UE_LOG(LogHelluna, Log, TEXT("[DefenseGameMode] [Phase16] 유휴 종료 타이머 시작 (%.0f초)"), IdleShutdownSeconds);
        }
    }

    // [Phase 19] 빈 상태에서 커맨드 파일 폴링 시작
    StartCommandPollTimer();

#if HELLUNA_DEBUG_DEFENSE
    UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] BeginPlay 완료 — BossSpawn:%d / MeleeSpawn:%d / RangeSpawn:%d / NightPCG:%d"),
        BossSpawnPoints.Num(), MeleeSpawnPoints.Num(), RangeSpawnPoints.Num(), CachedNightPCGComponents.Num());
#endif
}

// ============================================================
// InitializeGame
// ============================================================
void AHellunaDefenseGameMode::InitializeGame()
{
    if (bGameInitialized)
    {
#if HELLUNA_DEBUG_DEFENSE
        UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] 이미 초기화됨, 스킵"));
#endif
        return;
    }
    bGameInitialized = true;

#if HELLUNA_DEBUG_DEFENSE
    UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] 게임 시작!"));
#endif
    Debug::Print(TEXT("[DefenseGameMode] InitializeGame - 게임 시작!"), FColor::Green);

    EnterDay();
    StartAutoSave();
}

// ============================================================
// 스폰 포인트 캐싱
// ============================================================
void AHellunaDefenseGameMode::CacheBossSpawnPoints()
{
    BossSpawnPoints.Empty();
    TArray<AActor*> Found;
    UGameplayStatics::GetAllActorsOfClass(this, ATargetPoint::StaticClass(), Found);
    for (AActor* A : Found)
        if (ATargetPoint* TP = Cast<ATargetPoint>(A))
            if (TP->ActorHasTag(BossSpawnPointTag))
                BossSpawnPoints.Add(TP);

#if HELLUNA_DEBUG_DEFENSE
    UE_LOG(LogTemp, Warning, TEXT("[CacheBossSpawnPoints] BossSpawnPoints: %d개 (태그: %s)"),
        BossSpawnPoints.Num(), *BossSpawnPointTag.ToString());
#endif
}

void AHellunaDefenseGameMode::CacheMeleeSpawnPoints()
{
    MeleeSpawnPoints.Empty();
    TArray<AActor*> Found;
    UGameplayStatics::GetAllActorsOfClass(this, ATargetPoint::StaticClass(), Found);
    for (AActor* A : Found)
        if (ATargetPoint* TP = Cast<ATargetPoint>(A))
            if (TP->ActorHasTag(MeleeSpawnTag))
                MeleeSpawnPoints.Add(TP);

    Debug::Print(FString::Printf(TEXT("[CacheMeleeSpawnPoints] %d개 (태그: %s)"),
        MeleeSpawnPoints.Num(), *MeleeSpawnTag.ToString()),
        MeleeSpawnPoints.Num() > 0 ? FColor::Green : FColor::Red);
}

void AHellunaDefenseGameMode::CacheRangeSpawnPoints()
{
    RangeSpawnPoints.Empty();
    TArray<AActor*> Found;
    UGameplayStatics::GetAllActorsOfClass(this, ATargetPoint::StaticClass(), Found);
    for (AActor* A : Found)
        if (ATargetPoint* TP = Cast<ATargetPoint>(A))
            if (TP->ActorHasTag(RangeSpawnTag))
                RangeSpawnPoints.Add(TP);

    Debug::Print(FString::Printf(TEXT("[CacheRangeSpawnPoints] %d개 (태그: %s)"),
        RangeSpawnPoints.Num(), *RangeSpawnTag.ToString()),
        RangeSpawnPoints.Num() > 0 ? FColor::Green : FColor::Red);
}

// ============================================================
// PCG 밤 스폰 시스템
// ============================================================

void AHellunaDefenseGameMode::CacheNightPCGComponents()
{
    UE_LOG(LogTemp, Warning, TEXT("══════════════════════════════════════════════════════════"));
    UE_LOG(LogTemp, Warning, TEXT("[CacheNightPCG] ▶ 시작"));
    UE_LOG(LogTemp, Warning, TEXT("══════════════════════════════════════════════════════════"));

    CachedNightPCGComponents.Empty();

    UWorld* World = GetWorld();
    if (!IsValid(World))
    {
        UE_LOG(LogTemp, Error, TEXT("[CacheNightPCG] World가 nullptr — 캐싱 스킵"));
        return;
    }

    // [패키징debug] PCGLandscapeCache 핸들 (per-volume 검증에서 사용 — Shipping 외)
#if !UE_BUILD_SHIPPING
    UPCGLandscapeCache* PCGLandscapeCacheRef = nullptr;
#endif

    // PCG Landscape Cache 자동 설정
    {
        TArray<AActor*> FoundWorldActors;
        UGameplayStatics::GetAllActorsOfClass(World, APCGWorldActor::StaticClass(), FoundWorldActors);
        UE_LOG(LogTemp, Warning, TEXT("[CacheNightPCG] [1/4] PCGWorldActor 검색: %d개"), FoundWorldActors.Num());

        if (FoundWorldActors.Num() > 0)
        {
            APCGWorldActor* PCGWorldActor = Cast<APCGWorldActor>(FoundWorldActors[0]);
            if (IsValid(PCGWorldActor) && IsValid(PCGWorldActor->LandscapeCacheObject))
            {
                UPCGLandscapeCache* LandscapeCache = PCGWorldActor->LandscapeCacheObject;
#if !UE_BUILD_SHIPPING
                PCGLandscapeCacheRef = LandscapeCache;
#endif
                UE_LOG(LogTemp, Warning, TEXT("[CacheNightPCG] [1/4] LandscapeCache SerializationMode=%d (0=Never, 1=Always)"),
                    (int32)LandscapeCache->SerializationMode);
                if (LandscapeCache->SerializationMode == EPCGLandscapeCacheSerializationMode::NeverSerialize)
                {
                    LandscapeCache->SerializationMode = EPCGLandscapeCacheSerializationMode::AlwaysSerialize;
                    Debug::Print(TEXT("[CacheNightPCG] PCG Landscape Cache 자동 설정 (AlwaysSerialize)"), FColor::Cyan);
                }
            }
            else
            {
                UE_LOG(LogTemp, Warning, TEXT("[CacheNightPCG] [1/4] PCGWorldActor 유효=%d / LandscapeCache 유효=%d"),
                    IsValid(PCGWorldActor),
                    PCGWorldActor ? IsValid(PCGWorldActor->LandscapeCacheObject) : false);
            }
        }
    }

    // 태그 기반 검색 (수동 배치 + 자동 스폰 액터)
    TArray<AActor*> Found;
    UGameplayStatics::GetAllActorsWithTag(World, NightPCGTag, Found);
    UE_LOG(LogTemp, Warning, TEXT("[CacheNightPCG] [2/4] 태그 '%s' 검색 결과: %d개"), *NightPCGTag.ToString(), Found.Num());

    for (int32 i = 0; i < Found.Num(); ++i)
    {
        AActor* Actor = Found[i];
        if (!IsValid(Actor))
        {
            UE_LOG(LogTemp, Warning, TEXT("[CacheNightPCG] [3/4] [%d/%d] 유효하지 않은 액터 스킵"), i, Found.Num());
            continue;
        }

        UE_LOG(LogTemp, Warning, TEXT("[CacheNightPCG] [3/4] [%d/%d] 액터=%s | 클래스=%s | 위치=%s"),
            i, Found.Num(), *Actor->GetName(), *Actor->GetClass()->GetName(), *Actor->GetActorLocation().ToString());

        UPCGComponent* PCGComp = Actor->FindComponentByClass<UPCGComponent>();
        if (IsValid(PCGComp))
        {
            UPCGGraph* Graph = PCGComp->GetGraph();

            // 에디터에서 이미 생성된 managed resource 확인
            TArray<UObject*> ExistingManaged;
            GetObjectsWithOuter(PCGComp, ExistingManaged, false);
            UE_LOG(LogTemp, Warning, TEXT("[CacheNightPCG] [3/4]   기존 ManagedResources: %d개 | IsGenerating=%d"),
                ExistingManaged.Num(), PCGComp->IsGenerating());

            PCGComp->GenerationTrigger = EPCGComponentGenerationTrigger::GenerateOnDemand;
            PCGComp->bIsComponentPartitioned = false;
            // CleanupLocal 호출하지 않음 — 에디터에서 이미 생성된 광석도 유지

            CachedNightPCGComponents.Add(PCGComp);

            UE_LOG(LogTemp, Warning, TEXT("[CacheNightPCG] [3/4]   ✅ 캐싱 성공 | Graph=%s"),
                Graph ? *Graph->GetName() : TEXT("nullptr"));

            // ── [패키징debug] PCGVolume 영역 진단: Bounds + 겹치는 LandscapeProxy 조사 ──
            // 원인 미상으로 재발 가능성이 있어 Shipping 외 빌드에서 보존
#if !UE_BUILD_SHIPPING
            {
                const FBox VolumeBounds = Actor->GetComponentsBoundingBox(/*bNonColliding=*/true);
                const FVector BoundsCenter = VolumeBounds.GetCenter();
                const FVector BoundsExtent = VolumeBounds.GetExtent();
                UE_LOG(LogTemp, Warning,
                    TEXT("[패키징debug] Volume Bounds | Actor=%s | Center=(%.0f,%.0f,%.0f) | Extent=(%.0f,%.0f,%.0f) | Valid=%d"),
                    *Actor->GetName(),
                    BoundsCenter.X, BoundsCenter.Y, BoundsCenter.Z,
                    BoundsExtent.X, BoundsExtent.Y, BoundsExtent.Z,
                    VolumeBounds.IsValid ? 1 : 0);

                // ALandscapeProxy 클래스를 동적 조회 (모듈 의존성 회피)
                UClass* LandscapeProxyClass = FindObject<UClass>(nullptr, TEXT("/Script/Landscape.LandscapeProxy"));
                if (!LandscapeProxyClass)
                {
                    LandscapeProxyClass = FindObject<UClass>(nullptr, TEXT("/Script/Landscape.LandscapeStreamingProxy"));
                }
                if (LandscapeProxyClass)
                {
                    int32 TotalProxies = 0;
                    int32 OverlappingProxies = 0;
                    FString OverlappingNames;
                    for (TActorIterator<AActor> It(World, LandscapeProxyClass); It; ++It)
                    {
                        AActor* LP = *It;
                        if (!IsValid(LP)) continue;
                        TotalProxies++;
                        const FBox LPBounds = LP->GetComponentsBoundingBox(true);
                        // XY 평면에서 교차 확인 (Z 무시 — PCG Surface Sampler는 XY 영역 기준)
                        const bool bOverlapXY =
                            VolumeBounds.Min.X <= LPBounds.Max.X && VolumeBounds.Max.X >= LPBounds.Min.X &&
                            VolumeBounds.Min.Y <= LPBounds.Max.Y && VolumeBounds.Max.Y >= LPBounds.Min.Y;
                        if (bOverlapXY)
                        {
                            OverlappingProxies++;
                            if (OverlappingProxies <= 5)
                            {
                                OverlappingNames += FString::Printf(TEXT("%s "), *LP->GetName());
                            }
                            UE_LOG(LogTemp, Warning,
                                TEXT("[패키징debug]   겹침 LandscapeProxy=%s | Class=%s | LP X=[%.0f~%.0f] Y=[%.0f~%.0f]"),
                                *LP->GetName(), *LP->GetClass()->GetName(),
                                LPBounds.Min.X, LPBounds.Max.X, LPBounds.Min.Y, LPBounds.Max.Y);
                        }
                    }
                    UE_LOG(LogTemp, Warning,
                        TEXT("[패키징debug] Volume=%s | 전체 LandscapeProxy=%d | 겹침=%d | 이름=[%s]"),
                        *Actor->GetName(), TotalProxies, OverlappingProxies, *OverlappingNames);

                    if (OverlappingProxies == 0)
                    {
                        UE_LOG(LogTemp, Error,
                            TEXT("[패키징debug] ❌ Volume=%s 위에 LandscapeProxy 0개! Surface Sampler 입력이 비어있을 가능성 매우 높음"),
                            *Actor->GetName());
                    }
                }
                else
                {
                    UE_LOG(LogTemp, Warning,
                        TEXT("[패키징debug] LandscapeProxy 클래스 동적 조회 실패 — Landscape 모듈 미로드"));
                }

                // ── [패키징debug] LandscapeProxy 컴포넌트 상세 (런타임에서 PCG cache API export 안 됨) ──
                // PCGLandscapeCache::GetCacheEntry는 PCG_API 미export → 직접 호출 불가
                // 대신 각 프록시의 컴포넌트 수/섹션 키 범위/LandscapeInfo 유효성을 로깅하여
                // 빌드 시점에 PCG가 캐시를 만들 만한 정상 상태였는지 간접 판단
                {
                    int32 TotalProxiesChecked = 0;
                    int32 ProxiesNoLandscapeInfo = 0;
                    int32 ProxiesNoComponents = 0;
                    int32 GrandComponentsCount = 0;

                    for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
                    {
                        ALandscapeProxy* LP = *It;
                        if (!IsValid(LP)) continue;
                        const FBox LPBounds = LP->GetComponentsBoundingBox(true);
                        const bool bOverlapXY =
                            VolumeBounds.Min.X <= LPBounds.Max.X && VolumeBounds.Max.X >= LPBounds.Min.X &&
                            VolumeBounds.Min.Y <= LPBounds.Max.Y && VolumeBounds.Max.Y >= LPBounds.Min.Y;
                        if (!bOverlapXY) continue;

                        TotalProxiesChecked++;
                        ULandscapeInfo* LandscapeInfo = LP->GetLandscapeInfo();
                        const bool bHasInfo = (LandscapeInfo != nullptr);
                        if (!bHasInfo) ProxiesNoLandscapeInfo++;

                        const int32 NumComps = LP->LandscapeComponents.Num();
                        GrandComponentsCount += NumComps;
                        if (NumComps == 0) ProxiesNoComponents++;

                        FIntPoint MinKey(MAX_int32, MAX_int32);
                        FIntPoint MaxKey(MIN_int32, MIN_int32);
                        int32 ValidComps = 0;
                        int32 NullHeightmap = 0;
                        for (ULandscapeComponent* Comp : LP->LandscapeComponents)
                        {
                            if (!Comp) continue;
                            ValidComps++;
                            const FIntPoint Key = (Comp->ComponentSizeQuads > 0)
                                ? (Comp->GetSectionBase() / Comp->ComponentSizeQuads)
                                : FIntPoint::ZeroValue;
                            MinKey.X = FMath::Min(MinKey.X, Key.X);
                            MinKey.Y = FMath::Min(MinKey.Y, Key.Y);
                            MaxKey.X = FMath::Max(MaxKey.X, Key.X);
                            MaxKey.Y = FMath::Max(MaxKey.Y, Key.Y);
                            if (!Comp->GetHeightmap()) NullHeightmap++;
                        }

                        UE_LOG(LogTemp, Warning,
                            TEXT("[패키징debug]   Proxy=%s | LandscapeInfo=%d | Comps=%d/유효=%d | NullHeightmap=%d | 키=[(%d,%d)~(%d,%d)] | LP X=[%.0f~%.0f] Y=[%.0f~%.0f]"),
                            *LP->GetName(), bHasInfo ? 1 : 0, NumComps, ValidComps, NullHeightmap,
                            (ValidComps > 0 ? MinKey.X : 0), (ValidComps > 0 ? MinKey.Y : 0),
                            (ValidComps > 0 ? MaxKey.X : 0), (ValidComps > 0 ? MaxKey.Y : 0),
                            LPBounds.Min.X, LPBounds.Max.X, LPBounds.Min.Y, LPBounds.Max.Y);
                    }

                    UE_LOG(LogTemp, Warning,
                        TEXT("[패키징debug] LandscapeProxy 요약 | Volume=%s | 검사=%d | LandscapeInfo없음=%d | 컴포넌트0개=%d | 총컴포넌트=%d"),
                        *Actor->GetName(), TotalProxiesChecked, ProxiesNoLandscapeInfo, ProxiesNoComponents, GrandComponentsCount);
                }
                (void)PCGLandscapeCacheRef; // 향후 PCG_API export되면 사용
            }
#endif // !UE_BUILD_SHIPPING

#if ENABLE_DRAW_DEBUG
            const FVector Loc = Actor->GetActorLocation();
            DrawDebugSphere(GetWorld(), Loc, 150.f, 12, FColor::Cyan, false, 10.f, 0, 2.f);
            DrawDebugString(GetWorld(), Loc + FVector(0, 0, 200.f),
                FString::Printf(TEXT("PCG Cached [%s]"), *Actor->GetName()),
                nullptr, FColor::Cyan, 10.f, true);
#endif
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("[CacheNightPCG] [3/4]   ❌ PCGComponent 없음 — 스킵"));
        }
    }

    const int32 CachedCount = CachedNightPCGComponents.Num();
    UE_LOG(LogTemp, Warning, TEXT("[CacheNightPCG] [4/4] ◀ 최종 캐싱: %d개 | 태그: %s"),
        CachedCount, *NightPCGTag.ToString());

    Debug::Print(FString::Printf(TEXT("[CacheNightPCG] %d개 캐싱 완료 (태그: %s)"),
        CachedCount, *NightPCGTag.ToString()),
        CachedCount > 0 ? FColor::Green : FColor::Red);

    if (CachedCount == 0)
    {
        UE_LOG(LogTemp, Error, TEXT("[CacheNightPCG] PCG 컴포넌트가 0개! 레벨에 '%s' 태그 PCG 액터를 배치하세요."),
            *NightPCGTag.ToString());
        Debug::Print(TEXT("[CacheNightPCG] PCG 0개! 레벨에 NightPCG 태그 PCG 액터를 배치하세요"), FColor::Red);
    }
}

void AHellunaDefenseGameMode::ActivateNightPCG()
{
    if (!HasAuthority()) return;

    UE_LOG(LogTemp, Warning, TEXT("══════════════════════════════════════════════════════════"));
    UE_LOG(LogTemp, Warning, TEXT("[ActivateNightPCG] ▶ 시작 (프레임분산) | Day=%d | 캐싱된 PCG: %d개"), CurrentDay, CachedNightPCGComponents.Num());
    UE_LOG(LogTemp, Warning, TEXT("══════════════════════════════════════════════════════════"));

    if (CachedNightPCGComponents.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("[ActivateNightPCG] 캐싱된 PCG가 0개 — 밤 광석 스폰 없음"));
        Debug::Print(TEXT("[ActivateNightPCG] 캐싱된 PCG 0개 — 스폰 없음"), FColor::Red);
        return;
    }

    // ★ PCG 컴포넌트를 타이머로 한 개씩 순차 실행하여 프레임 히치 방지
    PCGStaggerIndex = 0;
    PCGStaggerActivatedCount = 0;

    UE_LOG(LogTemp, Warning, TEXT("[PCG 최적화] 순차 생성 시작 | 총 %d개 | 배치간격=%.3f초"),
        CachedNightPCGComponents.Num(), PCGBatchInterval);
    Debug::Print(FString::Printf(TEXT("[PCG 최적화] Day%d | %d개 PCG 순차 생성 시작"),
        CurrentDay, CachedNightPCGComponents.Num()), FColor::Cyan);

    ProcessNextPCGComponent();
}

void AHellunaDefenseGameMode::ProcessNextPCGComponent()
{
    if (PCGStaggerIndex >= CachedNightPCGComponents.Num())
    {
        UE_LOG(LogTemp, Warning, TEXT("[PCG 최적화] 순차 생성 완료 | 활성화: %d개"), PCGStaggerActivatedCount);
        return;
    }

    const int32 i = PCGStaggerIndex++;

    UPCGComponent* PCGComp = CachedNightPCGComponents[i].Get();
    if (!IsValid(PCGComp))
    {
        UE_LOG(LogTemp, Warning, TEXT("[ActivateNightPCG] [순차 %d/%d] ❌ PCGComponent 유효하지 않음"),
            i + 1, CachedNightPCGComponents.Num());
        GetWorldTimerManager().SetTimerForNextTick(this, &AHellunaDefenseGameMode::ProcessNextPCGComponent);
        return;
    }

    AActor* PCGActor = PCGComp->GetOwner();
    UPCGGraph* Graph = PCGComp->GetGraph();

    UE_LOG(LogTemp, Warning, TEXT("[ActivateNightPCG] [순차 %d/%d] Owner=%s | Graph=%s"),
        i + 1, CachedNightPCGComponents.Num(),
        PCGActor ? *PCGActor->GetName() : TEXT("nullptr"),
        Graph ? *Graph->GetName() : TEXT("nullptr"));

    if (!Graph)
    {
        UE_LOG(LogTemp, Error, TEXT("[ActivateNightPCG] [순차 %d/%d] ❌ Graph가 nullptr — 스킵"), i + 1, CachedNightPCGComponents.Num());
        GetWorldTimerManager().SetTimerForNextTick(this, &AHellunaDefenseGameMode::ProcessNextPCGComponent);
        return;
    }

#if WITH_EDITOR
    {
        int32 DirtiedCount = 0;
        auto ForceMarkDirty = [](UPackage* Pkg)
        {
            if (Pkg && !Pkg->IsDirty())
            {
                Pkg->SetDirtyFlag(true);
                return true;
            }
            return false;
        };

        if (UWorld* W = GetWorld())
        {
            for (ULevel* Level : W->GetLevels())
            {
                if (!Level) continue;
                if (ForceMarkDirty(Level->GetPackage())) DirtiedCount++;
                Level->ForEachActorFolder([&](UActorFolder* Folder)
                {
                    if (Folder)
                    {
                        if (UPackage* ExtPkg = Folder->GetExternalPackage())
                        {
                            if (ForceMarkDirty(ExtPkg)) DirtiedCount++;
                        }
                        if (ForceMarkDirty(Folder->GetPackage())) DirtiedCount++;
                    }
                    return true;
                });
            }
        }

        TArray<UObject*> ManagedObjs;
        GetObjectsWithOuter(PCGComp, ManagedObjs, false);
        for (UObject* Obj : ManagedObjs)
        {
            if (UPackage* Pkg = Obj->GetExternalPackage())
            {
                if (ForceMarkDirty(Pkg)) DirtiedCount++;
            }
        }

        if (PCGActor)
        {
            if (ULevel* PCGLevel = PCGActor->GetLevel())
            {
                ForceMarkDirty(PCGLevel->GetPackage());
            }
        }
    }
#endif

    PCGComp->CleanupLocal(/*bRemoveComponents=*/true);

    PCGComp->OnPCGGraphGeneratedDelegate.RemoveAll(this);
    PCGComp->OnPCGGraphGeneratedDelegate.AddUObject(this, &AHellunaDefenseGameMode::OnNightPCGGraphGenerated);

    if (!PCGComp->IsActive())
    {
        PCGComp->Activate(/*bReset=*/true);
    }

    PCGComp->bIsComponentPartitioned = false;
    PCGComp->Seed = FMath::Rand();

    // 촘촘함(0.1~5.0) → 비선형 과장(^1.5) → 베이스 PPSM(0.001) 곱해 SurfaceSampler.PointsPerSquaredMeter로 직접 사용.
    // 그래프: UserParameterGet("PlacementDensity") → SurfaceSampler.PointsPerSquaredMeter override pin.
    // 따라서 여기서 push하는 값은 "밀도 배수"가 아니라 "최종 PPSM"이다.
    float DbgRawDensity = -1.f;
    float DbgPushedPPSM = -1.f;
    EPropertyBagResult DbgPushResult = EPropertyBagResult::PropertyNotFound;
    bool DbgHasScoreComp = false;
    bool DbgHasGraphInst = false;
    if (AActor* PreGenOwner = PCGComp->GetOwner())
    {
        if (const UPCGScoreComponent* ScoreComp = PreGenOwner->FindComponentByClass<UPCGScoreComponent>())
        {
            DbgHasScoreComp = true;
            const float RawDensity = FMath::Max(ScoreComp->PlacementDensity, 0.01f);
            constexpr float BasePointsPerSquaredMeter = 0.001f; // SurfaceSampler 기본값과 동일
            const float EffectivePPSM = BasePointsPerSquaredMeter * FMath::Pow(RawDensity, 1.5f);
            DbgRawDensity = RawDensity;
            DbgPushedPPSM = EffectivePPSM;
            if (UPCGGraphInstance* GraphInst = PCGComp->GetGraphInstance())
            {
                DbgHasGraphInst = true;
                DbgPushResult = GraphInst->SetGraphParameter<float>(FName(TEXT("PlacementDensity")), EffectivePPSM);
            }
        }
    }
    if (DbgPushResult == EPropertyBagResult::Success)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[촘촘함→PCG] ✅ 전달 성공 | Owner=%s | 촘촘함=%.2f → PPSM=%.5f (=0.001×%.2f^1.5)"),
            PCGActor ? *PCGActor->GetName() : TEXT("nullptr"), DbgRawDensity, DbgPushedPPSM, DbgRawDensity);
        Debug::Print(FString::Printf(TEXT("[촘촘함→PCG] ✅ 촘촘함 %.2f → PPSM %.5f | %s"),
            DbgRawDensity, DbgPushedPPSM, PCGActor ? *PCGActor->GetName() : TEXT("?")), FColor::Green);
    }
    else
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[촘촘함→PCG] ❌ 전달 실패 | Owner=%s | Score=%s | GraphInst=%s | 촘촘함=%.2f | PPSM=%.5f | Result=%d — 그래프에 float 'PlacementDensity' 유저 파라미터가 있는지 확인"),
            PCGActor ? *PCGActor->GetName() : TEXT("nullptr"),
            DbgHasScoreComp ? TEXT("O") : TEXT("X"),
            DbgHasGraphInst ? TEXT("O") : TEXT("X"),
            DbgRawDensity, DbgPushedPPSM, static_cast<int32>(DbgPushResult));
        Debug::Print(FString::Printf(TEXT("[촘촘함→PCG] ❌ 실패(Result=%d) | %s"),
            static_cast<int32>(DbgPushResult), PCGActor ? *PCGActor->GetName() : TEXT("?")), FColor::Red);
    }

    PCGComp->GenerateLocal(/*bForce=*/true);
    UE_LOG(LogTemp, Warning, TEXT("[ActivateNightPCG] [순차 %d/%d] ✅ GenerateLocal 완료 (Seed=%d)"),
        i + 1, CachedNightPCGComponents.Num(), PCGComp->Seed);

#if WITH_EDITOR
    {
        TWeakObjectPtr<UPCGComponent> WeakPCGForDirty = PCGComp;
        GetWorldTimerManager().ClearTimer(PCGActorFolderDirtyGuardTimer);
        GetWorldTimerManager().SetTimer(PCGActorFolderDirtyGuardTimer, [this, WeakPCGForDirty]()
        {
            if (!WeakPCGForDirty.IsValid() || !WeakPCGForDirty->IsGenerating())
            {
                GetWorldTimerManager().ClearTimer(PCGActorFolderDirtyGuardTimer);
                return;
            }
            if (UWorld* W = GetWorld())
            {
                for (ULevel* Level : W->GetLevels())
                {
                    if (!Level) continue;
                    Level->ForEachActorFolder([](UActorFolder* Folder)
                    {
                        if (Folder)
                        {
                            if (UPackage* ExtPkg = Folder->GetExternalPackage())
                            {
                                if (!ExtPkg->IsDirty()) ExtPkg->SetDirtyFlag(true);
                            }
                            UPackage* Pkg = Folder->GetPackage();
                            if (Pkg && !Pkg->IsDirty()) Pkg->SetDirtyFlag(true);
                        }
                        return true;
                    });
                }
            }
        }, 0.01f, true);
        }
#endif

    PCGStaggerActivatedCount++;

    // 다음 PCG 컴포넌트를 타이머로 지연 처리
    if (PCGStaggerIndex < CachedNightPCGComponents.Num())
    {
        GetWorldTimerManager().SetTimer(PCGStaggerTimer, this,
            &AHellunaDefenseGameMode::ProcessNextPCGComponent, PCGBatchInterval, false);
    }
    else
    {
        GetWorldTimerManager().SetTimerForNextTick(this, &AHellunaDefenseGameMode::ProcessNextPCGComponent);
    }
}

void AHellunaDefenseGameMode::OnNightPCGGraphGenerated(UPCGComponent* InComponent)
{
    if (!IsValid(InComponent))
    {
        UE_LOG(LogTemp, Warning, TEXT("[PCG 생성 콜백] ❌ InComponent가 nullptr"));
        return;
    }

    InComponent->OnPCGGraphGeneratedDelegate.RemoveAll(this);

    AActor* PCGOwner = InComponent->GetOwner();
    UE_LOG(LogTemp, Warning, TEXT("──────────────────────────────────────────────────────────"));
    UE_LOG(LogTemp, Warning, TEXT("[PCG 생성 콜백] ▶ 시작 | Day=%d | Owner=%s"),
        CurrentDay, PCGOwner ? *PCGOwner->GetName() : TEXT("nullptr"));

    // ── PCG 액터의 PCGScoreComponent에서 설정 읽기 ──
    // 뭉침비(ClusterAmount): 한 뭉침의 크기 / 촘촘함(PlacementDensity): 전체 배치 밀도
    float PCGClusterAmount = 1.f;
    float PCGPlacementDensity = 1.f;
    UClass* SpawnClassOverride = nullptr;
    if (IsValid(PCGOwner))
    {
        if (const UPCGScoreComponent* ScoreComp = PCGOwner->FindComponentByClass<UPCGScoreComponent>())
        {
            PCGClusterAmount = FMath::Max(ScoreComp->ClusterAmount, 0.01f);
            PCGPlacementDensity = FMath::Max(ScoreComp->PlacementDensity, 0.01f);
            if (ScoreComp->SpawnActorClassOverride)
            {
                SpawnClassOverride = ScoreComp->SpawnActorClassOverride;
            }
        }
    }

    // ── PCG 출력 포인트 데이터에서 위치 직접 추출 (Spawn Actor 노드 불필요) ──
    TArray<FPreservedOre> ExtractedOres;
    {
        if (!SpawnClassOverride)
        {
            UE_LOG(LogTemp, Error, TEXT("[PCG 생성 콜백] ❌ SpawnActorClassOverride가 설정되지 않음! PCGScoreComponent에서 스폰할 클래스를 지정하세요."));
            Debug::Print(TEXT("[PCG] SpawnActorClassOverride 미설정 — PCGScoreComponent 확인 필요"), FColor::Red);
            return;
        }

        const FPCGDataCollection& Output = InComponent->GetGeneratedGraphOutput();
        TArray<FPCGTaggedData> SpatialData = Output.GetAllSpatialInputs();

        // ── [패키징debug] PCG 출력 전체 진단 (Shipping 외 빌드에서만) ──
#if !UE_BUILD_SHIPPING
        UE_LOG(LogTemp, Warning,
            TEXT("[패키징debug] PCG Output | Owner=%s | TaggedDataAll=%d | SpatialInputs=%d"),
            PCGOwner ? *PCGOwner->GetName() : TEXT("nullptr"),
            Output.TaggedData.Num(), SpatialData.Num());

        for (int32 SIdx = 0; SIdx < Output.TaggedData.Num(); ++SIdx)
        {
            const FPCGTaggedData& Td = Output.TaggedData[SIdx];
            UE_LOG(LogTemp, Warning,
                TEXT("[패키징debug]   [%d] Pin=%s | DataClass=%s | Tags=%d"),
                SIdx, *Td.Pin.ToString(),
                Td.Data ? *Td.Data->GetClass()->GetName() : TEXT("nullptr"),
                Td.Tags.Num());
        }

        if (PCGOwner)
        {
            const FBox VolBounds = PCGOwner->GetComponentsBoundingBox(true);
            UE_LOG(LogTemp, Warning,
                TEXT("[패키징debug] PCG Owner Bounds | Center=(%.0f,%.0f,%.0f) | Extent=(%.0f,%.0f,%.0f)"),
                VolBounds.GetCenter().X, VolBounds.GetCenter().Y, VolBounds.GetCenter().Z,
                VolBounds.GetExtent().X, VolBounds.GetExtent().Y, VolBounds.GetExtent().Z);
        }
#endif // !UE_BUILD_SHIPPING

        for (const FPCGTaggedData& Tagged : SpatialData)
        {
            const UPCGBasePointData* BasePointData = Cast<UPCGBasePointData>(Tagged.Data);
            if (!BasePointData)
            {
#if !UE_BUILD_SHIPPING
                UE_LOG(LogTemp, Warning,
                    TEXT("[패키징debug]   Spatial → BasePointData CAST FAIL | DataClass=%s"),
                    Tagged.Data ? *Tagged.Data->GetClass()->GetName() : TEXT("nullptr"));
#endif
                continue;
            }

            const TArray<FTransform> Transforms = BasePointData->GetTransformsCopy();
            UE_LOG(LogTemp, Warning, TEXT("[PCG 디버그]   포인트: %d개"), Transforms.Num());

            for (const FTransform& T : Transforms)
            {
                ExtractedOres.Add({ SpawnClassOverride, T, { FName(TEXT("Ore")) }, PCGClusterAmount, PCGPlacementDensity });
            }
        }
    }

    UE_LOG(LogTemp, Warning, TEXT("[PCG 생성 콜백] 추출된 광석 데이터: %d개"), ExtractedOres.Num());
    // [촘촘함 검증] PCG 그래프가 파라미터를 실제로 반영했는지 — 촘촘함과 포인트 수가 함께 움직여야 정상
    UE_LOG(LogTemp, Warning,
        TEXT("[촘촘함 검증] 촘촘함=%.2f | PCG 출력 포인트=%d개 | Owner=%s  (촘촘함을 올렸는데 포인트 수가 비슷하면 그래프가 파라미터를 사용하지 않는 것)"),
        PCGPlacementDensity, ExtractedOres.Num(),
        PCGOwner ? *PCGOwner->GetName() : TEXT("nullptr"));
    Debug::Print(FString::Printf(TEXT("[촘촘함 검증] 촘촘함=%.2f → PCG 포인트 %d개"),
        PCGPlacementDensity, ExtractedOres.Num()), FColor::Cyan);

    // ── PCG 포인트 분포 디버그: 중심 대비 좌우 편향 확인 ──
    if (ExtractedOres.Num() > 0)
    {
        FVector CenterSum = FVector::ZeroVector;
        for (const FPreservedOre& Ore : ExtractedOres)
        {
            CenterSum += Ore.Transform.GetLocation();
        }
        const FVector Center = CenterSum / ExtractedOres.Num();

        // PCG 액터 위치 기준으로 오프셋 확인
        AActor* PCGOwnerDbg = InComponent->GetOwner();
        const FVector PCGOrigin = PCGOwnerDbg ? PCGOwnerDbg->GetActorLocation() : FVector::ZeroVector;
        const FVector Offset = Center - PCGOrigin;

        FVector MinPt(TNumericLimits<float>::Max());
        FVector MaxPt(TNumericLimits<float>::Lowest());
        for (const FPreservedOre& Ore : ExtractedOres)
        {
            const FVector Loc = Ore.Transform.GetLocation();
            MinPt.X = FMath::Min(MinPt.X, Loc.X); MinPt.Y = FMath::Min(MinPt.Y, Loc.Y);
            MaxPt.X = FMath::Max(MaxPt.X, Loc.X); MaxPt.Y = FMath::Max(MaxPt.Y, Loc.Y);
        }

        UE_LOG(LogTemp, Warning, TEXT("[PCG 편향 디버그] PCG원점=(%.0f, %.0f) | 포인트 평균=(%.0f, %.0f) | 오프셋=(%.0f, %.0f) | X범위=[%.0f ~ %.0f] | Y범위=[%.0f ~ %.0f]"),
            PCGOrigin.X, PCGOrigin.Y, Center.X, Center.Y, Offset.X, Offset.Y,
            MinPt.X, MaxPt.X, MinPt.Y, MaxPt.Y);
        Debug::Print(FString::Printf(TEXT("[PCG 편향] 오프셋=(%.0f, %.0f) | %d개"), Offset.X, Offset.Y, ExtractedOres.Num()), FColor::Yellow);
    }

    // ── PCG 즉시 클린업 → PCG 관리 액터 제거, 기존 독립 광석은 영향 없음 ──
    InComponent->CleanupLocal(/*bRemoveComponents=*/true);
    InComponent->Deactivate();

    if (ExtractedOres.Num() == 0)
    {
        UE_LOG(LogTemp, Error, TEXT("[PCG 생성 콜백] ❌ 추출된 광석 0개! PCG 그래프 설정을 확인하세요."));
        Debug::Print(TEXT("[PCG 생성 콜백] 추출된 광석 0개 — PCG 그래프 확인 필요"), FColor::Red);
        return;
    }

    // ── 종합 점수 로그 (개발 빌드 전용 — Shipping에서는 O(N×M) 스캔 회피) ──
#if !UE_BUILD_SHIPPING
    {
        float AvgDensityScore = 0.f;
        TArray<AActor*> CurrentOres;
        UGameplayStatics::GetAllActorsWithTag(GetWorld(), FName(TEXT("Ore")), CurrentOres);

        for (const FPreservedOre& Ore : ExtractedOres)
        {
            const FVector Loc = Ore.Transform.GetLocation();
            AvgDensityScore += CalculateOreDensityFactor(Loc, CurrentOres);
        }
        AvgDensityScore /= ExtractedOres.Num();
        const float AvgTotalScore = AvgDensityScore * PCGPlacementDensity;

        // 예상 스폰 수 계산 — 실제 절차적 스폰 로직과 일치
        // 클러스터는 후보 풀 N에서 뽑는 게 아니라 시드 주변에 각도·거리 공식으로 생성되므로 N에 캡되지 않는다.
        const int32 N = ExtractedOres.Num();
        // 시드 수: 후보 풀(N)에 비례. 촘촘함은 PCG 그래프 쪽에서 N 자체를 이미 스케일했으므로 여기선 배수 없음
        const int32 EstSeedCount = FMath::Max(1, FMath::RoundToInt(N * ClusterSeedRatio));
        // 시드당 클러스터 크기(평균): (Min+Max)/2 × 뭉침비. 절차적 생성이라 N에 묶이지 않음
        const float AvgClusterSize = 0.5f * (MinClusterSize + MaxClusterSize) * PCGClusterAmount;
        const int32 EstClusterOres = FMath::RoundToInt(EstSeedCount * AvgClusterSize);
        // 단독 후보: N에서 시드 + 시드당 WantMembers만큼 배정된 것을 제외한 나머지
        // WantMembers 평균: ((Min-1)+(Max-1))/2 × 뭉침비
        const float AvgWantMembers = 0.5f * ((MinClusterSize - 1) + (MaxClusterSize - 1)) * PCGClusterAmount;
        const int32 Assigned = FMath::Min(EstSeedCount + FMath::RoundToInt(EstSeedCount * AvgWantMembers), N);
        const int32 Unassigned = FMath::Max(0, N - Assigned);
        // 단독 생존율도 그대로 — 촘촘함은 N에 이미 반영됨
        const float WeightedSurvival = FMath::Clamp(IsolatedOreSurvivalChance, 0.f, 1.f);
        const int32 EstIsolated = FMath::RoundToInt(Unassigned * WeightedSurvival);
        const int32 EstTotal = EstClusterOres + EstIsolated;

        UPCGGraph* Graph = InComponent->GetGraph();
        const FString GraphName = Graph ? Graph->GetName() : TEXT("Unknown");

        UE_LOG(LogTemp, Warning, TEXT("[PCG 점수] 그래프=%s | 뭉침비=%.1f | 촘촘함=%.1f | 평균밀도=%.2f | 예상 스폰: ~%d개 (시드%d + 단독%d)"),
            *GraphName, PCGClusterAmount, PCGPlacementDensity, AvgDensityScore, EstTotal, EstClusterOres, EstIsolated);
        Debug::Print(FString::Printf(TEXT("[PCG 점수] %s | 뭉침=%.1f 촘촘=%.1f | 밀도=%.2f → 예상 ~%d개"),
            *GraphName, PCGClusterAmount, PCGPlacementDensity, AvgDensityScore, EstTotal), FColor::Cyan);
    }
#endif // !UE_BUILD_SHIPPING

    // 밀도 기반 클러스터 후처리 (추출한 데이터 기반, PCG 무관)
    PostProcessNightPCGDensity(ExtractedOres);
}

// ════════════════════════════════════════════════════════════════════════════════
// PCG 밀도 기반 후처리 — 추출 데이터 기반 독립 액터 스폰
// ════════════════════════════════════════════════════════════════════════════════

void AHellunaDefenseGameMode::PostProcessNightPCGDensity(const TArray<AHellunaDefenseGameMode::FPreservedOre>& NewOreData)
{
    UWorld* World = GetWorld();
    if (!World) return;

    const double PostProcessStartTime = FPlatformTime::Seconds();
    UE_LOG(LogTemp, Warning, TEXT("[밀도 컬링] ▶ 시작 (클러스터+프레임분산) | Day=%d"), CurrentDay);

    const int32 TotalNew = NewOreData.Num();
    if (TotalNew == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("[밀도 컬링] ❌ 신규 Ore 0개 — 컬링 스킵"));
        Debug::Print(TEXT("[밀도 컬링] 신규 Ore 0개 — PCG 그래프 확인 필요"), FColor::Red);
        return;
    }

    // 1) 기존 광석 수집 (밀도 계산용 — 이전 밤에 스폰된 독립 액터들)
    TArray<AActor*> ExistingOres;
    UGameplayStatics::GetAllActorsWithTag(World, FName(TEXT("Ore")), ExistingOres);

    UE_LOG(LogTemp, Warning, TEXT("[밀도 컬링] [1/4] 기존 Ore: %d개 | 신규 데이터: %d개"),
        ExistingOres.Num(), TotalNew);

    // 기존 광석 → 밀도 격자(셀 크기 = 검사 반경)
    // CalculateOreDensityFactor의 O(N×M) 선형 스캔을 평균 O(1) 격자 조회로 대체
    HellunaPCGInternal::FOreReservedGrid2D ExistingOreGrid;
    ExistingOreGrid.Init(DensityCheckRadius, ExistingOres.Num());
    for (const AActor* Ore : ExistingOres)
    {
        if (IsValid(Ore))
        {
            ExistingOreGrid.Add(Ore->GetActorLocation());
        }
    }
    const double DensityRadiusSq =
        static_cast<double>(DensityCheckRadius) * static_cast<double>(DensityCheckRadius);
    const float MaxNeighborInv = 1.f / static_cast<float>(FMath::Max(MaxNeighborOreCount, 1));

    auto DensityFactorFromGrid = [&](const FVector& Loc) -> float
    {
        const int32 NeighborCount = ExistingOreGrid.CountWithinSq(Loc, DensityRadiusSq);
        const float Ratio = NeighborCount * MaxNeighborInv;
        return FMath::Clamp(1.0f - Ratio, MinKeepRatio, 1.0f);
    };

    // 2) 시드 선정: 밀도/편향 점수가 높은 위치를 우선
    struct FSeedCandidate { int32 Index; float Score; };
    TArray<FSeedCandidate> Candidates;
    Candidates.Reserve(TotalNew);
    for (int32 i = 0; i < TotalNew; ++i)
    {
        const FVector Loc = NewOreData[i].Transform.GetLocation();
        // 후보 풀 수량은 PCG 그래프 측 PlacementDensity 파라미터가 결정
        // 여기서는 기존 광석 주변 회피용 밀도 점수만 시드 우선순위로 사용
        Candidates.Add({ i, DensityFactorFromGrid(Loc) });
    }
    // 동률 점수(=기존 광석 없는 첫날)에서 PCG 스캔 순서 편향을 끊기 위해 먼저 셔플
    // 그 뒤 stable sort → 높은 점수끼리는 랜덤 순서, 점수 순위는 유지
    for (int32 i = Candidates.Num() - 1; i > 0; --i)
    {
        const int32 j = FMath::RandRange(0, i);
        Candidates.Swap(i, j);
    }
    Candidates.StableSort([](const FSeedCandidate& A, const FSeedCandidate& B) { return A.Score > B.Score; });

    // 시드 수는 후보 풀에 선형 비례. 촘촘함은 PCG 그래프가 N 자체를 이미 스케일했으므로 배수 없음
    const int32 SeedCount = FMath::Max(1, FMath::RoundToInt(TotalNew * ClusterSeedRatio));
    const float MinSeedDistSq = ClusterRadius * ClusterRadius;
    TArray<int32> SelectedSeedIndices;
    SelectedSeedIndices.Reserve(SeedCount);
    for (const FSeedCandidate& C : Candidates)
    {
        if (SelectedSeedIndices.Num() >= SeedCount) break;
        const FVector Loc = NewOreData[C.Index].Transform.GetLocation();
        bool bTooClose = false;
        for (int32 ExistIdx : SelectedSeedIndices)
        {
            if (FVector::DistSquared(Loc, NewOreData[ExistIdx].Transform.GetLocation()) < MinSeedDistSq)
            { bTooClose = true; break; }
        }
        if (!bTooClose) SelectedSeedIndices.Add(C.Index);
    }

    // 3) 각 시드에 가까운 광석 배정
    TSet<int32> AssignedIndices;
    for (int32 SeedIdx : SelectedSeedIndices) AssignedIndices.Add(SeedIdx);

    TMap<int32, TArray<int32>> SeedToMembers;
    for (int32 SeedIdx : SelectedSeedIndices) SeedToMembers.Add(SeedIdx, TArray<int32>());

    for (int32 SeedIdx : SelectedSeedIndices)
    {
        const FVector SeedLoc = NewOreData[SeedIdx].Transform.GetLocation();
        // 뭉침비로 클러스터 크기 스케일 — 단독 광석은 별도 경로(B)에서 살아남음
        const float ClusterAmt = NewOreData[SeedIdx].ClusterAmount;
        const int32 ScaledMin = FMath::Max(1, FMath::RoundToInt((MinClusterSize - 1) * ClusterAmt));
        const int32 ScaledMax = FMath::Max(ScaledMin, FMath::RoundToInt((MaxClusterSize - 1) * ClusterAmt));
        const int32 WantMembers = FMath::RandRange(ScaledMin, ScaledMax);

        struct FDistEntry { int32 Index; double DistSq; };
        TArray<FDistEntry> NearOres;
        for (int32 i = 0; i < TotalNew; ++i)
        {
            if (AssignedIndices.Contains(i)) continue;
            NearOres.Add({ i, FVector::DistSquared(NewOreData[i].Transform.GetLocation(), SeedLoc) });
        }
        NearOres.Sort([](const FDistEntry& A, const FDistEntry& B) { return A.DistSq < B.DistSq; });

        TArray<int32>& Members = SeedToMembers[SeedIdx];
        for (int32 j = 0; j < NearOres.Num() && Members.Num() < WantMembers; ++j)
        {
            Members.Add(NearOres[j].Index);
            AssignedIndices.Add(NearOres[j].Index);
        }
    }

    UE_LOG(LogTemp, Warning, TEXT("[클러스터] 시드: %d개 | 신규 Ore: %d개"), SelectedSeedIndices.Num(), TotalNew);

    // ── 스폰 요청 리스트 준비 (PCG는 이미 클린업됨 — 파괴 단계 불필요) ──
    // [경사 보정] 지면 노멀에 부분 정렬된 기준 회전 + 기존 랜덤 Pitch/Roll을 합성
    // Yaw는 항상 0~360 랜덤, Pitch/Roll은 ±OreTiltMax 범위 내 랜덤 (기존 거동 유지)
    // 이후 TargetUp = Lerp(WorldUp, GroundNormal, TerrainAlignAlpha)에 정렬시켜 지형에 기대게 만듦
    auto MakeRandomTiltRotation = [this](const FVector& GroundNormal) -> FRotator
    {
        const float RandPitch = FMath::FRandRange(-OreTiltMaxPitch, OreTiltMaxPitch);
        const float RandYaw   = FMath::FRandRange(0.f, 360.f);
        const float RandRoll  = FMath::FRandRange(-OreTiltMaxRoll, OreTiltMaxRoll);

        // 지면 노멀이 비정상(제로 벡터)이거나 Alpha가 0이면 기존 랜덤 회전만 반환
        const FVector SafeNormal = GroundNormal.IsNearlyZero() ? FVector::UpVector : GroundNormal.GetSafeNormal();
        if (TerrainAlignAlpha <= KINDA_SMALL_NUMBER)
        {
            return FRotator(RandPitch, RandYaw, RandRoll);
        }
        // [평지 dead zone] 거의 평평한 지형(울퉁불퉁한 평지 포함)은 정렬을 건너뜀
        // 그러지 않으면 Normal.Z가 0.97~0.99인 모든 포인트에 미세한 기울기가 누적됨
        if (SafeNormal.Z >= FlatGroundNormalZ)
        {
            return FRotator(RandPitch, RandYaw, RandRoll);
        }

        // TargetUp: World Up과 지면 노멀을 Alpha로 블렌드
        FVector TargetUp = FMath::Lerp(FVector::UpVector, SafeNormal, TerrainAlignAlpha);
        if (!TargetUp.Normalize())
        {
            TargetUp = FVector::UpVector;
        }

        // Yaw 기준 Forward 벡터를 TargetUp 평면에 투영 → Yaw 보존
        const FVector YawForward = FRotator(0.f, RandYaw, 0.f).Vector();
        FVector ProjForward = YawForward - FVector::DotProduct(YawForward, TargetUp) * TargetUp;
        if (!ProjForward.Normalize())
        {
            // YawForward가 TargetUp과 평행한 극단 케이스
            FVector Fallback = FVector::RightVector - FVector::DotProduct(FVector::RightVector, TargetUp) * TargetUp;
            Fallback.Normalize();
            ProjForward = Fallback;
        }
        const FVector Right = FVector::CrossProduct(TargetUp, ProjForward).GetSafeNormal();

        // 지형 정렬된 기준 회전
        const FMatrix AlignedMat(ProjForward, Right, TargetUp, FVector::ZeroVector);
        const FQuat AlignedQuat(AlignedMat);

        // 기존 랜덤 Pitch/Roll 노이즈를 로컬 공간에서 얹음 (Yaw는 이미 AlignedQuat에 포함)
        const FQuat NoiseQuat = FQuat(FRotator(RandPitch, 0.f, RandRoll));
        return (AlignedQuat * NoiseQuat).Rotator();
    };

    auto MakeRandomScale = [this]() -> FVector
    {
        const float S = FMath::FRandRange(OreScaleMin, OreScaleMax);
        return FVector(S, S, S);
    };

    // Foliage(ISM) 액터를 한 번만 수집 — 라인트레이스에서 무시할 목록
    TArray<AActor*> FoliageActorsToIgnore;
    for (TActorIterator<AActor> It(GetWorld()); It; ++It)
    {
        TArray<UInstancedStaticMeshComponent*> ISMs;
        (*It)->GetComponents<UInstancedStaticMeshComponent>(ISMs);
        if (ISMs.Num() > 0)
        {
            FoliageActorsToIgnore.Add(*It);
        }
    }

    // 지면 보정 라인트레이스 — 랜드스케이프에만 스냅 (스태틱 메쉬/바위/건물 위에 깔리는 것 방지)
    // MultiTrace 후 LandscapeProxy 소유 컴포넌트 히트만 채택
    // [경사 보정] OutNormal로 지면 노멀을 반환, Normal.Z가 MinGroundNormalZ 미만이면 가파른 곳이므로 false 반환
    auto SnapToGround = [this, &FoliageActorsToIgnore](FVector& InOutLoc, FVector& OutNormal) -> bool
    {
        // 볼륨 Extent.Z 최대 20000 대응 — 넉넉히 ±50000 범위 트레이스
        const float TraceHalfRange = 50000.f;
        const FVector TraceStart = FVector(InOutLoc.X, InOutLoc.Y, InOutLoc.Z + TraceHalfRange);
        const FVector TraceEnd   = FVector(InOutLoc.X, InOutLoc.Y, InOutLoc.Z - TraceHalfRange);

        FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(OreGroundSnap), true);
        for (AActor* FoliageActor : FoliageActorsToIgnore)
        {
            TraceParams.AddIgnoredActor(FoliageActor);
        }

        TArray<FHitResult> Hits;
        if (GetWorld()->LineTraceMultiByChannel(Hits, TraceStart, TraceEnd, ECC_Visibility, TraceParams))
        {
            // 위에서 아래로 순차 히트. 랜드스케이프 소유 히트만 채택해 스태틱 메쉬/건물 위 스냅을 방지
            for (const FHitResult& Hit : Hits)
            {
                const AActor* HitActor = Hit.GetActor();
                if (!HitActor || !HitActor->IsA(ALandscapeProxy::StaticClass()))
                {
                    continue;
                }

                OutNormal = Hit.ImpactNormal;
                // [A 경사 필터] 가파른 지형이면 실패 처리 → 호출부에서 스킵
                if (OutNormal.Z < MinGroundNormalZ)
                {
                    return false;
                }
                InOutLoc.Z = Hit.ImpactPoint.Z;
                return true;
            }
        }
        OutNormal = FVector::UpVector;
        return false;
    };

    // [패키징debug] 스폰 시도/실패/성공 카운터 + 실제 스폰 위치 범위 집계
    int32 DbgSnapAttempt = 0;
    int32 DbgSnapFail = 0;
    int32 DbgSpawned = 0;
    FVector DbgMinSpawn(TNumericLimits<float>::Max());
    FVector DbgMaxSpawn(TNumericLimits<float>::Lowest());

    // 기울기에 따른 Z 오프셋 계산 — 기울어진 만큼 광석을 땅 속으로 내림
    auto CalcTiltZOffset = [this](const FRotator& Rot, const FVector& Scale) -> float
    {
        const float PitchRad = FMath::DegreesToRadians(FMath::Abs(Rot.Pitch));
        const float RollRad  = FMath::DegreesToRadians(FMath::Abs(Rot.Roll));
        const float MaxTiltRad = FMath::Max(PitchRad, RollRad);
        // 기울어진 면의 가장자리가 원래 높이보다 올라가는 양 = HalfHeight * (1 - cos(tilt))
        // 그만큼 Z를 내려서 가장 낮은 점이 지면에 닿도록 보정
        return OreHalfHeight * Scale.Z * (1.f - FMath::Cos(MaxTiltRad));
    };

    // 이전 그래프의 스폰 요청이 남아 있으면 이어붙임 (덮어쓰지 않음)

    // 기울기 보정: 광석이 최대 각도로 기울어졌을 때 메시가 차지하는 추가 반경을 반영
    // 광석 높이를 대략 스케일 * 50cm로 가정, sin(15°) ≈ 0.26 → 양쪽 다 기울면 x2
    const float MaxTiltRad = FMath::DegreesToRadians(FMath::Max(OreTiltMaxPitch, OreTiltMaxRoll));
    const float TiltBuffer = OreScaleMax * 50.f * FMath::Sin(MaxTiltRad) * 2.f;
    const float EffectiveMinSpacing = ClusterPlaceMinDist + TiltBuffer;
    const double MinSpacingSq = static_cast<double>(EffectiveMinSpacing) * static_cast<double>(EffectiveMinSpacing);
    const int32 MaxRetries = 5;

    // 겹침 방지용 균일 격자 — 셀 크기 = 최소 간격, 인접 3×3만 검사하므로 평균 O(1) 조회
    HellunaPCGInternal::FOreReservedGrid2D ReservedGrid;
    ReservedGrid.Init(EffectiveMinSpacing,
        ExistingOres.Num() + PendingClusterSpawns.Num() + TotalNew * 2);
    for (AActor* Ore : ExistingOres)
    {
        if (IsValid(Ore))
        {
            ReservedGrid.Add(Ore->GetActorLocation());
        }
    }
    // 이전 그래프에서 이미 예약된 스폰 위치도 겹침 방지에 포함
    for (const FClusterSpawnRequest& Prev : PendingClusterSpawns)
    {
        ReservedGrid.Add(Prev.Transform.GetLocation());
    }

    auto IsLocationValid = [&ReservedGrid, MinSpacingSq](const FVector& Loc) -> bool
    {
        return !ReservedGrid.AnyWithinSq(Loc, MinSpacingSq);
    };

    // (A) 뭉침 광석 스폰 요청 — 뭉침비로 클러스터 크기 스케일, 크기에 비례해 반경 확장
    for (int32 SeedIdx : SelectedSeedIndices)
    {
        const FVector SeedLoc = NewOreData[SeedIdx].Transform.GetLocation();
        const float SeedZ = SeedLoc.Z;
        const float ClusterAmt = NewOreData[SeedIdx].ClusterAmount;
        // 뭉침비 기반 클러스터 크기 — 기본 MinClusterSize..MaxClusterSize에 배수 적용
        const int32 ClusterMin = FMath::Max(1, FMath::RoundToInt(MinClusterSize * ClusterAmt));
        const int32 ClusterMax = FMath::Max(ClusterMin, FMath::RoundToInt(MaxClusterSize * ClusterAmt));
        const int32 TotalInCluster = FMath::RandRange(ClusterMin, ClusterMax);
        const float AngleStep = 360.f / TotalInCluster;
        UClass* OreClass = NewOreData[SeedIdx].OreClass;
        TArray<FName> OreTags = NewOreData[SeedIdx].Tags;

        // 클러스터 크기에 비례해 배치 반경 확장 (기본 크기 대비)
        const int32 BaseMax = (MinClusterSize + MaxClusterSize) / 2;
        const float RadiusScale = (BaseMax > 0) ? FMath::Sqrt(static_cast<float>(TotalInCluster) / BaseMax) : 1.f;
        const float ScaledMinDist = ClusterPlaceMinDist * RadiusScale;
        const float ScaledMaxDist = ClusterPlaceMaxDist * RadiusScale;

        for (int32 j = 0; j < TotalInCluster; ++j)
        {
            FVector SpawnLoc;
            bool bValid = false;

            for (int32 Retry = 0; Retry <= MaxRetries; ++Retry)
            {
                const float BaseAngle = AngleStep * j + FMath::FRandRange(-20.f, 20.f) + (Retry * 72.f);
                const float Dist = (j == 0 && Retry == 0) ? 0.f : FMath::FRandRange(ScaledMinDist, ScaledMaxDist);
                const FVector Dir = FVector::ForwardVector.RotateAngleAxis(BaseAngle, FVector::UpVector);
                SpawnLoc = SeedLoc + Dir * Dist;
                SpawnLoc.Z = SeedZ;

                if (IsLocationValid(SpawnLoc))
                {
                    bValid = true;
                    break;
                }
            }

            if (bValid)
            {
                ++DbgSnapAttempt;
                FVector GroundNormal;
                if (!SnapToGround(SpawnLoc, GroundNormal))
                {
                    ++DbgSnapFail;
                    continue; // Landscape 없거나 경사가 너무 가파른 위치 — 스폰 포기
                }

                const FRotator TiltRot = MakeRandomTiltRotation(GroundNormal);
                const FVector  Scale   = MakeRandomScale();
                SpawnLoc.Z -= CalcTiltZOffset(TiltRot, Scale);
                // [D] 경사도에 비례해 추가로 땅에 박아넣기 (평지 dead zone 이내면 0)
                if (GroundNormal.Z < FlatGroundNormalZ)
                {
                    SpawnLoc.Z -= (FlatGroundNormalZ - GroundNormal.Z) * GroundSlopeSinkAmount;
                }

                ReservedGrid.Add(SpawnLoc);
                PendingClusterSpawns.Add({ OreClass, FTransform(TiltRot, SpawnLoc, Scale), OreTags });
                ++DbgSpawned;
                DbgMinSpawn.X = FMath::Min(DbgMinSpawn.X, SpawnLoc.X); DbgMinSpawn.Y = FMath::Min(DbgMinSpawn.Y, SpawnLoc.Y);
                DbgMaxSpawn.X = FMath::Max(DbgMaxSpawn.X, SpawnLoc.X); DbgMaxSpawn.Y = FMath::Max(DbgMaxSpawn.Y, SpawnLoc.Y);
            }
        }
    }

    // (B) 단독 광석 스폰 요청 (클러스터에 배정되지 않은 광석 — 가중치로 생존 확률 조절)
    for (int32 i = 0; i < TotalNew; ++i)
    {
        if (AssignedIndices.Contains(i)) continue;
        // 단독 광석 생존율은 기본값 그대로 — 수량 스케일은 PCG 그래프가 담당
        const float WeightedSurvival = FMath::Clamp(IsolatedOreSurvivalChance, 0.f, 1.f);
        if (FMath::FRand() < WeightedSurvival)
        {
            FVector IsoLoc = NewOreData[i].Transform.GetLocation();
            if (!IsLocationValid(IsoLoc)) continue;

            ++DbgSnapAttempt;
            FVector GroundNormal;
            if (!SnapToGround(IsoLoc, GroundNormal))
            {
                ++DbgSnapFail;
                continue; // Landscape 없거나 경사가 너무 가파른 위치 — 스폰 포기
            }

            const FRotator TiltRot = MakeRandomTiltRotation(GroundNormal);
            const FVector  Scale   = MakeRandomScale();
            IsoLoc.Z -= CalcTiltZOffset(TiltRot, Scale);
            // [D] 경사도에 비례해 추가로 땅에 박아넣기 (평지 dead zone 이내면 0)
            if (GroundNormal.Z < FlatGroundNormalZ)
            {
                IsoLoc.Z -= (FlatGroundNormalZ - GroundNormal.Z) * GroundSlopeSinkAmount;
            }

            ReservedGrid.Add(IsoLoc);
            PendingClusterSpawns.Add({ NewOreData[i].OreClass,
                FTransform(TiltRot, IsoLoc, Scale),
                NewOreData[i].Tags });
            ++DbgSpawned;
            DbgMinSpawn.X = FMath::Min(DbgMinSpawn.X, IsoLoc.X); DbgMinSpawn.Y = FMath::Min(DbgMinSpawn.Y, IsoLoc.Y);
            DbgMaxSpawn.X = FMath::Max(DbgMaxSpawn.X, IsoLoc.X); DbgMaxSpawn.Y = FMath::Max(DbgMaxSpawn.Y, IsoLoc.Y);
        }
    }

    const double CalcTime = (FPlatformTime::Seconds() - PostProcessStartTime) * 1000.0;
#if !UE_BUILD_SHIPPING
    if (DbgSpawned > 0)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[스폰 편향 디버그] 시도=%d | SnapFail=%d (%.1f%%) | 실제스폰=%d | X범위=[%.0f ~ %.0f] | Y범위=[%.0f ~ %.0f]"),
            DbgSnapAttempt, DbgSnapFail,
            DbgSnapAttempt > 0 ? (100.f * DbgSnapFail / DbgSnapAttempt) : 0.f,
            DbgSpawned,
            DbgMinSpawn.X, DbgMaxSpawn.X, DbgMinSpawn.Y, DbgMaxSpawn.Y);
    }
    else
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[스폰 편향 디버그] 시도=%d | SnapFail=%d | 실제스폰=0"),
            DbgSnapAttempt, DbgSnapFail);
    }
#endif
    UE_LOG(LogTemp, Warning, TEXT("[PCG 최적화] 클러스터 계산 완료 (%.1fms) | 스폰예정: %d개 → 배칭 시작"),
        CalcTime, PendingClusterSpawns.Num());
    Debug::Print(FString::Printf(TEXT("[PCG 최적화] 클러스터 계산 %.1fms | 스폰 %d (배칭 중)"),
        CalcTime, PendingClusterSpawns.Num()), FColor::Orange);

    // ★ 스폰 배칭 시작 (파괴 단계 없음 — PCG 클린업 시 이미 제거됨)
    if (PendingClusterSpawns.Num() > 0)
    {
        // 타이머가 이미 돌고 있으면 (이전 그래프 배칭 중) 중복 시작하지 않음 — 배열에 추가만 되면 됨
        if (!GetWorldTimerManager().IsTimerActive(ClusterSpawnTimer))
        {
            ClusterSpawnBatchIndex = 0;
            GetWorldTimerManager().SetTimer(ClusterSpawnTimer, this,
                &AHellunaDefenseGameMode::ProcessClusterSpawnBatch, PCGBatchInterval, true);
        }
    }
    else
    {
        FinalizePostProcess();
    }
}

void AHellunaDefenseGameMode::ProcessClusterSpawnBatch()
{
    const int32 Total = PendingClusterSpawns.Num();
    int32 SpawnedThisBatch = 0;

    while (ClusterSpawnBatchIndex < Total && SpawnedThisBatch < PCGBatchSpawnCount)
    {
        const FClusterSpawnRequest& Req = PendingClusterSpawns[ClusterSpawnBatchIndex++];
        FActorSpawnParameters Params;
        Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        if (AActor* Spawned = GetWorld()->SpawnActor(Req.OreClass, &Req.Transform, Params))
        {
            Spawned->Tags = Req.Tags;
            Spawned->SetActorScale3D(Req.Transform.GetScale3D());
            SpawnedClusterOresResult.Add(Spawned);
        }
        SpawnedThisBatch++;
    }

    UE_LOG(LogTemp, Warning, TEXT("[PCG 최적화] 스폰 배치 | %d/%d (이번: %d개)"),
        ClusterSpawnBatchIndex, Total, SpawnedThisBatch);

    if (ClusterSpawnBatchIndex >= Total)
    {
        GetWorldTimerManager().ClearTimer(ClusterSpawnTimer);
        FinalizePostProcess();
    }
}

void AHellunaDefenseGameMode::FinalizePostProcess()
{
    UE_LOG(LogTemp, Warning, TEXT("[PCG 최적화] ◀ PostProcess 완료 | Day=%d | 스폰: %d개"),
        CurrentDay, SpawnedClusterOresResult.Num());
    Debug::Print(FString::Printf(TEXT("[PCG 최적화] Day%d | PostProcess 완료 — 스폰 %d개"),
        CurrentDay, SpawnedClusterOresResult.Num()),
        FColor::Green);

    PendingClusterSpawns.Empty();
    SpawnedClusterOresResult.Empty();
}

float AHellunaDefenseGameMode::CalculateSpawnScore(const FVector& Location, const TArray<AActor*>& ExistingOres) const
{
    return FMath::Clamp(CalculateOreDensityFactor(Location, ExistingOres), 0.f, 1.f);
}

float AHellunaDefenseGameMode::CalculateOreDensityFactor(const FVector& Location, const TArray<AActor*>& ExistingOres) const
{
    const float RadiusSq = DensityCheckRadius * DensityCheckRadius;
    int32 NeighborCount = 0;

    for (const AActor* Ore : ExistingOres)
    {
        if (!IsValid(Ore)) continue;
        if (FVector::DistSquared(Location, Ore->GetActorLocation()) <= RadiusSq)
        {
            NeighborCount++;
        }
    }

    // 이웃 수가 MaxNeighborOreCount 이상이면 MinKeepRatio, 0이면 1.0
    const float Ratio = static_cast<float>(NeighborCount) / static_cast<float>(FMath::Max(MaxNeighborOreCount, 1));
    return FMath::Clamp(1.0f - Ratio, MinKeepRatio, 1.0f);
}


// DeactivateNightPCG 삭제됨 — 광석은 낮이 되어도 영구 유지
// 다음 밤 ActivateNightPCG()에서 기존 광석 밀도를 기반으로 새 광석 컬링

// CurrentDay에 맞는 NightSpawnConfig 반환
// FromDay <= CurrentDay 중 FromDay가 가장 큰 항목 선택
const FNightSpawnConfig* AHellunaDefenseGameMode::GetCurrentNightConfig() const
{
    const FNightSpawnConfig* Best = nullptr;
    for (const FNightSpawnConfig& Config : NightSpawnTable)
    {
        if (Config.FromDay <= CurrentDay)
        {
            if (!Best || Config.FromDay > Best->FromDay)
                Best = &Config;
        }
    }
    return Best;
}

// ============================================================
// 낮/밤 시스템
// ============================================================
float AHellunaDefenseGameMode::GetEffectiveDayDuration() const
{
    // Day 1(첫째 날): TestDayDuration (BP 기본 5초 — 빠른 첫 밤 진입)
    // Day 2+: NormalDayDuration (BP 기본 300초 = 5분)
    return (CurrentDay <= 1) ? TestDayDuration : NormalDayDuration;
}

void AHellunaDefenseGameMode::EnterDay()
{
    if (!bGameInitialized) return;

    // 밤 워치독 해제 (밤 종료)
    GetWorldTimerManager().ClearTimer(TimerHandle_NightWatchdog);

    // 낮 카운터 증가 (게임 시작 첫 낮은 Day 1)
    CurrentDay++;

    // Day에 따라 적절한 낮 지속 시간 결정
    const float EffectiveDayDuration = GetEffectiveDayDuration();

    Debug::Print(FString::Printf(TEXT("[EnterDay] %d일차 낮 시작 (지속시간: %.0f초)"), CurrentDay, EffectiveDayDuration), FColor::Yellow);

    RemainingMonstersThisNight = 0;

    // PCG 생성 광석은 영구 유지 — 낮에 정리하지 않음

    // 낮 전환 시 대기 중인 스폰 타이머 전부 취소
    for (AHellunaEnemyMassSpawner* Spawner : CachedMeleeSpawners)
        if (IsValid(Spawner)) Spawner->CancelPendingSpawn();
    for (AHellunaEnemyMassSpawner* Spawner : CachedRangeSpawners)
        if (IsValid(Spawner)) Spawner->CancelPendingSpawn();

    if (AHellunaDefenseGameState* GS = GetGameState<AHellunaDefenseGameState>())
    {
        GS->SetPhase(EDefensePhase::Day);
        GS->SetAliveMonsterCount(0);
        GS->SetCurrentDayForUI(CurrentDay);
        GS->SetDayTimeRemaining(EffectiveDayDuration);
        GS->SetTotalMonstersThisNight(0);
        GS->SetIsBossNight(false);
        GS->MulticastPrintDay();

        // Phase 10: 채팅 시스템 메시지
        GS->BroadcastChatMessage(TEXT(""), TEXT("낮이 시작됩니다"), EChatMessageType::System);

        GS->NetMulticast_OnDawnPassed(EffectiveDayDuration);
    }

    GetWorldTimerManager().SetTimerForNextTick(this, &ThisClass::ActivateNightPCG);

    GetWorldTimerManager().ClearTimer(TimerHandle_ToNight);
    GetWorldTimerManager().SetTimer(TimerHandle_ToNight, this, &ThisClass::EnterNight, EffectiveDayDuration, false);

    // 1초마다 남은 낮 시간 감소
    GetWorldTimerManager().ClearTimer(TimerHandle_DayCountdown);
    GetWorldTimerManager().SetTimer(TimerHandle_DayCountdown, this, &ThisClass::TickDayCountdown, 1.f, true);
}

void AHellunaDefenseGameMode::EnterNight()
{
    if (!HasAuthority() || !bGameInitialized) return;

    Debug::Print(FString::Printf(TEXT("[EnterNight] %d일차 밤 시작"), CurrentDay), FColor::Purple);
    UE_LOG(LogTemp, Warning, TEXT("[EnterNight] Day=%d | CachedPCG=%d"), CurrentDay, CachedNightPCGComponents.Num());

    RemainingMonstersThisNight = 0;

    if (AHellunaDefenseGameState* GS = GetGameState<AHellunaDefenseGameState>())
    {
        GS->SetPhase(EDefensePhase::Night);

        GS->SetDayTimeRemaining(0.f);   // 밤엔 낮 타이머 0
        // AliveMonsterCount는 TriggerMassSpawning/보스 소환 확정 후 설정

        GS->SetAliveMonsterCount(0);

        // Phase 10: 채팅 시스템 메시지
        GS->BroadcastChatMessage(TEXT(""), TEXT("밤이 시작됩니다"), EChatMessageType::System);

    }

    // 낮 카운트다운 타이머 정지
    GetWorldTimerManager().ClearTimer(TimerHandle_DayCountdown);

    // 밤 워치독 시작 — 카운터와 실제 적 수 불일치 시 낮 전환 강제.
    // Why: NotifyMonsterDied가 호출되지 않는 사망 경로(맵 밖 낙사, ECS 디스폰 오류 등)가 있으면
    //      RemainingMonstersThisNight가 0이 되지 않아 낮으로 복귀 못 하는 회귀가 발생함.
    GetWorldTimerManager().ClearTimer(TimerHandle_NightWatchdog);
    GetWorldTimerManager().SetTimer(TimerHandle_NightWatchdog, this, &ThisClass::TickNightWatchdog, 1.0f, true);

    // ── 보스 소환 일 체크 ──────────────────────────────────────────────
    // BossSchedule 배열에서 CurrentDay와 일치하는 항목을 찾는다.
    // 동일 Day 중복 시 첫 번째 항목만 사용.
    const FBossSpawnEntry* FoundEntry = BossSchedule.FindByPredicate(
        [this](const FBossSpawnEntry& E){ return E.SpawnDay == CurrentDay; });

#if HELLUNA_DEBUG_DEFENSE
    UE_LOG(LogTemp, Warning, TEXT("[EnterNight] BossSchedule 체크 — CurrentDay=%d | Schedule 항목수=%d | FoundEntry=%s"),
        CurrentDay, BossSchedule.Num(), FoundEntry ? TEXT("찾음 ✅") : TEXT("없음"));
#endif
    if (FoundEntry)
    {
#if HELLUNA_DEBUG_DEFENSE
        UE_LOG(LogTemp, Warning, TEXT("[EnterNight] FoundEntry — SpawnDay=%d | BossClass=%s"),
            FoundEntry->SpawnDay,
            FoundEntry->BossClass ? *FoundEntry->BossClass->GetName() : TEXT("null ⚠️"));
#endif
    }

    if (FoundEntry)
    {
        Debug::Print(FString::Printf(
            TEXT("[EnterNight] %d일차 — %s 소환 대상, 일반 몬스터 스폰 생략"),
            CurrentDay,
            FoundEntry->BossClass ? *FoundEntry->BossClass->GetName() : TEXT("null")),
            FColor::Red);

        // 보스 1마리 소환 → UI용 몬스터 수 설정
        if (AHellunaDefenseGameState* GS = GetGameState<AHellunaDefenseGameState>())
        {
            GS->SetIsBossNight(true);
            GS->SetTotalMonstersThisNight(1);
            GS->SetAliveMonsterCount(1);
        }

        SetBossReady(true);
        TrySummonBoss(*FoundEntry);
        return;
    }

    int32 Current = 0, Need = 0;
    if (IsSpaceShipFullyRepaired(Current, Need))
    {
        // 우주선 수리 완료 시 BossSchedule과 무관하게 bBossReady만 세팅.
        // BossClass가 없는 폴백 상태이므로 별도 소환은 하지 않는다.
        SetBossReady(true);
        return;
    }

    TriggerMassSpawning();
}

// ============================================================
// TriggerMassSpawning — 밤 시작 시 근거리/원거리 몬스터 ECS 소환
//
// [소환 수 결정 우선순위]
//   1. NightSpawnTable에 CurrentDay에 맞는 FNightSpawnConfig가 있으면 해당 수 사용
//   2. 없으면 레거시 MassSpawnCountPerNight를 근거리에 적용 (원거리 0)
//
// [Spawner 생성 규칙]
//   - MeleeMassSpawnerClass가 설정되어 있으면 MeleeSpawnTag TargetPoint마다 근거리 Spawner 생성
//   - RangeMassSpawnerClass가 설정되어 있으면 RangeSpawnTag TargetPoint마다 원거리 Spawner 생성
//   - MeleeMassSpawnerClass가 없고 레거시 MassSpawnerClass가 있으면 MonsterSpawnTag로 폴백
//
// [소환 수 = Spawner 수 × 설정 값]
//   TargetPoint 2개 + MeleeCount=3 → 근거리 6마리 소환
// ============================================================
void AHellunaDefenseGameMode::TriggerMassSpawning()
{
    Debug::Print(TEXT("[TriggerMassSpawning] 진입"), FColor::Cyan);
    if (!HasAuthority()) return;

    // ── 이번 밤 소환 수 결정 ─────────────────────────────────────────
    int32 MeleeCount = MassSpawnCountPerNight; // NightSpawnTable 미설정 시 기본값
    int32 RangeCount = 0;

    if (const FNightSpawnConfig* Config = GetCurrentNightConfig())
    {
        MeleeCount = Config->MeleeCount;
        RangeCount = Config->RangeCount;
        Debug::Print(FString::Printf(
            TEXT("[TriggerMassSpawning] Day%d 설정 적용 (FromDay=%d) — 근거리:%d 원거리:%d"),
            CurrentDay, Config->FromDay, MeleeCount, RangeCount), FColor::Cyan);
    }
    else
    {
        Debug::Print(FString::Printf(
            TEXT("[TriggerMassSpawning] NightSpawnTable 미설정 — 기본값 근거리:%d 원거리:0"), MeleeCount), FColor::Yellow);
    }

    // ── 근거리 Spawner 초기화 (첫 번째 밤) ──────────────────────────
    if (MeleeMassSpawnerClass && CachedMeleeSpawners.IsEmpty())
    {
        if (MeleeSpawnPoints.IsEmpty())
            Debug::Print(FString::Printf(TEXT("[TriggerMassSpawning] 근거리 스폰 포인트 없음 — TargetPoint에 태그 '%s' 추가하세요"), *MeleeSpawnTag.ToString()), FColor::Red);

        for (ATargetPoint* TP : MeleeSpawnPoints)
        {
            if (!IsValid(TP)) continue;
            FActorSpawnParameters Params;
            Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            // SpawnActorDeferred: BeginPlay 전에 CDO 값이 올바르게 복사되도록 보장
            AHellunaEnemyMassSpawner* Spawner = GetWorld()->SpawnActorDeferred<AHellunaEnemyMassSpawner>(
                MeleeMassSpawnerClass, FTransform(TP->GetActorRotation(), TP->GetActorLocation()), nullptr, nullptr,
                ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
            if (IsValid(Spawner))
            {
                UGameplayStatics::FinishSpawningActor(Spawner, FTransform(TP->GetActorRotation(), TP->GetActorLocation()));
                CachedMeleeSpawners.Add(Spawner);
                Debug::Print(FString::Printf(TEXT("[TriggerMassSpawning] 근거리 Spawner 생성: %s | EntityTypes:%d"),
                    *Spawner->GetName(), Spawner->GetEntityTypesNum()), FColor::Green);
            }
        }
    }
    else if (!MeleeMassSpawnerClass)
        Debug::Print(TEXT("[TriggerMassSpawning] MeleeMassSpawnerClass 미설정 — BP에서 설정하세요"), FColor::Red);

    // ── 원거리 Spawner 초기화 (첫 번째 밤) ──────────────────────────
    if (RangeMassSpawnerClass && CachedRangeSpawners.IsEmpty())
    {
        if (RangeSpawnPoints.IsEmpty())
            Debug::Print(FString::Printf(TEXT("[TriggerMassSpawning] 원거리 스폰 포인트 없음 — TargetPoint에 태그 '%s' 추가하세요"), *RangeSpawnTag.ToString()), FColor::Red);

        for (ATargetPoint* TP : RangeSpawnPoints)
        {
            if (!IsValid(TP)) continue;
            // SpawnActorDeferred: BeginPlay 전에 CDO 값이 올바르게 복사되도록 보장
            AHellunaEnemyMassSpawner* Spawner = GetWorld()->SpawnActorDeferred<AHellunaEnemyMassSpawner>(
                RangeMassSpawnerClass, FTransform(TP->GetActorRotation(), TP->GetActorLocation()), nullptr, nullptr,
                ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
            if (IsValid(Spawner))
            {
                UGameplayStatics::FinishSpawningActor(Spawner, FTransform(TP->GetActorRotation(), TP->GetActorLocation()));
                CachedRangeSpawners.Add(Spawner);
                Debug::Print(FString::Printf(TEXT("[TriggerMassSpawning] 원거리 Spawner 생성: %s | EntityTypes:%d"),
                    *Spawner->GetName(), Spawner->GetEntityTypesNum()), FColor::Green);
            }
        }
    }

    // ── 매 밤: RequestSpawn 호출 + 카운터 확정 ───────────────────────
    float SpawnSequenceDelay = 0.f;

    for (AHellunaEnemyMassSpawner* Spawner : CachedMeleeSpawners)
    {
        if (!IsValid(Spawner)) continue;
        Spawner->RequestSpawn(MeleeCount, SpawnSequenceDelay);
        RemainingMonstersThisNight += Spawner->GetRequestedSpawnCount();
        Debug::Print(FString::Printf(
            TEXT("[TriggerMassSpawning] 근거리 RequestSpawn(%d, Delay=%.2f): %s | 누적: %d"),
            MeleeCount, SpawnSequenceDelay, *Spawner->GetName(), RemainingMonstersThisNight), FColor::Green);
        SpawnSequenceDelay += Spawner->GetEstimatedSpawnSequenceSpacing(MeleeCount);
    }

    for (AHellunaEnemyMassSpawner* Spawner : CachedRangeSpawners)
    {
        if (!IsValid(Spawner)) continue;
        if (RangeCount <= 0)    
        {
            Debug::Print(FString::Printf(TEXT("[TriggerMassSpawning] 원거리 0마리 — %s 스킵"), *Spawner->GetName()), FColor::Yellow);
            continue;
        }
        Spawner->RequestSpawn(RangeCount, SpawnSequenceDelay);
        RemainingMonstersThisNight += Spawner->GetRequestedSpawnCount();
        Debug::Print(FString::Printf(
            TEXT("[TriggerMassSpawning] 원거리 RequestSpawn(%d, Delay=%.2f): %s | 누적: %d"),
            RangeCount, SpawnSequenceDelay, *Spawner->GetName(), RemainingMonstersThisNight), FColor::Green);
        SpawnSequenceDelay += Spawner->GetEstimatedSpawnSequenceSpacing(RangeCount);
    }

    // 총 소환 수 확정 후 GameState에 반영 — Total과 Alive를 동시에 설정
    if (AHellunaDefenseGameState* GS = GetGameState<AHellunaDefenseGameState>())
    {
        GS->SetTotalMonstersThisNight(RemainingMonstersThisNight);
        GS->SetAliveMonsterCount(RemainingMonstersThisNight);
    }
}

// ============================================================
// RegisterAliveMonster — 하위 호환용 빈 함수
//
// 이전에는 EnemyCharacter::BeginPlay에서 이 함수를 호출해 AliveMonsters TSet에 등록했으나
// ECS 타이밍 이슈(Actor BeginPlay가 낮으로 넘어간 뒤 호출되어 등록 거부 → 미등록 몬스터 누적)로
// 카운터 기반 시스템으로 전환하면서 더 이상 사용하지 않음.
//
// 기존 BP나 EnemyCharacter에서 호출하는 코드가 있어도 문제없도록 빈 함수로 유지.
// 카운터는 TriggerMassSpawning의 RequestSpawn 호출 시점에 RemainingMonstersThisNight로 확정됨.
// ============================================================
void AHellunaDefenseGameMode::RegisterAliveMonster(AActor* Monster)
{
    // 의도적으로 비워둠 — 위 주석 참고
}

// ============================================================
// NotifyMonsterDied — 몬스터 사망 통보 및 낮 전환 판정
//
// GA_Death::HandleDeathFinished에서 호출됨.
// EnemyGrade에 따라 처리 경로 분기:
//   Normal   → RemainingMonstersThisNight 차감 → 0이 되면 낮 전환 타이머 시작
//   SemiBoss → NotifyBossDied 호출
//   Boss     → NotifyBossDied 호출
//
// [주의] 보스 BP에서 EnemyGrade를 Boss/SemiBoss로 설정하지 않으면
//        Normal 경로로 빠져 카운터만 차감되고 낮 전환이 되지 않음.
// ============================================================

void AHellunaDefenseGameMode::NotifyMonsterDied(AActor* DeadMonster)
{
    if (!HasAuthority() || !DeadMonster || !bGameInitialized) return;

    AHellunaDefenseGameState* GS = GetGameState<AHellunaDefenseGameState>();
    if (!GS) return;

    // ── 보스/세미보스 분기 ────────────────────────────────────────────
    if (const AHellunaEnemyCharacter* EnemyChar = Cast<AHellunaEnemyCharacter>(DeadMonster))
    {
        if (EnemyChar->EnemyGrade != EEnemyGrade::Normal)
        {
            NotifyBossDied(DeadMonster);
            return;
        }
    }

    // ── 일반 몬스터: 카운터 차감 ──────────────────────────────────────
    RemainingMonstersThisNight = FMath::Max(0, RemainingMonstersThisNight - 1);
    GS->SetAliveMonsterCount(RemainingMonstersThisNight); // UI 갱신

    if (RemainingMonstersThisNight <= 0)
    {
        GetWorldTimerManager().ClearTimer(TimerHandle_ToDay);
        GetWorldTimerManager().SetTimer(TimerHandle_ToDay, this, &ThisClass::EnterDay, TestNightFailToDayDelay, false);
    }
}

// ============================================================
// TickNightWatchdog — 카운터 불일치 보정
//
// RemainingMonstersThisNight는 NotifyMonsterDied 경로로만 감소한다.
// 맵 밖 낙사, ECS 디스폰 실패, GA_Death 미부여 등으로 카운터가 틀어지면
// "적을 다 죽여도 낮으로 돌아오지 않는" 증상이 발생.
//
// 조건:
//   - 현재 Night phase 이고 bGameEnded=false
//   - 보스 나이트가 아닐 것 (보스 살아있으면 별도 경로)
//   - 모든 스포너의 PendingSpawnCount == 0 (아직 배출할 적 없음)
//   - 월드에 살아있는 일반 AHellunaEnemyCharacter == 0
// → RemainingMonstersThisNight = 0 동기화 + TimerHandle_ToDay가 비어 있으면 EnterDay 예약
// ============================================================
void AHellunaDefenseGameMode::TickNightWatchdog()
{
    if (!HasAuthority() || !bGameInitialized || bGameEnded) return;

    AHellunaDefenseGameState* GS = GetGameState<AHellunaDefenseGameState>();
    if (!GS || GS->GetPhase() != EDefensePhase::Night) return;

    // 보스 나이트에서 보스가 살아있으면 NotifyBossDied에 맡기고 종료 분기 패스
    if (GS->bIsBossNight && AliveBoss.IsValid())
    {
        return;
    }

    // 스포너 pending 잔여 체크 — 아직 배출될 적이 있다면 대기
    int32 TotalPending = 0;
    for (AHellunaEnemyMassSpawner* Spawner : CachedMeleeSpawners)
    {
        if (IsValid(Spawner)) TotalPending += Spawner->GetPendingSpawnCount();
    }
    for (AHellunaEnemyMassSpawner* Spawner : CachedRangeSpawners)
    {
        if (IsValid(Spawner)) TotalPending += Spawner->GetPendingSpawnCount();
    }
    if (TotalPending > 0) return;

    // 월드의 살아있는 일반 적 스캔 (Normal grade, IsDead=false)
    UWorld* World = GetWorld();
    if (!World) return;

    int32 AliveCount = 0;
    for (TActorIterator<AHellunaEnemyCharacter> It(World); It; ++It)
    {
        AHellunaEnemyCharacter* Enemy = *It;
        if (!IsValid(Enemy) || Enemy->IsActorBeingDestroyed()) continue;
        if (Enemy->EnemyGrade != EEnemyGrade::Normal) continue; // 보스는 별도 경로

        UHellunaHealthComponent* Health = Enemy->FindComponentByClass<UHellunaHealthComponent>();
        if (Health && Health->IsDead()) continue;

        ++AliveCount;
    }

    if (AliveCount > 0) return; // 아직 살아있는 적 존재

    // 카운터와 실제 상태 불일치 → 보정 + 낮 전환
    if (RemainingMonstersThisNight > 0)
    {
        UE_LOG(LogHelluna, Warning,
            TEXT("[NightWatchdog] 카운터 불일치 보정 — RemainingMonstersThisNight=%d → 0 (실제 살아있는 적 0)"),
            RemainingMonstersThisNight);
        RemainingMonstersThisNight = 0;
        GS->SetAliveMonsterCount(0);
    }

    if (!GetWorldTimerManager().IsTimerActive(TimerHandle_ToDay))
    {
        UE_LOG(LogHelluna, Warning, TEXT("[NightWatchdog] 낮 전환 타이머 시작 (%0.1f초)"), TestNightFailToDayDelay);
        GetWorldTimerManager().SetTimer(TimerHandle_ToDay, this, &ThisClass::EnterDay, TestNightFailToDayDelay, false);
    }
}

// 보스몬스터 사망 로직 — NotifyMonsterDied에서 EnemyGrade != Normal 시 호출
void AHellunaDefenseGameMode::NotifyBossDied(AActor* DeadBoss)
{
    if (!HasAuthority() || !DeadBoss) return;

    AliveBoss.Reset();

    // 캐릭터 자체의 EnemyGrade로 등급 판별 (스케줄 조회 불필요)
    EEnemyGrade Grade = EEnemyGrade::Boss;
    if (const AHellunaEnemyCharacter* EnemyChar = Cast<AHellunaEnemyCharacter>(DeadBoss))
    {
        Grade = EnemyChar->EnemyGrade;
    }

    FString TypeLabel;
    switch (Grade)
    {
    case EEnemyGrade::SemiBoss: TypeLabel = TEXT("세미보스"); break;
    case EEnemyGrade::Boss:     TypeLabel = TEXT("보스");     break;
    default:                    TypeLabel = TEXT("알 수 없음"); break;
    }

    Debug::Print(FString::Printf(
        TEXT("[%s 사망] %s 처치됨 — Day %d"),
        *TypeLabel, *DeadBoss->GetName(), CurrentDay),
        FColor::Red);

    // TODO: 보스/세미보스 사망 후속 처리 (보상, 연출, 클리어 조건 등) 이후 구현

    // 보스 사망 -> AliveMonsterCount 0으로 설정
    if (AHellunaDefenseGameState* GS = GetGameState<AHellunaDefenseGameState>())
    {
        GS->SetAliveMonsterCount(0);
    }

    // 최종 보스 처치 → 승리
    if (Grade == EEnemyGrade::Boss)
    {
        UE_LOG(LogHelluna, Warning, TEXT("[Victory] 최종 보스 처치! EndGame(Escaped) 호출"));
        EndGame(EHellunaGameEndReason::Escaped);
    }
    else
    {
        // 세미보스 처치 → 낮 전환
        UE_LOG(LogHelluna, Log, TEXT("[NotifyBossDied] 세미보스 처치 — 낮 전환 타이머 시작"));
        GetWorldTimerManager().ClearTimer(TimerHandle_ToDay);
        GetWorldTimerManager().SetTimer(TimerHandle_ToDay, this, &ThisClass::EnterDay, TestNightFailToDayDelay, false);
    }
}

// ============================================================
// NotifyPlayerDied — 플레이어 사망 → 전원 사망 체크
// ============================================================
void AHellunaDefenseGameMode::NotifyPlayerDied(APlayerController* DeadPC)
{
    if (!HasAuthority() || !bGameInitialized || bGameEnded) return;

    UE_LOG(LogHelluna, Log, TEXT("[NotifyPlayerDied] %s 사망"), *GetNameSafe(DeadPC));

    // 생존자가 한 명이라도 있는지 확인
    for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
    {
        APlayerController* PC = It->Get();
        if (!IsValid(PC)) continue;

        APawn* Pawn = PC->GetPawn();
        if (!IsValid(Pawn)) continue;

        UHellunaHealthComponent* HealthComp = Pawn->FindComponentByClass<UHellunaHealthComponent>();
        if (HealthComp && HealthComp->IsAliveAndNotDowned())
        {
            // 생존자 있음 (다운 상태 제외) → 게임 계속
            UE_LOG(LogHelluna, Log, TEXT("[NotifyPlayerDied] 생존자 있음: %s"), *GetNameSafe(PC));
            return;
        }
    }

    // 전원 사망 → 패배
    UE_LOG(LogHelluna, Warning, TEXT("[Defeat] 전원 사망! EndGame(AllDead) 호출"));
    EndGame(EHellunaGameEndReason::AllDead);
}

// ============================================================
// [Phase 21] Downed/Revive — 솔로 감지 / 전원사망 판정
// ============================================================

bool AHellunaDefenseGameMode::ShouldSkipDowned() const
{
    if (!HasAuthority()) return true;

    UWorld* World = GetWorld();
    if (!World) return true;

    int32 AlivePlayerCount = 0;
    for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
    {
        APlayerController* PC = It->Get();
        if (!IsValid(PC)) continue;

        APawn* Pawn = PC->GetPawn();
        if (IsValid(Pawn))
        {
            ++AlivePlayerCount;
        }
    }

    // Pawn 보유 플레이어 1명 이하 → 솔로 → 다운 없이 즉사
    return (AlivePlayerCount <= 1);
}

void AHellunaDefenseGameMode::NotifyPlayerDowned(APlayerController* DownedPC)
{
    if (!HasAuthority() || !bGameInitialized || bGameEnded) return;

    UE_LOG(LogHelluna, Log, TEXT("[Phase21] NotifyPlayerDowned: %s"), *GetNameSafe(DownedPC));

    UWorld* World = GetWorld();
    if (!World) return;

    // 생존자(IsAliveAndNotDowned) 가 1명이라도 있는지 확인
    bool bAnyAlive = false;
    for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
    {
        APlayerController* PC = It->Get();
        if (!IsValid(PC)) continue;

        APawn* Pawn = PC->GetPawn();
        if (!IsValid(Pawn)) continue;

        UHellunaHealthComponent* HealthComp = Pawn->FindComponentByClass<UHellunaHealthComponent>();
        if (HealthComp && HealthComp->IsAliveAndNotDowned())
        {
            bAnyAlive = true;
            break;
        }
    }

    if (!bAnyAlive)
    {
        UE_LOG(LogHelluna, Warning, TEXT("[Phase21] 생존자 없음 → ForceKillAllDownedPlayers"));
        ForceKillAllDownedPlayers();
    }
}

void AHellunaDefenseGameMode::ForceKillAllDownedPlayers()
{
    if (!HasAuthority()) return;

    UWorld* World = GetWorld();
    if (!World) return;

    for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
    {
        APlayerController* PC = It->Get();
        if (!IsValid(PC)) continue;

        APawn* Pawn = PC->GetPawn();
        if (!IsValid(Pawn)) continue;

        UHellunaHealthComponent* HealthComp = Pawn->FindComponentByClass<UHellunaHealthComponent>();
        if (HealthComp && HealthComp->IsDowned())
        {
            UE_LOG(LogHelluna, Log, TEXT("[Phase21] ForceKillFromDowned: %s"), *GetNameSafe(Pawn));
            HealthComp->ForceKillFromDowned();
        }
    }
}

// ============================================================
// 보스/세미보스 스폰
// ============================================================
void AHellunaDefenseGameMode::TrySummonBoss(const FBossSpawnEntry& Entry)
{
    if (!HasAuthority() || !bGameInitialized) return;

    if (!Entry.BossClass)
    {
        Debug::Print(FString::Printf(
            TEXT("[TrySummonBoss] Day %d — BossClass null. BossSchedule 항목을 확인하세요."), CurrentDay),
            FColor::Red);
        return;
    }

    if (BossSpawnPoints.IsEmpty())
    {
        Debug::Print(TEXT("[TrySummonBoss] BossSpawnPoints 없음 — TargetPoint에 'BossSpawn' 태그를 추가하세요."), FColor::Red);
#if HELLUNA_DEBUG_DEFENSE
        UE_LOG(LogTemp, Error, TEXT("[TrySummonBoss] BossSpawnPoints 없음! BossSpawnPointTag=%s"), *BossSpawnPointTag.ToString());
#endif
        return;
    }

    ATargetPoint* TP = BossSpawnPoints[FMath::RandRange(0, BossSpawnPoints.Num() - 1)];
    if (!IsValid(TP))
    {
        Debug::Print(TEXT("[TrySummonBoss] 선택된 BossSpawnPoint가 유효하지 않습니다."), FColor::Red);
        return;
    }

    FActorSpawnParameters Param;
    Param.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

    APawn* SpawnedBoss = GetWorld()->SpawnActor<APawn>(
        Entry.BossClass,
        TP->GetActorLocation() + FVector(0.f, 0.f, SpawnZOffset),
        TP->GetActorRotation(),
        Param);

    if (!IsValid(SpawnedBoss))
    {
        Debug::Print(FString::Printf(
            TEXT("[TrySummonBoss] SpawnActor 실패 — Class: %s"), *Entry.BossClass->GetName()),
            FColor::Red);
        return;
    }

    // ── 보스 스폰 직후 상태 진단 ──────────────────────────────────────
    Debug::Print(FString::Printf(TEXT("[TrySummonBoss] 소환 직후 진단 — %s"), *SpawnedBoss->GetName()), FColor::Cyan);
    Debug::Print(FString::Printf(TEXT("  Controller    : %s"),
        SpawnedBoss->GetController() ? *SpawnedBoss->GetController()->GetName() : TEXT("❌ 없음")), FColor::Cyan);
    Debug::Print(FString::Printf(TEXT("  AutoPossessAI : %d"), (int32)SpawnedBoss->AutoPossessAI), FColor::Cyan);

    if (AAIController* AIC = Cast<AAIController>(SpawnedBoss->GetController()))
    {
        UStateTreeComponent* STComp = AIC->FindComponentByClass<UStateTreeComponent>();
        Debug::Print(FString::Printf(TEXT("  StateTree     : %s"),
            STComp ? TEXT("✅ 있음") : TEXT("❌ 없음 — AIController BP에 StateTreeComponent 추가 필요")), FColor::Cyan);
    }

    if (AHellunaEnemyCharacter* BossChar = Cast<AHellunaEnemyCharacter>(SpawnedBoss))
    {
        Debug::Print(FString::Printf(TEXT("  EnemyGrade  : %s"),
            BossChar->EnemyGrade == EEnemyGrade::Boss     ? TEXT("Boss ✅") :
            BossChar->EnemyGrade == EEnemyGrade::SemiBoss ? TEXT("SemiBoss") :
                                                             TEXT("Normal ❌ — BP에서 EnemyGrade=Boss 로 설정 필요")), FColor::Cyan);
        BossChar->DebugPrintBossStatus();
    }

    AliveBoss = SpawnedBoss;
    bBossReady = false;

    // 소환된 액터의 EnemyGrade로 등급 표시
    FString BossTypeLabel = TEXT("보스 계열");
    if (const AHellunaEnemyCharacter* EnemyChar = Cast<AHellunaEnemyCharacter>(SpawnedBoss))
    {
        switch (EnemyChar->EnemyGrade)
        {
        case EEnemyGrade::SemiBoss: BossTypeLabel = TEXT("세미보스"); break;
        case EEnemyGrade::Boss:     BossTypeLabel = TEXT("보스");     break;
        default:                    BossTypeLabel = TEXT("Normal(경고: EnemyGrade 확인 필요)"); break;
        }
    }

    Debug::Print(FString::Printf(
        TEXT("[TrySummonBoss] %s 소환 완료: %s (Day %d)"),
        *BossTypeLabel, *SpawnedBoss->GetName(), CurrentDay),
        FColor::Red);
}

void AHellunaDefenseGameMode::SetBossReady(bool bReady)
{
    if (!HasAuthority() || bBossReady == bReady) return;
    bBossReady = bReady;
    // 소환은 EnterNight에서 TrySummonBoss(Entry) 직접 호출로 처리.
    // SetBossReady(true)는 우주선 수리 완료 폴백 경로에서만 사용.
}

// ============================================================
// 우주선 상태 체크
// ============================================================
bool AHellunaDefenseGameMode::IsSpaceShipFullyRepaired(int32& OutCurrent, int32& OutNeed) const
{
    OutCurrent = OutNeed = 0;
    const AHellunaDefenseGameState* GS = GetGameState<AHellunaDefenseGameState>();
    if (!GS) return false;
    AResourceUsingObject_SpaceShip* Ship = GS->GetSpaceShip();
    if (!Ship) return false;
    OutCurrent = Ship->GetCurrentResource();
    OutNeed    = Ship->GetNeedResource();
    return (OutNeed > 0) && (OutCurrent >= OutNeed);
}

// ============================================================
// 게임 재시작
// ============================================================
void AHellunaDefenseGameMode::RestartGame()
{
    if (!HasAuthority()) return;
    bGameInitialized = false;
    GetWorld()->ServerTravel(TEXT("/Game/Minwoo/MinwooTestMap?listen"));
}


void AHellunaDefenseGameMode::TickDayCountdown()
{
    AHellunaDefenseGameState* GS = GetGameState<AHellunaDefenseGameState>();
    if (!GS) return;

    float Remaining = FMath::Max(0.f, GS->DayTimeRemaining - 1.f);
    GS->SetDayTimeRemaining(Remaining);
}
// ============================================================
// Phase 10: PostLogin — 접속 채팅 + Phase 12b 레지스트리
// ============================================================
void AHellunaDefenseGameMode::PostLogin(APlayerController* NewPlayer)
{
    Super::PostLogin(NewPlayer);

    // Phase 12b: 레지스트리 갱신
    CurrentPlayerCount++;
    WriteRegistryFile(TEXT("playing"), CurrentPlayerCount);
    UE_LOG(LogHelluna, Log, TEXT("[DefenseGameMode] PostLogin 레지스트리 갱신 | Players=%d"), CurrentPlayerCount);

    // [Phase 16] 유휴 종료 타이머 해제 (첫 접속 시)
    if (UWorld* W = GetWorld())
    {
        W->GetTimerManager().ClearTimer(IdleShutdownTimer);
    }

    // [Phase 19] 커맨드 폴링 중지 (플레이어 접속)
    StopCommandPollTimer();

    // Phase 10: 접속 메시지
    if (bGameInitialized && IsValid(NewPlayer))
    {
        FString PlayerName;
        if (AHellunaPlayerState* HellunaPS = NewPlayer->GetPlayerState<AHellunaPlayerState>())
        {
            PlayerName = HellunaPS->GetPlayerUniqueId();
        }
        if (PlayerName.IsEmpty())
        {
            PlayerName = GetNameSafe(NewPlayer);
        }

        if (AHellunaDefenseGameState* GS = GetGameState<AHellunaDefenseGameState>())
        {
            GS->BroadcastChatMessage(TEXT(""), FString::Printf(TEXT("%s 님이 접속했습니다"), *PlayerName), EChatMessageType::System);
        }
    }
}

// ============================================================
// Phase 7: 게임 종료
// ============================================================

// 콘솔 커맨드 핸들러 (디버그용)
void AHellunaDefenseGameMode::CmdEndGame(const TArray<FString>& Args, UWorld* World)
{
    if (!World) return;

    AHellunaDefenseGameMode* GM = Cast<AHellunaDefenseGameMode>(World->GetAuthGameMode());
    if (!GM)
    {
        UE_LOG(LogHelluna, Warning, TEXT("[Phase7] EndGame 커맨드: DefenseGameMode가 아닌 맵에서 호출됨"));
        return;
    }

    EHellunaGameEndReason Reason = EHellunaGameEndReason::Escaped;
    if (Args.Num() > 0)
    {
        if (Args[0].Equals(TEXT("AllDead"), ESearchCase::IgnoreCase))
        {
            Reason = EHellunaGameEndReason::AllDead;
        }
        else if (Args[0].Equals(TEXT("ServerShutdown"), ESearchCase::IgnoreCase))
        {
            Reason = EHellunaGameEndReason::ServerShutdown;
        }
    }

    GM->EndGame(Reason);
}

static FAutoConsoleCommandWithWorldAndArgs GCmdEndGame(
    TEXT("EndGame"),
    TEXT("Phase 7: 게임 종료. 사용법: EndGame [Escaped|AllDead|ServerShutdown]"),
    FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&AHellunaDefenseGameMode::CmdEndGame)
);

// EndGame 메인 함수
void AHellunaDefenseGameMode::EndGame(EHellunaGameEndReason Reason)
{
    if (!HasAuthority()) return;

    if (bGameEnded)
    {
        UE_LOG(LogHelluna, Warning, TEXT("[Phase7] EndGame: 이미 게임이 종료됨, 스킵"));
        return;
    }

    bGameEnded = true;

    UE_LOG(LogHelluna, Warning, TEXT("[Phase7] EndGame — Reason: %s | LobbyServerURL: '%s'"),
        Reason == EHellunaGameEndReason::Escaped ? TEXT("탈출 성공") :
        Reason == EHellunaGameEndReason::AllDead ? TEXT("전원 사망") :
        Reason == EHellunaGameEndReason::ServerShutdown ? TEXT("서버 셧다운") : TEXT("None"),
        *LobbyServerURL);

    // 낮/밤 타이머 정지
    GetWorldTimerManager().ClearTimer(TimerHandle_ToNight);
    GetWorldTimerManager().ClearTimer(TimerHandle_ToDay);
    GetWorldTimerManager().ClearTimer(TimerHandle_NightWatchdog);

    FString ReasonString;
    switch (Reason)
    {
    case EHellunaGameEndReason::Escaped:       ReasonString = TEXT("탈출 성공"); break;
    case EHellunaGameEndReason::AllDead:        ReasonString = TEXT("전원 사망"); break;
    case EHellunaGameEndReason::ServerShutdown: ReasonString = TEXT("서버 셧다운"); break;
    default:                                    ReasonString = TEXT("알 수 없음"); break;
    }

    // 각 플레이어 처리
    for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
    {
        APlayerController* PC = It->Get();
        if (!IsValid(PC)) continue;

        bool bSurvived = false;
        if (Reason != EHellunaGameEndReason::AllDead)
        {
            APawn* Pawn = PC->GetPawn();
            if (IsValid(Pawn))
            {
                UHellunaHealthComponent* HealthComp = Pawn->FindComponentByClass<UHellunaHealthComponent>();
                if (HealthComp && !HealthComp->IsDead())
                {
                    bSurvived = true;
                }
            }
        }

        UE_LOG(LogHelluna, Log, TEXT("[Phase7] EndGame: Player=%s Survived=%s"),
            *GetNameSafe(PC), bSurvived ? TEXT("Yes") : TEXT("No"));

        ProcessPlayerGameResult(PC, bSurvived);

        AHellunaHeroController* HeroPC = Cast<AHellunaHeroController>(PC);
        if (HeroPC)
        {
            TArray<FInv_SavedItemData> ResultItems;
            if (bSurvived)
            {
                AInv_PlayerController* InvPC = Cast<AInv_PlayerController>(PC);
                if (InvPC)
                {
                    UInv_InventoryComponent* InvComp = PC->FindComponentByClass<UInv_InventoryComponent>();
                    if (InvComp)
                    {
                        ResultItems = InvComp->CollectInventoryDataForSave();
                    }
                }
            }

            // [Fix15] 네트워크 최적화: SerializedManifest 제거
            TArray<FInv_SavedItemData> LightweightItems;
            LightweightItems.Reserve(ResultItems.Num());
            for (const FInv_SavedItemData& Item : ResultItems)
            {
                FInv_SavedItemData LightItem;
                LightItem.ItemType = Item.ItemType;
                LightItem.StackCount = Item.StackCount;
                LightItem.GridPosition = Item.GridPosition;
                LightItem.GridCategory = Item.GridCategory;
                LightItem.bEquipped = Item.bEquipped;
                LightItem.WeaponSlotIndex = Item.WeaponSlotIndex;
                LightweightItems.Add(MoveTemp(LightItem));
            }

            HeroPC->Client_ShowGameResult(LightweightItems, bSurvived, ReasonString, LobbyServerURL);
        }
    }

    // [Phase 14b] DisconnectedPlayers 처리 — 끊겨있으므로 사망 취급
    {
        UGameInstance* GI = GetGameInstance();
        UHellunaSQLiteSubsystem* DB = GI ? GI->GetSubsystem<UHellunaSQLiteSubsystem>() : nullptr;

        for (auto& Pair : DisconnectedPlayers)
        {
            FDisconnectedPlayerData& Data = Pair.Value;
            GetWorldTimerManager().ClearTimer(Data.GraceTimerHandle);

            if (DB)
            {
                TArray<FInv_SavedItemData> EmptyItems;
                DB->ExportGameResultToFile(Data.PlayerId, EmptyItems, false);
                UE_LOG(LogHelluna, Log, TEXT("[Phase14] EndGame: 끊긴 플레이어 사망 처리 | PlayerId=%s"), *Data.PlayerId);
            }

            if (Data.PreservedPawn.IsValid())
            {
                Data.PreservedPawn->Destroy();
            }
        }
        DisconnectedPlayers.Empty();
    }

    UE_LOG(LogHelluna, Log, TEXT("[Phase7] EndGame 완료 — 모든 플레이어 결과 처리됨"));

    // [Phase 16] EndGame 후 서버 자동 종료 (레지스트리 삭제 + RequestExit)
    if (UWorld* W = GetWorld())
    {
        W->GetTimerManager().SetTimer(ShutdownTimer, [this]()
        {
            UE_LOG(LogHelluna, Log, TEXT("[Phase16] 서버 자동 종료 실행"));
            DeleteRegistryFile();
            FGenericPlatformMisc::RequestExit(false);
        }, ShutdownDelaySeconds, false);

        UE_LOG(LogHelluna, Log, TEXT("[Phase16] 서버 자동 종료 타이머 시작 (%.0f초 후)"), ShutdownDelaySeconds);
    }
}

// ============================================================
// ProcessPlayerGameResult
// ============================================================
void AHellunaDefenseGameMode::ProcessPlayerGameResult(APlayerController* PC, bool bSurvived)
{
    if (!IsValid(PC)) return;

    FString PlayerId = GetPlayerSaveId(PC);
    if (PlayerId.IsEmpty())
    {
        UE_LOG(LogHelluna, Warning, TEXT("[Phase7] ProcessPlayerGameResult: PlayerId가 비어있음 | PC=%s"),
            *GetNameSafe(PC));
        return;
    }

    UGameInstance* GI = GetGameInstance();
    UHellunaSQLiteSubsystem* DB = GI ? GI->GetSubsystem<UHellunaSQLiteSubsystem>() : nullptr;
    if (!DB)
    {
        UE_LOG(LogHelluna, Error, TEXT("[Phase7] ProcessPlayerGameResult: SQLite 서브시스템 없음 | PlayerId=%s"), *PlayerId);
        return;
    }

    TArray<FInv_SavedItemData> ResultItems;
    if (bSurvived)
    {
        UInv_InventoryComponent* InvComp = PC->FindComponentByClass<UInv_InventoryComponent>();
        if (InvComp)
        {
            ResultItems = InvComp->CollectInventoryDataForSave();
            UE_LOG(LogHelluna, Log, TEXT("[Phase7] 생존자 아이템 수집: %d개 | PlayerId=%s"),
                ResultItems.Num(), *PlayerId);
        }
    }
    else
    {
        UE_LOG(LogHelluna, Log, TEXT("[Phase7] 사망자: 아이템 전부 손실 | PlayerId=%s"), *PlayerId);
    }

    if (DB->ExportGameResultToFile(PlayerId, ResultItems, bSurvived))
    {
        UE_LOG(LogHelluna, Log, TEXT("[Phase7] ExportGameResultToFile 성공 | PlayerId=%s | Items=%d | Survived=%s"),
            *PlayerId, ResultItems.Num(), bSurvived ? TEXT("Y") : TEXT("N"));
    }
    else
    {
        UE_LOG(LogHelluna, Error, TEXT("[Phase7] ExportGameResultToFile 실패! | PlayerId=%s"), *PlayerId);
    }
}

// ============================================================
// Logout — Phase 10 채팅 + Phase 7 사망 처리 + Phase 12b 레지스트리
// ============================================================
void AHellunaDefenseGameMode::Logout(AController* Exiting)
{
    // [Fix50] LoginController → HeroController 스왑 시 Logout 스킵
    // SwapPlayerControllers가 LoginController를 파괴하면 Logout이 호출되지만,
    // 이것은 실제 플레이어 이탈이 아닌 컨트롤러 교체.
    if (AHellunaLoginController* ExitingLC = Cast<AHellunaLoginController>(Exiting))
    {
        AHellunaPlayerState* PS = ExitingLC->GetPlayerState<AHellunaPlayerState>();
        bool bIsControllerSwap = (!PS || PS->GetPlayerUniqueId().IsEmpty());
        if (bIsControllerSwap)
        {
            UE_LOG(LogHelluna, Log, TEXT("[Fix50] LoginController 스왑 감지 — Logout 처리 스킵 | Controller=%s"),
                *GetNameSafe(Exiting));
            Super::Logout(Exiting);
            return;
        }
    }

    // Phase 10: 퇴장 메시지
    if (bGameInitialized)
    {
        APlayerController* ExitPC = Cast<APlayerController>(Exiting);
        if (IsValid(ExitPC))
        {
            FString PlayerName;
            if (AHellunaPlayerState* HellunaPS = ExitPC->GetPlayerState<AHellunaPlayerState>())
            {
                PlayerName = HellunaPS->GetPlayerUniqueId();
            }
            if (PlayerName.IsEmpty())
            {
                PlayerName = GetNameSafe(ExitPC);
            }

            if (AHellunaDefenseGameState* GS = GetGameState<AHellunaDefenseGameState>())
            {
                GS->BroadcastChatMessage(TEXT(""), FString::Printf(TEXT("%s 님이 퇴장했습니다"), *PlayerName), EChatMessageType::System);
            }
        }
    }

    // [Phase 14b] 게임 진행 중이면 Grace Period 시작 (즉시 사망 대신 상태 보존)
    if (bGameInitialized && !bGameEnded)
    {
        APlayerController* PC = Cast<APlayerController>(Exiting);
        if (IsValid(PC))
        {
            // [Phase 21] 다운 중 연결 끊김 → 강제 사망 (Grace Period 대신)
            APawn* PawnCheck = PC->GetPawn();
            if (IsValid(PawnCheck))
            {
                UHellunaHealthComponent* HealthCheck = PawnCheck->FindComponentByClass<UHellunaHealthComponent>();
                if (HealthCheck && HealthCheck->IsDowned())
                {
                    UE_LOG(LogHelluna, Warning, TEXT("[Phase21] 다운 중 Logout → ForceKillFromDowned | Player=%s"),
                        *GetNameSafe(PC));
                    HealthCheck->ForceKillFromDowned();
                    // ForceKillFromDowned → HandleDeath → OnHeroDeath → NotifyPlayerDied 경유
                    // Grace Period 진입 불필요
                }
            }

            const FString PlayerId = GetPlayerSaveId(PC);
            APawn* Pawn = PC->GetPawn();

            if (!PlayerId.IsEmpty() && IsValid(Pawn))
            {
                UE_LOG(LogHelluna, Warning, TEXT("[Phase14] Logout 중 게임 진행 중 — Grace Period 시작 (%0.f초) | Player=%s"),
                    DisconnectGracePeriodSeconds, *PlayerId);

                FDisconnectedPlayerData Data;
                Data.PlayerId = PlayerId;

                // 영웅타입 추출
                if (AHellunaPlayerState* HellunaPS2 = PC->GetPlayerState<AHellunaPlayerState>())
                {
                    Data.HeroType = HellunaPS2->GetSelectedHeroType();
                }

                // 인벤토리 저장
                if (UInv_InventoryComponent* InvComp = PC->FindComponentByClass<UInv_InventoryComponent>())
                {
                    Data.SavedInventory = InvComp->CollectInventoryDataForSave();
                }

                // 위치/회전 저장
                Data.LastLocation = Pawn->GetActorLocation();
                Data.LastRotation = Pawn->GetActorRotation();

                // 체력 저장
                if (UHellunaHealthComponent* HealthComp = Pawn->FindComponentByClass<UHellunaHealthComponent>())
                {
                    Data.Health = HealthComp->GetHealth();
                    Data.MaxHealth = HealthComp->GetMaxHealth();
                }

                // Pawn Unpossess + 숨김 (월드에 유지)
                PC->UnPossess();
                Pawn->SetActorHiddenInGame(true);
                Pawn->SetActorEnableCollision(false);
                Data.PreservedPawn = Pawn;

                // Grace 타이머 시작
                FTimerDelegate TimerDelegate;
                TimerDelegate.BindUObject(this, &AHellunaDefenseGameMode::OnGracePeriodExpired, PlayerId);
                GetWorldTimerManager().SetTimer(Data.GraceTimerHandle, TimerDelegate, DisconnectGracePeriodSeconds, false);

                DisconnectedPlayers.Add(PlayerId, MoveTemp(Data));

                // 채팅으로 알림
                if (AHellunaDefenseGameState* GS2 = GetGameState<AHellunaDefenseGameState>())
                {
                    GS2->BroadcastChatMessage(TEXT(""), FString::Printf(TEXT("%s 님이 연결이 끊겼습니다 (%.0f초 대기)"),
                        *Data.PlayerId, DisconnectGracePeriodSeconds), EChatMessageType::System);
                }
            }
            else
            {
                // PlayerId 없거나 Pawn 없으면 기존 사망 처리
                UE_LOG(LogHelluna, Warning, TEXT("[Phase14] Logout: PlayerId/Pawn 없음 → 즉시 사망 처리 | Player=%s"),
                    *GetNameSafe(PC));
                ProcessPlayerGameResult(PC, false);
            }
        }
    }

    // Phase 12b: 레지스트리 갱신
    CurrentPlayerCount = FMath::Max(0, CurrentPlayerCount - 1);
    WriteRegistryFile(CurrentPlayerCount > 0 ? TEXT("playing") : TEXT("empty"), CurrentPlayerCount);
    UE_LOG(LogHelluna, Log, TEXT("[DefenseGameMode] Logout 레지스트리 갱신 | Players=%d"), CurrentPlayerCount);

    // [Phase 16] 전원 이탈 시 유휴 종료 타이머 재시작
    if (CurrentPlayerCount == 0 && !bGameEnded && IdleShutdownSeconds > 0.f)
    {
        if (UWorld* W = GetWorld())
        {
            W->GetTimerManager().SetTimer(IdleShutdownTimer, this,
                &AHellunaDefenseGameMode::CheckIdleShutdown, IdleShutdownSeconds, false);
            UE_LOG(LogHelluna, Log, TEXT("[Phase16] 전원 이탈 — 유휴 종료 타이머 재시작 (%.0f초)"), IdleShutdownSeconds);
        }
    }

    // [Phase 19] 전원 이탈 → 커맨드 폴링 재시작
    if (CurrentPlayerCount == 0 && !bGameEnded)
    {
        StartCommandPollTimer();
    }

    Super::Logout(Exiting);
}

// ============================================================
// EndPlay — Phase 7 셧다운 + Phase 12b 레지스트리 정리
// ============================================================
void AHellunaDefenseGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (HasAuthority() && bGameInitialized && !bGameEnded)
    {
        UE_LOG(LogHelluna, Warning, TEXT("[Phase7] EndPlay: 서버 셧다운 — EndGame(ServerShutdown) 호출"));
        EndGame(EHellunaGameEndReason::ServerShutdown);
    }

    // [Phase 14b] Grace 타이머 정리 (EndGame에서 이미 처리됐지만 안전장치)
    for (auto& Pair : DisconnectedPlayers)
    {
        GetWorldTimerManager().ClearTimer(Pair.Value.GraceTimerHandle);
    }
    DisconnectedPlayers.Empty();

    // Phase 12b: 하트비트 타이머 정리 + 레지스트리 파일 삭제
    if (UWorld* W = GetWorld())
    {
        W->GetTimerManager().ClearTimer(RegistryHeartbeatTimer);
        // [Phase 16] 종료/유휴 타이머 정리
        W->GetTimerManager().ClearTimer(ShutdownTimer);
        W->GetTimerManager().ClearTimer(IdleShutdownTimer);
    }
    DeleteRegistryFile();
    UE_LOG(LogHelluna, Log, TEXT("[DefenseGameMode] EndPlay: 레지스트리 파일 삭제"));

    Super::EndPlay(EndPlayReason);
}

// ============================================================
// [Phase 14b] 재참가 시스템
// ============================================================

void AHellunaDefenseGameMode::OnGracePeriodExpired(FString PlayerId)
{
    UE_LOG(LogHelluna, Warning, TEXT("[Phase14] Grace Period 만료 → 사망 처리 | PlayerId=%s"), *PlayerId);

    FDisconnectedPlayerData* Data = DisconnectedPlayers.Find(PlayerId);
    if (!Data) return;

    // GameResult 파일 내보내기 (빈 배열 = 사망)
    UGameInstance* GI = GetGameInstance();
    UHellunaSQLiteSubsystem* DB = GI ? GI->GetSubsystem<UHellunaSQLiteSubsystem>() : nullptr;
    if (DB)
    {
        TArray<FInv_SavedItemData> EmptyItems;
        DB->ExportGameResultToFile(PlayerId, EmptyItems, false);
        UE_LOG(LogHelluna, Log, TEXT("[Phase14] Grace 만료: ExportGameResultToFile (사망) | PlayerId=%s"), *PlayerId);
    }

    // 보존된 Pawn 파괴
    if (Data->PreservedPawn.IsValid())
    {
        Data->PreservedPawn->Destroy();
    }

    // 레지스트리에서 연결 끊김 플레이어 제거
    DisconnectedPlayers.Remove(PlayerId);
    WriteRegistryFile(TEXT("playing"), CurrentPlayerCount);
}

bool AHellunaDefenseGameMode::HasDisconnectedPlayer(const FString& PlayerId) const
{
    return DisconnectedPlayers.Contains(PlayerId);
}

void AHellunaDefenseGameMode::RestoreReconnectedPlayer(APlayerController* PC, const FString& PlayerId)
{
    FDisconnectedPlayerData* Data = DisconnectedPlayers.Find(PlayerId);
    if (!Data)
    {
        UE_LOG(LogHelluna, Error, TEXT("[Phase14] RestoreReconnectedPlayer: 데이터 없음 | PlayerId=%s"), *PlayerId);
        return;
    }

    UE_LOG(LogHelluna, Log, TEXT("[Phase14] ▶ RestoreReconnectedPlayer 시작 | PlayerId=%s | HeroType=%d | Items=%d"),
        *PlayerId, static_cast<int32>(Data->HeroType), Data->SavedInventory.Num());

    // 1. Grace 타이머 취소
    GetWorldTimerManager().ClearTimer(Data->GraceTimerHandle);

    // 2. 보존된 Pawn 파괴 (새로 스폰할 것이므로)
    if (Data->PreservedPawn.IsValid())
    {
        Data->PreservedPawn->Destroy();
    }

    // 3. 저장된 위치에 새 캐릭터 스폰
    // HeroType 설정
    if (AHellunaPlayerState* PS = PC->GetPlayerState<AHellunaPlayerState>())
    {
        PS->SetSelectedHeroType(Data->HeroType);
    }

    // SpawnHeroCharacter 사용 (HellunaBaseGameMode 가상 함수)
    // 위치 오버라이드를 위해 약간의 지연 후 스폰
    const FVector SpawnLoc = Data->LastLocation;
    const FRotator SpawnRot = Data->LastRotation;
    const float SavedHealth = Data->Health;
    const float SavedMaxHealth = Data->MaxHealth;
    const TArray<FInv_SavedItemData> SavedItems = Data->SavedInventory;

    // DisconnectedPlayers에서 제거 (데이터 복사 완료)
    DisconnectedPlayers.Remove(PlayerId);

    // 스폰 딜레이 (Controller 초기화 대기)
    FTimerHandle& SpawnTimer = LambdaTimerHandles.AddDefaulted_GetRef();
    GetWorldTimerManager().SetTimer(SpawnTimer, [this, PC, PlayerId, SpawnLoc, SpawnRot, SavedHealth, SavedMaxHealth, SavedItems]()
    {
        if (!IsValid(PC)) return;

        // 캐릭터 스폰 (기본 스폰 흐름 사용)
        SpawnHeroCharacter(PC);

        // 0.3초 추가 딜레이: Pawn 스폰 + Possess 완료 대기
        FTimerHandle& RestoreTimer = LambdaTimerHandles.AddDefaulted_GetRef();
        GetWorldTimerManager().SetTimer(RestoreTimer, [this, PC, PlayerId, SpawnLoc, SpawnRot, SavedHealth, SavedMaxHealth, SavedItems]()
        {
            if (!IsValid(PC)) return;

            APawn* Pawn = PC->GetPawn();
            if (!IsValid(Pawn)) return;

            // 위치 복원
            Pawn->SetActorLocationAndRotation(SpawnLoc, SpawnRot);

            // 체력 복원
            if (UHellunaHealthComponent* HC = Pawn->FindComponentByClass<UHellunaHealthComponent>())
            {
                HC->SetHealth(SavedHealth);
            }

            // 인벤토리 복원 (PreCachedInventoryMap 활용)
            FInv_PlayerSaveData CachedData;
            CachedData.Items = SavedItems;
            PreCachedInventoryMap.Add(PlayerId, MoveTemp(CachedData));
            LoadAndSendInventoryToClient(PC);

            UE_LOG(LogHelluna, Log, TEXT("[Phase14] ✓ RestoreReconnectedPlayer 완료 | PlayerId=%s | Loc=%s"),
                *PlayerId, *SpawnLoc.ToString());

            // 채팅 알림
            if (AHellunaDefenseGameState* GS = GetGameState<AHellunaDefenseGameState>())
            {
                GS->BroadcastChatMessage(TEXT(""), FString::Printf(TEXT("%s 님이 재접속했습니다"), *PlayerId), EChatMessageType::System);
            }
        }, 0.3f, false);
    }, 0.5f, false);
}

// ============================================================
// Phase 12b: 서버 레지스트리 헬퍼 함수
// ============================================================

FString AHellunaDefenseGameMode::GetRegistryDirectoryPath() const
{
    return FPaths::ConvertRelativePathToFull(
        FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("ServerRegistry")));
}

int32 AHellunaDefenseGameMode::GetServerPort() const
{
    UWorld* World = GetWorld();
    if (World)
    {
        return World->URL.Port;
    }
    return 7777;
}

FString AHellunaDefenseGameMode::GetRegistryFilePath() const
{
    const int32 Port = GetServerPort();
    return FPaths::Combine(GetRegistryDirectoryPath(), FString::Printf(TEXT("channel_%d.json"), Port));
}

void AHellunaDefenseGameMode::WriteRegistryFile(const FString& Status, int32 PlayerCount)
{
    const int32 Port = GetServerPort();
    const FString ChannelId = FString::Printf(TEXT("channel_%d"), Port);
    const FString MapName = GetWorld() ? GetWorld()->GetMapName() : TEXT("Unknown");
    const FString LastUpdate = FDateTime::UtcNow().ToIso8601();

    // [Phase 14g] disconnectedPlayers 배열 생성
    FString DisconnectedArray = TEXT("[");
    {
        bool bFirst = true;
        for (const auto& Pair : DisconnectedPlayers)
        {
            if (!bFirst) DisconnectedArray += TEXT(", ");
            DisconnectedArray += FString::Printf(TEXT("\"%s\""), *Pair.Key);
            bFirst = false;
        }
    }
    DisconnectedArray += TEXT("]");

    const FString JsonContent = FString::Printf(
        TEXT("{\n")
        TEXT("  \"channelId\": \"%s\",\n")
        TEXT("  \"port\": %d,\n")
        TEXT("  \"status\": \"%s\",\n")
        TEXT("  \"currentPlayers\": %d,\n")
        TEXT("  \"maxPlayers\": 3,\n")
        TEXT("  \"mapName\": \"%s\",\n")
        TEXT("  \"lastUpdate\": \"%s\",\n")
        TEXT("  \"disconnectedPlayers\": %s\n")
        TEXT("}"),
        *ChannelId, Port, *Status, PlayerCount, *MapName, *LastUpdate, *DisconnectedArray
    );

    const FString FilePath = GetRegistryFilePath();
    if (!FFileHelper::SaveStringToFile(JsonContent, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    {
        UE_LOG(LogHelluna, Error, TEXT("[DefenseGameMode] 레지스트리 파일 쓰기 실패: %s"), *FilePath);
    }
}

void AHellunaDefenseGameMode::DeleteRegistryFile()
{
    const FString FilePath = GetRegistryFilePath();
    if (IFileManager::Get().FileExists(*FilePath))
    {
        IFileManager::Get().Delete(*FilePath);
        UE_LOG(LogHelluna, Log, TEXT("[DefenseGameMode] 레지스트리 파일 삭제: %s"), *FilePath);
    }
}

// ============================================================
// [Phase 16] CheckIdleShutdown
// ============================================================

void AHellunaDefenseGameMode::CheckIdleShutdown()
{
    if (CurrentPlayerCount == 0 && !bGameEnded)
    {
        UE_LOG(LogHelluna, Log, TEXT("[Phase16] 유휴 종료 — 접속자 0, 서버 종료"));
        DeleteRegistryFile();
        FGenericPlatformMisc::RequestExit(false);
    }
}

// ============================================================
// [Phase 19] 커맨드 파일 폴링 — 빈 서버 맵 전환
// ============================================================

void AHellunaDefenseGameMode::PollForCommand()
{
    if (CurrentPlayerCount > 0 || bGameEnded)
    {
        return;
    }

    const FString CmdPath = FPaths::Combine(
        GetRegistryDirectoryPath(),
        FString::Printf(TEXT("command_%d.json"), GetServerPort()));

    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *CmdPath))
    {
        return;
    }

    TSharedPtr<FJsonObject> JsonObj;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, JsonObj) || !JsonObj.IsValid())
    {
        return;
    }

    const FString Command = JsonObj->GetStringField(TEXT("command"));
    if (Command != TEXT("servertravel"))
    {
        return;
    }

    const FString MapPath = JsonObj->GetStringField(TEXT("mapPath"));
    if (MapPath.IsEmpty())
    {
        return;
    }

    // 커맨드 파일 삭제 (먼저 삭제 — 중복 실행 방지)
    IFileManager::Get().Delete(*CmdPath);

    // 타이머 정리
    StopCommandPollTimer();
    if (UWorld* W = GetWorld())
    {
        W->GetTimerManager().ClearTimer(IdleShutdownTimer);
        W->GetTimerManager().ClearTimer(RegistryHeartbeatTimer);
    }

    // [Phase 19 수정] ServerTravel은 UE 5.7 World Partition 크래시 유발 → RequestExit로 프로세스 종료
    UE_LOG(LogHelluna, Log, TEXT("[Phase19] 커맨드 파일 감지 → RequestExit (ServerTravel 대신) | MapPath=%s"), *MapPath);
    FGenericPlatformMisc::RequestExit(false);
}

void AHellunaDefenseGameMode::StartCommandPollTimer()
{
    if (UWorld* W = GetWorld())
    {
        W->GetTimerManager().SetTimer(CommandPollTimer, this,
            &AHellunaDefenseGameMode::PollForCommand, 2.0f, true);
    }
}

void AHellunaDefenseGameMode::StopCommandPollTimer()
{
    if (UWorld* W = GetWorld())
    {
        W->GetTimerManager().ClearTimer(CommandPollTimer);
    }
}

// [cheatdebug] F2 — 낮/밤 전환 타이머 pause/unpause
void AHellunaDefenseGameMode::Cheat_SetPhaseTimersPaused(bool bPaused)
{
    FTimerManager& TM = GetWorldTimerManager();

    auto PausePrint = [&](FTimerHandle& H, const TCHAR* Name)
    {
        if (!TM.IsTimerActive(H) && !TM.IsTimerPaused(H)) { return; }
        if (bPaused) TM.PauseTimer(H); else TM.UnPauseTimer(H);
        UE_LOG(LogTemp, Warning, TEXT("[cheatdebug] PhaseTimer %s -> %s (Active=%d Paused=%d Remaining=%.2f)"),
            Name, bPaused ? TEXT("PAUSED") : TEXT("RESUMED"),
            (int32)TM.IsTimerActive(H), (int32)TM.IsTimerPaused(H),
            TM.GetTimerRemaining(H));
    };

    PausePrint(TimerHandle_ToNight, TEXT("ToNight"));
    PausePrint(TimerHandle_ToDay, TEXT("ToDay"));
    PausePrint(TimerHandle_DayCountdown, TEXT("DayCountdown"));
}
