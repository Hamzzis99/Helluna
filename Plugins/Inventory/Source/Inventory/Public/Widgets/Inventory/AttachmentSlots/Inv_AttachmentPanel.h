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
// 📌 계층 구조 (WBP에서 생성 — 배그 스타일 가로형):
//    CanvasPanel (Root)               ← 화면 내 위치 제어 (앵커 + 오프셋)
//     └─ Overlay                      ← 배경 + 콘텐츠 겹치기
//          ├─ Image "Border_Background"  ← 배경 텍스처 (Fill/Fill)
//          └─ VerticalBox_Main           ← 전체 콘텐츠
//               ├─ HorizontalBox_Header  ← 헤더
//               │    ├─ Image_WeaponIcon   ★ BindWidget
//               │    ├─ Text_WeaponName    ★ BindWidget
//               │    └─ Button_Close       ★ BindWidget
//               │
//               └─ HorizontalBox_Body    ← 좌: 슬롯 리스트 / 우: 3D 프리뷰
//                    ├─ VerticalBox_Slots   ← 부착물 슬롯 세로 배치
//                    │    ├─ SlotWidget (Scope)
//                    │    ├─ SlotWidget (Muzzle)
//                    │    ├─ SlotWidget (Grip)
//                    │    └─ SlotWidget (Magazine)
//                    └─ Image_WeaponPreview ★ BindWidget (3D 프리뷰, Fill)
//
//    ※ WBP에 배치된 UInv_AttachmentSlotWidget을 WidgetTree에서 자동 수집
//      각 슬롯 위젯의 Details에서 SlotType(GameplayTag) 설정 필요
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

// 패널 닫기 델리게이트 (InventoryGrid에서 정리 작업용)
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FAttachmentPanelClosed);

UCLASS()
class INVENTORY_API UInv_AttachmentPanel : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
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
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_WeaponName;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> Image_WeaponIcon;

	// 중앙 무기 3D 프리뷰 이미지
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> Image_WeaponPreview;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> Button_Close;

	// WidgetTree에서 수집한 전체 슬롯 위젯 (NativeOnInitialized에서 1회 수집)
	// WBP에 배치된 UInv_AttachmentSlotWidget을 자동으로 찾아 저장
	UPROPERTY()
	TArray<TObjectPtr<UInv_AttachmentSlotWidget>> CollectedSlotWidgets;

	// 생성된 슬롯 위젯 배열 (인덱스 = SlotDef 인덱스)
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

	// NativeConstruct에서 캐싱한 WBP의 원본 ImageSize (SetupWeaponPreview에서 복원용)
	FVector2D CachedPreviewImageSize = FVector2D::ZeroVector;

	// ── Phase 8: 드래그 회전 ──
	bool bIsDragging = false;
	FVector2D DragCurrentPosition = FVector2D::ZeroVector;
	FVector2D DragLastPosition = FVector2D::ZeroVector;

	// ── 내부 함수 ──

	// 슬롯 위젯 생성 및 십자형 레이아웃에 배치
	void BuildSlotWidgets();

	// 슬롯 위젯 전부 정리
	void ClearSlotWidgets();

	// 수집된 슬롯 전부 Hidden + SetEmpty (패널 열 때 초기화용)
	void ResetAllSlots();

	// WidgetTree 순회하여 UInv_AttachmentSlotWidget 전부 수집
	void CollectSlotWidgetsFromTree();

	// SlotType 태그로 수집된 슬롯 위젯 검색 (없으면 nullptr)
	UInv_AttachmentSlotWidget* FindSlotWidgetByTag(const FGameplayTag& SlotType) const;

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
