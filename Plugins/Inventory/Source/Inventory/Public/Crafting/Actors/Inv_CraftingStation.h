// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Crafting/Interfaces/Inv_CraftingInterface.h"
#include "Inv_CraftingStation.generated.h"

class UUserWidget;

/**
 * 크래프팅 스테이션 베이스 액터
 * BP_WeaponCrafting, BP_PotionCrafting 등으로 블루프린트 확장 가능
 */
UCLASS(Blueprintable)
class INVENTORY_API AInv_CraftingStation : public AActor, public IInv_CraftingInterface
{
	GENERATED_BODY()
	
public:	
	AInv_CraftingStation();

	// IInv_CraftingInterface 구현
	virtual void OnInteract_Implementation(APlayerController* PlayerController) override;
	virtual TSubclassOf<UUserWidget> GetCraftingMenuClass_Implementation() const override;
	virtual float GetInteractionDistance_Implementation() const override;

	// 상호작용 메시지 가져오기 (ItemComponent와 동일한 방식)
	UFUNCTION(BlueprintCallable, Category = "Crafting")
	FString GetPickupMessage() const { return PickupMessage; }

protected:
	virtual void BeginPlay() override;

	// 크래프팅 메뉴 위젯 클래스 (블루프린트에서 설정)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crafting")
	TSubclassOf<UUserWidget> CraftingMenuClass;
	
	// 상호작용 메시지 (블루프린트에서 수정 가능 - ItemComponent와 동일)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crafting")
	FString PickupMessage = "E - Craft";

	// 상호작용 가능 거리
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crafting")
	float InteractionDistance = 300.0f;

	// 거리 체크 간격 (초 단위)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crafting", meta = (ClampMin = "0.1", ClampMax = "5.0"))
	float DistanceCheckInterval = 0.5f;

	// 스테이션 메시 (외형)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> StationMesh;

private:
	// 플레이어별 크래프팅 메뉴 맵 (멀티플레이 지원)
	UPROPERTY()
	TMap<TObjectPtr<APlayerController>, TObjectPtr<UUserWidget>> PlayerMenuMap;

	// 플레이어별 거리 체크 타이머 맵
	TMap<TObjectPtr<APlayerController>, FTimerHandle> PlayerTimerMap;

	// 특정 플레이어의 거리 체크 함수
	UFUNCTION()
	void CheckDistanceToPlayer(APlayerController* PC);

	// 특정 플레이어의 메뉴를 강제로 닫는 함수
	void ForceCloseMenu(APlayerController* PC);
};

