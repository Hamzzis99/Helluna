// Gihyeon's Inventory Project

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameplayTagContainer.h"
#include "Inv_BuildModeHUD.generated.h"

class UTextBlock;
class UImage;
class UHorizontalBox;
class UTexture2D;

/**
 * 빌드 모드 진입 시 화면 오른쪽에 표시되는 HUD 위젯
 * 건물 이름, 재료 아이콘/수량, 배치 가능 상태를 표시
 * 조작법 텍스트는 블루프린트에서 정적으로 배치
 */
UCLASS()
class INVENTORY_API UInv_BuildModeHUD : public UUserWidget
{
	GENERATED_BODY()

public:
	/**
	 * BuildingComponent에서 호출: HUD 초기화
	 * 건물 이름, 미리보기 아이콘, 재료 3개의 아이콘/필요수량/태그를 설정
	 */
	UFUNCTION(BlueprintCallable, Category = "건설|HUD")
	void SetBuildingInfo(
		const FText& BuildingName,
		UTexture2D* BuildingIcon,
		UTexture2D* MatIcon1, int32 ReqAmount1, FGameplayTag MatTag1,
		UTexture2D* MatIcon2, int32 ReqAmount2, FGameplayTag MatTag2,
		UTexture2D* MatIcon3, int32 ReqAmount3, FGameplayTag MatTag3
	);

	/** 매 프레임 또는 상태 변경 시 호출: 배치 가능 상태 업데이트 */
	UFUNCTION(BlueprintCallable, Category = "건설|HUD")
	void UpdatePlacementStatus(bool bCanPlace);

	/** 재료 수량 실시간 업데이트 (인벤토리 변경 시) */
	UFUNCTION(BlueprintCallable, Category = "건설|HUD")
	void UpdateMaterialAmounts();

protected:
	virtual void NativeConstruct() override;

private:
	// === BindWidget 멤버 ===

	// 건물 이름
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> Text_BuildingName;

	// === 재료 슬롯 1 ===
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UHorizontalBox> HBox_Material1;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> Image_Material1;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> Text_Material1Amount;

	// === 재료 슬롯 2 ===
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UHorizontalBox> HBox_Material2;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> Image_Material2;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> Text_Material2Amount;

	// === 재료 슬롯 3 ===
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UHorizontalBox> HBox_Material3;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> Image_Material3;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> Text_Material3Amount;

	// === 건축 가능/불가능 상태 표시 ===
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> Text_PlacementStatus;

	// === 미리보기 이미지 (선택적) ===
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> Image_Preview;

	// === 저장된 재료 정보 (UpdateMaterialAmounts에서 사용) ===
	FGameplayTag StoredMatTag1;
	FGameplayTag StoredMatTag2;
	FGameplayTag StoredMatTag3;
	int32 StoredReqAmount1 = 0;
	int32 StoredReqAmount2 = 0;
	int32 StoredReqAmount3 = 0;

	// 이전 배치 상태 (변경 시에만 로그 출력용)
	bool bPreviousCanPlace = false;
	bool bPlacementStatusInitialized = false;
};
