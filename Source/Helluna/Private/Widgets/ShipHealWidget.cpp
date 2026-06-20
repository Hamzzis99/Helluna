// Fill out your copyright notice in the Description page of Project Settings.

#include "Widgets/ShipHealWidget.h"
#include "Component/RepairComponent.h"
#include "InventoryManagement/Components/Inv_InventoryComponent.h"
#include "Character/HellunaHeroCharacter.h"
#include "Object/ResourceUsingObject/ResourceUsingObject_SpaceShip.h"
#include "Character/EnemyComponent/HellunaHealthComponent.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/Slider.h"
#include "Components/ProgressBar.h"

#include "DebugHelper.h"
#include "Helluna.h"

void UShipHealWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (Button_Confirm)
	{
		Button_Confirm->OnClicked.AddDynamic(this, &UShipHealWidget::OnConfirmClicked);
	}
	if (Button_Cancel)
	{
		Button_Cancel->OnClicked.AddDynamic(this, &UShipHealWidget::OnCancelClicked);
	}
	if (Slider_Material1)
	{
		Slider_Material1->OnValueChanged.AddDynamic(this, &UShipHealWidget::OnMaterial1SliderChanged);
	}
	if (Slider_Material2)
	{
		Slider_Material2->OnValueChanged.AddDynamic(this, &UShipHealWidget::OnMaterial2SliderChanged);
	}
}

void UShipHealWidget::NativeDestruct()
{
	if (Button_Confirm)
	{
		Button_Confirm->OnClicked.RemoveDynamic(this, &UShipHealWidget::OnConfirmClicked);
	}
	if (Button_Cancel)
	{
		Button_Cancel->OnClicked.RemoveDynamic(this, &UShipHealWidget::OnCancelClicked);
	}
	if (Slider_Material1)
	{
		Slider_Material1->OnValueChanged.RemoveDynamic(this, &UShipHealWidget::OnMaterial1SliderChanged);
	}
	if (Slider_Material2)
	{
		Slider_Material2->OnValueChanged.RemoveDynamic(this, &UShipHealWidget::OnMaterial2SliderChanged);
	}

	// HP 델리게이트 언바인드 (위젯 파괴 후 콜백 방지 — use-after-free 가드)
	if (TargetShip)
	{
		if (UHellunaHealthComponent* HC = TargetShip->GetShipHealthComponent())
		{
			HC->OnHealthChanged.RemoveAll(this);
		}
	}
	TargetShip = nullptr;

	Super::NativeDestruct();
}

void UShipHealWidget::InitializeWidget(URepairComponent* InRepairComponent, UInv_InventoryComponent* InInventoryComponent)
{
	RepairComponent = InRepairComponent;
	InventoryComponent = InInventoryComponent;

	if (!RepairComponent || !InventoryComponent)
	{
		UE_LOG(LogTemp, Error, TEXT("[ShipHealWidget] RepairComponent or InventoryComponent is nullptr!"));
		return;
	}

	// 우주선 참조 + HealPerMaterial + HP 델리게이트 바인딩 (RepairComponent 의 Owner 가 우주선)
	TargetShip = Cast<AResourceUsingObject_SpaceShip>(RepairComponent->GetOwner());
	if (TargetShip)
	{
		HealPerMaterial = FMath::Max(1.f, TargetShip->GetHealPerMaterial());
		if (UHellunaHealthComponent* HC = TargetShip->GetShipHealthComponent())
		{
			HC->OnHealthChanged.AddUniqueDynamic(this, &UShipHealWidget::OnShipHealthChangedCB);
		}
	}

	// ========== 재료 1 초기화 ==========
	Material1Tag = DefaultMaterial1Tag;
	Material1MaxAvailable = InventoryComponent->GetTotalMaterialCount(Material1Tag);
	Material1UseAmount = 0;

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

	UpdateTotalHealUI();
	RefreshShipHPUI();
}

void UShipHealWidget::OnConfirmClicked()
{
	const int32 TotalUse = Material1UseAmount + Material2UseAmount;
	if (TotalUse <= 0)
	{
		return;
	}

	APlayerController* PC = GetOwningPlayer();
	if (!PC) return;

	AHellunaHeroCharacter* HeroCharacter = Cast<AHellunaHeroCharacter>(PC->GetPawn());
	if (!HeroCharacter) return;

	HeroCharacter->Server_HealShipFromMaterials(Material1Tag, Material1UseAmount, Material2Tag, Material2UseAmount);

	PC->SetInputMode(FInputModeGameOnly());
	PC->bShowMouseCursor = false;
	RemoveFromParent();
}

void UShipHealWidget::OnCancelClicked()
{
	CloseWidget();
}

void UShipHealWidget::CloseWidget()
{
	APlayerController* PC = GetOwningPlayer();
	if (PC)
	{
		PC->SetInputMode(FInputModeGameOnly());
		PC->bShowMouseCursor = false;
	}
	RemoveFromParent();
}

void UShipHealWidget::OnMaterial1SliderChanged(float Value)
{
	Material1UseAmount = FMath::FloorToInt(Value);
	if (Text_Material1UseCount)
	{
		Text_Material1UseCount->SetText(FText::FromString(FString::Printf(TEXT("%d"), Material1UseAmount)));
	}
	UpdateTotalHealUI();
}

void UShipHealWidget::OnMaterial2SliderChanged(float Value)
{
	Material2UseAmount = FMath::FloorToInt(Value);
	if (Text_Material2UseCount)
	{
		Text_Material2UseCount->SetText(FText::FromString(FString::Printf(TEXT("%d"), Material2UseAmount)));
	}
	UpdateTotalHealUI();
}

void UShipHealWidget::UpdateTotalHealUI()
{
	const int32 TotalMaterials = Material1UseAmount + Material2UseAmount;
	const int32 TotalHeal = FMath::RoundToInt(TotalMaterials * HealPerMaterial);
	if (Text_TotalResource)
	{
		Text_TotalResource->SetText(FText::FromString(FString::Printf(TEXT("+%d HP"), TotalHeal)));
	}
}

void UShipHealWidget::OnShipHealthChangedCB(UActorComponent* /*HealthComp*/, float /*OldHealth*/, float /*NewHealth*/, AActor* /*InstigatorActor*/)
{
	RefreshShipHPUI();
}

void UShipHealWidget::RefreshShipHPUI()
{
	if (!TargetShip)
	{
		return;
	}

	const float Cur = TargetShip->GetShipHealth();
	const float Max = TargetShip->GetShipMaxHealth();

	if (ShipHPBar)
	{
		ShipHPBar->SetPercent(TargetShip->GetShipHealthPercent());
	}
	if (Text_ShipHP)
	{
		Text_ShipHP->SetText(FText::FromString(FString::Printf(TEXT("%d / %d"), FMath::RoundToInt(Cur), FMath::RoundToInt(Max))));
	}
}
