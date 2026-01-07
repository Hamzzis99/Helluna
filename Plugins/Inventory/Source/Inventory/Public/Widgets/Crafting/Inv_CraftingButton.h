// Gihyeon's Inventory Project

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameplayTagContainer.h"
#include "Inv_CraftingButton.generated.h"

class UButton;
class UImage;
class UTextBlock;
class UHorizontalBox;
class UInv_InventoryItem;

/**
 * 크래프팅 메뉴에서 개별 아이템 제작 버튼 위젯
 * Building 시스템과 동일한 구조 사용
 */
UCLASS()
class INVENTORY_API UInv_CraftingButton : public UUserWidget
{
	GENERATED_BODY()

public:
	// 제작 아이템 정보 설정 (Blueprint에서 호출 가능)
	UFUNCTION(BlueprintCallable, Category = "Crafting")
	void SetCraftingInfo(const FText& Name, UTexture2D* Icon, TSubclassOf<AActor> ItemActorClass);

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	// 버튼 클릭 이벤트
	UFUNCTION()
	void OnButtonClicked();

	// 재료 체크 함수 (Building과 동일)
	bool HasRequiredMaterials();
	void UpdateButtonState();
	
	// 재료 UI 업데이트 (이미지 표시/숨김)
	void UpdateMaterialUI();

	// 재료 소비 함수 (Building과 동일한 방식)
	void ConsumeMaterials();

	// 제작 완료 후 아이템을 인벤토리에 추가
	void AddCraftedItemToInventory();

	// 델리게이트 바인딩/언바인딩
	void BindInventoryDelegates();
	void UnbindInventoryDelegates();

	// 인벤토리 변경 시 호출될 콜백 함수들
	UFUNCTION()
	void OnInventoryItemAdded(UInv_InventoryItem* Item);

	UFUNCTION()
	void OnInventoryItemRemoved(UInv_InventoryItem* Item);

	UFUNCTION()
	void OnInventoryStackChanged(const FInv_SlotAvailabilityResult& Result);

	// === 블루프린트에서 바인딩할 위젯들 (meta = (BindWidget)) ===
	
	// 클릭 가능한 버튼
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> Button_Main;

	// 제작할 아이템 아이콘 이미지
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> Image_ItemIcon;

	// 제작할 아이템 이름 텍스트
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> Text_ItemName;

	// === 재료 UI 컨테이너 및 위젯 (반드시 Blueprint에 있어야 함) ===
	
	// 재료 1 컨테이너 (HorizontalBox) - 이미지 + 텍스트를 감쌈
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UHorizontalBox> HorizontalBox_Material1;

	// 재료 1 이미지
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> Image_Material1;

	// 재료 1 개수 텍스트
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> Text_Material1Amount;

	// 재료 2 컨테이너 (HorizontalBox) - 이미지 + 텍스트를 감쌈
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UHorizontalBox> HorizontalBox_Material2;

	// 재료 2 이미지
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> Image_Material2;

	// 재료 2 개수 텍스트
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> Text_Material2Amount;

	// 재료 3 컨테이너 (HorizontalBox) - 이미지 + 텍스트를 감쌈
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UHorizontalBox> HorizontalBox_Material3;

	// 재료 3 이미지
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> Image_Material3;

	// 재료 3 개수 텍스트
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> Text_Material3Amount;

	// === 제작 아이템 정보 (블루프린트에서 설정 가능) ===
	
	// 제작할 아이템 이름
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crafting", meta = (AllowPrivateAccess = "true"))
	FText ItemName;

	// 제작할 아이템 아이콘
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crafting", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UTexture2D> ItemIcon;

	// 제작할 아이템 액터 클래스 (PickUp Actor)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crafting", meta = (AllowPrivateAccess = "true"))
	TSubclassOf<AActor> ItemActorClass;

	// === 재료 정보 (Building과 동일한 구조) ===

	// 필요한 재료 1 태그 (Craftables 중 선택)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crafting|Materials", meta = (AllowPrivateAccess = "true", Categories = "GameItems.Craftables"))
	FGameplayTag RequiredMaterialTag;

	// 필요한 재료 1 개수
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crafting|Materials", meta = (AllowPrivateAccess = "true"))
	int32 RequiredAmount = 0;

	// 필요한 재료 1 아이콘 (Blueprint에서 직접 설정)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crafting|Materials", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UTexture2D> MaterialIcon1;

	// 필요한 재료 2 태그 (Craftables 중 선택, None이면 미사용)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crafting|Materials", meta = (AllowPrivateAccess = "true", Categories = "GameItems.Craftables"))
	FGameplayTag RequiredMaterialTag2;

	// 필요한 재료 2 개수
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crafting|Materials", meta = (AllowPrivateAccess = "true"))
	int32 RequiredAmount2 = 0;

	// 필요한 재료 2 아이콘 (Blueprint에서 직접 설정)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crafting|Materials", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UTexture2D> MaterialIcon2;

	// 필요한 재료 3 태그 (Craftables 중 선택, None이면 미사용)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crafting|Materials", meta = (AllowPrivateAccess = "true", Categories = "GameItems.Craftables"))
	FGameplayTag RequiredMaterialTag3;

	// 필요한 재료 3 개수
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crafting|Materials", meta = (AllowPrivateAccess = "true"))
	int32 RequiredAmount3 = 0;

	// 필요한 재료 3 아이콘 (Blueprint에서 직접 설정)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crafting|Materials", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UTexture2D> MaterialIcon3;

	// === 버튼 쿨다운 (연타 방지) ===

	// 제작 쿨다운 시간 (초 단위, 블루프린트에서 수정 가능)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crafting|Settings", meta = (AllowPrivateAccess = "true", ClampMin = "0.0", ClampMax = "5.0"))
	float CraftingCooldown = 0.5f;

	// 마지막 제작 시간 (내부 사용)
	float LastCraftTime = 0.f;
};

