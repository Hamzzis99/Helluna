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
#include "PCGGraph.h"
#include "PCGManagedResource.h"
#include "PCGWorldActor.h"
#include "Grid/PCGLandscapeCache.h"
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

    // 생성 전 월드의 기존 Ore 액터 데이터 저장 (PCG가 파괴해도 복원 가능하도록)
    PendingPreservedOres.Empty();
    {
        TArray<AActor*> ExistingOres;
        UGameplayStatics::GetAllActorsWithTag(GetWorld(), FName(TEXT("Ore")), ExistingOres);
        for (AActor* Ore : ExistingOres)
        {
            if (IsValid(Ore))
            {
                FTransform SaveTransform(Ore->GetActorRotation(), Ore->GetActorLocation(), FVector::OneVector);
                PendingPreservedOres.Add({ Ore->GetClass(), SaveTransform, Ore->Tags });
            }
        }
        UE_LOG(LogTemp, Warning, TEXT("[ActivateNightPCG] [1/3] 기존 Ore 데이터 저장: %d개"), PendingPreservedOres.Num());
    }

    // ★ PCG 컴포넌트를 타이머로 한 개씩 순차 실행하여 프레임 히치 방지
    PCGStaggerIndex = 0;
    PCGStaggerActivatedCount = 0;
    PreGenerationOreSnapshot.Empty();

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
        // 모든 PCG 컴포넌트 처리 완료 → Ore 복원 배칭 시작
        UE_LOG(LogTemp, Warning, TEXT("[PCG 최적화] 순차 생성 완료 | 활성화: %d개 | → Ore 복원 배칭 시작 (%d개)"),
            PCGStaggerActivatedCount, PendingPreservedOres.Num());

        OreRestoreBatchIndex = 0;
        if (PendingPreservedOres.Num() > 0)
        {
            GetWorldTimerManager().SetTimer(OreRestoreTimer, this,
                &AHellunaDefenseGameMode::ProcessOreRestoreBatch, PCGBatchInterval, true);
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("[ActivateNightPCG] 복원할 Ore 없음 — 완료"));
        }
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
    PCGComp->Seed = CurrentDay;

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

void AHellunaDefenseGameMode::ProcessOreRestoreBatch()
{
    const int32 Total = PendingPreservedOres.Num();
    int32 SpawnedThisBatch = 0;

    while (OreRestoreBatchIndex < Total && SpawnedThisBatch < PCGBatchSpawnCount)
    {
        const FPreservedOre& Data = PendingPreservedOres[OreRestoreBatchIndex++];
        FActorSpawnParameters Params;
        Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        if (AActor* Respawned = GetWorld()->SpawnActor(Data.OreClass, &Data.Transform, Params))
        {
            Respawned->Tags = Data.Tags;
            PreGenerationOreSnapshot.Add(Respawned);
        }
        SpawnedThisBatch++;
    }

    UE_LOG(LogTemp, Warning, TEXT("[PCG 최적화] Ore 복원 배치 | %d/%d 완료 (이번 배치: %d개)"),
        OreRestoreBatchIndex, Total, SpawnedThisBatch);

    if (OreRestoreBatchIndex >= Total)
    {
        GetWorldTimerManager().ClearTimer(OreRestoreTimer);
        UE_LOG(LogTemp, Warning, TEXT("[PCG 최적화] ◀ Ore 복원 완료 | Day=%d | 총 %d개 복원"),
            CurrentDay, Total);
        Debug::Print(FString::Printf(TEXT("[PCG 최적화] Day%d | Ore %d개 복원 완료"),
            CurrentDay, Total), FColor::Yellow);
        PendingPreservedOres.Empty();
    }
}

void AHellunaDefenseGameMode::CleanupInitialNightPCGArtifacts()
{
    if (!HasAuthority())
    {
        return;
    }

    UWorld* World = GetWorld();
    if (!IsValid(World))
    {
        return;
    }

    for (const TWeakObjectPtr<UPCGComponent>& WeakPCGComp : CachedNightPCGComponents)
    {
        UPCGComponent* PCGComp = WeakPCGComp.Get();
        if (!IsValid(PCGComp))
        {
            continue;
        }

        PCGComp->GenerationTrigger = EPCGComponentGenerationTrigger::GenerateOnDemand;
        PCGComp->CleanupLocal(true);
        PCGComp->Deactivate();
    }

    TArray<AActor*> ExistingOres;
    UGameplayStatics::GetAllActorsWithTag(World, FName(TEXT("Ore")), ExistingOres);
    for (AActor* OreActor : ExistingOres)
    {
        if (IsValid(OreActor))
        {
            OreActor->Destroy();
        }
    }

    PreGenerationOreSnapshot.Empty();
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

    // ── 방법 1: GetObjectsWithOuter ──
    int32 ManagedActorCount = 0;
    int32 ManagedISMCount = 0;
    {
        TArray<UObject*> PCGManagedObjects;
        GetObjectsWithOuter(InComponent, PCGManagedObjects, /*bIncludeNestedOuters=*/false);
        UE_LOG(LogTemp, Warning, TEXT("[PCG 생성 콜백] [1/3] GetObjectsWithOuter: %d개"), PCGManagedObjects.Num());

        for (int32 ObjIdx = 0; ObjIdx < PCGManagedObjects.Num(); ++ObjIdx)
        {
            UObject* Obj = PCGManagedObjects[ObjIdx];
            UE_LOG(LogTemp, Warning, TEXT("[PCG 생성 콜백] [1/3]   [%d] 클래스=%s | 이름=%s"),
                ObjIdx, Obj ? *Obj->GetClass()->GetName() : TEXT("null"), Obj ? *Obj->GetName() : TEXT("null"));

            if (UPCGManagedActors* ManagedActors = Cast<UPCGManagedActors>(Obj))
            {
                const int32 SetCount = ManagedActors->GeneratedActors.Num();
                ManagedActorCount += SetCount;
                UE_LOG(LogTemp, Warning, TEXT("[PCG 생성 콜백] [1/3]   ManagedActors: %d개"), SetCount);
                int32 LogIdx = 0;
                for (const TSoftObjectPtr<AActor>& Soft : ManagedActors->GeneratedActors)
                {
                    if (LogIdx >= 3) break;
                    AActor* A = Soft.Get();
                    UE_LOG(LogTemp, Warning, TEXT("[PCG 생성 콜백] [1/3]     [%d] %s | 클래스=%s"),
                        LogIdx, A ? *A->GetName() : TEXT("null"), A ? *A->GetClass()->GetName() : TEXT("N/A"));
                    LogIdx++;
                }
            }
            else if (UPCGManagedISMComponent* ManagedISM = Cast<UPCGManagedISMComponent>(Obj))
            {
                if (UInstancedStaticMeshComponent* ISMComp = ManagedISM->GetComponent())
                {
                    ManagedISMCount += ISMComp->GetInstanceCount();
                }
            }
        }
    }

    // ── 방법 2: 월드 쿼리 (스냅샷 비교) ──
    int32 NewOreCount = 0;
    {
        TArray<AActor*> AllOres;
        UGameplayStatics::GetAllActorsWithTag(GetWorld(), FName(TEXT("Ore")), AllOres);
        for (AActor* Ore : AllOres)
        {
            if (IsValid(Ore) && !PreGenerationOreSnapshot.Contains(Ore))
            {
                NewOreCount++;
                if (NewOreCount <= 5)
                {
                    FString TagStr;
                    for (const FName& Tag : Ore->Tags)
                    {
                        TagStr += Tag.ToString() + TEXT(" ");
                    }
                    UE_LOG(LogTemp, Warning, TEXT("[PCG 생성 콜백] [2/3] 신규 Ore #%d: %s | 클래스=%s | 위치=%s | 태그=[%s]"),
                        NewOreCount,
                        *Ore->GetName(),
                        *Ore->GetClass()->GetName(),
                        *Ore->GetActorLocation().ToString(),
                        TagStr.IsEmpty() ? TEXT("없음") : *TagStr.TrimEnd());
                }
            }
        }
        UE_LOG(LogTemp, Warning, TEXT("[PCG 생성 콜백] [2/3] 기존Ore=%d | 전체Ore=%d | 신규Ore=%d"),
            PreGenerationOreSnapshot.Num(), AllOres.Num(), NewOreCount);
    }

    UE_LOG(LogTemp, Warning, TEXT("[PCG 생성 콜백] [2/3] Managed(Actor:%d ISM:%d) | 월드쿼리 신규Ore: %d개"),
        ManagedActorCount, ManagedISMCount, NewOreCount);

    const int32 Total = FMath::Max(ManagedActorCount + ManagedISMCount, NewOreCount);

    if (Total == 0)
    {
        UE_LOG(LogTemp, Error, TEXT("[PCG 생성 콜백] ❌ 생성된 오브젝트가 0개! PCG 그래프 설정을 확인하세요."));
    }

    Debug::Print(
        FString::Printf(TEXT("[PCG 생성 완료] Day%d | Managed: %d개 / 신규Ore: %d개 / 기존Ore: %d개"),
            CurrentDay, ManagedActorCount + ManagedISMCount, NewOreCount, PreGenerationOreSnapshot.Num()),
        Total > 0 ? FColor::Green : FColor::Red
    );

    // [3/3] 밀도 기반 후처리 컬링
    PostProcessNightPCGDensity(InComponent);
}

// ════════════════════════════════════════════════════════════════════════════════
// PCG 밀도 기반 후처리 컬링
// ════════════════════════════════════════════════════════════════════════════════

void AHellunaDefenseGameMode::PostProcessNightPCGDensity(UPCGComponent* InComponent)
{
    UWorld* World = GetWorld();
    if (!World) return;

    const double PostProcessStartTime = FPlatformTime::Seconds();
    UE_LOG(LogTemp, Warning, TEXT("[밀도 컬링] ▶ 시작 (클러스터+프레임분산) | Day=%d"), CurrentDay);

    // 1) 신규 광석 판별
    TArray<AActor*> NewlySpawnedOres;
    TArray<AActor*> ExistingOres;
    UGameplayStatics::GetAllActorsWithTag(World, FName(TEXT("Ore")), ExistingOres);

    for (AActor* Ore : ExistingOres)
    {
        if (IsValid(Ore) && !PreGenerationOreSnapshot.Contains(Ore))
        {
            NewlySpawnedOres.Add(Ore);
        }
    }

    const int32 PreExistingCount = ExistingOres.Num() - NewlySpawnedOres.Num();
    UE_LOG(LogTemp, Warning, TEXT("[밀도 컬링] [1/5] 월드 Ore: %d개 (기존: %d + 신규: %d)"),
        ExistingOres.Num(), PreExistingCount, NewlySpawnedOres.Num());

    if (NewlySpawnedOres.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("[밀도 컬링] ❌ 신규 Ore 0개 — 컬링 스킵"));
        Debug::Print(TEXT("[밀도 컬링] 신규 Ore 0개 — PCG 그래프 확인 필요"), FColor::Red);
        return;
    }

    // 2) PCG ManagedActors 수집
    PostProcessManagedActorsList.Empty();
    PostProcessPCGComp = InComponent;
    if (IsValid(InComponent))
    {
        TArray<UObject*> PCGManagedObjects;
        GetObjectsWithOuter(InComponent, PCGManagedObjects, false);
        for (UObject* Obj : PCGManagedObjects)
        {
            if (UPCGManagedActors* MA = Cast<UPCGManagedActors>(Obj))
            {
                PostProcessManagedActorsList.Add(MA);
            }
        }
    }

    // 3) 기준점(우주선)
    FVector Origin = FVector::ZeroVector;
    if (const AHellunaDefenseGameState* GS = GetGameState<AHellunaDefenseGameState>())
    {
        if (AResourceUsingObject_SpaceShip* Ship = GS->GetSpaceShip())
        {
            Origin = Ship->GetActorLocation();
        }
    }

    // ── 클러스터 계산 (CPU만, 경량 — 한 프레임에서 처리) ──
    const int32 TotalNew = NewlySpawnedOres.Num();

    // 4-a) 시드 선정: 밀도/편향 점수가 높은 광석을 우선
    struct FSeedCandidate { AActor* Actor; float Score; };
    TArray<FSeedCandidate> Candidates;
    Candidates.Reserve(TotalNew);
    for (AActor* Ore : NewlySpawnedOres)
    {
        if (!IsValid(Ore)) continue;
        Candidates.Add({ Ore, CalculateSpawnScore(Ore->GetActorLocation(), ExistingOres) });
    }
    Candidates.Sort([](const FSeedCandidate& A, const FSeedCandidate& B) { return A.Score > B.Score; });

    const int32 SeedCount = FMath::Max(1, FMath::RoundToInt(TotalNew * ClusterSeedRatio));
    const float MinSeedDistSq = ClusterRadius * ClusterRadius;
    TArray<AActor*> SelectedSeeds;
    SelectedSeeds.Reserve(SeedCount);
    for (const FSeedCandidate& C : Candidates)
    {
        if (SelectedSeeds.Num() >= SeedCount) break;
        if (!IsValid(C.Actor)) continue;
        const FVector Loc = C.Actor->GetActorLocation();
        bool bTooClose = false;
        for (AActor* Existing : SelectedSeeds)
        {
            if (FVector::DistSquared(Loc, Existing->GetActorLocation()) < MinSeedDistSq)
            { bTooClose = true; break; }
        }
        if (!bTooClose) SelectedSeeds.Add(C.Actor);
    }

    // 4-b) 각 시드에 가까운 광석 배정
    TSet<AActor*> AssignedOres;
    for (AActor* Seed : SelectedSeeds) AssignedOres.Add(Seed);

    TMap<AActor*, TArray<AActor*>> SeedToMembers;
    for (AActor* Seed : SelectedSeeds) SeedToMembers.Add(Seed, TArray<AActor*>());

    for (AActor* Seed : SelectedSeeds)
    {
        if (!IsValid(Seed)) continue;
        const FVector SeedLoc = Seed->GetActorLocation();
        const int32 WantMembers = FMath::RandRange(MinClusterSize - 1, MaxClusterSize - 1);

        struct FDistEntry { AActor* Actor; double DistSq; };
        TArray<FDistEntry> NearOres;
        for (AActor* Ore : NewlySpawnedOres)
        {
            if (!IsValid(Ore) || AssignedOres.Contains(Ore)) continue;
            NearOres.Add({ Ore, FVector::DistSquared(Ore->GetActorLocation(), SeedLoc) });
        }
        NearOres.Sort([](const FDistEntry& A, const FDistEntry& B) { return A.DistSq < B.DistSq; });

        TArray<AActor*>& Members = SeedToMembers[Seed];
        for (int32 j = 0; j < NearOres.Num() && Members.Num() < WantMembers; ++j)
        {
            Members.Add(NearOres[j].Actor);
            AssignedOres.Add(NearOres[j].Actor);
        }
    }

    UE_LOG(LogTemp, Warning, TEXT("[클러스터] 시드: %d개 | 신규 Ore: %d개"), SelectedSeeds.Num(), TotalNew);

    // ── 스폰/파괴 요청 리스트 준비 ──
    // 랜덤 회전 헬퍼
    auto MakeRandomTiltRotation = [this]() -> FRotator
    {
        return FRotator(
            FMath::FRandRange(-OreTiltMaxPitch, OreTiltMaxPitch),
            FMath::FRandRange(0.f, 360.f),
            FMath::FRandRange(-OreTiltMaxRoll, OreTiltMaxRoll));
    };

    // 랜덤 스케일 헬퍼
    auto MakeRandomScale = [this]() -> FVector
    {
        const float S = FMath::FRandRange(OreScaleMin, OreScaleMax);
        return FVector(S, S, S);
    };

    PendingClusterSpawns.Empty();
    PendingClusterDestroys.Empty();
    SpawnedClusterOresResult.Empty();

    // (A) 뭉침 광석 스폰 요청
    for (AActor* Seed : SelectedSeeds)
    {
        if (!IsValid(Seed)) continue;
        const FVector SeedLoc = Seed->GetActorLocation();
        const float SeedZ = SeedLoc.Z;
        const TArray<AActor*>& Members = SeedToMembers[Seed];
        const int32 TotalInCluster = Members.Num() + 1;
        const float AngleStep = 360.f / TotalInCluster;
        UClass* OreClass = Seed->GetClass();
        TArray<FName> OreTags = Seed->Tags;

        for (int32 j = 0; j < TotalInCluster; ++j)
        {
            const float BaseAngle = AngleStep * j + FMath::FRandRange(-20.f, 20.f);
            const float Dist = (j == 0) ? 0.f : FMath::FRandRange(ClusterPlaceMinDist, ClusterPlaceMaxDist);
            const FVector Dir = FVector::ForwardVector.RotateAngleAxis(BaseAngle, FVector::UpVector);
            FVector SpawnLoc = SeedLoc + Dir * Dist;
            SpawnLoc.Z = SeedZ;

            PendingClusterSpawns.Add({ OreClass, FTransform(MakeRandomTiltRotation(), SpawnLoc, MakeRandomScale()), OreTags });
        }
    }

    // (B) 단독 광석 스폰 요청
    for (AActor* OreActor : NewlySpawnedOres)
    {
        if (!IsValid(OreActor) || AssignedOres.Contains(OreActor)) continue;
        if (FMath::FRand() < IsolatedOreSurvivalChance)
        {
            PendingClusterSpawns.Add({ OreActor->GetClass(),
                FTransform(MakeRandomTiltRotation(), OreActor->GetActorLocation(), MakeRandomScale()),
                OreActor->Tags });
        }
    }

    // (C) 파괴 대상 (신규 PCG 광석 전부)
    for (AActor* OreActor : NewlySpawnedOres)
    {
        if (IsValid(OreActor))
        {
            PendingClusterDestroys.Add(OreActor);
        }
    }

    const double CalcTime = (FPlatformTime::Seconds() - PostProcessStartTime) * 1000.0;
    UE_LOG(LogTemp, Warning, TEXT("[PCG 최적화] 클러스터 계산 완료 (%.1fms) | 파괴예정: %d개 | 스폰예정: %d개 → 배칭 시작"),
        CalcTime, PendingClusterDestroys.Num(), PendingClusterSpawns.Num());
    Debug::Print(FString::Printf(TEXT("[PCG 최적화] 클러스터 계산 %.1fms | 파괴 %d → 스폰 %d (배칭 중)"),
        CalcTime, PendingClusterDestroys.Num(), PendingClusterSpawns.Num()), FColor::Orange);

    // ★ 파괴 배칭 먼저 → 완료 후 스폰 배칭
    ClusterDestroyBatchIndex = 0;
    ClusterSpawnBatchIndex = 0;

    if (PendingClusterDestroys.Num() > 0)
    {
        GetWorldTimerManager().SetTimer(ClusterDestroyTimer, this,
            &AHellunaDefenseGameMode::ProcessClusterDestroyBatch, PCGBatchInterval, true);
    }
    else if (PendingClusterSpawns.Num() > 0)
    {
        GetWorldTimerManager().SetTimer(ClusterSpawnTimer, this,
            &AHellunaDefenseGameMode::ProcessClusterSpawnBatch, PCGBatchInterval, true);
    }
    else
    {
        FinalizePostProcess();
    }
}

void AHellunaDefenseGameMode::ProcessClusterDestroyBatch()
{
    const int32 Total = PendingClusterDestroys.Num();
    int32 DestroyedThisBatch = 0;

    while (ClusterDestroyBatchIndex < Total && DestroyedThisBatch < PCGBatchDestroyCount)
    {
        AActor* OreActor = PendingClusterDestroys[ClusterDestroyBatchIndex++].Get();
        if (IsValid(OreActor))
        {
            for (UPCGManagedActors* MA : PostProcessManagedActorsList)
            {
                TSoftObjectPtr<AActor> SoftOre(OreActor);
                MA->GeneratedActors.Remove(SoftOre);
            }
            OreActor->Destroy();
        }
        DestroyedThisBatch++;
    }

    UE_LOG(LogTemp, Warning, TEXT("[PCG 최적화] 파괴 배치 | %d/%d (이번: %d개)"),
        ClusterDestroyBatchIndex, Total, DestroyedThisBatch);

    if (ClusterDestroyBatchIndex >= Total)
    {
        GetWorldTimerManager().ClearTimer(ClusterDestroyTimer);
        UE_LOG(LogTemp, Warning, TEXT("[PCG 최적화] 파괴 완료 (%d개) → 스폰 배칭 시작 (%d개)"),
            Total, PendingClusterSpawns.Num());

        if (PendingClusterSpawns.Num() > 0)
        {
            GetWorldTimerManager().SetTimer(ClusterSpawnTimer, this,
                &AHellunaDefenseGameMode::ProcessClusterSpawnBatch, PCGBatchInterval, true);
        }
        else
        {
            FinalizePostProcess();
        }
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
        Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
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
    FVector Origin = FVector::ZeroVector;
    if (const AHellunaDefenseGameState* GS = GetGameState<AHellunaDefenseGameState>())
    {
        if (AResourceUsingObject_SpaceShip* Ship = GS->GetSpaceShip())
        {
            Origin = Ship->GetActorLocation();
        }
    }
    const FVector Bias2D = FVector(BiasDirection.X, BiasDirection.Y, 0.f).GetSafeNormal();

    int32 LeftPlaced = 0, RightPlaced = 0;
    for (AActor* Spawned : SpawnedClusterOresResult)
    {
        if (!IsValid(Spawned)) continue;
        const FVector Loc = Spawned->GetActorLocation();
        const FVector Dir2D = FVector(Loc.X - Origin.X, Loc.Y - Origin.Y, 0.f).GetSafeNormal();
        if (FVector::DotProduct(Dir2D, Bias2D) >= 0.f) LeftPlaced++; else RightPlaced++;
    }

    UE_LOG(LogTemp, Warning, TEXT("[PCG 최적화] ◀ PostProcess 완료 | Day=%d | 파괴: %d개 | 스폰: %d개 | 좌: %d / 우: %d"),
        CurrentDay, PendingClusterDestroys.Num(), SpawnedClusterOresResult.Num(), LeftPlaced, RightPlaced);
    Debug::Print(FString::Printf(TEXT("[PCG 최적화] Day%d | PostProcess 완료 — 파괴 %d → 스폰 %d (좌:%d/우:%d)"),
        CurrentDay, PendingClusterDestroys.Num(), SpawnedClusterOresResult.Num(), LeftPlaced, RightPlaced),
        FColor::Green);

    PendingClusterSpawns.Empty();
    PendingClusterDestroys.Empty();
    SpawnedClusterOresResult.Empty();
    PostProcessManagedActorsList.Empty();
    PostProcessPCGComp.Reset();
}

float AHellunaDefenseGameMode::CalculateSpawnScore(const FVector& Location, const TArray<AActor*>& ExistingOres) const
{
    float Score = 1.0f;

    // Factor 1: 광석 밀도
    Score *= CalculateOreDensityFactor(Location, ExistingOres);

    // Factor 2: 방향 편향 (우주선 기준)
    Score *= CalculateDirectionBiasFactor(Location);

    return FMath::Clamp(Score, 0.f, 1.f);
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

float AHellunaDefenseGameMode::CalculateDirectionBiasFactor(const FVector& Location) const
{
    // 우주선 위치를 기준점으로 사용
    FVector Origin = FVector::ZeroVector;

    if (const AHellunaDefenseGameState* GS = GetGameState<AHellunaDefenseGameState>())
    {
        if (AResourceUsingObject_SpaceShip* Ship = GS->GetSpaceShip())
        {
            Origin = Ship->GetActorLocation();
        }
    }

    const FVector Dir2D = FVector(Location.X - Origin.X, Location.Y - Origin.Y, 0.f).GetSafeNormal();
    const FVector Bias2D = FVector(BiasDirection.X, BiasDirection.Y, 0.f).GetSafeNormal();

    if (Dir2D.IsNearlyZero() || Bias2D.IsNearlyZero())
    {
        return 1.0f;
    }

    // Dot: +1(같은 방향) ~ -1(반대 방향) → 0~1 범위로 변환
    const float Dot = FVector::DotProduct(Dir2D, Bias2D);
    const float T = (Dot + 1.f) * 0.5f; // 0(반대) ~ 1(같은 방향)

    return FMath::Lerp(MinBiasScore, 1.0f, T);
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

    // 낮 카운터 증가 (게임 시작 첫 낮은 Day 1)
    CurrentDay++;

    if (CurrentDay == 1)
    {
        CleanupInitialNightPCGArtifacts();
    }

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
