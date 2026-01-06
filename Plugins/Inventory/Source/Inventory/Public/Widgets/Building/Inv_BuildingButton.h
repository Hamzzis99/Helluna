// Gihyeon's Inventory Project

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameplayTagContainer.h"
#include "Inv_BuildingButton.generated.h"

class UButton;
class UImage;
class UTextBlock;

/**
 * 빌드 메뉴에서 개별 건물 선택 버튼 위젯
 * 블루프린트에서 UI 디자인 후 이 클래스를 상속받아 사용
 */
UCLASS()
class INVENTORY_API UInv_BuildingButton : public UUserWidget
{
	GENERATED_BODY()

public:
	// 건물 정보 설정
	void SetBuildingInfo(const FText& Name, UTexture2D* Icon, TSubclassOf<AActor> GhostClass, TSubclassOf<AActor> BuildingClass, int32 ID);

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	// 버튼 클릭 이벤트
	UFUNCTION()
	void OnButtonClicked();

	// 재료 체크 함수
	bool HasRequiredMaterials(); // const 제거
	void UpdateButtonState();

	// === 블루프린트에서 바인딩할 위젯들 (meta = (BindWidget)) ===
	
	// 클릭 가능한 버튼
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> Button_Main;

	// 건물 아이콘 이미지
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> Image_Icon;

	// 건물 이름 텍스트
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> Text_BuildingName;

	// === 건물 정보 (블루프린트에서 설정 가능) ===
	
	// 건물 이름
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Building", meta = (AllowPrivateAccess = "true"))
	FText BuildingName;

	// 건물 아이콘
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Building", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UTexture2D> BuildingIcon;

	// 고스트 액터 클래스
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Building", meta = (AllowPrivateAccess = "true"))
	TSubclassOf<AActor> GhostActorClass;

	// 실제 건물 액터 클래스
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Building", meta = (AllowPrivateAccess = "true"))
	TSubclassOf<AActor> ActualBuildingClass;

	// 건물 ID
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Building", meta = (AllowPrivateAccess = "true"))
	int32 BuildingID = 0;

	// === 재료 정보 ===

	// 필요한 재료 1 태그 (Craftables 중 선택)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Building|Materials", meta = (AllowPrivateAccess = "true", Categories = "GameItems.Craftables"))
	FGameplayTag RequiredMaterialTag;

	// 필요한 재료 1 개수
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Building|Materials", meta = (AllowPrivateAccess = "true"))
	int32 RequiredAmount = 0;

	// 필요한 재료 2 태그 (Craftables 중 선택, None이면 미사용)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Building|Materials", meta = (AllowPrivateAccess = "true", Categories = "GameItems.Craftables"))
	FGameplayTag RequiredMaterialTag2;

	// 필요한 재료 2 개수
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Building|Materials", meta = (AllowPrivateAccess = "true"))
	int32 RequiredAmount2 = 0;
};

