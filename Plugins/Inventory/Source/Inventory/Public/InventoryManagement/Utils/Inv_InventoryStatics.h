// Gihyeon's Inventory Project

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Widgets/Utils/Inv_WidgetUtils.h"
#include "Inv_InventoryStatics.generated.h"

/**
 *
 */
UCLASS()
class INVENTORY_API UInv_InventoryStatics : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:

	UFUNCTION(BlueprintCallable, Category = "Inventory")
	static UInv_InventoryComponent* GetInventoryComponent(const APlayerController* PlayerController);

	UFUNCTION(BlueprintCallable, Category = "Inventory")
	static EInv_ItemCategory GetItemCategoryFromItemComp(UInv_ItemComponent* ItemComp);

	// 2D 배열 순회 알고리즘 (그리드 조각을 for each문으로 돌리는 알고리즘 제작 부분)
	template<typename T, typename FuncT>
	static void ForEach2D(TArray<T>& Array, int32 Index, const FIntPoint& Range2D, int32 GridColumns, const FuncT& Function); 
	
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	static void ItemHovered(APlayerController* PC, UInv_InventoryItem* Item);
	
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	static void ItemUnHovered(APlayerController* PC);
};


//여기 부분 왜 static 부분이 빨간줄이지? 나중에 분석을 할 필요가 있음.
template<typename T, typename FuncT>
static void UInv_InventoryStatics::ForEach2D(TArray<T>& Array, int32 Index, const FIntPoint& Range2D, int32 GridColumns, const FuncT& Function) // 알고리즘 부분.
{
	for (int32 j = 0; j < Range2D.Y; ++j)
	{
		for (int32 i = 0; i < Range2D.X; ++i)
		{
			const FIntPoint Coordinates = UInv_WidgetUtils::GetPositionFromIndex(Index, GridColumns) + FIntPoint(i, j); // 2D 좌표 계산
			const int32 TileIndex = UInv_WidgetUtils::GetIndexFromPosition(Coordinates, GridColumns); // 1D 인덱스 계산
			if (Array.IsValidIndex(TileIndex)) // 유효한 인덱스인지 확인
			{
				Function(Array[TileIndex]); // 함수 호출
			}
		}
	}
}
