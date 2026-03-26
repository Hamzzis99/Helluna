// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameplayTagContainer.h"
#include "RepairWidget.generated.h"

class UButton;
class UImage;
class UTextBlock;
class USlider;
class UVerticalBox;
class UHorizontalBox;
class UOverlay;
class USizeBox;
class URepairComponent;
class UInv_InventoryComponent;

/**
 * 수리 위젯 (리디자인 버전)
 * - SF 테마 디자인
 * - 2개 재료 슬롯 (아이콘 + 이름 + 사용/보유 + 슬라이더)
 * - 총 투입 자원 표시
 * - 수리 시작 / 취소 버튼
 */
UCLASS()
class HELLUNA_API URepairWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

public:
	/** Widget 초기화 */
	UFUNCTION(BlueprintCallable, Category = "Repair")
	void InitializeWidget(URepairComponent* InRepairComponent, UInv_InventoryComponent* InInventoryComponent);

	/** Widget 닫기 */
	UFUNCTION(BlueprintCallable, Category = "Repair")
	void CloseWidget();

private:
	// ========================================
	// [이벤트 핸들러]
	// ========================================
	UFUNCTION()
	void OnConfirmClicked();

	UFUNCTION()
	void OnCancelClicked();

	UFUNCTION()
	void OnMaterial1SliderChanged(float Value);

	UFUNCTION()
	void OnMaterial2SliderChanged(float Value);

	void UpdateTotalResourceUI();

	// ========================================
	// [위젯 바인딩] — 재료 1 카드
	// ========================================

	/** 재료 1 아이콘 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> Image_Material1;

	/** 재료 1 이름 (예: "파이어 펀 열매") */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> Text_Material1Name;

	/** 재료 1 사용량 텍스트 (예: "5") */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> Text_Material1UseCount;

	/** 재료 1 보유량 텍스트 (예: "/ 12") */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> Text_Material1MaxCount;

	/** 재료 1 슬라이더 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<USlider> Slider_Material1;

	// ========================================
	// [위젯 바인딩] — 재료 2 카드
	// ========================================

	/** 재료 2 아이콘 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> Image_Material2;

	/** 재료 2 이름 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> Text_Material2Name;

	/** 재료 2 사용량 텍스트 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> Text_Material2UseCount;

	/** 재료 2 보유량 텍스트 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> Text_Material2MaxCount;

	/** 재료 2 슬라이더 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<USlider> Slider_Material2;

	// ========================================
	// [위젯 바인딩] — 총 자원 & 버튼
	// ========================================

	/** 총 투입 자원 텍스트 (예: "+8") */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> Text_TotalResource;

	/** 수리 시작 버튼 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> Button_Confirm;

	/** 취소 버튼 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> Button_Cancel;

	// ========================================
	// [데이터]
	// ========================================

	UPROPERTY()
	TObjectPtr<URepairComponent> RepairComponent;

	UPROPERTY()
	TObjectPtr<UInv_InventoryComponent> InventoryComponent;

	FGameplayTag Material1Tag;
	int32 Material1MaxAvailable = 0;
	int32 Material1UseAmount = 0;

	FGameplayTag Material2Tag;
	int32 Material2MaxAvailable = 0;
	int32 Material2UseAmount = 0;

	// ========================================
	// [Blueprint 설정]
	// ========================================

	/** 재료 1 GameplayTag */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Repair|Material 1", meta = (AllowPrivateAccess = "true", Categories = "GameItems.Craftables"))
	FGameplayTag DefaultMaterial1Tag;

	/** 재료 1 아이콘 텍스처 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Repair|Material 1", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UTexture2D> DefaultMaterial1Icon;

	/** 재료 1 표시 이름 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Repair|Material 1", meta = (AllowPrivateAccess = "true"))
	FText Material1DisplayName = FText::FromString(TEXT("재료 1"));

	/** 재료 2 GameplayTag */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Repair|Material 2", meta = (AllowPrivateAccess = "true", Categories = "GameItems.Craftables"))
	FGameplayTag DefaultMaterial2Tag;

	/** 재료 2 아이콘 텍스처 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Repair|Material 2", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UTexture2D> DefaultMaterial2Icon;

	/** 재료 2 표시 이름 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Repair|Material 2", meta = (AllowPrivateAccess = "true"))
	FText Material2DisplayName = FText::FromString(TEXT("재료 2"));
};
