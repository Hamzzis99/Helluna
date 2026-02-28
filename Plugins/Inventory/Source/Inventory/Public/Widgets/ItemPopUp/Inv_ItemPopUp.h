// Gihyeon's Inventory Project

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Inv_ItemPopUp.generated.h"

/**
 * The item popup widget shows up when right-clicking on an item
 * is the inventory grid.
 * 아이템 팝업 위젯은 인벤토리 그리드에서 아이템을 우클릭할 때 나타납니다.
 */
class UButton;
class USlider;
class UTextBlock;
class USizeBox;

//콜백 브로드캐스트 알림 창
DECLARE_DYNAMIC_DELEGATE_TwoParams(FPopUpMenuSplit, int32, SplitAmount, int32, Index);
DECLARE_DYNAMIC_DELEGATE_OneParam(FPopUpMenuDrop, int32, Index);
DECLARE_DYNAMIC_DELEGATE_OneParam(FPopUpMenuConsume, int32, Index);
DECLARE_DYNAMIC_DELEGATE_OneParam(FPopUpMenuAttachment, int32, Index); // 부착물 관리 버튼 콜백
DECLARE_DYNAMIC_DELEGATE_OneParam(FPopUpMenuTransfer, int32, Index); // 로비 전송 버튼 콜백
DECLARE_DYNAMIC_DELEGATE_OneParam(FPopUpMenuRotate, int32, Index); // 아이템 회전 버튼 콜백

UCLASS()
class INVENTORY_API UInv_ItemPopUp : public UUserWidget
{
	GENERATED_BODY()
public:
	virtual void NativeOnInitialized() override; // 위젯이 초기화될 때 호출되는 함수 재정의
	virtual void NativeOnMouseLeave(const FPointerEvent& InMouseEvent) override;
	
	FPopUpMenuSplit OnSplit;
	FPopUpMenuDrop OnDrop;
	FPopUpMenuConsume OnConsume;
	FPopUpMenuAttachment OnAttachment; // 부착물 관리 콜백
	FPopUpMenuTransfer OnTransfer; // 로비 전송 콜백
	FPopUpMenuRotate OnRotate; // 아이템 회전 콜백

	int32 GetSplitAmount() const;
	void CollapseSplitButton() const;
	void CollapseConsumeButton() const;
	void CollapseDropButton() const; // 드롭 버튼 숨기기
	void CollapseAttachmentButton() const; // 부착물 관리 버튼 숨기기
	void CollapseTransferButton() const; // 전송 버튼 숨기기
	void CollapseRotateButton() const; // 회전 버튼 숨기기
	void SetSliderParams(const float Max, const float Value) const;
	FVector2D GetBoxSize() const;
	void SetGridIndex(int32 Index) {GridIndex = Index;}
	int32 GetGridIndex() const {return GridIndex;}
	
	
private:
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> Button_Split;
	
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> Button_Drop;
	
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> Button_Consume;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> Button_Attachment; // 부착물 관리 버튼

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> Button_Transfer; // 로비 전송 버튼

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> Button_Rotate; // 아이템 회전 버튼

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<USlider> Slider_Split; // 슬라이더 버튼 만드는 법.
	
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> Text_SplitAmount; // 텍스트칸 만들기.
	
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<USizeBox> SizeBox_Root; // 사이즈박스 (위젯 크기 전체를 감싸는 부분)
	
	int32 GridIndex{INDEX_NONE};
	
	UFUNCTION() // 함수를 만들 때 선언하는 것이 유펑션
	void SplitButtonClicked(); // 분할 버튼 클릭시 실행되는 함수	
	
	UFUNCTION()
	void DropButtonClicked(); // 드롭 버튼 클릭시 실행되는 함수
	
	UFUNCTION()
	void ConsumeButtonClicked(); // 소비 버튼 클릭시 실행되는 함수

	UFUNCTION()
	void AttachmentButtonClicked(); // 부착물 관리 버튼 클릭시 실행되는 함수

	UFUNCTION()
	void TransferButtonClicked(); // 전송 버튼 클릭시 실행되는 함수

	UFUNCTION()
	void RotateButtonClicked(); // 회전 버튼 클릭시 실행되는 함수

	UFUNCTION()
	void SliderValueChanged(float Value); // 슬라이더 값이 변경될 때 실행되는 함수
	

};
