// Fill out your copyright notice in the Description page of Project Settings.

#include "Widgets/RepairMaterialWidget.h"
#include "Component/RepairComponent.h"
#include "InventoryManagement/Components/Inv_InventoryComponent.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/Slider.h"

#include "DebugHelper.h"

void URepairMaterialWidget::NativeConstruct()
{
	Super::NativeConstruct();

	UE_LOG(LogTemp, Warning, TEXT("=== [RepairMaterialWidget] NativeConstruct ==="));

	// 버튼 이벤트 바인딩
	if (Button_Confirm)
	{
		Button_Confirm->OnClicked.AddDynamic(this, &URepairMaterialWidget::OnConfirmClicked);
	}

	if (Button_Cancel)
	{
		Button_Cancel->OnClicked.AddDynamic(this, &URepairMaterialWidget::OnCancelClicked);
	}

	// 슬라이더 이벤트 바인딩
	if (Slider_Material1)
	{
		Slider_Material1->OnValueChanged.AddDynamic(this, &URepairMaterialWidget::OnMaterial1SliderChanged);
	}

	if (Slider_Material2)
	{
		Slider_Material2->OnValueChanged.AddDynamic(this, &URepairMaterialWidget::OnMaterial2SliderChanged);
	}
}

void URepairMaterialWidget::NativeDestruct()
{
	Super::NativeDestruct();

	UE_LOG(LogTemp, Warning, TEXT("=== [RepairMaterialWidget] NativeDestruct ==="));
}

// ========================================
// [Public Functions]
// ========================================

void URepairMaterialWidget::InitializeWidget(URepairComponent* InRepairComponent, UInv_InventoryComponent* InInventoryComponent)
{
	UE_LOG(LogTemp, Warning, TEXT("=== [InitializeWidget] 시작 ==="));

	RepairComponent = InRepairComponent;
	InventoryComponent = InInventoryComponent;

	if (!RepairComponent || !InventoryComponent)
	{
		UE_LOG(LogTemp, Error, TEXT("  ❌ RepairComponent 또는 InventoryComponent가 nullptr!"));
		return;
	}

	// ========================================
	// 재료 1 초기화
	// ========================================

	Material1Tag = DefaultMaterial1Tag;
	Material1MaxAvailable = InventoryComponent->GetTotalMaterialCount(Material1Tag);
	Material1UseAmount = 0;

	UE_LOG(LogTemp, Warning, TEXT("  📦 재료 1: %s, 보유량: %d"), *Material1Tag.ToString(), Material1MaxAvailable);

	// UI 업데이트
	if (Text_Material1Name)
	{
		Text_Material1Name->SetText(FText::FromString(Material1Tag.GetTagName().ToString()));
	}

	if (Text_Material1Available)
	{
		Text_Material1Available->SetText(FText::FromString(FString::Printf(TEXT("보유: %d"), Material1MaxAvailable)));
	}

	if (Slider_Material1)
	{
		Slider_Material1->SetMinValue(0.0f);
		Slider_Material1->SetMaxValue(FMath::Max(1.0f, (float)Material1MaxAvailable));
		Slider_Material1->SetValue(0.0f);
	}

	if (Image_Material1 && DefaultMaterial1Icon)
	{
		Image_Material1->SetBrushFromTexture(DefaultMaterial1Icon);
	}

	// ⭐ 작은 아이콘에도 동일한 이미지 설정!
	if (Image_Material1Available_Icon && DefaultMaterial1Icon)
	{
		Image_Material1Available_Icon->SetBrushFromTexture(DefaultMaterial1Icon);
	}

	if (Image_Material1Use_Icon && DefaultMaterial1Icon)
	{
		Image_Material1Use_Icon->SetBrushFromTexture(DefaultMaterial1Icon);
	}

	// ========================================
	// 재료 2 초기화
	// ========================================

	Material2Tag = DefaultMaterial2Tag;
	Material2MaxAvailable = InventoryComponent->GetTotalMaterialCount(Material2Tag);
	Material2UseAmount = 0;

	UE_LOG(LogTemp, Warning, TEXT("  📦 재료 2: %s, 보유량: %d"), *Material2Tag.ToString(), Material2MaxAvailable);

	// UI 업데이트
	if (Text_Material2Name)
	{
		Text_Material2Name->SetText(FText::FromString(Material2Tag.GetTagName().ToString()));
	}

	if (Text_Material2Available)
	{
		Text_Material2Available->SetText(FText::FromString(FString::Printf(TEXT("보유: %d"), Material2MaxAvailable)));
	}

	if (Slider_Material2)
	{
		Slider_Material2->SetMinValue(0.0f);
		Slider_Material2->SetMaxValue(FMath::Max(1.0f, (float)Material2MaxAvailable));
		Slider_Material2->SetValue(0.0f);
	}

	if (Image_Material2 && DefaultMaterial2Icon)
	{
		Image_Material2->SetBrushFromTexture(DefaultMaterial2Icon);
	}

	// ⭐ 작은 아이콘에도 동일한 이미지 설정!
	if (Image_Material2Available_Icon && DefaultMaterial2Icon)
	{
		Image_Material2Available_Icon->SetBrushFromTexture(DefaultMaterial2Icon);
	}

	if (Image_Material2Use_Icon && DefaultMaterial2Icon)
	{
		Image_Material2Use_Icon->SetBrushFromTexture(DefaultMaterial2Icon);
	}

	// 총 자원량 초기화
	UpdateTotalResourceUI();

	UE_LOG(LogTemp, Warning, TEXT("=== [InitializeWidget] 완료 ==="));
}

// ========================================
// [Private Event Handlers]
// ========================================

void URepairMaterialWidget::OnConfirmClicked()
{
	UE_LOG(LogTemp, Warning, TEXT("=== [OnConfirmClicked] 확인 버튼 클릭! ==="));

	if (!RepairComponent)
	{
		UE_LOG(LogTemp, Error, TEXT("  ❌ RepairComponent가 nullptr!"));
		return;
	}

	// 총 사용량 체크
	int32 TotalUse = Material1UseAmount + Material2UseAmount;
	if (TotalUse <= 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("  ⚠️ 사용할 재료가 없습니다!"));
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("  📤 Repair 요청 전송:"));
	UE_LOG(LogTemp, Warning, TEXT("    - 재료 1: %s x %d"), *Material1Tag.ToString(), Material1UseAmount);
	UE_LOG(LogTemp, Warning, TEXT("    - 재료 2: %s x %d"), *Material2Tag.ToString(), Material2UseAmount);

	// PlayerController 가져오기
	APlayerController* PC = GetOwningPlayer();
	if (!PC)
	{
		UE_LOG(LogTemp, Error, TEXT("  ❌ PlayerController를 찾을 수 없음!"));
		return;
	}

	// ⭐ RepairComponent의 Server RPC 호출!
	RepairComponent->Server_ProcessRepairRequest(
		PC,
		Material1Tag,
		Material1UseAmount,
		Material2Tag,
		Material2UseAmount
	);

	UE_LOG(LogTemp, Warning, TEXT("  ✅ Repair 요청 전송 완료!"));

	// Widget 닫기
	RemoveFromParent();
}

void URepairMaterialWidget::OnCancelClicked()
{
	UE_LOG(LogTemp, Warning, TEXT("=== [OnCancelClicked] 취소 버튼 클릭! ==="));

	// Widget 닫기
	RemoveFromParent();
}

void URepairMaterialWidget::OnMaterial1SliderChanged(float Value)
{
	Material1UseAmount = FMath::FloorToInt(Value);

	// UI 업데이트
	if (Text_Material1Use)
	{
		Text_Material1Use->SetText(FText::FromString(FString::Printf(TEXT("사용: %d"), Material1UseAmount)));
	}

	UpdateTotalResourceUI();
}

void URepairMaterialWidget::OnMaterial2SliderChanged(float Value)
{
	Material2UseAmount = FMath::FloorToInt(Value);

	// UI 업데이트
	if (Text_Material2Use)
	{
		Text_Material2Use->SetText(FText::FromString(FString::Printf(TEXT("사용: %d"), Material2UseAmount)));
	}

	UpdateTotalResourceUI();
}

void URepairMaterialWidget::UpdateTotalResourceUI()
{
	int32 TotalResource = Material1UseAmount + Material2UseAmount;

	if (Text_TotalResource)
	{
		Text_TotalResource->SetText(FText::FromString(FString::Printf(TEXT("총 자원: +%d"), TotalResource)));
	}
}
