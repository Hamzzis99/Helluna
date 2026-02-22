// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Inv_CraftingInterface.generated.h"

/**
 * 크래프팅 인터페이스
 * BP_WeaponCrafting, BP_PotionCrafting 등에서 구현
 */
UINTERFACE(MinimalAPI, Blueprintable)
class UInv_CraftingInterface : public UInterface
{
	GENERATED_BODY()
};

class INVENTORY_API IInv_CraftingInterface
{
	GENERATED_BODY()

public:
	/**
	 * 플레이어가 E키로 상호작용할 때 호출
	 * @param PlayerController 상호작용하는 플레이어 컨트롤러
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "제작", meta = (DisplayName = "상호작용 시"))
	void OnInteract(APlayerController* PlayerController);

	/**
	 * 이 크래프팅 스테이션의 위젯 클래스 반환
	 * BP_WeaponCrafting → WBP_WeaponCraftingMenu
	 * BP_PotionCrafting → WBP_PotionCraftingMenu
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "제작", meta = (DisplayName = "제작 메뉴 클래스 가져오기"))
	TSubclassOf<UUserWidget> GetCraftingMenuClass() const;

	/**
	 * 상호작용 가능 거리 반환 (기본값: 300.0f)
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "제작", meta = (DisplayName = "상호작용 거리 가져오기"))
	float GetInteractionDistance() const;
};

