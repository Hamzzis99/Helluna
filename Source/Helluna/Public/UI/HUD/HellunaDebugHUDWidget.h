// ════════════════════════════════════════════════════════════════════════════════
// HellunaDebugHUDWidget.h
// ════════════════════════════════════════════════════════════════════════════════
//
// 디버그 전용 HUD 오버레이 — FPS / Player ID / Hero Class / Ping / Net Role 표시.
// 좌하단(7시 방향) 배치, F5 토글, Helluna.Debug.ShowHUD 콘솔 변수.
//
// ════════════════════════════════════════════════════════════════════════════════

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "HellunaTypes.h"
#include "HellunaDebugHUDWidget.generated.h"

class UTextBlock;

UCLASS()
class HELLUNA_API UHellunaDebugHUDWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Debug")
	void ForceRefreshInfo();

	UFUNCTION(BlueprintCallable, Category = "Debug")
	void ToggleVisibility();

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Debug")
	bool IsDebugVisible() const;

protected:
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	UPROPERTY(BlueprintReadWrite, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_FPS = nullptr;

	UPROPERTY(BlueprintReadWrite, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_PlayerId = nullptr;

	UPROPERTY(BlueprintReadWrite, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_HeroClass = nullptr;

	UPROPERTY(BlueprintReadWrite, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_Ping = nullptr;

	UPROPERTY(BlueprintReadWrite, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_NetRole = nullptr;

private:
	float FPSAccumulator = 0.f;
	int32 FrameCount = 0;
	float FPSUpdateInterval = 0.25f;
	float LastDisplayedFPS = 0.f;

	void UpdatePlayerInfo();
	void UpdateFPS(float DeltaTime);
	static FString HeroTypeToDisplayString(EHellunaHeroType HeroType);
};
