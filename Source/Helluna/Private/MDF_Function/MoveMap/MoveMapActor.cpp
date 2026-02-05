// Gihyeon's MeshDeformation Project (Ported to Helluna)
// MoveMapActor.cpp

#include "MDF_Function/MoveMap/MoveMapActor.h"
#include "Kismet/GameplayStatics.h"
#include "GameMode/HellunaDefenseGameState.h"
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
    UE_LOG(LogHellunaVote, Log, TEXT("[MoveMapActor] Server RPC 수신 → Interact() 호출"));
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
    AHellunaDefenseGameState* GameState = Cast<AHellunaDefenseGameState>(UGameplayStatics::GetGameState(this));
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
    AHellunaDefenseGameState* GameState = Cast<AHellunaDefenseGameState>(UGameplayStatics::GetGameState(this));
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