// Capstone Project Helluna

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "WeaponHUDWidget.generated.h"

class UImage;
class UTextBlock;
class AHellunaWeaponBase;
class AHeroWeapon_GunBase;
class AHellunaFarmingWeapon;

/**
 * 화면에 표시되는 무기 HUD 위젯.
 *
 * [구성]
 *  - WeaponIconImage : 현재 무기 아이콘 이미지
 *  - AmmoText        : 탄약 텍스트 (총기: "현재 / 최대", 파밍무기: "X")
 *
 * [탄약 갱신 방식 — 폴링]
 *  NativeTick에서 TrackedWeapon->CurrentMag를 직접 읽어 값이 바뀔 때만 갱신한다.
 *  델리게이트 구독 방식은 무기 재스폰 시 재구독이 필요하고 서버/클라 타이밍 문제가
 *  발생해서 폴링 방식으로 대체했다.
 *
 * [사용법]
 *  1. 이 클래스를 부모로 WBP 생성
 *  2. BP Designer에서 Image → 이름 "WeaponIconImage" / TextBlock → 이름 "AmmoText" (Is Variable 체크)
 *  3. BP_HeroCharacter 디테일 → 무기 HUD 위젯 클래스에 WBP 지정
 */
UCLASS()
class HELLUNA_API UWeaponHUDWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 무기 교체 시 호출 — 아이콘·탄약 초기값 반영 및 폴링 대상 교체 */
	UFUNCTION(BlueprintCallable, Category = "WeaponHUD")
	void UpdateWeapon(AHellunaWeaponBase* NewWeapon);

	/** 탄약 텍스트만 갱신 */
	UFUNCTION(BlueprintCallable, Category = "WeaponHUD")
	void UpdateAmmo(int32 Current, int32 Max);

protected:
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	/** BP에서 이름으로 바인딩되는 무기 아이콘 이미지 */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
	TObjectPtr<UImage> WeaponIconImage = nullptr;

	/** BP에서 이름으로 바인딩되는 무기 표시 이름 텍스트 */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
	TObjectPtr<UTextBlock> WeaponNameText = nullptr;

	/** BP에서 이름으로 바인딩되는 탄약 텍스트 */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
	TObjectPtr<UTextBlock> AmmoText = nullptr;

private:
	/** NativeTick 폴링 대상 — UpdateWeapon 호출 시 교체됨 */
	UPROPERTY()
	TWeakObjectPtr<AHellunaWeaponBase> TrackedWeapon;

	/** 직전 프레임에 표시한 탄약값 — 변경 시에만 SetText 호출하기 위해 캐싱 */
	int32 LastDisplayedAmmo    = -1;
	int32 LastDisplayedMaxAmmo = -1;
};
