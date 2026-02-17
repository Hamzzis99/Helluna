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

	Button_Split->OnClicked.AddDynamic(this, &ThisClass::SplitButtonClicked);
	Button_Drop->OnClicked.AddDynamic(this, &ThisClass::DropButtonClicked);
	Button_Consume->OnClicked.AddDynamic(this, &ThisClass::ConsumeButtonClicked);
	Button_Attachment->OnClicked.AddDynamic(this, &ThisClass::AttachmentButtonClicked);
	Slider_Split->OnValueChanged.AddDynamic(this, &ThisClass::SliderValueChanged);
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

void UInv_ItemPopUp::CollapseAttachmentButton() const
{
	Button_Attachment->SetVisibility(ESlateVisibility::Collapsed); // 부착물 관리 버튼 숨기기
}

void UInv_ItemPopUp::AttachmentButtonClicked()
{
	if (OnAttachment.ExecuteIfBound(GridIndex))
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
