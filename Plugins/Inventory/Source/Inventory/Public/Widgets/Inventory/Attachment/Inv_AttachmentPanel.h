// Gihyeon's Inventory Project
//
// ════════════════════════════════════════════════════════════════════════════════
// 📌 부착물 패널 위젯 (Attachment Panel) — Phase 3
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 이 파일의 역할:
//    무기 아이템의 모든 부착물 슬롯을 표시하는 오버레이 패널
//    Inv_InventoryGrid의 팝업 메뉴에서 "부착물 관리" 버튼 클릭 시 열림
//
// 📌 동작 흐름:
//    1. InventoryGrid::OnPopUpMenuAttachment → OpenPanel() 호출
//    2. OpenPanel(WeaponItem, EntryIndex) → 무기의 AttachmentHostFragment 읽기
//    3. SlotDefinitions 순회 → AttachmentSlotWidget 동적 생성 → VerticalBox에 추가
//    4. 슬롯 클릭 시 → 분리(Detach) 요청 or 드래그 부착(Attach) 요청
//    5. 닫기 버튼 또는 패널 밖 클릭 → ClosePanel()
//
// 📌 BindWidget 규칙:
//    - VerticalBox_Slots: 슬롯 위젯들이 동적으로 추가되는 컨테이너
//    - Button_Close: 패널 닫기 버튼
//    - Text_WeaponName: 무기 이름 표시
//
// ════════════════════════════════════════════════════════════════════════════════

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Inv_AttachmentPanel.generated.h"

class UInv_InventoryItem;
class UInv_InventoryComponent;
class UInv_AttachmentSlotWidget;
class UInv_HoverItem;
class UVerticalBox;
class UButton;
class UTextBlock;

// 패널 닫기 델리게이트
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FAttachmentPanelClosed);

// 분리 요청 델리게이트 (InventoryGrid에서 처리)
DECLARE_DYNAMIC_DELEGATE_TwoParams(FAttachmentDetachRequested, int32, WeaponEntryIndex, int32, SlotIndex);

// 부착 요청 델리게이트 (HoverItem을 슬롯에 놓을 때)
DECLARE_DYNAMIC_DELEGATE_ThreeParams(FAttachmentAttachRequested, int32, WeaponEntryIndex, int32, AttachmentEntryIndex, int32, SlotIndex);

UCLASS()
class INVENTORY_API UInv_AttachmentPanel : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeOnInitialized() override;

	// 패널 열기 — 무기 아이템의 부착물 슬롯을 표시
	void OpenPanel(UInv_InventoryItem* WeaponItem, int32 WeaponEntryIndex, UInv_InventoryComponent* InInventoryComp);

	// 패널 닫기 — 슬롯 위젯 정리 및 패널 숨기기
	void ClosePanel();

	// 패널이 열려있는지 확인
	bool IsPanelOpen() const { return bIsPanelOpen; }

	// HoverItem을 슬롯에 드롭하려는 시도 (InventoryGrid에서 호출)
	bool TryDropHoverItemOnSlot(UInv_HoverItem* HoverItem);

	// 현재 표시 중인 무기 아이템
	UInv_InventoryItem* GetWeaponItem() const { return WeaponItem.Get(); }
	int32 GetWeaponEntryIndex() const { return WeaponEntryIndex; }

	// 델리게이트
	FAttachmentPanelClosed OnPanelClosed;
	FAttachmentDetachRequested OnDetachRequested;
	FAttachmentAttachRequested OnAttachRequested;

private:
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UVerticalBox> VerticalBox_Slots;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> Button_Close;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> Text_WeaponName;

	// 슬롯 위젯 클래스 (에디터에서 WBP 지정)
	UPROPERTY(EditAnywhere, Category = "Attachment", meta = (DisplayName = "슬롯 위젯 클래스", Tooltip = "부착물 슬롯 위젯 블루프린트 클래스"))
	TSubclassOf<UInv_AttachmentSlotWidget> SlotWidgetClass;

	// 동적 생성된 슬롯 위젯 배열
	UPROPERTY()
	TArray<TObjectPtr<UInv_AttachmentSlotWidget>> SlotWidgets;

	// 상태
	bool bIsPanelOpen{false};
	TWeakObjectPtr<UInv_InventoryItem> WeaponItem;
	int32 WeaponEntryIndex{INDEX_NONE};
	TWeakObjectPtr<UInv_InventoryComponent> InventoryComponent;

	// 슬롯 위젯 생성 및 배치
	void PopulateSlots();

	// 슬롯 위젯 전부 정리
	void ClearSlots();

	// 슬롯 클릭 콜백 (장착된 부착물이면 분리 요청)
	UFUNCTION()
	void OnSlotClicked(int32 SlotIndex);

	// 닫기 버튼 클릭
	UFUNCTION()
	void OnCloseButtonClicked();
};
