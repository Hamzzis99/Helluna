// Gihyeon's Inventory Project

#include "Widgets/Building/Inv_BuildModeHUD.h"
#include "Inventory.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Engine/Texture2D.h"
#include "GameFramework/PlayerController.h"
#include "InventoryManagement/Components/Inv_InventoryComponent.h"

void UInv_BuildModeHUD::NativeConstruct()
{
	Super::NativeConstruct();

	bPreviousCanPlace = false;
	bPlacementStatusInitialized = false;

	if (IsValid(Text_PlacementStatus))
	{
		Text_PlacementStatus->SetText(FText::FromString(TEXT("배치 대기 중...")));
	}

	if (IsValid(Image_Preview))
	{
		Image_Preview->SetVisibility(ESlateVisibility::Collapsed);
	}

	// 재료 아이콘 크기 제한 (기본 브러시 흰색 큰 사각형 방지)
	auto FixIcon = [](UImage* Img)
	{
		if (IsValid(Img))
			Img->SetDesiredSizeOverride(FVector2D(24.0, 24.0));
	};
	FixIcon(Image_Material1);
	FixIcon(Image_Material2);
	FixIcon(Image_Material3);

	// 건물 이름 폰트 크기
	if (IsValid(Text_BuildingName))
	{
		FSlateFontInfo F = Text_BuildingName->GetFont();
		F.Size = 16;
		Text_BuildingName->SetFont(F);
	}

	// 기존 위젯 숨기기
	for (int32 i = 0; i <= 4; ++i)
	{
		FString N = FString::Printf(TEXT("Text_Control%d"), i);
		if (UWidget* W = GetWidgetFromName(*N))
			W->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (UWidget* Sp = GetWidgetFromName(TEXT("Spacer_Controls")))
		Sp->SetVisibility(ESlateVisibility::Collapsed);

	UVerticalBox* VBox = Cast<UVerticalBox>(GetWidgetFromName(TEXT("VBox_Main")));
	if (!VBox) return;

	// === 이전 NativeConstruct에서 추가된 동적 자식 제거 ===
	// WBP에 원래 있는 위젯 이름 목록
	static const TSet<FString> OriginalNames = {
		TEXT("Text_BuildingName"), TEXT("Image_Preview"), TEXT("HBox_MaterialContainer"),
		TEXT("Text_PlacementStatus"), TEXT("Spacer_Controls"),
		TEXT("Text_Control0"), TEXT("Text_Control1"), TEXT("Text_Control2"),
		TEXT("Text_Control3"), TEXT("Text_Control4")
	};

	// VBox 자식 중 원래 이름이 아닌 것 = 동적 추가된 것 → 제거
	TArray<UWidget*> ToRemove;
	for (int32 i = VBox->GetChildrenCount() - 1; i >= 0; --i)
	{
		if (UWidget* Child = VBox->GetChildAt(i))
		{
			if (!OriginalNames.Contains(Child->GetName()))
			{
				ToRemove.Add(Child);
			}
		}
	}
	for (UWidget* W : ToRemove)
	{
		VBox->RemoveChild(W);
	}

	// === Keycap 스타일 조작 가이드 생성 ===
	const FLinearColor CyanColor(0.314f, 0.706f, 1.0f, 1.0f);
	const FLinearColor CyanBg(0.314f, 0.706f, 1.0f, 0.12f);
	const FLinearColor DescColor(0.75f, 0.75f, 0.75f, 1.0f);
	const FLinearColor SepColor(0.45f, 0.45f, 0.45f, 0.8f);

	auto MakeKeycap = [&](const FString& KeyName) -> UOverlay*
	{
		UOverlay* Ov = NewObject<UOverlay>(this);

		UImage* Bg = NewObject<UImage>(this);
		Bg->SetColorAndOpacity(CyanBg);
		Bg->SetDesiredSizeOverride(FVector2D(0, 24));
		if (UOverlaySlot* BgSlot = Cast<UOverlaySlot>(Ov->AddChild(Bg)))
		{
			BgSlot->SetHorizontalAlignment(HAlign_Fill);
			BgSlot->SetVerticalAlignment(VAlign_Fill);
		}

		UTextBlock* Txt = NewObject<UTextBlock>(this);
		FSlateFontInfo Font = Txt->GetFont();
		Font.Size = 11;
		Txt->SetFont(Font);
		Txt->SetText(FText::FromString(KeyName));
		Txt->SetColorAndOpacity(FSlateColor(CyanColor));
		Txt->SetJustification(ETextJustify::Center);
		if (UOverlaySlot* TxtSlot = Cast<UOverlaySlot>(Ov->AddChild(Txt)))
		{
			TxtSlot->SetPadding(FMargin(7.f, 2.f, 7.f, 2.f));
			TxtSlot->SetHorizontalAlignment(HAlign_Center);
			TxtSlot->SetVerticalAlignment(VAlign_Center);
		}

		return Ov;
	};

	auto MakeSep = [&](const FString& S) -> UTextBlock*
	{
		UTextBlock* T = NewObject<UTextBlock>(this);
		FSlateFontInfo F = T->GetFont();
		F.Size = 11;
		T->SetFont(F);
		T->SetText(FText::FromString(S));
		T->SetColorAndOpacity(FSlateColor(SepColor));
		return T;
	};

	auto MakeDesc = [&](const FString& D) -> UTextBlock*
	{
		UTextBlock* T = NewObject<UTextBlock>(this);
		FSlateFontInfo F = T->GetFont();
		F.Size = 13;
		T->SetFont(F);
		T->SetText(FText::FromString(D));
		T->SetColorAndOpacity(FSlateColor(DescColor));
		return T;
	};

	auto AddRow = [&](const TArray<FString>& Keys, const FString& Sep, const FString& Desc)
	{
		UHorizontalBox* HBox = NewObject<UHorizontalBox>(this);
		for (int32 i = 0; i < Keys.Num(); ++i)
		{
			if (i > 0 && !Sep.IsEmpty())
			{
				UTextBlock* SepW = MakeSep(Sep);
				if (auto* Slot = Cast<UHorizontalBoxSlot>(HBox->AddChild(SepW)))
				{
					Slot->SetPadding(FMargin(3.f, 0.f, 3.f, 0.f));
					Slot->SetVerticalAlignment(VAlign_Center);
				}
			}
			UOverlay* Keycap = MakeKeycap(Keys[i]);
			if (auto* Slot = Cast<UHorizontalBoxSlot>(HBox->AddChild(Keycap)))
			{
				Slot->SetVerticalAlignment(VAlign_Center);
				Slot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
			}
		}
		UTextBlock* DescW = MakeDesc(Desc);
		if (auto* Slot = Cast<UHorizontalBoxSlot>(HBox->AddChild(DescW)))
		{
			Slot->SetPadding(FMargin(8.f, 0.f, 0.f, 0.f));
			Slot->SetVerticalAlignment(VAlign_Center);
		}
		if (auto* RowSlot = Cast<UVerticalBoxSlot>(VBox->AddChild(HBox)))
		{
			RowSlot->SetPadding(FMargin(0.f, 2.f, 0.f, 2.f));
		}
	};

	AddRow({TEXT("LMB")}, TEXT(""), TEXT("건축"));
	AddRow({TEXT("Shift"), TEXT("LMB")}, TEXT("+"), TEXT("연속 건축"));
	AddRow({TEXT("RMB")}, TEXT(""), TEXT("건축 중단"));
	AddRow({TEXT("Q"), TEXT("E")}, TEXT("/"), TEXT("회전"));
	AddRow({TEXT("G")}, TEXT(""), TEXT("스냅 회전"));
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

	// 미리보기 이미지 항상 숨김 (고스트 액터가 월드에 표시됨)
	if (IsValid(Image_Preview))
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
			Text_PlacementStatus->SetColorAndOpacity(FSlateColor(FLinearColor(0.29f, 0.87f, 0.5f, 1.0f))); // #4ADE80
		}
		else
		{
			Text_PlacementStatus->SetText(FText::FromString(TEXT("건축 불가")));
			Text_PlacementStatus->SetColorAndOpacity(FSlateColor(FLinearColor(0.97f, 0.44f, 0.44f, 1.0f))); // #F87171
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
