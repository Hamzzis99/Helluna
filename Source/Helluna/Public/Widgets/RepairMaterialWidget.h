// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameplayTagContainer.h"
#include "RepairMaterialWidget.generated.h"

class UButton;
class UImage;
class UTextBlock;
class USlider;
class UVerticalBox;
class UHorizontalBox;
class URepairComponent;
class UInv_InventoryComponent;

/**
 * Repair Material Widget
 * - SpaceShip 수리 시 사용할 재료를 선택하고 개수를 조절하는 UI
 * - 2개의 재료 슬롯 (Material 1, Material 2)
 * - 각 슬롯마다 이미지 + 슬라이더로 사용량 조절
 */
UCLASS()
class HELLUNA_API URepairMaterialWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

public:
	/**
	 * Widget 초기화
	 * @param InRepairComponent - Repair 담당 컴포넌트
	 * @param InInventoryComponent - 플레이어 인벤토리 컴포넌트
	 */
	UFUNCTION(BlueprintCallable, Category = "Repair")
	void InitializeWidget(URepairComponent* InRepairComponent, UInv_InventoryComponent* InInventoryComponent);

private:
	// ========================================
	// [이벤트 핸들러]
	// ========================================

	/** 확인 버튼 클릭 */
	UFUNCTION()
	void OnConfirmClicked();

	/** 취소 버튼 클릭 */
	UFUNCTION()
	void OnCancelClicked();

	/** 재료 1 슬라이더 값 변경 */
	UFUNCTION()
	void OnMaterial1SliderChanged(float Value);

	/** 재료 2 슬라이더 값 변경 */
	UFUNCTION()
	void OnMaterial2SliderChanged(float Value);

	/** UI 업데이트 (총 자원량 표시) */
	void UpdateTotalResourceUI();

	// ========================================
	// [위젯 바인딩 - meta = (BindWidget)]
	// ========================================

	/** 재료 1 이미지 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> Image_Material1;

	/** 재료 1 이름 텍스트 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> Text_Material1Name;

	/** 재료 1 보유량 컨테이너 (HorizontalBox) */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UHorizontalBox> HorizontalBox_Material1Available;

	/** 재료 1 보유량 아이콘 (작은 아이콘) */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> Image_Material1Available_Icon;

	/** 재료 1 보유량 텍스트 (예: "보유: 20") */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> Text_Material1Available;

	/** 재료 1 사용량 슬라이더 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<USlider> Slider_Material1;

	/** 재료 1 사용량 컨테이너 (HorizontalBox) */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UHorizontalBox> HorizontalBox_Material1Use;

	/** 재료 1 사용량 아이콘 (작은 아이콘) */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> Image_Material1Use_Icon;

	/** 재료 1 사용량 텍스트 (예: "사용: 10") */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> Text_Material1Use;

	// ========================================

	/** 재료 2 이미지 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> Image_Material2;

	/** 재료 2 이름 텍스트 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> Text_Material2Name;

	/** 재료 2 보유량 컨테이너 (HorizontalBox) */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UHorizontalBox> HorizontalBox_Material2Available;

	/** 재료 2 보유량 아이콘 (작은 아이콘) */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> Image_Material2Available_Icon;

	/** 재료 2 보유량 텍스트 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> Text_Material2Available;

	/** 재료 2 사용량 슬라이더 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<USlider> Slider_Material2;

	/** 재료 2 사용량 컨테이너 (HorizontalBox) */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UHorizontalBox> HorizontalBox_Material2Use;

	/** 재료 2 사용량 아이콘 (작은 아이콘) */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> Image_Material2Use_Icon;

	/** 재료 2 사용량 텍스트 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> Text_Material2Use;

	// ========================================

	/** 총 자원량 텍스트 (예: "총 자원: +15") */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> Text_TotalResource;

	/** 확인 버튼 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> Button_Confirm;

	/** 취소 버튼 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> Button_Cancel;

	// ========================================
	// [데이터 변수]
	// ========================================

	/** RepairComponent 참조 */
	UPROPERTY()
	TObjectPtr<URepairComponent> RepairComponent;

	/** InventoryComponent 참조 */
	UPROPERTY()
	TObjectPtr<UInv_InventoryComponent> InventoryComponent;

	/** 재료 1 GameplayTag */
	FGameplayTag Material1Tag;

	/** 재료 1 최대 보유량 */
	int32 Material1MaxAvailable = 0;

	/** 재료 1 현재 사용량 */
	int32 Material1UseAmount = 0;

	/** 재료 2 GameplayTag */
	FGameplayTag Material2Tag;

	/** 재료 2 최대 보유량 */
	int32 Material2MaxAvailable = 0;

	/** 재료 2 현재 사용량 */
	int32 Material2UseAmount = 0;

	// ========================================
	// [Blueprint에서 설정 가능]
	// ========================================

	/** 재료 1 기본 Tag (Blueprint에서 설정) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Repair|Settings", meta = (AllowPrivateAccess = "true", Categories = "GameItems.Craftables"))
	FGameplayTag DefaultMaterial1Tag;

	/** 재료 1 기본 아이콘 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Repair|Settings", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UTexture2D> DefaultMaterial1Icon;

	/** 재료 2 기본 Tag (Blueprint에서 설정) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Repair|Settings", meta = (AllowPrivateAccess = "true", Categories = "GameItems.Craftables"))
	FGameplayTag DefaultMaterial2Tag;

	/** 재료 2 기본 아이콘 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Repair|Settings", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UTexture2D> DefaultMaterial2Icon;

	/** 빈 슬롯 기본 이미지 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Repair|Settings", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UTexture2D> EmptySlotIcon;
};
