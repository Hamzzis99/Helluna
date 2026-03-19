// Gihyeon's Inventory Project

#include "Widgets/Building/Inv_BuildModeHUD.h"
#include "Inventory.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Components/HorizontalBox.h"
#include "Engine/Texture2D.h"
#include "GameFramework/PlayerController.h"
#include "InventoryManagement/Components/Inv_InventoryComponent.h"

void UInv_BuildModeHUD::NativeConstruct()
{
	Super::NativeConstruct();

	// 초기 상태: 배치 불가로 시작
	bPreviousCanPlace = false;
	bPlacementStatusInitialized = false;

	if (IsValid(Text_PlacementStatus))
	{
		Text_PlacementStatus->SetText(FText::FromString(TEXT("배치 대기 중...")));
	}
}

void UInv_BuildModeHUD::SetBuildingInfo(
	const FText& BuildingName,
	UTexture2D* BuildingIcon,
	UTexture2D* MatIcon1, int32 ReqAmount1, FGameplayTag MatTag1,
	UTexture2D* MatIcon2, int32 ReqAmount2, FGameplayTag MatTag2,
	UTexture2D* MatIcon3, int32 ReqAmount3, FGameplayTag MatTag3)
{
	// 건물 이름 설정
	if (IsValid(Text_BuildingName))
	{
		Text_BuildingName->SetText(BuildingName);
	}

	// 미리보기 이미지 설정
	if (IsValid(Image_Preview) && IsValid(BuildingIcon))
	{
		Image_Preview->SetBrushFromTexture(BuildingIcon);
		Image_Preview->SetVisibility(ESlateVisibility::Visible);
	}
	else if (IsValid(Image_Preview))
	{
		Image_Preview->SetVisibility(ESlateVisibility::Collapsed);
	}

	// 재료 정보 저장
	StoredMatTag1 = MatTag1;
	StoredReqAmount1 = ReqAmount1;
	StoredMatTag2 = MatTag2;
	StoredReqAmount2 = ReqAmount2;
	StoredMatTag3 = MatTag3;
	StoredReqAmount3 = ReqAmount3;

	// 재료 슬롯 1
	if (MatTag1.IsValid() && ReqAmount1 > 0)
	{
		if (IsValid(HBox_Material1))
			HBox_Material1->SetVisibility(ESlateVisibility::Visible);
		if (IsValid(Image_Material1) && IsValid(MatIcon1))
			Image_Material1->SetBrushFromTexture(MatIcon1);
	}
	else
	{
		if (IsValid(HBox_Material1))
			HBox_Material1->SetVisibility(ESlateVisibility::Collapsed);
	}

	// 재료 슬롯 2
	if (MatTag2.IsValid() && ReqAmount2 > 0)
	{
		if (IsValid(HBox_Material2))
			HBox_Material2->SetVisibility(ESlateVisibility::Visible);
		if (IsValid(Image_Material2) && IsValid(MatIcon2))
			Image_Material2->SetBrushFromTexture(MatIcon2);
	}
	else
	{
		if (IsValid(HBox_Material2))
			HBox_Material2->SetVisibility(ESlateVisibility::Collapsed);
	}

	// 재료 슬롯 3
	if (MatTag3.IsValid() && ReqAmount3 > 0)
	{
		if (IsValid(HBox_Material3))
			HBox_Material3->SetVisibility(ESlateVisibility::Visible);
		if (IsValid(Image_Material3) && IsValid(MatIcon3))
			Image_Material3->SetBrushFromTexture(MatIcon3);
	}
	else
	{
		if (IsValid(HBox_Material3))
			HBox_Material3->SetVisibility(ESlateVisibility::Collapsed);
	}

	// 초기 재료 수량 업데이트
	UpdateMaterialAmounts();

#if INV_DEBUG_BUILD
	UE_LOG(LogTemp, Warning, TEXT("BuildModeHUD: SetBuildingInfo - Name=%s, Mat1=%s x%d, Mat2=%s x%d, Mat3=%s x%d"),
		*BuildingName.ToString(),
		*MatTag1.ToString(), ReqAmount1,
		*MatTag2.ToString(), ReqAmount2,
		*MatTag3.ToString(), ReqAmount3);
#endif
}

void UInv_BuildModeHUD::UpdatePlacementStatus(bool bCanPlace)
{
	// 상태 변경 시에만 업데이트 (매 프레임 로그 방지)
	if (bPlacementStatusInitialized && bPreviousCanPlace == bCanPlace)
		return;

	bPreviousCanPlace = bCanPlace;
	bPlacementStatusInitialized = true;

	if (IsValid(Text_PlacementStatus))
	{
		if (bCanPlace)
		{
			Text_PlacementStatus->SetText(FText::FromString(TEXT("건축 가능")));
			Text_PlacementStatus->SetColorAndOpacity(FSlateColor(FLinearColor::Green));
		}
		else
		{
			Text_PlacementStatus->SetText(FText::FromString(TEXT("건축 불가")));
			Text_PlacementStatus->SetColorAndOpacity(FSlateColor(FLinearColor::Red));
		}
	}

#if INV_DEBUG_BUILD
	UE_LOG(LogTemp, Warning, TEXT("BuildModeHUD: 배치상태 변경 → %s"),
		bCanPlace ? TEXT("건축 가능") : TEXT("건축 불가"));
#endif
}

void UInv_BuildModeHUD::UpdateMaterialAmounts()
{
	APlayerController* PC = GetOwningPlayer();
	if (!IsValid(PC)) return;

	UInv_InventoryComponent* InvComp = PC->FindComponentByClass<UInv_InventoryComponent>();
	if (!IsValid(InvComp)) return;

	// 헬퍼 람다: 재료 수량 텍스트 업데이트
	auto UpdateSlot = [&InvComp](const FGameplayTag& Tag, int32 Required, UTextBlock* AmountText)
	{
		if (!Tag.IsValid() || Required <= 0 || !IsValid(AmountText)) return;

		const int32 CurrentAmount = InvComp->GetTotalMaterialCount(Tag);
		AmountText->SetText(FText::FromString(FString::Printf(TEXT("%d/%d"), CurrentAmount, Required)));

		// 부족하면 빨간색, 충족하면 흰색
		if (CurrentAmount < Required)
		{
			AmountText->SetColorAndOpacity(FSlateColor(FLinearColor::Red));
		}
		else
		{
			AmountText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		}
	};

	UpdateSlot(StoredMatTag1, StoredReqAmount1, Text_Material1Amount);
	UpdateSlot(StoredMatTag2, StoredReqAmount2, Text_Material2Amount);
	UpdateSlot(StoredMatTag3, StoredReqAmount3, Text_Material3Amount);

#if INV_DEBUG_BUILD
	const int32 Cur1 = StoredMatTag1.IsValid() ? InvComp->GetTotalMaterialCount(StoredMatTag1) : 0;
	const int32 Cur2 = StoredMatTag2.IsValid() ? InvComp->GetTotalMaterialCount(StoredMatTag2) : 0;
	const int32 Cur3 = StoredMatTag3.IsValid() ? InvComp->GetTotalMaterialCount(StoredMatTag3) : 0;
	UE_LOG(LogTemp, Warning, TEXT("BuildModeHUD: 재료 수량 업데이트 - Mat1: %d/%d, Mat2: %d/%d, Mat3: %d/%d"),
		Cur1, StoredReqAmount1, Cur2, StoredReqAmount2, Cur3, StoredReqAmount3);
#endif
}
