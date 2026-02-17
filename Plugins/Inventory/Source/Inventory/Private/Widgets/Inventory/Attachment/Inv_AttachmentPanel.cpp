// Gihyeon's Inventory Project
//
// ════════════════════════════════════════════════════════════════════════════════
// 📌 부착물 패널 위젯 (Attachment Panel) — Phase 3 구현
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 핵심 흐름:
//    OpenPanel → PopulateSlots → 슬롯 클릭 → OnSlotClicked → 분리/부착 요청
//    ClosePanel → ClearSlots → 패널 숨기기
//
// ════════════════════════════════════════════════════════════════════════════════

#include "Widgets/Inventory/Attachment/Inv_AttachmentPanel.h"

#include "Widgets/Inventory/Attachment/Inv_AttachmentSlotWidget.h"
#include "Widgets/Inventory/HoverItem/Inv_HoverItem.h"
#include "Items/Inv_InventoryItem.h"
#include "Items/Fragments/Inv_AttachmentFragments.h"
#include "Items/Fragments/Inv_ItemFragment.h"
#include "Items/Fragments/Inv_FragmentTags.h"
#include "InventoryManagement/Components/Inv_InventoryComponent.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"

void UInv_AttachmentPanel::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	if (IsValid(Button_Close))
	{
		Button_Close->OnClicked.AddDynamic(this, &ThisClass::OnCloseButtonClicked);
	}
}

void UInv_AttachmentPanel::OpenPanel(UInv_InventoryItem* InWeaponItem, int32 InWeaponEntryIndex, UInv_InventoryComponent* InInventoryComp)
{
	if (!IsValid(InWeaponItem) || !IsValid(InInventoryComp))
	{
		UE_LOG(LogTemp, Warning, TEXT("[AttachmentPanel] OpenPanel 실패: WeaponItem 또는 InventoryComponent가 nullptr"));
		return;
	}

	// 부착물 슬롯이 없는 아이템이면 무시
	if (!InWeaponItem->HasAttachmentSlots())
	{
		UE_LOG(LogTemp, Warning, TEXT("[AttachmentPanel] OpenPanel 실패: 부착물 슬롯이 없는 아이템"));
		return;
	}

	WeaponItem = InWeaponItem;
	WeaponEntryIndex = InWeaponEntryIndex;
	InventoryComponent = InInventoryComp;
	bIsPanelOpen = true;

	// 무기 이름 표시
	if (IsValid(Text_WeaponName))
	{
		const FInv_TextFragment* TextFrag = InWeaponItem->GetItemManifest().GetFragmentOfType<FInv_TextFragment>();
		if (TextFrag)
		{
			Text_WeaponName->SetText(TextFrag->GetText());
		}
		else
		{
			Text_WeaponName->SetText(FText::FromString(TEXT("무기")));
		}
	}

	// 슬롯 위젯 생성
	PopulateSlots();

	SetVisibility(ESlateVisibility::Visible);

	UE_LOG(LogTemp, Log, TEXT("[AttachmentPanel] 패널 열림: WeaponEntry=%d, 슬롯 수=%d"),
		InWeaponEntryIndex, InWeaponItem->GetAttachmentSlotCount());
}

void UInv_AttachmentPanel::ClosePanel()
{
	ClearSlots();
	bIsPanelOpen = false;
	WeaponItem.Reset();
	WeaponEntryIndex = INDEX_NONE;

	SetVisibility(ESlateVisibility::Collapsed);

	OnPanelClosed.Broadcast();

	UE_LOG(LogTemp, Log, TEXT("[AttachmentPanel] 패널 닫힘"));
}

void UInv_AttachmentPanel::PopulateSlots()
{
	ClearSlots();

	if (!WeaponItem.IsValid()) return;
	if (!SlotWidgetClass)
	{
		UE_LOG(LogTemp, Error, TEXT("[AttachmentPanel] SlotWidgetClass가 설정되지 않음!"));
		return;
	}

	const FInv_AttachmentHostFragment* HostFrag = WeaponItem->GetItemManifest().GetFragmentOfType<FInv_AttachmentHostFragment>();
	if (!HostFrag) return;

	const TArray<FInv_AttachmentSlotDef>& SlotDefs = HostFrag->GetSlotDefinitions();

	for (int32 i = 0; i < SlotDefs.Num(); ++i)
	{
		UInv_AttachmentSlotWidget* SlotWidget = CreateWidget<UInv_AttachmentSlotWidget>(this, SlotWidgetClass);
		if (!IsValid(SlotWidget)) continue;

		// 슬롯 정보 설정
		SlotWidget->SetupSlot(i, SlotDefs[i]);

		// 장착된 부착물이 있으면 아이콘 표시
		const FInv_AttachedItemData* AttachedData = HostFrag->GetAttachedItemData(i);
		if (AttachedData)
		{
			SlotWidget->SetAttachedItem(*AttachedData);
		}

		// 클릭 델리게이트 바인딩
		SlotWidget->OnSlotClicked.AddDynamic(this, &ThisClass::OnSlotClicked);

		// VerticalBox에 추가
		if (IsValid(VerticalBox_Slots))
		{
			VerticalBox_Slots->AddChild(SlotWidget);
		}

		SlotWidgets.Add(SlotWidget);
	}
}

void UInv_AttachmentPanel::ClearSlots()
{
	for (TObjectPtr<UInv_AttachmentSlotWidget>& Widget : SlotWidgets)
	{
		if (IsValid(Widget))
		{
			Widget->RemoveFromParent();
		}
	}
	SlotWidgets.Empty();
}

bool UInv_AttachmentPanel::TryDropHoverItemOnSlot(UInv_HoverItem* HoverItem)
{
	if (!bIsPanelOpen || !IsValid(HoverItem)) return false;
	if (!WeaponItem.IsValid() || !InventoryComponent.IsValid()) return false;

	UInv_InventoryItem* AttachmentItem = HoverItem->GetInventoryItem();
	if (!IsValid(AttachmentItem)) return false;

	// 부착물 아이템인지 확인
	if (!AttachmentItem->IsAttachableItem()) return false;

	const FInv_AttachableFragment* AttachableFrag = AttachmentItem->GetItemManifest().GetFragmentOfType<FInv_AttachableFragment>();
	if (!AttachableFrag) return false;

	// 호환되는 빈 슬롯 찾기
	const FInv_AttachmentHostFragment* HostFrag = WeaponItem->GetItemManifest().GetFragmentOfType<FInv_AttachmentHostFragment>();
	if (!HostFrag) return false;

	const TArray<FInv_AttachmentSlotDef>& SlotDefs = HostFrag->GetSlotDefinitions();
	for (int32 i = 0; i < SlotDefs.Num(); ++i)
	{
		if (HostFrag->IsSlotOccupied(i)) continue;

		if (AttachableFrag->CanAttachToSlot(SlotDefs[i]))
		{
			// 호환되는 빈 슬롯 발견 → 부착 요청
			int32 AttachmentEntryIndex = HoverItem->GetEntryIndex();

			UE_LOG(LogTemp, Log, TEXT("[AttachmentPanel] HoverItem 드롭 → 부착 요청: WeaponEntry=%d, AttachmentEntry=%d, Slot=%d"),
				WeaponEntryIndex, AttachmentEntryIndex, i);

			OnAttachRequested.ExecuteIfBound(WeaponEntryIndex, AttachmentEntryIndex, i);
			return true;
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("[AttachmentPanel] HoverItem 드롭 실패: 호환되는 빈 슬롯 없음"));
	return false;
}

void UInv_AttachmentPanel::OnSlotClicked(int32 SlotIndex)
{
	if (!WeaponItem.IsValid()) return;

	const FInv_AttachmentHostFragment* HostFrag = WeaponItem->GetItemManifest().GetFragmentOfType<FInv_AttachmentHostFragment>();
	if (!HostFrag) return;

	// 장착된 부착물이 있으면 분리 요청
	if (HostFrag->IsSlotOccupied(SlotIndex))
	{
		UE_LOG(LogTemp, Log, TEXT("[AttachmentPanel] 슬롯 %d 클릭 → 분리 요청 (WeaponEntry=%d)"),
			SlotIndex, WeaponEntryIndex);

		OnDetachRequested.ExecuteIfBound(WeaponEntryIndex, SlotIndex);
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("[AttachmentPanel] 슬롯 %d 클릭 → 빈 슬롯 (HoverItem으로 부착 가능)"), SlotIndex);
	}
}

void UInv_AttachmentPanel::OnCloseButtonClicked()
{
	ClosePanel();
}
