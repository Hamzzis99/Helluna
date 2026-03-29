// Capstone Project Helluna

#include "UI/HUD/HellunaHealthHUDWidget.h"

#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/ProgressBar.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Blueprint/WidgetTree.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Engine/Texture2D.h"

#include "Weapon/HellunaWeaponBase.h"
#include "Weapon/HeroWeapon_GunBase.h"

// ============================================================================
// NativeConstruct — 위젯 생성 시 MID 생성 및 머티리얼 적용
// ============================================================================
void UHellunaHealthHUDWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// ── BindWidgetOptional: WBP에 배치 안 된 위젯은 동적 생성 ──
	UCanvasPanel* RootCanvas = Cast<UCanvasPanel>(GetRootWidget());
	if (!RootCanvas)
	{
		RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("RootCanvas"));
		RootCanvas->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		WidgetTree->RootWidget = RootCanvas;
	}

	auto AddImageToCanvas = [&](TObjectPtr<UImage>& ImgPtr, const FName& Name, FVector2D Pos, FVector2D Size) -> UImage*
	{
		if (!ImgPtr)
		{
			ImgPtr = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), Name);
			UCanvasPanelSlot* PanelSlot = RootCanvas->AddChildToCanvas(ImgPtr);
			if (PanelSlot)
			{
				PanelSlot->SetAnchors(FAnchors(1.f, 1.f, 1.f, 1.f));
				PanelSlot->SetOffsets(FMargin(Pos.X, Pos.Y, Size.X, Size.Y));
				PanelSlot->SetAutoSize(false);
			}
		}
		return ImgPtr;
	};

	const float RingSize = 256.f;
	const float OffX = -(RingSize + 30.f);
	const float OffY = -(RingSize + 60.f);

	AddImageToCanvas(OuterRingImage, TEXT("OuterRingImage"), FVector2D(OffX, OffY), FVector2D(RingSize, RingSize));
	AddImageToCanvas(HealthArcImage, TEXT("HealthArcImage"), FVector2D(OffX, OffY), FVector2D(RingSize, RingSize));
	AddImageToCanvas(InnerRingImage, TEXT("InnerRingImage"), FVector2D(OffX, OffY), FVector2D(RingSize, RingSize));
	AddImageToCanvas(PrimaryWeaponIcon, TEXT("PrimaryWeaponIcon"), FVector2D(OffX + 180.f, OffY + 100.f), FVector2D(80.f, 40.f));
	AddImageToCanvas(SecondaryWeaponIcon, TEXT("SecondaryWeaponIcon"), FVector2D(OffX + 190.f, OffY + 150.f), FVector2D(60.f, 30.f));

	if (!AmmoText)
	{
		AmmoText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("AmmoText"));
		UCanvasPanelSlot* AmmoPanelSlot = RootCanvas->AddChildToCanvas(AmmoText);
		if (AmmoPanelSlot)
		{
			AmmoPanelSlot->SetAnchors(FAnchors(1.f, 1.f, 1.f, 1.f));
			AmmoPanelSlot->SetOffsets(FMargin(OffX + 190.f, OffY + 60.f, 60.f, 40.f));
			AmmoPanelSlot->SetAutoSize(false);
		}
	}

	if (!StaminaBar)
	{
		StaminaBar = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), TEXT("StaminaBar"));
		UCanvasPanelSlot* StaminaPanelSlot = RootCanvas->AddChildToCanvas(StaminaBar);
		if (StaminaPanelSlot)
		{
			StaminaPanelSlot->SetAnchors(FAnchors(1.f, 1.f, 1.f, 1.f));
			StaminaPanelSlot->SetOffsets(FMargin(OffX + 70.f, OffY + 210.f, 110.f, 6.f));
			StaminaPanelSlot->SetAutoSize(false);
		}
	}

	if (!SubBar)
	{
		SubBar = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), TEXT("SubBar"));
		UCanvasPanelSlot* SubPanelSlot = RootCanvas->AddChildToCanvas(SubBar);
		if (SubPanelSlot)
		{
			SubPanelSlot->SetAnchors(FAnchors(1.f, 1.f, 1.f, 1.f));
			SubPanelSlot->SetOffsets(FMargin(OffX + 70.f, OffY + 222.f, 110.f, 6.f));
			SubPanelSlot->SetAutoSize(false);
		}
	}

	// ── 체력 게이지 머티리얼 ──
	if (HealthArcImage)
	{
		UMaterialInterface* BaseMat = HealthArcMaterialPath.LoadSynchronous();
		if (!BaseMat)
		{
			BaseMat = LoadObject<UMaterialInterface>(nullptr,
				TEXT("/Game/UI/Materials/M_HealthArc270.M_HealthArc270"));
		}

		if (BaseMat)
		{
			HealthArcMID = UMaterialInstanceDynamic::Create(BaseMat, this);
			FSlateBrush Brush;
			Brush.SetResourceObject(HealthArcMID);
			Brush.ImageSize = FVector2D(256.f, 256.f);
			Brush.DrawAs = ESlateBrushDrawType::Image;
			HealthArcImage->SetBrush(Brush);

			HealthArcMID->SetScalarParameterValue(TEXT("HealthPercent"), 1.f);
			HealthArcMID->SetVectorParameterValue(TEXT("HealthColor"),
				FLinearColor(0.4f, 0.93f, 0.65f, 1.f));
		}
	}

	// ── 외곽 링 머티리얼 ──
	if (OuterRingImage)
	{
		UMaterialInterface* OuterMat = OuterRingMaterialPath.LoadSynchronous();
		if (!OuterMat)
		{
			OuterMat = LoadObject<UMaterialInterface>(nullptr,
				TEXT("/Game/UI/Materials/M_OuterRing270.M_OuterRing270"));
		}
		if (OuterMat)
		{
			UMaterialInstanceDynamic* OuterMID = UMaterialInstanceDynamic::Create(OuterMat, this);
			FSlateBrush Brush;
			Brush.SetResourceObject(OuterMID);
			Brush.ImageSize = FVector2D(256.f, 256.f);
			Brush.DrawAs = ESlateBrushDrawType::Image;
			OuterRingImage->SetBrush(Brush);
		}
	}

	// ── 내측 링 머티리얼 ──
	if (InnerRingImage)
	{
		UMaterialInterface* InnerMat = InnerRingMaterialPath.LoadSynchronous();
		if (!InnerMat)
		{
			InnerMat = LoadObject<UMaterialInterface>(nullptr,
				TEXT("/Game/UI/Materials/M_InnerRing270.M_InnerRing270"));
		}
		if (InnerMat)
		{
			UMaterialInstanceDynamic* InnerMID = UMaterialInstanceDynamic::Create(InnerMat, this);
			FSlateBrush Brush;
			Brush.SetResourceObject(InnerMID);
			Brush.ImageSize = FVector2D(256.f, 256.f);
			Brush.DrawAs = ESlateBrushDrawType::Image;
			InnerRingImage->SetBrush(Brush);
		}
	}

	// ── 초기값 설정 ──
	if (AmmoText)
	{
		AmmoText->SetText(FText::FromString(TEXT("0")));
		FSlateFontInfo FontInfo = AmmoText->GetFont();
		FontInfo.Size = 28;
		AmmoText->SetFont(FontInfo);
		AmmoText->SetColorAndOpacity(FSlateColor(FLinearColor(1.f, 1.f, 1.f, 0.95f)));
	}

	if (PrimaryWeaponIcon)
	{
		PrimaryWeaponIcon->SetColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, 0.9f));
		PrimaryWeaponIcon->SetVisibility(ESlateVisibility::Collapsed);
	}

	if (SecondaryWeaponIcon)
	{
		SecondaryWeaponIcon->SetColorAndOpacity(FLinearColor(0.5f, 0.5f, 0.55f, 0.6f));
		SecondaryWeaponIcon->SetVisibility(ESlateVisibility::Collapsed);
	}

	if (StaminaBar)
	{
		StaminaBar->SetPercent(1.f);
		StaminaBar->SetFillColorAndOpacity(FLinearColor(0.35f, 0.86f, 0.54f, 0.8f));
	}

	if (SubBar)
	{
		SubBar->SetPercent(0.65f);
		SubBar->SetFillColorAndOpacity(FLinearColor(0.35f, 0.86f, 0.54f, 0.45f));
	}
}

// ============================================================================
// NativeTick — 체력 보간 + 탄약 폴링
// ============================================================================
void UHellunaHealthHUDWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// ── 체력 게이지 부드러운 보간 ──
	if (!FMath::IsNearlyEqual(DisplayedHealthPercent, CurrentHealthPercent, 0.001f))
	{
		DisplayedHealthPercent = FMath::FInterpTo(
			DisplayedHealthPercent, CurrentHealthPercent, InDeltaTime, 8.f);

		if (HealthArcMID)
		{
			HealthArcMID->SetScalarParameterValue(TEXT("HealthPercent"), DisplayedHealthPercent);

			const FLinearColor NewColor = GetHealthColor(DisplayedHealthPercent);
			HealthArcMID->SetVectorParameterValue(TEXT("HealthColor"), NewColor);
		}
	}

	// ── 탄약 폴링 (WeaponHUDWidget 패턴) ──
	if (TrackedPrimaryWeapon.IsValid())
	{
		if (AHeroWeapon_GunBase* Gun = Cast<AHeroWeapon_GunBase>(TrackedPrimaryWeapon.Get()))
		{
			if (Gun->CurrentMag != LastPolledAmmo)
			{
				LastPolledAmmo = Gun->CurrentMag;
				UpdateAmmoText(Gun->CurrentMag);
			}
		}
	}
}

// ============================================================================
// NativeDestruct
// ============================================================================
void UHellunaHealthHUDWidget::NativeDestruct()
{
	TrackedPrimaryWeapon.Reset();
	Super::NativeDestruct();
}

// ============================================================================
// UpdateHealth — 정규화 체력값 (0~1) 수신
// ============================================================================
void UHellunaHealthHUDWidget::UpdateHealth(float NormalizedHealth)
{
	CurrentHealthPercent = FMath::Clamp(NormalizedHealth, 0.f, 1.f);
}

// ============================================================================
// UpdatePrimaryWeapon — 주무기 교체
// ============================================================================
void UHellunaHealthHUDWidget::UpdatePrimaryWeapon(AHellunaWeaponBase* Weapon)
{
	TrackedPrimaryWeapon = Weapon;
	LastPolledAmmo = -1;

	if (!Weapon)
	{
		if (PrimaryWeaponIcon)
			PrimaryWeaponIcon->SetVisibility(ESlateVisibility::Collapsed);
		if (AmmoText)
			AmmoText->SetText(FText::FromString(TEXT("-")));
		return;
	}

	if (PrimaryWeaponIcon)
	{
		UTexture2D* Icon = Weapon->GetWeaponIcon();
		if (Icon)
		{
			PrimaryWeaponIcon->SetBrushFromTexture(Icon);
			PrimaryWeaponIcon->SetColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, 0.9f));
			PrimaryWeaponIcon->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
		else
		{
			PrimaryWeaponIcon->SetVisibility(ESlateVisibility::Collapsed);
		}
	}

	if (AHeroWeapon_GunBase* Gun = Cast<AHeroWeapon_GunBase>(Weapon))
	{
		UpdateAmmoText(Gun->CurrentMag);
	}
	else
	{
		if (AmmoText)
			AmmoText->SetText(FText::FromString(TEXT("-")));
	}
}

// ============================================================================
// UpdateSecondaryWeapon — 보조무기 교체
// ============================================================================
void UHellunaHealthHUDWidget::UpdateSecondaryWeapon(AHellunaWeaponBase* Weapon)
{
	if (!SecondaryWeaponIcon) return;

	if (!Weapon)
	{
		SecondaryWeaponIcon->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}

	UTexture2D* Icon = Weapon->GetWeaponIcon();
	if (Icon)
	{
		SecondaryWeaponIcon->SetBrushFromTexture(Icon);
		SecondaryWeaponIcon->SetColorAndOpacity(FLinearColor(0.5f, 0.5f, 0.55f, 0.6f));
		SecondaryWeaponIcon->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	else
	{
		SecondaryWeaponIcon->SetVisibility(ESlateVisibility::Collapsed);
	}
}

// ============================================================================
// UpdateAmmoText — 현재탄만 표시
// ============================================================================
void UHellunaHealthHUDWidget::UpdateAmmoText(int32 CurrentAmmo)
{
	if (AmmoText)
	{
		AmmoText->SetText(FText::FromString(FString::Printf(TEXT("%d"), CurrentAmmo)));
	}
}

// ============================================================================
// UpdateStaminaBar / UpdateSubBar
// ============================================================================
void UHellunaHealthHUDWidget::UpdateStaminaBar(float Percent)
{
	if (StaminaBar)
		StaminaBar->SetPercent(FMath::Clamp(Percent, 0.f, 1.f));
}

void UHellunaHealthHUDWidget::UpdateSubBar(float Percent)
{
	if (SubBar)
		SubBar->SetPercent(FMath::Clamp(Percent, 0.f, 1.f));
}

// ============================================================================
// GetHealthColor — HP 퍼센트에 따른 색상 전환
//  >60%: 민트 그린  |  25~60%: 앰버  |  <25%: 빨강
// ============================================================================
FLinearColor UHellunaHealthHUDWidget::GetHealthColor(float Percent) const
{
	const FLinearColor Green(0.4f, 0.93f, 0.65f, 1.f);
	const FLinearColor Amber(0.96f, 0.62f, 0.04f, 1.f);
	const FLinearColor Red(0.97f, 0.27f, 0.27f, 1.f);

	if (Percent > 0.6f)
	{
		return Green;
	}
	else if (Percent > 0.25f)
	{
		const float T = (Percent - 0.25f) / 0.35f;
		return FMath::Lerp(Amber, Green, T);
	}
	else
	{
		const float T = Percent / 0.25f;
		return FMath::Lerp(Red, Amber, T);
	}
}
