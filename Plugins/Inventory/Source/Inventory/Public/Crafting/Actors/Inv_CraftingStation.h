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
	UFUNCTION(BlueprintCallable, Category = "제작", meta = (DisplayName = "상호작용 메시지 가져오기"))
	FString GetPickupMessage() const { return PickupMessage; }

protected:
	virtual void BeginPlay() override;

	// 크래프팅 메뉴 위젯 클래스 (블루프린트에서 설정)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "제작",
		meta = (DisplayName = "제작 메뉴 위젯 클래스", Tooltip = "이 제작 스테이션에서 열릴 제작 메뉴 위젯 블루프린트 클래스입니다."))
	TSubclassOf<UUserWidget> CraftingMenuClass;

	// 상호작용 메시지 (블루프린트에서 수정 가능 - ItemComponent와 동일)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "제작",
		meta = (DisplayName = "상호작용 메시지", Tooltip = "플레이어가 가까이 갔을 때 화면에 표시되는 상호작용 안내 메시지입니다."))
	FString PickupMessage = "E - Craft";

	// 상호작용 가능 거리
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "제작",
		meta = (DisplayName = "상호작용 거리", Tooltip = "플레이어가 이 거리(cm) 이내에 있어야 상호작용이 가능합니다."))
	float InteractionDistance = 300.0f;

	// 거리 체크 간격 (초 단위)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "제작",
		meta = (DisplayName = "거리 체크 간격", Tooltip = "플레이어와의 거리를 확인하는 주기(초). 메뉴가 열린 상태에서 거리를 벗어나면 자동으로 닫힙니다.", ClampMin = "0.1", ClampMax = "5.0"))
	float DistanceCheckInterval = 0.5f;

	// 스테이션 메시 (외형)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "제작|컴포넌트", meta = (DisplayName = "스테이션 메시"))
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

