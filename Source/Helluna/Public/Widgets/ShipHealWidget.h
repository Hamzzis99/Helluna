// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameplayTagContainer.h"
#include "ShipHealWidget.generated.h"

class UButton;
class UImage;
class UTextBlock;
class USlider;
class UProgressBar;
class UTexture2D;
class UActorComponent;
class URepairComponent;
class UInv_InventoryComponent;
class AResourceUsingObject_SpaceShip;

/**
 * 우주선 HP 회복 위젯 (수리 위젯 URepairWidget 복제 — 재료 비례 HP 회복).
 * - 레이아웃/BindWidget 이름은 WBP_RepairWidget 와 동일(WBP 복제 후 reparent 가능).
 * - Confirm 시 HeroCharacter->Server_HealShipFromMaterials 호출 (수리 진행도가 아닌 HP 회복).
 * - 우주선 현재 HP 를 실시간 표시(OnHealthChanged 구독, 표시 위젯은 선택).
 */
UCLASS()
class HELLUNA_API UShipHealWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

public:
	/** Widget 초기화 — RepairComponent(우주선/HealPerMaterial 접근), InventoryComponent(재료 보유량) */
	UFUNCTION(BlueprintCallable, Category = "ShipHeal")
	void InitializeWidget(URepairComponent* InRepairComponent, UInv_InventoryComponent* InInventoryComponent);

	/** Widget 닫기 */
	UFUNCTION(BlueprintCallable, Category = "ShipHeal")
	void CloseWidget();

private:
	UFUNCTION()
	void OnConfirmClicked();

	UFUNCTION()
	void OnCancelClicked();

	UFUNCTION()
	void OnMaterial1SliderChanged(float Value);

	UFUNCTION()
	void OnMaterial2SliderChanged(float Value);

	/** 우주선 HP 변경 콜백 (UHellunaHealthComponent::OnHealthChanged 4-param 시그니처와 일치) */
	UFUNCTION()
	void OnShipHealthChangedCB(UActorComponent* HealthComp, float OldHealth, float NewHealth, AActor* InstigatorActor);

	/** "+N HP" 회복 미리보기 갱신 (재료수 × HealPerMaterial) */
	void UpdateTotalHealUI();

	/** 우주선 현재 HP 바/텍스트 갱신 (선택 위젯) */
	void RefreshShipHPUI();

	// ===== [BindWidget] 재료 1 카드 (WBP_RepairWidget 와 동일 이름) =====
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UImage> Image_Material1;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> Text_Material1Name;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> Text_Material1UseCount;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> Text_Material1MaxCount;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<USlider> Slider_Material1;

	// ===== [BindWidget] 재료 2 카드 =====
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UImage> Image_Material2;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> Text_Material2Name;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> Text_Material2UseCount;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> Text_Material2MaxCount;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<USlider> Slider_Material2;

	// ===== [BindWidget] 총 회복량 & 버튼 =====
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> Text_TotalResource;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> Button_Confirm;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> Button_Cancel;

	// ===== [BindWidgetOptional] 우주선 HP 표시 — WBP 에 있으면 실시간 갱신 =====
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UProgressBar> ShipHPBar;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> Text_ShipHP;

	// ===== 데이터 =====
	UPROPERTY() TObjectPtr<URepairComponent> RepairComponent;
	UPROPERTY() TObjectPtr<UInv_InventoryComponent> InventoryComponent;
	UPROPERTY() TObjectPtr<AResourceUsingObject_SpaceShip> TargetShip;

	FGameplayTag Material1Tag;
	int32 Material1MaxAvailable = 0;
	int32 Material1UseAmount = 0;

	FGameplayTag Material2Tag;
	int32 Material2MaxAvailable = 0;
	int32 Material2UseAmount = 0;

	/** 우주선에서 읽어온 재료당 회복 HP (미리보기 표시용; 실제 회복은 서버 권위) */
	float HealPerMaterial = 100.f;

	// ===== [Blueprint 설정] WBP_RepairWidget 와 동일 이름 =====
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heal|Material 1", meta = (AllowPrivateAccess = "true", Categories = "GameItems.Craftables"))
	FGameplayTag DefaultMaterial1Tag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heal|Material 1", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UTexture2D> DefaultMaterial1Icon;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heal|Material 1", meta = (AllowPrivateAccess = "true"))
	FText Material1DisplayName = FText::FromString(TEXT("재료 1"));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heal|Material 2", meta = (AllowPrivateAccess = "true", Categories = "GameItems.Craftables"))
	FGameplayTag DefaultMaterial2Tag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heal|Material 2", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UTexture2D> DefaultMaterial2Icon;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heal|Material 2", meta = (AllowPrivateAccess = "true"))
	FText Material2DisplayName = FText::FromString(TEXT("재료 2"));
};
