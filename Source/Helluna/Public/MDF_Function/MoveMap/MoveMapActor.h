// Gihyeon's MeshDeformation Project (Ported to Helluna)
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

// [확인 필요] 졸업작품 프로젝트로 옮긴 인터페이스 경로가 맞는지 확인하세요.
#include "Interfaces/Inv_Interface_Primary.h" 

#include "MoveMapActor.generated.h"

UCLASS()
class HELLUNA_API AMoveMapActor : public AActor, public IInv_Interface_Primary
{
    GENERATED_BODY()
public:
    AMoveMapActor();

protected:
    virtual void BeginPlay() override;

public:
    // 실제 이동 로직을 수행하는 함수 (서버에서만 실행됨)
    UFUNCTION(BlueprintCallable, Category = "Helluna|Interaction")
    void Interact();

    // [인터페이스 구현] PlayerController가 호출하는 상호작용 함수
    virtual bool ExecuteInteract_Implementation(APlayerController* Controller) override;

public:
    // 에디터에서 이동할 맵 이름을 적으세요 (예: LobbyMap, GameMap)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map Settings", meta = (ExposeOnSpawn = "true", DisplayName = "이동할 맵 이름"))
    FName NextLevelName;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    TObjectPtr<UStaticMeshComponent> MeshComp;
};