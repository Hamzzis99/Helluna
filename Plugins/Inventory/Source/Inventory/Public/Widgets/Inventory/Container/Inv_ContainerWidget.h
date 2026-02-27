// File: Plugins/Inventory/Source/Inventory/Public/Widgets/Inventory/Container/Inv_ContainerWidget.h
// ════════════════════════════════════════════════════════════════════════════════
// UInv_ContainerWidget — 컨테이너 Grid UI (SpatialInventory 통합)
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 역할:
//    상자/사체 루팅 시 표시되는 컨테이너 전용 Grid UI
//    ContainerGrid만 포함, 플레이어 인벤토리는 SpatialInventory(풀 UI)를 그대로 사용
//
// 📌 BP 바인딩:
//    WBP_Inv_ContainerWidget에서 BindWidget으로 연결
//    ContainerGrid, Text_ContainerName 필수
//    Button_TakeAll 선택 (BindWidgetOptional)
//
// 📌 변경 이력:
//    Phase 9 초기: 듀얼 Grid (ContainerGrid + PlayerGrid)
//    Phase 9 개선: PlayerGrid 제거 → SpatialInventory 크로스 링크 방식으로 전환
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
class UInv_SpatialInventory;
class UTextBlock;
class UButton;

UCLASS()
class INVENTORY_API UInv_ContainerWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/**
	 * 컨테이너 + SpatialInventory 크로스 링크 초기화
	 *
	 * @param InContainerComp     컨테이너 컴포넌트
	 * @param InPlayerComp        플레이어 인벤토리 컴포넌트 (RPC용)
	 * @param InSpatialInventory  플레이어 SpatialInventory (크로스 링크용)
	 */
	UFUNCTION(BlueprintCallable, Category = "Container|UI",
		meta = (DisplayName = "Initialize Panels (패널 초기화)"))
	void InitializePanels(UInv_LootContainerComponent* InContainerComp,
		UInv_InventoryComponent* InPlayerComp,
		UInv_SpatialInventory* InSpatialInventory);

	/** UI 정리 (닫기 시 호출) */
	UFUNCTION(BlueprintCallable, Category = "Container|UI",
		meta = (DisplayName = "Cleanup Panels (패널 정리)"))
	void CleanupPanels();

	/** 바인딩된 컨테이너 컴포넌트 반환 */
	UInv_LootContainerComponent* GetContainerComponent() const { return CachedContainerComp.Get(); }

	/** ContainerGrid Getter */
	UInv_InventoryGrid* GetContainerGrid() const { return ContainerGrid; }

protected:
	virtual void NativeConstruct() override;

	// ═══════════════════════════════════════════
	// BindWidget — BP에서 연결
	// ═══════════════════════════════════════════

	/** 컨테이너 Grid */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UInv_InventoryGrid> ContainerGrid;

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
	TWeakObjectPtr<UInv_SpatialInventory> CachedSpatialInventory;
};
