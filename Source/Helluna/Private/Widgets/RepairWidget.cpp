// Fill out your copyright notice in the Description page of Project Settings.

#include "Widgets/RepairWidget.h"
#include "Component/RepairComponent.h"
#include "InventoryManagement/Components/Inv_InventoryComponent.h"
#include "Character/HellunaHeroCharacter.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/Slider.h"

#include "DebugHelper.h"
#include "Helluna.h"

void URepairWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (Button_Confirm)
	{
		Button_Confirm->OnClicked.AddDynamic(this, &URepairWidget::OnConfirmClicked);
	}
	if (Button_Cancel)
	{
		Button_Cancel->OnClicked.AddDynamic(this, &URepairWidget::OnCancelClicked);
	}
	if (Slider_Material1)
	{
		Slider_Material1->OnValueChanged.AddDynamic(this, &URepairWidget::OnMaterial1SliderChanged);
	}
	if (Slider_Material2)
	{
		Slider_Material2->OnValueChanged.AddDynamic(this, &URepairWidget::OnMaterial2SliderChanged);
	}
}

void URepairWidget::NativeDestruct()
{
	if (Button_Confirm)
	{
		Button_Confirm->OnClicked.RemoveDynamic(this, &URepairWidget::OnConfirmClicked);
	}
	if (Button_Cancel)
	{
		Button_Cancel->OnClicked.RemoveDynamic(this, &URepairWidget::OnCancelClicked);
	}
	if (Slider_Material1)
	{
		Slider_Material1->OnValueChanged.RemoveDynamic(this, &URepairWidget::OnMaterial1SliderChanged);
	}
	if (Slider_Material2)
	{
		Slider_Material2->OnValueChanged.RemoveDynamic(this, &URepairWidget::OnMaterial2SliderChanged);
	}

	Super::NativeDestruct();
}

void URepairWidget::InitializeWidget(URepairComponent* InRepairComponent, UInv_InventoryComponent* InInventoryComponent)
{
	RepairComponent = InRepairComponent;
	InventoryComponent = InInventoryComponent;

	if (!RepairComponent || !InventoryComponent)
	{
		UE_LOG(LogTemp, Error, TEXT("[RepairWidget] RepairComponent or InventoryComponent is nullptr!"));
		return;
	}

	// ========== 재료 1 초기화 ==========
	Material1Tag = DefaultMaterial1Tag;
	Material1MaxAvailable = InventoryComponent->GetTotalMaterialCount(Material1Tag);
	Material1UseAmount = 0;

	UE_LOG(LogTemp, Warning, TEXT("[RepairWidget] Material1: %s, Available: %d"), *Material1Tag.ToString(), Material1MaxAvailable);

	if (Text_Material1Name)
	{
		Text_Material1Name->SetText(Material1DisplayName);
	}
	if (Text_Material1UseCount)
	{
		Text_Material1UseCount->SetText(FText::FromString(TEXT("0")));
	}
	if (Text_Material1MaxCount)
	{
		Text_Material1MaxCount->SetText(FText::FromString(FString::Printf(TEXT("/ %d"), Material1MaxAvailable)));
	}
	if (Slider_Material1)
	{
		Slider_Material1->SetMinValue(0.0f);
		Slider_Material1->SetMaxValue((float)FMath::Max(0, Material1MaxAvailable));
		Slider_Material1->SetValue(0.0f);
		Slider_Material1->SetIsEnabled(Material1MaxAvailable > 0);
	}
	if (Image_Material1 && DefaultMaterial1Icon)
	{
		Image_Material1->SetBrushFromTexture(DefaultMaterial1Icon);
	}

	// ========== 재료 2 초기화 ==========
	Material2Tag = DefaultMaterial2Tag;
	Material2MaxAvailable = InventoryComponent->GetTotalMaterialCount(Material2Tag);
	Material2UseAmount = 0;

	UE_LOG(LogTemp, Warning, TEXT("[RepairWidget] Material2: %s, Available: %d"), *Material2Tag.ToString(), Material2MaxAvailable);

	if (Text_Material2Name)
	{
		Text_Material2Name->SetText(Material2DisplayName);
	}
	if (Text_Material2UseCount)
	{
		Text_Material2UseCount->SetText(FText::FromString(TEXT("0")));
	}
	if (Text_Material2MaxCount)
	{
		Text_Material2MaxCount->SetText(FText::FromString(FString::Printf(TEXT("/ %d"), Material2MaxAvailable)));
	}
	if (Slider_Material2)
	{
		Slider_Material2->SetMinValue(0.0f);
		Slider_Material2->SetMaxValue((float)FMath::Max(0, Material2MaxAvailable));
		Slider_Material2->SetValue(0.0f);
		Slider_Material2->SetIsEnabled(Material2MaxAvailable > 0);
	}
	if (Image_Material2 && DefaultMaterial2Icon)
	{
		Image_Material2->SetBrushFromTexture(DefaultMaterial2Icon);
	}

	UpdateTotalResourceUI();
}

void URepairWidget::OnConfirmClicked()
{
	if (!RepairComponent)
	{
		return;
	}

	int32 TotalUse = Material1UseAmount + Material2UseAmount;
	if (TotalUse <= 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[RepairWidget] No materials selected!"));
		return;
	}

	APlayerController* PC = GetOwningPlayer();
	if (!PC) return;

	AHellunaHeroCharacter* HeroCharacter = Cast<AHellunaHeroCharacter>(PC->GetPawn());
	if (!HeroCharacter) return;

	UE_LOG(LogTemp, Warning, TEXT("[RepairWidget] Confirm: Mat1=%s x%d, Mat2=%s x%d"),
		*Material1Tag.ToString(), Material1UseAmount,
		*Material2Tag.ToString(), Material2UseAmount);

	HeroCharacter->Server_RepairSpaceShip(Material1Tag, Material1UseAmount, Material2Tag, Material2UseAmount);

	PC->SetInputMode(FInputModeGameOnly());
	PC->bShowMouseCursor = false;
	RemoveFromParent();
}

void URepairWidget::OnCancelClicked()
{
	APlayerController* PC = GetOwningPlayer();
	if (PC)
	{
		PC->SetInputMode(FInputModeGameOnly());
		PC->bShowMouseCursor = false;
	}
	RemoveFromParent();
}

void URepairWidget::CloseWidget()
{
	APlayerController* PC = GetOwningPlayer();
	if (PC)
	{
		PC->SetInputMode(FInputModeGameOnly());
		PC->bShowMouseCursor = false;
	}
	RemoveFromParent();
}

void URepairWidget::OnMaterial1SliderChanged(float Value)
{
	Material1UseAmount = FMath::FloorToInt(Value);
	if (Text_Material1UseCount)
	{
		Text_Material1UseCount->SetText(FText::FromString(FString::Printf(TEXT("%d"), Material1UseAmount)));
	}
	UpdateTotalResourceUI();
}

void URepairWidget::OnMaterial2SliderChanged(float Value)
{
	Material2UseAmount = FMath::FloorToInt(Value);
	if (Text_Material2UseCount)
	{
		Text_Material2UseCount->SetText(FText::FromString(FString::Printf(TEXT("%d"), Material2UseAmount)));
	}
	UpdateTotalResourceUI();
}

void URepairWidget::UpdateTotalResourceUI()
{
	int32 TotalResource = Material1UseAmount + Material2UseAmount;
	if (Text_TotalResource)
	{
		Text_TotalResource->SetText(FText::FromString(FString::Printf(TEXT("+%d"), TotalResource)));
	}
}
