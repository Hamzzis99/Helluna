// Gihyeon's Inventory Project
//
// ════════════════════════════════════════════════════════════════════════════════
// 📌 부착물 슬롯 위젯 (Attachment Slot Widget) — Phase 3 구현
// ════════════════════════════════════════════════════════════════════════════════

#include "Widgets/Inventory/Attachment/Inv_AttachmentSlotWidget.h"

#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Items/Fragments/Inv_AttachmentFragments.h"
#include "Items/Fragments/Inv_ItemFragment.h"

void UInv_AttachmentSlotWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	if (IsValid(Button_Slot))
	{
		Button_Slot->OnClicked.AddDynamic(this, &ThisClass::OnButtonClicked);
	}
}

void UInv_AttachmentSlotWidget::SetupSlot(int32 InSlotIndex, const FInv_AttachmentSlotDef& SlotDef)
{
	SlotIndex = InSlotIndex;
	SlotType = SlotDef.SlotType;
	bIsOccupied = false;

	// 슬롯 이름 표시
	if (IsValid(Text_SlotName))
	{
		Text_SlotName->SetText(SlotDef.SlotDisplayName);
	}

	// 빈 슬롯 아이콘 설정
	ClearAttachedItem();
}

void UInv_AttachmentSlotWidget::SetAttachedItem(const FInv_AttachedItemData& AttachedData)
{
	bIsOccupied = true;

	// 부착물의 이미지 프래그먼트에서 아이콘 가져오기
	const FInv_ImageFragment* ImageFrag = AttachedData.ItemManifestCopy.GetFragmentOfType<FInv_ImageFragment>();
	if (ImageFrag && IsValid(Image_SlotIcon))
	{
		UTexture2D* Icon = ImageFrag->GetIcon();
		if (IsValid(Icon))
		{
			FSlateBrush Brush;
			Brush.SetResourceObject(Icon);
			Brush.ImageSize = FVector2D(44.f, 44.f);
			Image_SlotIcon->SetBrush(Brush);
		}
	}
}

void UInv_AttachmentSlotWidget::ClearAttachedItem()
{
	bIsOccupied = false;

	if (IsValid(Image_SlotIcon))
	{
		Image_SlotIcon->SetBrush(Brush_EmptySlot);
	}
}

void UInv_AttachmentSlotWidget::OnButtonClicked()
{
	UE_LOG(LogTemp, Log, TEXT("[AttachmentSlotWidget] 슬롯 %d 클릭됨 (타입: %s, 장착 여부: %s)"),
		SlotIndex, *SlotType.ToString(), bIsOccupied ? TEXT("O") : TEXT("X"));

	OnSlotClicked.Broadcast(SlotIndex);
}
