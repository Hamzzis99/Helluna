// Gihyeon's Inventory Project
//
// ════════════════════════════════════════════════════════════════════════════════
// 📌 부착물 슬롯 위젯 (Attachment Slot Widget) — Phase 3
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 이 파일의 역할:
//    부착물 슬롯 1개를 표시하는 UI 위젯
//    AttachmentPanel의 자식으로 사용되며, 슬롯 정보 + 장착된 부착물 아이콘 표시
//
// 📌 BindWidget 규칙:
//    WBP에서 동일 이름의 위젯을 배치해야 함
//    - Image_SlotIcon: 빈 슬롯 아이콘 또는 장착된 부착물 아이콘
//    - Text_SlotName: 슬롯 이름 ("스코프 슬롯" 등)
//    - Button_Slot: 클릭 감지용 버튼 (오버레이 방식)
//
// ════════════════════════════════════════════════════════════════════════════════

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameplayTagContainer.h"
#include "Inv_AttachmentSlotWidget.generated.h"

class UImage;
class UTextBlock;
class UButton;
struct FInv_AttachmentSlotDef;
struct FInv_AttachedItemData;

// 슬롯 클릭 델리게이트 (슬롯 인덱스 전달)
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FAttachmentSlotClicked, int32, SlotIndex);

// 슬롯에 부착물 드롭 델리게이트 (HoverItem을 슬롯에 놓을 때)
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FAttachmentSlotDropRequested, int32, SlotIndex);

UCLASS()
class INVENTORY_API UInv_AttachmentSlotWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeOnInitialized() override;

	// 슬롯 정보 설정 (AttachmentPanel에서 호출)
	void SetupSlot(int32 InSlotIndex, const FInv_AttachmentSlotDef& SlotDef);

	// 장착된 부착물 표시 업데이트
	void SetAttachedItem(const FInv_AttachedItemData& AttachedData);

	// 빈 슬롯 상태로 초기화
	void ClearAttachedItem();

	// 슬롯 정보 접근
	int32 GetSlotIndex() const { return SlotIndex; }
	FGameplayTag GetSlotType() const { return SlotType; }
	bool IsOccupied() const { return bIsOccupied; }

	// 델리게이트
	FAttachmentSlotClicked OnSlotClicked;
	FAttachmentSlotDropRequested OnSlotDropRequested;

private:
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> Image_SlotIcon;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> Text_SlotName;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> Button_Slot;

	// 슬롯 상태
	int32 SlotIndex{INDEX_NONE};
	FGameplayTag SlotType;
	bool bIsOccupied{false};

	// 빈 슬롯 기본 브러시 (에디터에서 설정)
	UPROPERTY(EditAnywhere, Category = "Attachment", meta = (DisplayName = "빈 슬롯 아이콘", Tooltip = "부착물이 없을 때 표시할 기본 아이콘"))
	FSlateBrush Brush_EmptySlot;

	UFUNCTION()
	void OnButtonClicked();
};
