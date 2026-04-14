// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "HellunaCheatComponent.generated.h"

class UDataTable;
class UInv_InventoryComponent;

/**
 * F1~F4 디버그 치트 컴포넌트.
 *  F1: 필드 내 모든 AHellunaEnemyCharacter 처치
 *  F2: UDS 시간 정지 토글(낮 고정)
 *  F3: 노클립(비행 + 충돌 해제 + 속도 x10) 토글
 *  F4: ItemTypeMapping DataTable의 "GameItems.Craftables.*" 재료 전부를 99개 지급
 * 모든 효과는 서버 권한에서 실행된다.
 */
UCLASS(ClassGroup=(Helluna), meta=(BlueprintSpawnableComponent))
class HELLUNA_API UHellunaCheatComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UHellunaCheatComponent();

    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    // ── 로컬 입력 진입점 ──────────────────────────────────────────
    void HandleKey_KillAll();
    void HandleKey_TimeFreeze();
    void HandleKey_Noclip();
    void HandleKey_GrantMaterials();
    void HandleKey_SpeedUp();
    void HandleKey_SpeedDown();

    virtual void BeginPlay() override;

    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction) override;

    // ── 서버 RPC ──────────────────────────────────────────────────
    UFUNCTION(Server, Reliable)
    void Server_KillAllEnemies();

    UFUNCTION(Server, Reliable)
    void Server_ToggleTimeFreeze();

    UFUNCTION(Server, Reliable)
    void Server_ToggleNoclip();

    /** [cheatdebug] 클라 예측과 맞추기 위해 CMC 속도 설정을 전원에 전파 */
    UFUNCTION(NetMulticast, Reliable)
    void Multicast_ApplyNoclipState(bool bEnable, float Multiplier);

    UFUNCTION(Server, Reliable)
    void Server_GrantAllMaterials();

    /** 노클립 상태에서 수직 이동 입력 (+1 = 상승, -1 = 하강) */
    UFUNCTION(Server, Reliable)
    void Server_NoclipAscend(float Direction);

    /** 노클립 속도 배율을 Scale배 한다 (2.0 = 2배, 0.5 = 1/2배) */
    UFUNCTION(Server, Reliable)
    void Server_AdjustNoclipSpeed(float Scale);

    /** 재료 지급에 사용할 ItemType → Actor 매핑 DataTable (BP 기본값에서 DT_ItemTypeMapping 할당) */
    UPROPERTY(EditDefaultsOnly, Category="Cheat|Inventory")
    TObjectPtr<UDataTable> ItemTypeMappingDataTable;

    /** 명시적으로 할당되지 않았을 때 F4 실행 시점에 런타임 로드할 기본 경로 */
    UPROPERTY()
    FSoftObjectPath DefaultItemTypeMappingPath;

    /** 재료로 간주할 GameplayTag 접두어 (기본: "GameItems.Craftables") */
    UPROPERTY(EditDefaultsOnly, Category="Cheat|Inventory")
    FString MaterialTagPrefix = TEXT("GameItems.Craftables");

    /** 재료당 지급 스택 수 */
    UPROPERTY(EditDefaultsOnly, Category="Cheat|Inventory", meta=(ClampMin="1", ClampMax="9999"))
    int32 GrantStackCount = 99;

    /** 노클립 시 속도 배율 (BP에서 조정 가능) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cheat|Noclip", meta=(ClampMin="1.0", ClampMax="10000.0"))
    float NoclipSpeedMultiplier = 10.f;

private:
    UPROPERTY(Replicated)
    bool bNoclipOn = false;

    float CachedWalkSpeed = 0.f;
    float CachedFlySpeed = 0.f;
    float CachedAcceleration = 0.f;
    bool bCachedMovementValid = false;

    /** 노클립을 캐릭터에 적용(서버 권한). */
    void ApplyNoclipOnOwner(bool bEnable);

    /** 오너 캐릭터의 PlayerController에 부착된 InventoryComponent를 찾는다. */
    UInv_InventoryComponent* FindInventoryComponent() const;
};
