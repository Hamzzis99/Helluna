// Capstone Project Helluna

#include "UI/Weapon/WeaponHUDWidget.h"

#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"

#include "Weapon/HellunaWeaponBase.h"
#include "Weapon/HeroWeapon_GunBase.h"
#include "Weapon/HellunaFarmingWeapon.h"

// ============================================================================
// NativeConstruct — 위젯 생성 직후 초기 상태 설정
// ============================================================================
void UWeaponHUDWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (WeaponIconImage)
		WeaponIconImage->SetVisibility(ESlateVisibility::Collapsed);

	if (AmmoText)
		AmmoText->SetText(FText::GetEmpty());
}

// ============================================================================
// NativeTick — 매 프레임 탄약 폴링
//
// 델리게이트 방식 대신 폴링을 사용하는 이유:
//  - OnAmmoChanged 델리게이트는 서버에서만 브로드캐스트되고
//    클라이언트는 OnRep_CurrentAmmoInMag 경로로 값을 받는다.
//  - 무기가 재스폰될 때마다 재구독이 필요해 타이밍 문제가 잦다.
//  - TrackedWeapon의 CurrentMag를 직접 읽으면 복제된 순간 즉시 반영된다.
//  - 값이 바뀔 때만 SetText를 호출하므로 성능 부담이 없다.
// ============================================================================
void UWeaponHUDWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (!TrackedWeapon.IsValid()) return;

	AHeroWeapon_GunBase* Gun = Cast<AHeroWeapon_GunBase>(TrackedWeapon.Get());
	if (!Gun) return;

	// 값이 바뀔 때만 텍스트 갱신 (불필요한 SetText 방지)
	if (Gun->CurrentMag != LastDisplayedAmmo || Gun->MaxMag != LastDisplayedMaxAmmo)
	{
		LastDisplayedAmmo    = Gun->CurrentMag;
		LastDisplayedMaxAmmo = Gun->MaxMag;
		UpdateAmmo(Gun->CurrentMag, Gun->MaxMag);
	}
}

// ============================================================================
// UpdateWeapon — 무기 교체 시 호출 (HeroCharacter::SetCurrentWeapon / OnRep_CurrentWeapon)
//
// - TrackedWeapon을 갱신해 NativeTick 폴링 대상을 교체한다.
// - 아이콘 및 탄약 초기값을 즉시 반영한다.
// ============================================================================
void UWeaponHUDWidget::UpdateWeapon(AHellunaWeaponBase* NewWeapon)
{
	// NativeTick 폴링 대상 교체 및 이전 표시값 리셋
	TrackedWeapon        = NewWeapon;
	LastDisplayedAmmo    = -1;
	LastDisplayedMaxAmmo = -1;

	if (!NewWeapon)
	{
		if (WeaponIconImage)
			WeaponIconImage->SetVisibility(ESlateVisibility::Collapsed);
		if (WeaponNameText)
			WeaponNameText->SetText(FText::GetEmpty());
		if (AmmoText)
			AmmoText->SetText(FText::GetEmpty());
		return;
	}

	// ── 아이콘 갱신 ────────────────────────────────────────────────
	if (WeaponIconImage)
	{
		UTexture2D* Icon = NewWeapon->GetWeaponIcon();
		if (Icon)
		{
			WeaponIconImage->SetBrushFromTexture(Icon);
			WeaponIconImage->SetBrushTintColor(FLinearColor::White);
			WeaponIconImage->SetColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, 1.f));
			WeaponIconImage->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
		else
		{
			WeaponIconImage->SetVisibility(ESlateVisibility::Collapsed);
		}
	}

	// ── 무기 이름 갱신 ─────────────────────────────────────────────
	if (WeaponNameText)
	{
		const FText DisplayName = NewWeapon->GetWeaponDisplayName();
		WeaponNameText->SetText(DisplayName.IsEmpty()
			? FText::FromString(TEXT("-"))
			: DisplayName);
	}

	// ── 탄약 텍스트 초기값 ─────────────────────────────────────────
	// NativeTick이 매 프레임 갱신하므로 여기서는 현재 값만 표시한다.
	if (AHeroWeapon_GunBase* Gun = Cast<AHeroWeapon_GunBase>(NewWeapon))
	{
		UpdateAmmo(Gun->CurrentMag, Gun->MaxMag);
	}
	else if (Cast<AHellunaFarmingWeapon>(NewWeapon))
	{
		// 파밍 무기는 탄약 개념이 없으므로 X로 표시
		if (AmmoText)
			AmmoText->SetText(FText::FromString(TEXT("∞")));
	}
	else
	{
		if (AmmoText)
			AmmoText->SetText(FText::GetEmpty());
	}
}

// ============================================================================
// UpdateAmmo — 탄약 텍스트만 갱신 ("현재 / 최대" 형식)
// ============================================================================
void UWeaponHUDWidget::UpdateAmmo(int32 Current, int32 Max)
{
	if (AmmoText)
	{
		const FString AmmoStr = FString::Printf(TEXT("%d / %d"), Current, Max);
		AmmoText->SetText(FText::FromString(AmmoStr));
	}
}
