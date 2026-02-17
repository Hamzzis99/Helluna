// Gihyeon's Inventory Project
//
// ════════════════════════════════════════════════════════════════════════════════
// 📌 부착물 슬롯 위젯 (Attachment Slot Widget) — Phase 3
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 이 파일의 역할:
//    부착물 패널 내 슬롯 1개 (타일 + 아래 텍스트)
//    AttachmentPanel의 UniformGridPanel에 동적 추가되어 2열 격자를 구성
//
// 📌 계층 구조 (WBP에서 생성):
//    VerticalBox_Root          ← UVerticalBox
//     ├─ Overlay_Tile          ← UOverlay (TileSize x TileSize)
//     │    ├─ Image_Background ← UImage (T_Unoccupied_Square / T_Occupied_Square)
//     │    ├─ Image_ItemIcon   ← UImage (부착물 아이콘, 기본 Collapsed)
//     │    └─ Image_Highlight  ← UImage (호환 슬롯 하이라이트, 기본 Collapsed)
//     └─ Text_SlotName         ← UTextBlock ("스코프", "총구")
//
// 📌 BindWidget 목록 (C++과 이름 정확히 일치해야 함):
//    - Image_Background : UImage
//    - Image_ItemIcon   : UImage
//    - Image_Highlight  : UImage
//    - Text_SlotName    : UTextBlock
//
// 📌 마우스 상호작용:
//    - NativeOnMouseButtonDown → 좌클릭(장착) / 우클릭(분리) 분기
//    - NativeOnMouseEnter/Leave → 호버 이벤트 (패널에서 하이라이트 갱신용)
//
// 📌 브러시 설정 (에디터에서 기존 GridSlot 텍스처 재사용):
//    Brush_Empty     → T_Unoccupied_Square
//    Brush_Occupied  → T_Occupied_Square
//    Brush_Highlight → T_Selected_Square
//
// ════════════════════════════════════════════════════════════════════════════════

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameplayTagContainer.h"
#include "Inv_AttachmentSlotWidget.generated.h"

class UImage;
class UTextBlock;
struct FInv_AttachmentSlotDef;
struct FInv_AttachedItemData;

// 슬롯 클릭 델리게이트 (슬롯 인덱스 + 마우스 이벤트 전달)
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FAttachmentSlotClicked, int32, SlotIndex, const FPointerEvent&, MouseEvent);

// 슬롯 호버 델리게이트 (패널에서 하이라이트 갱신용)
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FAttachmentSlotHovered, int32, SlotIndex);

UCLASS()
class INVENTORY_API UInv_AttachmentSlotWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// ════════════════════════════════════════════════════════════════
	// 📌 초기화 — 슬롯 정의 + 현재 장착 데이터로 UI 설정
	// ════════════════════════════════════════════════════════════════
	// 호출 경로: AttachmentPanel::BuildSlotWidgets → 이 함수
	// 처리 흐름:
	//   1. SlotIndex, SlotType 캐시
	//   2. Text_SlotName에 SlotDef.SlotDisplayName 설정
	//   3. Image_Background에 Brush_Empty 설정
	//   4. AttachedData가 있으면 → SetOccupied() 호출
	//   5. 없으면 → SetEmpty()
	// ════════════════════════════════════════════════════════════════
	void InitSlot(int32 InSlotIndex,
				  const FInv_AttachmentSlotDef& SlotDef,
				  const FInv_AttachedItemData* AttachedData = nullptr);

	// ── 상태 변경 ──
	void SetOccupied(const FInv_AttachedItemData& Data);    // 부착물 장착됨 → 아이콘 표시
	void SetEmpty();                                          // 빈 슬롯으로 복귀
	void SetHighlighted(bool bHighlight);                     // 호환 슬롯 테두리 강조

	// ── Getter ──
	int32 GetSlotIndex() const { return SlotIndex; }
	bool IsOccupied() const { return bIsOccupied; }
	FGameplayTag GetSlotType() const { return SlotType; }

	// ── 마우스 이벤트 ──
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnMouseEnter(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnMouseLeave(const FPointerEvent& InMouseEvent) override;

	// ── 델리게이트 ──
	FAttachmentSlotClicked OnSlotClicked;
	FAttachmentSlotHovered OnSlotHovered;
	FAttachmentSlotHovered OnSlotUnhovered;

private:
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> Image_Background;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> Image_ItemIcon;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> Image_Highlight;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> Text_SlotName;

	// ── 브러시 (에디터에서 설정, 기존 GridSlot 텍스처 재사용) ──
	UPROPERTY(EditAnywhere, Category = "Attachment|Style", meta = (DisplayName = "빈 슬롯 브러시", Tooltip = "T_Unoccupied_Square"))
	FSlateBrush Brush_Empty;

	UPROPERTY(EditAnywhere, Category = "Attachment|Style", meta = (DisplayName = "점유 슬롯 브러시", Tooltip = "T_Occupied_Square"))
	FSlateBrush Brush_Occupied;

	UPROPERTY(EditAnywhere, Category = "Attachment|Style", meta = (DisplayName = "하이라이트 브러시", Tooltip = "T_Selected_Square"))
	FSlateBrush Brush_Highlight;

	// ── 슬롯 상태 ──
	int32 SlotIndex = INDEX_NONE;
	bool bIsOccupied = false;
	FGameplayTag SlotType;  // 호환성 체크용 캐시
};
