// ════════════════════════════════════════════════════════════════════════════════
// HellunaDefenseGameMode.cpp
// ════════════════════════════════════════════════════════════════════════════════
//
// 게임 로직 전용 GameMode
// 로그인/인벤토리 시스템은 HellunaBaseGameMode.cpp 참고
//
// 🎮 이 파일의 역할:
//    - InitializeGame() : 게임 시작
//    - EnterDay() / EnterNight() : 낮밤 전환
//    - SpawnTestMonsters() : 몬스터 스폰
//    - TrySummonBoss() : 보스 소환
//
// ════════════════════════════════════════════════════════════════════════════════

#include "GameMode/HellunaDefenseGameMode.h"
#include "GameMode/HellunaDefenseGameState.h"
#include "Object/ResourceUsingObject/ResourceUsingObject_SpaceShip.h"
#include "Engine/TargetPoint.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "debughelper.h"

// [Phase 7] 게임 종료 + 결과 반영
#include "Lobby/Database/HellunaSQLiteSubsystem.h"
#include "Player/HellunaPlayerState.h"
#include "Controller/HellunaHeroController.h"
#include "Character/HellunaHeroCharacter.h"
#include "Character/EnemyComponent/HellunaHealthComponent.h"
#include "InventoryManagement/Components/Inv_InventoryComponent.h"
#include "Player/Inv_PlayerController.h"
#include "HAL/IConsoleManager.h"

AHellunaDefenseGameMode::AHellunaDefenseGameMode()
{
    // BaseGameMode에서 기본 설정 처리됨
    // ⚠️ BP에서 덮어쓰는 문제 방지를 위해 로그 추가
    UE_LOG(LogTemp, Warning, TEXT("⭐ [DefenseGameMode] Constructor 호출!"));
    UE_LOG(LogTemp, Warning, TEXT("⭐ PlayerControllerClass: %s"), PlayerControllerClass ? *PlayerControllerClass->GetName() : TEXT("nullptr"));
    UE_LOG(LogTemp, Warning, TEXT("⭐ DefaultPawnClass: %s"), DefaultPawnClass ? *DefaultPawnClass->GetName() : TEXT("nullptr"));
}

void AHellunaDefenseGameMode::BeginPlay()
{
    Super::BeginPlay();  // BaseGameMode의 로그인/인벤토리 초기화 호출

    if (!HasAuthority())
        return;

    // 커맨드라인에서 -LobbyURL= 인자가 있으면 BP 값 덮어쓰기
    // (패키징된 서버에서 BP 재쿠킹 없이 LobbyURL 설정 가능)
    FString CmdLobbyURL;
    if (FParse::Value(FCommandLine::Get(), TEXT("-LobbyURL="), CmdLobbyURL))
    {
        LobbyServerURL = CmdLobbyURL;
        UE_LOG(LogHelluna, Warning, TEXT("[DefenseGameMode] 커맨드라인에서 LobbyServerURL 설정: %s"), *LobbyServerURL);
    }

    UE_LOG(LogHelluna, Warning, TEXT("[DefenseGameMode] LobbyServerURL = '%s'"), *LobbyServerURL);

    // 게임 로직 초기화만
    CacheBossSpawnPoints();
    CacheMonsterSpawnPoints();

    UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] BeginPlay - 게임 로직 초기화 완료"));
    UE_LOG(LogTemp, Warning, TEXT("  - BossSpawnPoints: %d개"), BossSpawnPoints.Num());
    UE_LOG(LogTemp, Warning, TEXT("  - MonsterSpawnPoints: %d개"), MonsterSpawnPoints.Num());
}

// ════════════════════════════════════════════════════════════════════════════════
// ⭐⭐⭐ InitializeGame - 게임 로직 시작점 ⭐⭐⭐
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 호출 시점: 첫 번째 플레이어가 로그인 + 캐릭터 소환 완료 후
//
// 📌 이 함수가 호출되면:
//    - 게임이 본격적으로 시작됨
//    - EnterDay()가 호출되어 낮/밤 사이클 시작
//
// ✅ 팀원 작업: 게임 시작 시 필요한 초기화 로직을 여기에 추가하세요!
//    예시:
//    - 배경음악 재생
//    - 튜토리얼 시작
//    - UI 표시
//    - 환경 초기화
//
// ════════════════════════════════════════════════════════════════════════════════
void AHellunaDefenseGameMode::InitializeGame()
{
    if (bGameInitialized)
    {
        UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] 이미 초기화됨, 스킵"));
        return;
    }

    bGameInitialized = true;

    UE_LOG(LogTemp, Warning, TEXT(""));
    UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
    UE_LOG(LogTemp, Warning, TEXT("║     [DefenseGameMode] InitializeGame 🎮                    ║"));
    UE_LOG(LogTemp, Warning, TEXT("║     첫 플레이어 캐릭터 소환 완료! 게임 시작!               ║"));
    UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
    UE_LOG(LogTemp, Warning, TEXT(""));

    Debug::Print(TEXT("[DefenseGameMode] InitializeGame - 게임 시작!"), FColor::Green);

    // ════════════════════════════════════════════════════════════════════════════
    // ✅ 팀원 작업 영역 - 게임 시작 시 초기화 로직 추가
    // ════════════════════════════════════════════════════════════════════════════
    //
    // 여기에 게임 시작 시 필요한 코드를 추가하세요!
    //
    // 예시:
    // PlayBackgroundMusic();
    // ShowTutorialWidget();
    // InitializeEnvironment();
    //
    // ════════════════════════════════════════════════════════════════════════════

    // 낮/밤 사이클 시작
    EnterDay();

    // 자동저장 타이머 시작
    StartAutoSave();
}

// ════════════════════════════════════════════════════════════════════════════════
// 🗺️ 스폰 포인트 캐싱
// ════════════════════════════════════════════════════════════════════════════════

void AHellunaDefenseGameMode::CacheBossSpawnPoints()
{
    BossSpawnPoints.Empty();
    TArray<AActor*> Found;
    UGameplayStatics::GetAllActorsOfClass(this, ATargetPoint::StaticClass(), Found);

    for (AActor* A : Found)
    {
        if (ATargetPoint* TP = Cast<ATargetPoint>(A))
        {
            if (TP->ActorHasTag(BossSpawnPointTag))
                BossSpawnPoints.Add(TP);
        }
    }
}

void AHellunaDefenseGameMode::CacheMonsterSpawnPoints()
{
    MonsterSpawnPoints.Empty();
    TArray<AActor*> Found;
    UGameplayStatics::GetAllActorsOfClass(this, ATargetPoint::StaticClass(), Found);

    for (AActor* A : Found)
    {
        if (ATargetPoint* TP = Cast<ATargetPoint>(A))
        {
            if (TP->ActorHasTag(MonsterSpawnPointTag))
                MonsterSpawnPoints.Add(TP);
        }
    }
}

// ════════════════════════════════════════════════════════════════════════════════
// 🌅🌙 낮/밤 시스템
// ════════════════════════════════════════════════════════════════════════════════

// 🌅 EnterDay - 낮 시작
void AHellunaDefenseGameMode::EnterDay()
{
    if (!bGameInitialized)
    {
        UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] EnterDay 스킵 - 게임 미초기화"));
        return;
    }

    Debug::Print(TEXT("[DefenseGameMode] EnterDay - 낮 시작!"), FColor::Yellow);

    AliveMonsters.Empty();

    if (AHellunaDefenseGameState* GS = GetGameState<AHellunaDefenseGameState>())
    {
        // Phase 전환 → OnDayStarted (BP: 밤→아침 빠른 전환 연출)
        GS->SetPhase(EDefensePhase::Day);
        GS->SetAliveMonsterCount(0);
        GS->MulticastPrintDay();

        // 새벽 완료 신호 → OnDawnPassed (BP: UDS 비례 구동 시작)
        // RoundDuration을 같이 보내서 BP에서 UDS 속도 계산에 사용
        GS->NetMulticast_OnDawnPassed(TestDayDuration);
    }

    // 라운드 타이머: OnDawnPassed 이후부터 카운트 시작
    // (새벽 전환 연출 시간은 라운드 시간에 포함하지 않음)
    GetWorldTimerManager().ClearTimer(TimerHandle_ToNight);
    GetWorldTimerManager().SetTimer(TimerHandle_ToNight, this, &ThisClass::EnterNight, TestDayDuration, false);
}

// 🌙 EnterNight - 밤 시작
void AHellunaDefenseGameMode::EnterNight()
{
    if (!HasAuthority() || !bGameInitialized) return;

    Debug::Print(TEXT("[DefenseGameMode] EnterNight - 밤 시작!"), FColor::Purple);

    AliveMonsters.Empty();

    if (AHellunaDefenseGameState* GS = GetGameState<AHellunaDefenseGameState>())
    {
        GS->SetPhase(EDefensePhase::Night);
        GS->SetAliveMonsterCount(0);
    }

    int32 Current = 0, Need = 0;
    if (IsSpaceShipFullyRepaired(Current, Need))
    {
        SetBossReady(true);
        return;
    }

    SpawnTestMonsters();
}

// ════════════════════════════════════════════════════════════════════════════════
// 👾 몬스터/보스 스폰
// ════════════════════════════════════════════════════════════════════════════════

// 👾 SpawnTestMonsters - 몬스터 스폰
void AHellunaDefenseGameMode::SpawnTestMonsters()
{
    if (!HasAuthority() || !bGameInitialized) return;

    if (!TestMonsterClass)
    {
        Debug::Print(TEXT("[Defense] TestMonsterClass is null"), FColor::Red);
        return;
    }

    if (MonsterSpawnPoints.IsEmpty())
    {
        Debug::Print(TEXT("[Defense] No MonsterSpawn TargetPoints"), FColor::Red);
        return;
    }

    for (int32 i = 0; i < TestMonsterSpawnCount; ++i)
    {
        ATargetPoint* TP = MonsterSpawnPoints[FMath::RandRange(0, MonsterSpawnPoints.Num() - 1)];
        if (!TP) continue;

        FActorSpawnParameters Param;
        Param.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

        GetWorld()->SpawnActor<APawn>(TestMonsterClass, TP->GetActorLocation(), TP->GetActorRotation(), Param);
    }
}

void AHellunaDefenseGameMode::TrySummonBoss()
{
    if (!HasAuthority() || !bGameInitialized || !BossClass || BossSpawnPoints.IsEmpty())
        return;

    ATargetPoint* TP = BossSpawnPoints[FMath::RandRange(0, BossSpawnPoints.Num() - 1)];
    const FVector SpawnLoc = TP->GetActorLocation() + FVector(0, 0, SpawnZOffset);

    FActorSpawnParameters Param;
    Param.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

    APawn* Boss = GetWorld()->SpawnActor<APawn>(BossClass, SpawnLoc, TP->GetActorRotation(), Param);
    if (Boss) bBossReady = false;
}

void AHellunaDefenseGameMode::SetBossReady(bool bReady)
{
    if (!HasAuthority() || bBossReady == bReady) return;
    bBossReady = bReady;
    if (bBossReady) TrySummonBoss();
}

// ════════════════════════════════════════════════════════════════════════════════
// 📊 몬스터 관리
// ════════════════════════════════════════════════════════════════════════════════

void AHellunaDefenseGameMode::RegisterAliveMonster(AActor* Monster)
{
    if (!HasAuthority() || !Monster || !bGameInitialized) return;

    AHellunaDefenseGameState* GS = GetGameState<AHellunaDefenseGameState>();
    if (!GS || GS->GetPhase() != EDefensePhase::Night) return;

    if (AliveMonsters.Contains(Monster)) return;

    AliveMonsters.Add(Monster);
    GS->SetAliveMonsterCount(AliveMonsters.Num());
}

void AHellunaDefenseGameMode::NotifyMonsterDied(AActor* DeadMonster)
{
    if (!HasAuthority() || !DeadMonster || !bGameInitialized) return;

    AHellunaDefenseGameState* GS = GetGameState<AHellunaDefenseGameState>();
    if (!GS) return;

    AliveMonsters.Remove(TWeakObjectPtr<AActor>(DeadMonster));
    GS->SetAliveMonsterCount(AliveMonsters.Num());

    if (AliveMonsters.Num() <= 0)
    {
        GetWorldTimerManager().ClearTimer(TimerHandle_ToDay);
        GetWorldTimerManager().SetTimer(TimerHandle_ToDay, this, &ThisClass::EnterDay, TestNightFailToDayDelay, false);
    }
}

// ════════════════════════════════════════════════════════════════════════════════
// 🚀 우주선 상태 체크
// ════════════════════════════════════════════════════════════════════════════════

bool AHellunaDefenseGameMode::IsSpaceShipFullyRepaired(int32& OutCurrent, int32& OutNeed) const
{
    OutCurrent = 0;
    OutNeed = 0;

    const AHellunaDefenseGameState* GS = GetGameState<AHellunaDefenseGameState>();
    if (!GS) return false;

    AResourceUsingObject_SpaceShip* Ship = GS->GetSpaceShip();
    if (!Ship) return false;

    OutCurrent = Ship->GetCurrentResource();
    OutNeed = Ship->GetNeedResource();

    return (OutNeed > 0) && (OutCurrent >= OutNeed);
}

// ════════════════════════════════════════════════════════════════════════════════
// 🔄 게임 재시작
// ════════════════════════════════════════════════════════════════════════════════

void AHellunaDefenseGameMode::RestartGame()
{
    if (!HasAuthority()) return;

    bGameInitialized = false; // 리셋
    GetWorld()->ServerTravel(TEXT("/Game/Minwoo/MinwooTestMap?listen"));
}

// ════════════════════════════════════════════════════════════════════════════════
// Phase 7: 게임 종료 + 결과 반영 + 로비 복귀
// ════════════════════════════════════════════════════════════════════════════════

// ── 콘솔 커맨드 핸들러 (디버그용) ──
// 사용법: "EndGame Escaped" / "EndGame AllDead"
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

// ── BeginPlay에서 콘솔 커맨드 등록 (기존 BeginPlay 뒤에 추가하지 않고, 생성자 뒤에서 1회 등록) ──
// → 생성자에서 등록하면 CDO 생성 시점에 1회만 등록됨
// ── 대신 InitializeGame()에서 콘솔 커맨드를 등록하는 것이 안전하지만,
//    static 함수이므로 static 블록에서 FAutoConsoleCommand 사용

// FAutoConsoleCommand는 글로벌이므로 cpp 파일 상단 static 영역에서 등록
static FAutoConsoleCommandWithWorldAndArgs GCmdEndGame(
    TEXT("EndGame"),
    TEXT("Phase 7: 게임 종료. 사용법: EndGame [Escaped|AllDead|ServerShutdown]"),
    FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&AHellunaDefenseGameMode::CmdEndGame)
);

// ════════════════════════════════════════════════════════════════════════════════
// EndGame — 게임 종료 메인 함수
// ════════════════════════════════════════════════════════════════════════════════
void AHellunaDefenseGameMode::EndGame(EHellunaGameEndReason Reason)
{
    if (!HasAuthority()) return;

    // 중복 호출 방지
    if (bGameEnded)
    {
        UE_LOG(LogHelluna, Warning, TEXT("[Phase7] EndGame: 이미 게임이 종료됨, 스킵"));
        return;
    }

    bGameEnded = true;

    UE_LOG(LogHelluna, Warning, TEXT(""));
    UE_LOG(LogHelluna, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
    UE_LOG(LogHelluna, Warning, TEXT("║     [Phase7] EndGame — 게임 종료!                          ║"));
    UE_LOG(LogHelluna, Warning, TEXT("║     Reason: %s"),
        Reason == EHellunaGameEndReason::Escaped ? TEXT("탈출 성공") :
        Reason == EHellunaGameEndReason::AllDead ? TEXT("전원 사망") :
        Reason == EHellunaGameEndReason::ServerShutdown ? TEXT("서버 셧다운") : TEXT("None"));
    UE_LOG(LogHelluna, Warning, TEXT("║     LobbyServerURL: '%s'"), *LobbyServerURL);
    UE_LOG(LogHelluna, Warning, TEXT("║     GameMode 클래스: %s"), *GetClass()->GetName());
    UE_LOG(LogHelluna, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));

    // 낮/밤 타이머 정지 (더 이상 게임 진행 불필요)
    GetWorldTimerManager().ClearTimer(TimerHandle_ToNight);
    GetWorldTimerManager().ClearTimer(TimerHandle_ToDay);

    // 종료 사유 문자열
    FString ReasonString;
    switch (Reason)
    {
    case EHellunaGameEndReason::Escaped:       ReasonString = TEXT("탈출 성공"); break;
    case EHellunaGameEndReason::AllDead:        ReasonString = TEXT("전원 사망"); break;
    case EHellunaGameEndReason::ServerShutdown: ReasonString = TEXT("서버 셧다운"); break;
    default:                                    ReasonString = TEXT("알 수 없음"); break;
    }

    // ── 각 플레이어 처리 ──
    for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
    {
        APlayerController* PC = It->Get();
        if (!IsValid(PC)) continue;

        // 플레이어 생존 여부 판단
        bool bSurvived = false;

        // AllDead인 경우 모두 사망 처리
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

        // DB에 결과 반영
        ProcessPlayerGameResult(PC, bSurvived);

        // 클라이언트에 결과 UI 표시
        AHellunaHeroController* HeroPC = Cast<AHellunaHeroController>(PC);
        if (HeroPC)
        {
            // 생존자: 현재 인벤토리 수집
            TArray<FInv_SavedItemData> ResultItems;
            if (bSurvived)
            {
                AInv_PlayerController* InvPC = Cast<AInv_PlayerController>(PC);
                if (InvPC)
                {
                    // 서버에서 InvComp의 SaveData 수집
                    UInv_InventoryComponent* InvComp = PC->FindComponentByClass<UInv_InventoryComponent>();
                    if (InvComp)
                    {
                        ResultItems = InvComp->CollectInventoryDataForSave();
                    }
                }
            }

            // [Fix15] 네트워크 최적화: SerializedManifest 제거 (65KB RPC 제한 초과 방지)
            // 결과 UI는 ItemType, StackCount, bEquipped만 필요 — 무거운 바이너리 데이터 불필요
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
                // SerializedManifest, AttachmentsJson 제외 (대용량 바이너리)
                LightweightItems.Add(MoveTemp(LightItem));
            }

            HeroPC->Client_ShowGameResult(LightweightItems, bSurvived, ReasonString, LobbyServerURL);
        }
    }

    UE_LOG(LogHelluna, Log, TEXT("[Phase7] EndGame 완료 — 모든 플레이어 결과 처리됨"));
}

// ════════════════════════════════════════════════════════════════════════════════
// ProcessPlayerGameResult — 단일 플레이어의 게임 결과를 DB에 반영
// ════════════════════════════════════════════════════════════════════════════════
void AHellunaDefenseGameMode::ProcessPlayerGameResult(APlayerController* PC, bool bSurvived)
{
    if (!IsValid(PC)) return;

    // PlayerId 가져오기
    FString PlayerId = GetPlayerSaveId(PC);
    if (PlayerId.IsEmpty())
    {
        UE_LOG(LogHelluna, Warning, TEXT("[Phase7] ProcessPlayerGameResult: PlayerId가 비어있음 | PC=%s"),
            *GetNameSafe(PC));
        return;
    }

    // SQLite 서브시스템 가져오기 (DB 연결 불필요 — 파일 전송만 사용)
    UGameInstance* GI = GetGameInstance();
    UHellunaSQLiteSubsystem* DB = GI ? GI->GetSubsystem<UHellunaSQLiteSubsystem>() : nullptr;
    if (!DB)
    {
        UE_LOG(LogHelluna, Error, TEXT("[Phase7] ProcessPlayerGameResult: SQLite 서브시스템 없음 | PlayerId=%s"), *PlayerId);
        return;
    }

    // 결과 아이템 수집
    TArray<FInv_SavedItemData> ResultItems;
    if (bSurvived)
    {
        // 서버에서 InvComp의 SaveData 수집
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
        // ResultItems는 빈 배열 — 사망자는 아이템 전부 손실
    }

    // 게임 결과를 JSON 파일로 내보내기 (DB 잠금 회피)
    // → 로비 PostLogin에서 ImportGameResultFromFile로 읽어 Stash에 병합
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

// ════════════════════════════════════════════════════════════════════════════════
// Phase 7: Logout override — 게임 진행 중 나가기 = 사망 취급
// ════════════════════════════════════════════════════════════════════════════════
void AHellunaDefenseGameMode::Logout(AController* Exiting)
{
    // 게임 진행 중(InitializeGame 후, EndGame 전)이면 즉시 결과 처리
    if (bGameInitialized && !bGameEnded)
    {
        APlayerController* PC = Cast<APlayerController>(Exiting);
        if (IsValid(PC))
        {
            UE_LOG(LogHelluna, Warning, TEXT("[Phase7] Logout 중 게임 진행 중 — 사망 취급 | Player=%s"),
                *GetNameSafe(PC));

            // 사망 취급: 빈 배열로 Merge (아이템 전부 손실)
            ProcessPlayerGameResult(PC, false);
        }
    }

    Super::Logout(Exiting);
}

// ════════════════════════════════════════════════════════════════════════════════
// Phase 7: EndPlay override — 서버 셧다운 시 전원 결과 처리
// ════════════════════════════════════════════════════════════════════════════════
void AHellunaDefenseGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    // 서버 셧다운 + 게임 진행 중이면 모든 플레이어 결과 처리
    if (HasAuthority() && bGameInitialized && !bGameEnded)
    {
        UE_LOG(LogHelluna, Warning, TEXT("[Phase7] EndPlay: 서버 셧다운 — EndGame(ServerShutdown) 호출"));
        EndGame(EHellunaGameEndReason::ServerShutdown);
    }

    Super::EndPlay(EndPlayReason);
}
