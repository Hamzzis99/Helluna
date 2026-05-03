// Gihyeon's MeshDeformation Project (Ported to Helluna)
// MoveMapActor.cpp

#include "MDF_Function/MoveMap/MoveMapActor.h"
#include "Kismet/GameplayStatics.h"
#include "GameMode/HellunaBaseGameState.h"
#include "Utils/Vote/VoteManagerComponent.h"

AMoveMapActor::AMoveMapActor()
{
    PrimaryActorTick.bCanEverTick = false; 
    bReplicates = true; 

    MeshComp = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComp"));
    RootComponent = MeshComp;
}

void AMoveMapActor::BeginPlay()
{
    Super::BeginPlay();
}

// [인터페이스 구현]
bool AMoveMapActor::ExecuteInteract_Implementation(APlayerController* Controller)
{
    // ⭐ [수정] 서버면 바로 실행, 클라이언트면 Server RPC 호출
    if (HasAuthority())
    {
        // 서버 (Listen Server 호스트 또는 Dedicated Server)
        UE_LOG(LogHellunaVote, Log, TEXT("[MoveMapActor] 서버에서 직접 Interact() 호출"));
        Interact(Controller);
    }
    else
    {
        // 클라이언트 → Server RPC로 요청
        UE_LOG(LogHellunaVote, Log, TEXT("[MoveMapActor] 클라이언트 → Server_RequestInteract() RPC 호출"));
        Server_RequestInteract(Controller);
    }
    return true;
}

// ⭐ [추가] Server RPC 구현
void AMoveMapActor::Server_RequestInteract_Implementation(APlayerController* RequestingController)
{
    // [C5-VOT-1] RPC 호출자 위조 차단:
    // 클라가 임의의 PlayerController 포인터를 인자로 보낼 수 있으므로, 받은 Controller가
    // 실제 PlayerControllerIterator에 등록된 PC인지 검증. cheat 클라가 다른 플레이어의
    // PC 포인터를 추측해 보내는 시도 차단.
    if (!HasAuthority()) return;
    if (!IsValid(RequestingController))
    {
        UE_LOG(LogHellunaVote, Warning, TEXT("[MoveMapActor] RPC: RequestingController invalid - 무시"));
        return;
    }

    bool bIsRegisteredPC = false;
    if (UWorld* W = GetWorld())
    {
        for (FConstPlayerControllerIterator It = W->GetPlayerControllerIterator(); It; ++It)
        {
            if (It->Get() == RequestingController)
            {
                bIsRegisteredPC = true;
                break;
            }
        }
    }

    if (!bIsRegisteredPC)
    {
        UE_LOG(LogHellunaVote, Warning,
            TEXT("[MoveMapActor] RPC 위조 시도 차단: RequestingController(%s)가 PlayerControllerIterator에 없음"),
            *GetNameSafe(RequestingController));
        return;
    }

    // 추가 검증: NetConnection이 있어야 클라이언트 PC (서버 측 호출자가 아닌 진짜 클라)
    if (!RequestingController->GetNetConnection() && !RequestingController->IsLocalController())
    {
        UE_LOG(LogHellunaVote, Warning,
            TEXT("[MoveMapActor] RPC 위조: NetConnection 없고 LocalController 아님 - 무시"));
        return;
    }

    UE_LOG(LogHellunaVote, Log, TEXT("[MoveMapActor] Server RPC 수신 (검증 통과) → Interact() 호출"));
    Interact(RequestingController);
}

// =========================================================================================
// [투표 시스템] Interact() - 투표 시작 (김기현)
// =========================================================================================

void AMoveMapActor::Interact(APlayerController* InstigatorController)
{
    UE_LOG(LogHellunaVote, Log, TEXT("[MoveMapActor] Interact 진입"));

    // 1. 서버 권한 확인
    if (!HasAuthority())
    {
        UE_LOG(LogHellunaVote, Warning, TEXT("[MoveMapActor] 서버 권한 없음 - Interact() 중단"));
        return;
    }

    // 2. 맵 이름 체크
    if (NextLevelName.IsNone())
    {
        UE_LOG(LogHellunaVote, Error, TEXT("[MoveMapActor] 이동할 맵 이름이 설정되지 않았습니다!"));
        return;
    }

    // 3. GameState에서 VoteManager 가져오기
    AHellunaBaseGameState* GameState = Cast<AHellunaBaseGameState>(UGameplayStatics::GetGameState(this));
    if (!GameState)
    {
        UE_LOG(LogHellunaVote, Error, TEXT("[MoveMapActor] GameState를 찾을 수 없습니다!"));
        return;
    }

    UVoteManagerComponent* VoteManager = GameState->VoteManagerComponent;
    if (!VoteManager)
    {
        UE_LOG(LogHellunaVote, Error, TEXT("[MoveMapActor] VoteManagerComponent를 찾을 수 없습니다!"));
        return;
    }

    // 4. 이미 투표 진행 중이면 무시
    if (VoteManager->IsVoteInProgress())
    {
        UE_LOG(LogHellunaVote, Warning, TEXT("[MoveMapActor] 이미 투표가 진행 중입니다"));
        return;
    }

    // 5. Initiator PlayerState 가져오기
    APlayerState* InitiatorPS = nullptr;
    if (InstigatorController)
    {
        InitiatorPS = InstigatorController->GetPlayerState<APlayerState>();
    }

    if (!InitiatorPS)
    {
        UE_LOG(LogHellunaVote, Error, TEXT("[MoveMapActor] Initiator PlayerState를 찾을 수 없습니다!"));
        return;
    }

    // 6. 투표 요청 생성
    FVoteRequest Request;
    Request.VoteType = EVoteType::MapMove;
    Request.Condition = VoteCondition;
    Request.Timeout = VoteTimeout;
    Request.DisconnectPolicy = DisconnectPolicy;
    Request.TargetMapName = NextLevelName;
    Request.Initiator = InitiatorPS;

    UE_LOG(LogHellunaVote, Log, TEXT("[MoveMapActor] 투표 시작 요청 - %s"), *Request.ToString());

    // 7. 투표 시작
    bool bStarted = VoteManager->StartVote(Request, TScriptInterface<IVoteHandler>(this));

    if (bStarted)
    {
        UE_LOG(LogHellunaVote, Log, TEXT("[MoveMapActor] 투표 시작됨!"));
    }
    else
    {
        UE_LOG(LogHellunaVote, Warning, TEXT("[MoveMapActor] 투표 시작 실패"));
    }
}

// =========================================================================================
// [IVoteHandler 구현] 투표 시작 전 검증
// =========================================================================================

bool AMoveMapActor::OnVoteStarting_Implementation(const FVoteRequest& Request)
{
    // 맵 이름이 설정되어 있으면 투표 허용
    if (Request.TargetMapName.IsNone())
    {
        UE_LOG(LogHellunaVote, Warning, TEXT("[MoveMapActor] OnVoteStarting - 대상 맵 이름이 없음, 투표 거부"));
        return false;
    }

    UE_LOG(LogHellunaVote, Log, TEXT("[MoveMapActor] OnVoteStarting - 검증 통과, 투표 허용 (대상: %s)"),
        *Request.TargetMapName.ToString());
    return true;
}

// =========================================================================================
// [IVoteHandler 구현] 투표 통과 시 호출
// =========================================================================================

void AMoveMapActor::ExecuteVoteResult_Implementation(const FVoteRequest& Request)
{
    UE_LOG(LogHellunaVote, Log, TEXT("[MoveMapActor] ExecuteVoteResult - 투표 통과! 맵 이동 실행"));

    // 서버 권한 확인
    if (!HasAuthority())
    {
        UE_LOG(LogHellunaVote, Warning, TEXT("[MoveMapActor] ExecuteVoteResult - 서버 권한 없음"));
        return;
    }

    // GameState를 통해 맵 이동
    AHellunaBaseGameState* GameState = Cast<AHellunaBaseGameState>(UGameplayStatics::GetGameState(this));
    if (GameState)
    {
        UE_LOG(LogHellunaVote, Log, TEXT("[MoveMapActor] GameState->Server_SaveAndMoveLevel(%s) 호출"), *Request.TargetMapName.ToString());
        GameState->Server_SaveAndMoveLevel(Request.TargetMapName);
    }
    else
    {
        UE_LOG(LogHellunaVote, Error, TEXT("[MoveMapActor] ExecuteVoteResult - GameState를 찾을 수 없습니다!"));
    }
}