// File: Plugins/Inventory/Source/Inventory/Public/Widgets/Inventory/Container/Inv_ContainerWidget.h
// ════════════════════════════════════════════════════════════════════════════════
// UInv_ContainerWidget — 듀얼 Grid 컨테이너 UI
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 역할:
//    상자/사체 루팅 시 표시되는 듀얼 Grid UI
//    왼쪽: 컨테이너 Grid, 오른쪽: 플레이어 Grid
//
// 📌 BP 바인딩:
//    WBP_Inv_ContainerWidget에서 BindWidget으로 연결
//    ContainerGrid, PlayerGrid, Text_ContainerName 필수
//    Button_TakeAll 선택 (BindWidgetOptional)
//
// 작성자: Gihyeon (Claude Code 보조)
// ════════════════════════════════════════════════════════════════════════════════

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Inv_ContainerWidget.generated.h"

class UInv_InventoryGrid;
class UInv_InventoryComponent;
class UInv_LootContainerComponent;
class UTextBlock;
class UButton;

UCLASS()
class INVENTORY_API UInv_ContainerWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/**
	 * 컨테이너와 플레이어 InvComp로 초기화
	 *
	 * @param InContainerComp  컨테이너 컴포넌트
	 * @param InPlayerComp     플레이어 인벤토리 컴포넌트
	 */
	UFUNCTION(BlueprintCallable, Category = "Container|UI",
		meta = (DisplayName = "Initialize Panels (패널 초기화)"))
	void InitializePanels(UInv_LootContainerComponent* InContainerComp,
		UInv_InventoryComponent* InPlayerComp);

	/** UI 정리 (닫기 시 호출) */
	UFUNCTION(BlueprintCallable, Category = "Container|UI",
		meta = (DisplayName = "Cleanup Panels (패널 정리)"))
	void CleanupPanels();

	/** 바인딩된 컨테이너 컴포넌트 반환 */
	UInv_LootContainerComponent* GetContainerComponent() const { return CachedContainerComp.Get(); }

protected:
	virtual void NativeConstruct() override;

	// ═══════════════════════════════════════════
	// BindWidget — BP에서 연결
	// ═══════════════════════════════════════════

	/** 컨테이너 Grid (왼쪽) */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UInv_InventoryGrid> ContainerGrid;

	/** 플레이어 Grid (오른쪽) */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UInv_InventoryGrid> PlayerGrid;

	/** 컨테이너 이름 텍스트 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> Text_ContainerName;

	/** 전체 가져오기 버튼 (선택적) */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> Button_TakeAll;

private:
	UFUNCTION()
	void OnTakeAllClicked();

	TWeakObjectPtr<UInv_LootContainerComponent> CachedContainerComp;
	TWeakObjectPtr<UInv_InventoryComponent> CachedPlayerComp;
};
