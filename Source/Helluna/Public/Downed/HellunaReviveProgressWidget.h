// Phase 21: Revive Progress HUD Widget (부활 진행 링 + 타이머)
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "HellunaReviveProgressWidget.generated.h"

class UImage;
class UTextBlock;
class UMaterialInstanceDynamic;

/**
 * 부활 진행 HUD 위젯
 * 화면 중앙에 원형 프로그레스 링 + 가운데 남은 시간 표시
 */
UCLASS()
class HELLUNA_API UHellunaReviveProgressWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 프로그레스 업데이트 (0~1) */
	UFUNCTION(BlueprintCallable, Category = "Revive")
	void SetProgress(float InProgress);

	/** 남은 시간 표시 */
	UFUNCTION(BlueprintCallable, Category = "Revive")
	void SetRemainingTime(float Seconds);

	/** 대상 이름 표시 */
	UFUNCTION(BlueprintCallable, Category = "Revive")
	void SetTargetName(const FString& Name);

	/** 초기화 (머티리얼 인스턴스 생성) */
	UFUNCTION(BlueprintCallable, Category = "Revive")
	void InitWidget();

protected:
	virtual void NativeConstruct() override;

	/** 원형 프로그레스 링 이미지 (머티리얼 사용) */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> RingImage;

	/** 남은 시간 텍스트 (링 가운데) */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TimerText;

	/** 대상 이름 텍스트 (링 아래) */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TargetNameText;

	/** "부활 중..." 안내 텍스트 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ActionText;

	/** 링 머티리얼 에셋 (에디터에서 할당) */
	UPROPERTY(EditDefaultsOnly, Category = "Revive|Material")
	TObjectPtr<UMaterialInterface> RingMaterial;

private:
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> RingMID;
};
