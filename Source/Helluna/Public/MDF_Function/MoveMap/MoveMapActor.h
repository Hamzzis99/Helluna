// Gihyeon's MeshDeformation Project (Ported to Helluna)
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

// [확인 필요] 졸업작품 프로젝트로 옮긴 인터페이스 경로가 맞는지 확인하세요.
#include "Interfaces/Inv_Interface_Primary.h"

// [투표 시스템] (김기현)
#include "Utils/Vote/VoteTypes.h"
#include "Utils/Vote/VoteHandler.h"

#include "MoveMapActor.generated.h"

UCLASS()
class HELLUNA_API AMoveMapActor : public AActor, public IInv_Interface_Primary, public IVoteHandler
{
    GENERATED_BODY()
public:
    AMoveMapActor();

protected:
    virtual void BeginPlay() override;

public:
    // 실제 이동 로직을 수행하는 함수 (서버에서만 실행됨)
    UFUNCTION(BlueprintCallable, Category = "Helluna|Interaction")
    void Interact(APlayerController* InstigatorController = nullptr);

    // [인터페이스 구현] PlayerController가 호출하는 상호작용 함수
    virtual bool ExecuteInteract_Implementation(APlayerController* Controller) override;

    // ⭐ [추가] 클라이언트 → 서버 RPC (맵 이동은 서버에서만 가능)
    UFUNCTION(Server, Reliable)
    void Server_RequestInteract(APlayerController* RequestingController);

public:
    // 에디터에서 이동할 맵 이름을 적으세요 (예: LobbyMap, GameMap)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map Settings", meta = (ExposeOnSpawn = "true", DisplayName = "Next Level Name (이동할 맵 이름)"))
    FName NextLevelName;

    // =========================================================================================
    // [투표 시스템 설정] (김기현)
    // =========================================================================================

    /** 투표 조건 (만장일치 / 과반수) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vote Settings", meta = (DisplayName = "Vote Condition (투표 조건)"))
    EVoteCondition VoteCondition = EVoteCondition::Majority;

    /** 투표 제한 시간 (초) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vote Settings", meta = (DisplayName = "Vote Timeout (투표 제한 시간)", ClampMin = "10.0", ClampMax = "120.0"))
    float VoteTimeout = 30.0f;

    /** 중도 퇴장 시 정책 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vote Settings", meta = (DisplayName = "Disconnect Policy (퇴장 시 정책)"))
    EVoteDisconnectPolicy DisconnectPolicy = EVoteDisconnectPolicy::ExcludeAndContinue;

    // =========================================================================================
    // [IVoteHandler 인터페이스 구현]
    // =========================================================================================

    /** 투표 통과 시 맵 이동 실행 */
    virtual void ExecuteVoteResult_Implementation(const FVoteRequest& Request) override;

public:
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    TObjectPtr<UStaticMeshComponent> MeshComp;
};