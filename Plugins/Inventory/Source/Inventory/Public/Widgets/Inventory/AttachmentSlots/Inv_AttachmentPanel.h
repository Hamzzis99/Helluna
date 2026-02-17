// Gihyeon's Inventory Project
//
// ════════════════════════════════════════════════════════════════════════════════
// 📌 부착물 패널 위젯 (Attachment Panel) — Phase 3
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 이 파일의 역할:
//    무기의 부착물 슬롯을 2열 그리드(UniformGridPanel)로 보여주는 오버레이 패널
//    Inv_InventoryGrid의 팝업 메뉴에서 "부착물 관리" 버튼 클릭 시 열림
//
// 📌 동작 흐름:
//    1. InventoryGrid::OnPopUpMenuAttachment → OpenAttachmentPanel 호출
//    2. SetInventoryComponent / SetOwningGrid로 참조 설정
//    3. OpenForWeapon(WeaponItem, EntryIndex) → BuildSlotWidgets()
//    4. 슬롯 좌클릭 + HoverItem → TryAttachHoverItem(장착)
//    5. 슬롯 우클릭 + Occupied → TryDetachItem(분리)
//    6. NativeTick → UpdateSlotHighlights (HoverItem 호환 슬롯 실시간 하이라이트)
//    7. 닫기 버튼 → ClosePanel()
//
// 📌 계층 구조 (WBP에서 생성):
//    Border_Background             ← UBorder (배경)
//     └─ VerticalBox_Main          ← UVerticalBox
//          ├─ HorizontalBox_Header ← UHorizontalBox
//          │    ├─ Image_WeaponIcon  ← UImage ★ BindWidget
//          │    ├─ Text_WeaponName   ← UTextBlock ★ BindWidget
//          │    └─ Button_Close      ← UButton ★ BindWidget
//          │
//          └─ UniformGridPanel_Slots ← UUniformGridPanel ★ BindWidget (2열 자동 격자!)
//
// 📌 UniformGridPanel 동작:
//    SlotWidget 추가 시 → AddChildToUniformGrid(Widget, Row, Column)
//    Row = i / 2, Column = i % 2 → 자동 2열 배치
//
// ════════════════════════════════════════════════════════════════════════════════

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Inv_AttachmentPanel.generated.h"

class UInv_InventoryItem;
class UInv_InventoryComponent;
class UInv_InventoryGrid;
class UInv_AttachmentSlotWidget;
class UInv_HoverItem;
class UUniformGridPanel;
class UImage;
class UButton;
class UTextBlock;

// 패널 닫기 델리게이트 (InventoryGrid에서 정리 작업용)
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FAttachmentPanelClosed);

UCLASS()
class INVENTORY_API UInv_AttachmentPanel : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// ── 패널 열기/닫기 ──

	// ════════════════════════════════════════════════════════════════
	// 📌 OpenForWeapon — 무기 아이템의 부착물 슬롯을 패널에 표시
	// ════════════════════════════════════════════════════════════════
	void OpenForWeapon(UInv_InventoryItem* WeaponItem, int32 WeaponEntryIndex);
	void ClosePanel();
	bool IsOpen() const { return bIsOpen; }

	// ── 참조 설정 ──
	void SetInventoryComponent(UInv_InventoryComponent* InvComp);
	void SetOwningGrid(UInv_InventoryGrid* Grid);

	// ── 외부에서 슬롯 상태 갱신 요청 ──
	void RefreshSlotStates();

	// 현재 표시 중인 무기 아이템 접근
	UInv_InventoryItem* GetWeaponItem() const { return CurrentWeaponItem.Get(); }
	int32 GetWeaponEntryIndex() const { return CurrentWeaponEntryIndex; }

	// 델리게이트
	FAttachmentPanelClosed OnPanelClosed;

private:
	// ── BindWidget ──
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> Text_WeaponName;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> Image_WeaponIcon;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UUniformGridPanel> UniformGridPanel_Slots;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> Button_Close;

	// 슬롯 위젯 클래스 (WBP에서 할당)
	UPROPERTY(EditAnywhere, Category = "Attachment", meta = (DisplayName = "슬롯 위젯 클래스", Tooltip = "WBP_Inv_AttachmentSlotWidget 블루프린트 클래스"))
	TSubclassOf<UInv_AttachmentSlotWidget> AttachmentSlotWidgetClass;

	// 생성된 슬롯 위젯 배열
	UPROPERTY()
	TArray<TObjectPtr<UInv_AttachmentSlotWidget>> SlotWidgets;

	// 현재 열려있는 무기 정보
	TWeakObjectPtr<UInv_InventoryItem> CurrentWeaponItem;
	int32 CurrentWeaponEntryIndex = INDEX_NONE;
	bool bIsOpen = false;

	// 참조
	TWeakObjectPtr<UInv_InventoryComponent> InventoryComponent;
	TWeakObjectPtr<UInv_InventoryGrid> OwningGrid;

	// ── 내부 함수 ──

	// 슬롯 위젯 생성 및 UniformGridPanel에 배치
	void BuildSlotWidgets();

	// 슬롯 위젯 전부 정리
	void ClearSlotWidgets();

	// Tick에서 호출: HoverItem 호환 슬롯 실시간 하이라이트
	void UpdateSlotHighlights();

	// 슬롯 클릭 콜백 (좌클릭=장착, 우클릭=분리)
	UFUNCTION()
	void OnSlotClicked(int32 SlotIndex, const FPointerEvent& MouseEvent);

	// 좌클릭 + HoverItem → 부착물 장착 시도
	void TryAttachHoverItem(int32 SlotIndex);

	// 우클릭 → 부착물 분리 시도
	void TryDetachItem(int32 SlotIndex);

	// 닫기 버튼 클릭
	UFUNCTION()
	void OnCloseButtonClicked();

	// ── EntryIndex 동기화 ──
	// 부착물 제거 시 InventoryList에서 아이템이 삭제되어 EntryIndex 밀림 가능
	// 무기 포인터로 현재 EntryIndex 재검색
	int32 FindCurrentWeaponEntryIndex() const;
};
