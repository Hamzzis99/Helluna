// Gihyeon's Inventory Project


#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Inv_GridSlot.generated.h"

class UInv_ItemPopUp;
class UInv_InventoryItem;
class UImage;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FGridSlotEvent, int32, GridIndex, const FPointerEvent&, MouseEvent); // 마우스 클릭 했을 경우 관한 정보 델리게이트

UENUM(BlueprintType)
enum class EInv_GridSlotState : uint8
{
	Unoccupied,
	Occupied,
	Selected,
	GrayedOut
}; // 4가지의 이미지

UCLASS()
class INVENTORY_API UInv_GridSlot : public UUserWidget
{
	GENERATED_BODY()
public:
	virtual void NativeOnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override; // 마우스 엔터 이벤트
	virtual void NativeOnMouseLeave(const FPointerEvent& MouseEvent) override; // 마우스를 벗어났을 때의 이벤트
	virtual FReply NativeOnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override; // 마우스 버튼 다운 이벤트

	// Getter Setter 부분들.
	void SetTileIndex(int32 Index) { TileIndex = Index; } // 타일 인덱스 설정
	int32 GetTileIndex() const { return TileIndex; } //	타일 인덱스 반환
	EInv_GridSlotState GetGridSlotState() const { return GridSlotState; } // 현재 상태 반환
	TWeakObjectPtr<UInv_InventoryItem> GetInventoryItem() const { return InventoryItem; } // 인벤토리 아이템 반환
	void SetInventoryItem(UInv_InventoryItem* Item); // 인벤토리 아이템 설정
	int32 GetStackCount() const { return StackCount; } // 스택 카운트 반환
	void SetStackCount(int32 Count) { StackCount = Count; } // 스택 카운트 설정
	int32 GetIndex() const { return TileIndex; } // 인덱스 반환
	void SetIndex(int32 Index) { TileIndex = Index; } // 인덱스 설정

	//아이템 먹을수록 왼쪽부터 채워주는 함수들 만들기
	int32 GetUpperLeftIndex() const { return UpperLeftIndex; } // 왼쪽 위 인덱스 반환
	void SetUpperLeftIndex(int32 Index) { UpperLeftIndex = Index; } // 왼쪽 위 인덱스 설정
	bool IsAvailable() const { return bAvailable; } // 사용 가능 여부 반환
	void SetAvailable(bool bIsAvailable) { bAvailable = bIsAvailable; } // 사용 가능 여부 설정
	
	//팝업 창 관련 함수들
	void SetItemPopUp(UInv_ItemPopUp* PopUp); // 아이템 팝업 설정
	UInv_ItemPopUp* GetItemPopUp() const; // 아이템 팝업 반환
	
	void SetOccupiedTexture();
	void SetUnoccupiedTexture();
	void SetSelectedTexture();
	void SetGrayedOutTexture();

	FGridSlotEvent GridSlotClicked; // 그리드 슬롯 클릭 이벤트 델리게이트
	FGridSlotEvent GridSlotHovered; // 그리드 슬롯 호버 이벤트 델리게이트
	FGridSlotEvent GridSlotUnhovered; // 그리드 슬롯 언호버 이벤트 델리게이트

private:
	int32 StackCount{ 0 };
	bool bAvailable{ true }; // 사용 가능 여부
	int32 TileIndex{ INDEX_NONE };
	int32 UpperLeftIndex{ INDEX_NONE }; // 정사각형인지 확인해주는 것인가?
	TWeakObjectPtr<UInv_InventoryItem> InventoryItem; // 인벤토리 아이템 포인터
	TWeakObjectPtr<UInv_ItemPopUp> ItemPopUp; // 아이템 팝업 포인터
	
	
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> Image_GridSlot;

	UPROPERTY(EditAnywhere, Category = "인벤토리|그리드", meta = (DisplayName = "비점유 브러시", Tooltip = "슬롯이 비어 있을 때 표시되는 브러시입니다."))
	FSlateBrush Brush_Unoccupied;

	UPROPERTY(EditAnywhere, Category = "인벤토리|그리드", meta = (DisplayName = "점유 브러시", Tooltip = "슬롯에 아이템이 배치되어 있을 때 표시되는 브러시입니다."))
	FSlateBrush Brush_Occupied;

	UPROPERTY(EditAnywhere, Category = "인벤토리|그리드", meta = (DisplayName = "선택 브러시", Tooltip = "슬롯이 선택(하이라이트)되었을 때 표시되는 브러시입니다."))
	FSlateBrush Brush_Selected;

	UPROPERTY(EditAnywhere, Category = "인벤토리|그리드", meta = (DisplayName = "비활성 브러시", Tooltip = "슬롯이 비활성(GrayedOut) 상태일 때 표시되는 브러시입니다."))
	FSlateBrush Brush_GrayedOut;

	EInv_GridSlotState GridSlotState;
	
	UFUNCTION()
	void OnItemPopUpDestruct(UUserWidget* Menu); // 아이템 팝업 소멸시 호출되는 함수
};
