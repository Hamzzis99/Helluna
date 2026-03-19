// Gihyeon's Inventory Project
// @author 김기현

// 크래프팅 레시피 데이터 구조체 및 DataAsset
// FInv_CraftingRecipe: 개별 레시피 정보 (재료 3종, 결과물, 카테고리)
// UInv_CraftingRecipeDA: 레시피 목록을 담는 DataAsset (에디터에서 DA_CraftingRecipes로 생성)

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "Types/Inv_GridTypes.h" // EInv_ItemCategory
#include "Inv_CraftingRecipeData.generated.h"

class UTexture2D;

/**
 * 개별 크래프팅 레시피 정보
 * 제작할 아이템, 필요 재료(최대 3종), 카테고리, 쿨다운 등을 정의
 */
USTRUCT(BlueprintType)
struct INVENTORY_API FInv_CraftingRecipe
{
	GENERATED_BODY()

	// 제작할 아이템 이름 (UI 표시용)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "제작",
		meta = (DisplayName = "아이템 이름"))
	FText ItemName;

	// 제작할 아이템 아이콘 (UI 표시용)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "제작",
		meta = (DisplayName = "아이템 아이콘"))
	TObjectPtr<UTexture2D> ItemIcon = nullptr;

	// 제작 결과물 액터 클래스 (Pickup Actor - BP_Weapon_Rifle 등)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "제작",
		meta = (DisplayName = "결과 아이템 액터 클래스"))
	TSubclassOf<AActor> ResultActorClass;

	// 제작 카테고리 (탭 분류용)
	// Equippable = 장비 탭, Consumable = 소모품 탭, Craftable = 무기부착물 탭
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "제작",
		meta = (DisplayName = "제작 카테고리"))
	EInv_ItemCategory CraftCategory = EInv_ItemCategory::Equippable;

	// 제작에 필요한 결과물 개수 (스택 가능 아이템: 탄약 30발 등)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "제작",
		meta = (DisplayName = "제작 개수", ClampMin = "1"))
	int32 CraftedAmount = 1;

	// === 재료 1 ===
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "제작|재료",
		meta = (DisplayName = "재료 1 태그", Categories = "GameItems.Craftables"))
	FGameplayTag MaterialTag1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "제작|재료",
		meta = (DisplayName = "재료 1 필요 수량", ClampMin = "0"))
	int32 MaterialAmount1 = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "제작|재료",
		meta = (DisplayName = "재료 1 아이콘"))
	TObjectPtr<UTexture2D> MaterialIcon1 = nullptr;

	// === 재료 2 ===
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "제작|재료",
		meta = (DisplayName = "재료 2 태그", Categories = "GameItems.Craftables"))
	FGameplayTag MaterialTag2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "제작|재료",
		meta = (DisplayName = "재료 2 필요 수량", ClampMin = "0"))
	int32 MaterialAmount2 = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "제작|재료",
		meta = (DisplayName = "재료 2 아이콘"))
	TObjectPtr<UTexture2D> MaterialIcon2 = nullptr;

	// === 재료 3 ===
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "제작|재료",
		meta = (DisplayName = "재료 3 태그", Categories = "GameItems.Craftables"))
	FGameplayTag MaterialTag3;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "제작|재료",
		meta = (DisplayName = "재료 3 필요 수량", ClampMin = "0"))
	int32 MaterialAmount3 = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "제작|재료",
		meta = (DisplayName = "재료 3 아이콘"))
	TObjectPtr<UTexture2D> MaterialIcon3 = nullptr;

	// === 쿨다운 ===
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "제작|쿨다운",
		meta = (DisplayName = "제작 쿨다운(초)", ClampMin = "0.1", ClampMax = "5.0"))
	float CraftCooldown = 0.5f;
};

/**
 * 크래프팅 레시피 DataAsset
 * 에디터에서 DA_CraftingRecipes로 생성하여 레시피 목록을 관리
 */
UCLASS(BlueprintType)
class INVENTORY_API UInv_CraftingRecipeDA : public UDataAsset
{
	GENERATED_BODY()

public:
	// 전체 레시피 목록
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "제작",
		meta = (DisplayName = "레시피 목록"))
	TArray<FInv_CraftingRecipe> Recipes;

	// 카테고리별 레시피 필터 헬퍼 (런타임 사용)
	UFUNCTION(BlueprintCallable, Category = "제작")
	TArray<FInv_CraftingRecipe> GetRecipesByCategory(EInv_ItemCategory Category) const;
};
