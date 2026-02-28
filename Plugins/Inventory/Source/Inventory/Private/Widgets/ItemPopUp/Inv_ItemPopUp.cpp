// Gihyeon's Inventory Project


#include "Widgets/ItemPopUp/Inv_ItemPopUp.h"

#include "Components/Button.h"
#include "Components/SizeBox.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"

// 버튼 및 슬라이더 위젯들을 동적 할당 하는 부분들. (콜백 함수들 버튼 클릭 시 전달 할 수 있게 하는 것.)
void UInv_ItemPopUp::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	// [Fix26] AddDynamic → AddUniqueDynamic (NativeOnInitialized 재호출 시 이중 바인딩 방지)
	Button_Split->OnClicked.AddUniqueDynamic(this, &ThisClass::SplitButtonClicked);
	Button_Drop->OnClicked.AddUniqueDynamic(this, &ThisClass::DropButtonClicked);
	Button_Consume->OnClicked.AddUniqueDynamic(this, &ThisClass::ConsumeButtonClicked);
	Button_Attachment->OnClicked.AddUniqueDynamic(this, &ThisClass::AttachmentButtonClicked);
	Button_Transfer->OnClicked.AddUniqueDynamic(this, &ThisClass::TransferButtonClicked);
	Button_Rotate->OnClicked.AddUniqueDynamic(this, &ThisClass::RotateButtonClicked);
	Slider_Split->OnValueChanged.AddUniqueDynamic(this, &ThisClass::SliderValueChanged);
}

void UInv_ItemPopUp::NativeOnMouseLeave(const FPointerEvent& InMouseEvent)
{
	Super::NativeOnMouseLeave(InMouseEvent); 
	RemoveFromParent(); 
}

int32 UInv_ItemPopUp::GetSplitAmount() const
{
	return FMath::Floor(Slider_Split->GetValue());
}

void UInv_ItemPopUp::SplitButtonClicked()
{
	if (OnSplit.ExecuteIfBound(GetSplitAmount(), GridIndex))
	{
		RemoveFromParent(); // 위젯 제거
	}
}

void UInv_ItemPopUp::DropButtonClicked()
{
	if (OnDrop.ExecuteIfBound(GridIndex))
	{
		RemoveFromParent(); // 위젯 제거
	}
}

void UInv_ItemPopUp::ConsumeButtonClicked()
{
	if (OnConsume.ExecuteIfBound(GridIndex))
	{
		RemoveFromParent();
	}
}

void UInv_ItemPopUp::SliderValueChanged(float Value) // 슬라이더 밸류 부분
{
	Text_SplitAmount->SetText(FText::AsNumber(FMath::Floor(Value))); // 슬라이더 텍스트 지정
}

void UInv_ItemPopUp::CollapseSplitButton() const
{
	Button_Split->SetVisibility(ESlateVisibility::Collapsed); // 분할 버튼 접기
	Slider_Split->SetVisibility(ESlateVisibility::Collapsed); // 
	Text_SplitAmount->SetVisibility(ESlateVisibility::Collapsed);
}

void UInv_ItemPopUp::CollapseConsumeButton() const
{
	Button_Consume->SetVisibility(ESlateVisibility::Collapsed); //
}

void UInv_ItemPopUp::CollapseDropButton() const
{
	Button_Drop->SetVisibility(ESlateVisibility::Collapsed); // 드롭 버튼 숨기기
}

void UInv_ItemPopUp::CollapseAttachmentButton() const
{
	Button_Attachment->SetVisibility(ESlateVisibility::Collapsed); // 부착물 관리 버튼 숨기기
}

void UInv_ItemPopUp::CollapseTransferButton() const
{
	Button_Transfer->SetVisibility(ESlateVisibility::Collapsed); // 전송 버튼 숨기기
}

void UInv_ItemPopUp::CollapseRotateButton() const
{
	Button_Rotate->SetVisibility(ESlateVisibility::Collapsed); // 회전 버튼 숨기기
}

void UInv_ItemPopUp::AttachmentButtonClicked()
{
	if (OnAttachment.ExecuteIfBound(GridIndex))
	{
		RemoveFromParent(); // 위젯 제거
	}
}

void UInv_ItemPopUp::TransferButtonClicked()
{
	if (OnTransfer.ExecuteIfBound(GridIndex))
	{
		RemoveFromParent(); // 위젯 제거
	}
}

void UInv_ItemPopUp::RotateButtonClicked()
{
	if (OnRotate.ExecuteIfBound(GridIndex))
	{
		RemoveFromParent(); // 위젯 제거
	}
}

void UInv_ItemPopUp::SetSliderParams(const float Max, const float Value) const
{
	Slider_Split->SetMaxValue(Max);
	Slider_Split->SetMinValue(1);
	Slider_Split->SetValue(Value);
	Text_SplitAmount->SetText(FText::AsNumber(FMath::Floor(Value)));
}

FVector2D UInv_ItemPopUp::GetBoxSize() const
{
	return FVector2D(SizeBox_Root->GetWidthOverride(), SizeBox_Root->GetHeightOverride());
}
