// Capstone Project Helluna

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "HellunaHealthHUDWidget.generated.h"

class UImage;
class UTextBlock;
class UProgressBar;
class UMaterialInstanceDynamic;
class UHellunaHealthComponent;
class AHellunaWeaponBase;
class AHeroWeapon_GunBase;

/**
 * 헬루나 메인 HUD 위젯 — 원형 270도 체력 게이지 + 무기 슬롯 + 탄약.
 *
 * [레이어 구성 — 레퍼런스 이미지 기반]
 *  Layer 1: OuterRingImage    — 외곽 장식 링 (M_OuterRing270)
 *  Layer 2: HealthArcImage    — 270° 체력 게이지 (M_HealthArc270, MID)
 *  Layer 3: InnerRingImage    — 내측 얇은 링 (M_InnerRing270)
 *  Layer 4: AmmoText          — 현재 탄약 숫자 (우측)
 *  Layer 5: PrimaryWeaponIcon — 현재 장착 무기 아이콘 (우측, 밝게)
 *  Layer 6: SecondaryWeaponIcon — 보조 무기 아이콘 (우측 아래, 어둡게)
 *  Layer 7: StaminaBar / SubBar — 하단 가로 바 x2
 *
 * [사용법]
 *  1. 이 클래스를 부모로 WBP 생성 (WBP_HellunaHealthHUD)
 *  2. BP Designer에서 각 위젯의 이름을 아래 BindWidget 이름과 일치시킴
 *  3. HeroCharacter에서 BeginPlay 시 CreateWidget → AddToViewport
 */
UCLASS()
class HELLUNA_API UHellunaHealthHUDWidget : public UUserWidget
{
	GENERATED_BODY()

public:

	// ===== 외부 호출 =====

	/** 체력 갱신 (0~1 정규화 값) */
	UFUNCTION(BlueprintCallable, Category = "HealthHUD")
	void UpdateHealth(float NormalizedHealth);

	/** 무기 교체 — 주무기 */
	UFUNCTION(BlueprintCallable, Category = "HealthHUD")
	void UpdatePrimaryWeapon(AHellunaWeaponBase* Weapon);

	/** 무기 교체 — 보조무기 */
	UFUNCTION(BlueprintCallable, Category = "HealthHUD")
	void UpdateSecondaryWeapon(AHellunaWeaponBase* Weapon);

	/** 탄약 갱신 (현재탄만) */
	UFUNCTION(BlueprintCallable, Category = "HealthHUD")
	void UpdateAmmoText(int32 CurrentAmmo);

	/** 스태미나 바 갱신 (0~1) */
	UFUNCTION(BlueprintCallable, Category = "HealthHUD")
	void UpdateStaminaBar(float Percent);

	/** 보조 바 갱신 (0~1) */
	UFUNCTION(BlueprintCallable, Category = "HealthHUD")
	void UpdateSubBar(float Percent);

protected:
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual void NativeDestruct() override;

	// ===== BindWidgetOptional (WBP 또는 동적 생성 모두 지원) =====

	/** 외곽 장식 링 */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidgetOptional))
	TObjectPtr<UImage> OuterRingImage = nullptr;

	/** 체력 게이지 (270도 아크) */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidgetOptional))
	TObjectPtr<UImage> HealthArcImage = nullptr;

	/** 내측 장식 링 */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidgetOptional))
	TObjectPtr<UImage> InnerRingImage = nullptr;

	/** 현재 탄약 수 텍스트 */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> AmmoText = nullptr;

	/** 주무기 아이콘 */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidgetOptional))
	TObjectPtr<UImage> PrimaryWeaponIcon = nullptr;

	/** 보조무기 아이콘 */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidgetOptional))
	TObjectPtr<UImage> SecondaryWeaponIcon = nullptr;

	/** 스태미나 바 */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> StaminaBar = nullptr;

	/** 보조 게이지 바 */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> SubBar = nullptr;

	// ===== 머티리얼 경로 (에디터 오버라이드 가능) =====

	UPROPERTY(EditDefaultsOnly, Category = "HealthHUD|Materials")
	TSoftObjectPtr<UMaterialInterface> HealthArcMaterialPath;

	UPROPERTY(EditDefaultsOnly, Category = "HealthHUD|Materials")
	TSoftObjectPtr<UMaterialInterface> OuterRingMaterialPath;

	UPROPERTY(EditDefaultsOnly, Category = "HealthHUD|Materials")
	TSoftObjectPtr<UMaterialInterface> InnerRingMaterialPath;

private:

	// ===== Dynamic Material Instances =====
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> HealthArcMID = nullptr;

	// ===== 체력 색상 보간 =====
	float CurrentHealthPercent = 1.f;
	float DisplayedHealthPercent = 1.f;

	/** HP 퍼센트에 따른 색상 반환 (>60%: 초록, 25~60: 노랑, <25: 빨강) */
	FLinearColor GetHealthColor(float Percent) const;

	// ===== 탄약 폴링 (WeaponHUDWidget 패턴) =====
	UPROPERTY()
	TWeakObjectPtr<AHellunaWeaponBase> TrackedPrimaryWeapon;

	int32 LastPolledAmmo = -1;
};
