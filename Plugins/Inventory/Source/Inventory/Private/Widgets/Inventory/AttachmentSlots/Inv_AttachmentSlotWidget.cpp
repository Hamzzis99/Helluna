// Gihyeon's Inventory Project
//
// ════════════════════════════════════════════════════════════════════════════════
// 📌 부착물 슬롯 위젯 (Attachment Slot Widget) — Phase 3 구현
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 핵심 흐름:
//    InitSlot → SetOccupied/SetEmpty → 마우스 이벤트 → OnSlotClicked 브로드캐스트
//    패널의 NativeTick에서 UpdateSlotHighlights → SetHighlighted 호출
//
// ════════════════════════════════════════════════════════════════════════════════

#include "Widgets/Inventory/AttachmentSlots/Inv_AttachmentSlotWidget.h"

#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Items/Fragments/Inv_AttachmentFragments.h"
#include "Items/Fragments/Inv_ItemFragment.h"

// ════════════════════════════════════════════════════════════════
// 📌 InitSlot — 슬롯 정의와 현재 장착 데이터로 UI 초기화
// ════════════════════════════════════════════════════════════════
// 호출 경로: AttachmentPanel::BuildSlotWidgets → 이 함수
// 처리 흐름:
//   1. SlotIndex, SlotType 캐시
//   2. Text_SlotName에 슬롯 이름 설정
//   3. AttachedData 유무에 따라 SetOccupied / SetEmpty 분기
// 실패 조건: 없음 (방어 코드로 처리)
// Phase 연결: Phase 3 UI 초기화
// ════════════════════════════════════════════════════════════════
void UInv_AttachmentSlotWidget::InitSlot(int32 InSlotIndex, const FInv_AttachmentSlotDef& SlotDef, const FInv_AttachedItemData* AttachedData)
{
	SlotIndex = InSlotIndex;
	SlotType = SlotDef.SlotType;

	// 슬롯 이름 설정
	if (IsValid(Text_SlotName))
	{
		Text_SlotName->SetText(SlotDef.SlotDisplayName);
	}

	// 장착된 부착물이 있으면 점유 상태, 없으면 빈 상태
	if (AttachedData)
	{
		SetOccupied(*AttachedData);
	}
	else
	{
		SetEmpty();
	}

	UE_LOG(LogTemp, Log, TEXT("[Attachment UI] 슬롯 %d 초기화: %s (점유=%s)"),
		SlotIndex,
		*SlotType.ToString(),
		bIsOccupied ? TEXT("O") : TEXT("X"));
}

// ════════════════════════════════════════════════════════════════
// 📌 SetOccupied — 부착물이 장착된 상태로 변경
// ════════════════════════════════════════════════════════════════
// 호출 경로: InitSlot / RefreshSlotStates → 이 함수
// 처리 흐름:
//   1. bIsOccupied = true
//   2. Image_Background에 Brush_Occupied 설정
//   3. AttachedData의 ItemManifestCopy에서 아이콘 추출 → Image_ItemIcon에 표시
// ════════════════════════════════════════════════════════════════
void UInv_AttachmentSlotWidget::SetOccupied(const FInv_AttachedItemData& Data)
{
	bIsOccupied = true;

	// 배경 브러시를 점유 상태로 변경
	if (IsValid(Image_Background))
	{
		Image_Background->SetBrush(Brush_Occupied);
	}

	// 부착물의 아이콘 표시
	const FInv_ImageFragment* ImageFrag = Data.ItemManifestCopy.GetFragmentOfType<FInv_ImageFragment>();
	if (ImageFrag && IsValid(Image_ItemIcon))
	{
		UTexture2D* Icon = ImageFrag->GetIcon();
		if (IsValid(Icon))
		{
			FSlateBrush IconBrush;
			IconBrush.SetResourceObject(Icon);
			IconBrush.ImageSize = IconSize;
			Image_ItemIcon->SetBrush(IconBrush);
			Image_ItemIcon->SetVisibility(ESlateVisibility::Visible); // 아이콘 보이기
		}
	}

	// AttachmentItemType이 비어있을 때 AttachableFragment의 타입을 대신 표시 (방어 코드)
	FString DisplayName = Data.AttachmentItemType.ToString();
	if (!Data.AttachmentItemType.IsValid())
	{
		const FInv_AttachableFragment* AttachFrag = Data.ItemManifestCopy.GetFragmentOfType<FInv_AttachableFragment>();
		if (AttachFrag)
		{
			DisplayName = FString::Printf(TEXT("(타입:%s)"), *AttachFrag->GetAttachmentType().ToString());
		}
		else
		{
			DisplayName = TEXT("(태그 미설정)");
		}
		UE_LOG(LogTemp, Warning, TEXT("[Attachment UI] ⚠️ 슬롯 %d: AttachmentItemType이 비어있음! BP에서 태그 재설정 필요"), SlotIndex);
	}

	UE_LOG(LogTemp, Log, TEXT("[Attachment UI] 슬롯 %d 점유됨: %s"),
		SlotIndex, *DisplayName);
}

// ════════════════════════════════════════════════════════════════
// 📌 SetEmpty — 빈 슬롯 상태로 복귀
// ════════════════════════════════════════════════════════════════
// 호출 경로: InitSlot / RefreshSlotStates → 이 함수
// 처리 흐름:
//   1. bIsOccupied = false
//   2. Image_Background에 Brush_Empty 설정
//   3. Image_ItemIcon → Collapsed
// ════════════════════════════════════════════════════════════════
void UInv_AttachmentSlotWidget::SetEmpty()
{
	bIsOccupied = false;

	// 배경 브러시를 빈 상태로 변경
	if (IsValid(Image_Background))
	{
		Image_Background->SetBrush(Brush_Empty);
	}

	// 아이콘 숨기기
	if (IsValid(Image_ItemIcon))
	{
		Image_ItemIcon->SetVisibility(ESlateVisibility::Collapsed);
	}

	UE_LOG(LogTemp, Log, TEXT("[Attachment UI] 슬롯 %d 비워짐"), SlotIndex);
}

// ════════════════════════════════════════════════════════════════
// 📌 SetHighlighted — 호환 슬롯 하이라이트 토글
// ════════════════════════════════════════════════════════════════
// 호출 경로: AttachmentPanel::UpdateSlotHighlights → 이 함수
// 처리 흐름:
//   1. Image_Highlight의 가시성을 bHighlight에 따라 Visible / Collapsed 전환
// Phase 연결: NativeTick에서 HoverItem 호환성 실시간 표시
// ════════════════════════════════════════════════════════════════
void UInv_AttachmentSlotWidget::SetHighlighted(bool bHighlight)
{
	if (IsValid(Image_Highlight))
	{
		Image_Highlight->SetVisibility(bHighlight ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
}

// ════════════════════════════════════════════════════════════════
// 📌 NativeOnMouseButtonDown — 마우스 클릭 처리
// ════════════════════════════════════════════════════════════════
// 호출 경로: UMG 마우스 이벤트 → 이 함수
// 처리 흐름:
//   1. OnSlotClicked 델리게이트 브로드캐스트 (SlotIndex + MouseEvent)
//   2. 패널에서 좌/우 클릭 분기하여 장착/분리 처리
// ════════════════════════════════════════════════════════════════
FReply UInv_AttachmentSlotWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	const bool bIsLeft = InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton;
	const bool bIsRight = InMouseEvent.GetEffectingButton() == EKeys::RightMouseButton;

	UE_LOG(LogTemp, Log, TEXT("[Attachment UI] 슬롯 %d 클릭됨 (좌/우=%s, 점유=%s)"),
		SlotIndex,
		bIsLeft ? TEXT("좌") : bIsRight ? TEXT("우") : TEXT("기타"),
		bIsOccupied ? TEXT("O") : TEXT("X"));

	// 델리게이트 브로드캐스트 → 패널에서 좌/우 분기 처리
	OnSlotClicked.Broadcast(SlotIndex, InMouseEvent);

	return FReply::Handled();
}

// ════════════════════════════════════════════════════════════════
// 📌 NativeOnMouseEnter/Leave — 마우스 호버 이벤트
// ════════════════════════════════════════════════════════════════
void UInv_AttachmentSlotWidget::NativeOnMouseEnter(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	Super::NativeOnMouseEnter(InGeometry, InMouseEvent);
	OnSlotHovered.Broadcast(SlotIndex);
}

void UInv_AttachmentSlotWidget::NativeOnMouseLeave(const FPointerEvent& InMouseEvent)
{
	Super::NativeOnMouseLeave(InMouseEvent);
	OnSlotUnhovered.Broadcast(SlotIndex);
}
