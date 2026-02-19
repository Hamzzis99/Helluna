// Gihyeon's Inventory Project
//
// ════════════════════════════════════════════════════════════════════════════════
// 📌 부착물 패널 위젯 (Attachment Panel) — Phase 8 리뉴얼
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 이 파일의 역할:
//    무기의 부착물 슬롯을 십자형 레이아웃으로 보여주는 오버레이 패널
//    중앙에 3D 무기 프리뷰, 상하좌우에 부착물 슬롯 배치
//
// 📌 동작 흐름:
//    1. InventoryGrid::OnPopUpMenuAttachment → OpenAttachmentPanel 호출
//    2. SetInventoryComponent / SetOwningGrid로 참조 설정
//    3. OpenForWeapon(WeaponItem, EntryIndex) → SetupWeaponPreview + BuildSlotWidgets
//    4. 슬롯 좌클릭 + HoverItem → TryAttachHoverItem(장착)
//    5. 슬롯 우클릭 + Occupied → TryDetachItem(분리)
//    6. NativeTick → UpdateSlotHighlights + 드래그 회전 처리
//    7. 닫기 버튼 → ClosePanel() → CleanupWeaponPreview
//
// 📌 계층 구조 (WBP에서 생성):
//    Border_Background                ← UBorder (배경)
//     └─ VerticalBox_Main             ← UVerticalBox
//          ├─ HorizontalBox_Header    ← UHorizontalBox
//          │    ├─ Image_WeaponIcon     ← UImage ★ BindWidget
//          │    ├─ Text_WeaponName      ← UTextBlock ★ BindWidget
//          │    └─ Button_Close         ← UButton ★ BindWidget
//          │
//          ├─ VerticalBox_Top           ← UVerticalBox ★ BindWidget (상단 슬롯: 스코프)
//          ├─ HorizontalBox_Middle      ← UHorizontalBox
//          │    ├─ VerticalBox_Left       ← UVerticalBox ★ BindWidget (좌측: 그립)
//          │    ├─ Image_WeaponPreview    ← UImage ★ BindWidget (3D 프리뷰)
//          │    └─ VerticalBox_Right      ← UVerticalBox ★ BindWidget (우측: 레이저)
//          └─ VerticalBox_Bottom        ← UVerticalBox ★ BindWidget (하단: 탄창)
//
// 📌 3D 프리뷰:
//    AInv_WeaponPreviewActor를 Z=-10000에 스폰
//    SceneCaptureComponent2D → RenderTarget → Image_WeaponPreview에 표시
//    마우스 드래그로 무기 회전 가능
//
// ════════════════════════════════════════════════════════════════════════════════

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameplayTagContainer.h"
#include "Inv_AttachmentPanel.generated.h"

class UInv_InventoryItem;
class UInv_InventoryComponent;
class UInv_InventoryGrid;
class UInv_AttachmentSlotWidget;
class UInv_HoverItem;
class UVerticalBox;
class UImage;
class UButton;
class UTextBlock;
class AInv_WeaponPreviewActor;
enum class EInv_AttachmentSlotPosition : uint8;

// 패널 닫기 델리게이트 (InventoryGrid에서 정리 작업용)
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FAttachmentPanelClosed);

UCLASS()
class INVENTORY_API UInv_AttachmentPanel : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// ── 마우스 이벤트 (드래그 회전) ──
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnMouseLeave(const FPointerEvent& InMouseEvent) override;

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

	// ── Phase 8: 십자형 레이아웃 BindWidget ──
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UVerticalBox> VerticalBox_Top;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UVerticalBox> VerticalBox_Bottom;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UVerticalBox> VerticalBox_Left;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UVerticalBox> VerticalBox_Right;

	// 중앙 무기 3D 프리뷰 이미지
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> Image_WeaponPreview;

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

	// ── Phase 8: 3D 프리뷰 ──
	UPROPERTY()
	TWeakObjectPtr<AInv_WeaponPreviewActor> WeaponPreviewActor;

	// 프리뷰 액터 스폰 Z 위치 (월드 아래쪽, 카메라에 안 잡힘)
	static constexpr float PreviewSpawnZ = -10000.f;

	// ── Phase 8: 드래그 회전 ──
	bool bIsDragging = false;
	FVector2D DragCurrentPosition = FVector2D::ZeroVector;
	FVector2D DragLastPosition = FVector2D::ZeroVector;

	// ── 내부 함수 ──

	// 슬롯 위젯 생성 및 십자형 레이아웃에 배치
	void BuildSlotWidgets();

	// 슬롯 위젯 전부 정리
	void ClearSlotWidgets();

	// 4방향 VerticalBox 자식 전부 정리
	void ClearAllSlotContainers();

	// SlotPosition에 해당하는 VerticalBox 반환
	UVerticalBox* GetContainerForPosition(EInv_AttachmentSlotPosition Position) const;

	// SlotType 태그에서 UI 배치 위치 자동 추론
	EInv_AttachmentSlotPosition DerivePositionFromSlotType(const FGameplayTag& SlotType) const;

	// 무기 3D 프리뷰 설정/정리
	void SetupWeaponPreview();
	void CleanupWeaponPreview();

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
