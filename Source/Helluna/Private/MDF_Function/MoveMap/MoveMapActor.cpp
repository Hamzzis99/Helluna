// Gihyeon's MeshDeformation Project (Ported to Helluna)
// MoveMapActor.cpp

#include "MDF_Function/MoveMap/MoveMapActor.h"
#include "Kismet/GameplayStatics.h"
#include "GameMode/HellunaDefenseGameState.h"

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
        UE_LOG(LogTemp, Warning, TEXT("[MoveMapActor] 서버에서 직접 Interact() 호출"));
        Interact();
    }
    else
    {
        // 클라이언트 → Server RPC로 요청
        UE_LOG(LogTemp, Warning, TEXT("[MoveMapActor] 클라이언트 → Server_RequestInteract() RPC 호출"));
        Server_RequestInteract();
    }
    return true; 
}

// ⭐ [추가] Server RPC 구현
void AMoveMapActor::Server_RequestInteract_Implementation()
{
    UE_LOG(LogTemp, Warning, TEXT("[MoveMapActor] Server RPC 수신 → Interact() 호출"));
    Interact();
}

void AMoveMapActor::Interact()
{
    // 1. 서버 권한 확인 (맵 이동은 서버만 가능)
    if (!HasAuthority()) 
    {
        UE_LOG(LogTemp, Warning, TEXT("[MoveMapActor] ⚠️ 서버 권한 없음 - Interact() 중단"));
        return;
    }

    // 2. 맵 이름 체크
    if (NextLevelName.IsNone())
    {
        UE_LOG(LogTemp, Error, TEXT("[MoveMapActor] 이동할 맵 이름이 설정되지 않았습니다!"));
        return;
    }

    // 3. GameState 가져오기
    AGameStateBase* GS = UGameplayStatics::GetGameState(this);
    
    // [수정됨] TestGameState가 아닌, 실제 'HellunaDefenseGameState'로 캐스팅합니다.
    AHellunaDefenseGameState* HellunaGS = Cast<AHellunaDefenseGameState>(GS);

    if (HellunaGS)
    {
        // ★ 핵심: GameState야, 데이터(MDF) 저장 좀 하고 맵 이동 시켜줘!
        UE_LOG(LogTemp, Warning, TEXT("[MoveMapActor] ✅ GameState에게 저장 및 이동 요청: %s"), *NextLevelName.ToString());
       
        // HellunaGameState에 우리가 추가한 함수 호출
        HellunaGS->Server_SaveAndMoveLevel(NextLevelName);
    }
    else
    {
        // 만약 Cast 실패 시 로그 출력 (프로젝트 세팅의 GameStateClass 확인 필요)
        UE_LOG(LogTemp, Error, TEXT("[MoveMapActor] HellunaDefenseGameState를 찾을 수 없습니다! 캐스팅 실패."));
    }
}