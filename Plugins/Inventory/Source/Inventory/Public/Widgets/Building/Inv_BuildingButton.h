// Gihyeon's Inventory Project

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameplayTagContainer.h"
#include "Building/Actor/Inv_BuildableActor.h"
#include "Inv_BuildingButton.generated.h"

class UButton;
class UImage;
class UTextBlock;
class UHorizontalBox;
class UInv_InventoryComponent;

// 건설 카드 클릭 델리게이트 (BuildMenu에서 바인딩)
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnBuildingCardClicked, UInv_BuildingButton*, ClickedButton);

/**
 * 빌드 메뉴에서 개별 건물 선택 버튼 위젯
 * BuildableActorClass 하나만 설정하면 CDO에서 이름/아이콘/재료 등을 자동 로드
 */
UCLASS()
class INVENTORY_API UInv_BuildingButton : public UUserWidget
{
	GENERATED_BODY()

public:
	// 건설 카드 클릭 시 BuildMenu에 알리는 델리게이트
	FOnBuildingCardClicked OnCardClicked;

	// 기존 건설 로직 실행 (디테일 패널의 Button_Build에서 호출)
	void ExecuteBuild();

	// === Getter 함수들 (CDO에서 읽어서 반환) ===
	TSubclassOf<AInv_BuildableActor> GetBuildableActorClass() const { return BuildableActorClass; }
	EBuildCategory GetBuildCategory() const;
	FText GetBuildingName() const;
	FText GetBuildingDescription() const;
	UStaticMesh* GetPreviewMesh() const;
	FRotator GetPreviewRotationOffset() const;
	float GetPreviewCameraDistance() const;
	TSubclassOf<AActor> GetGhostActorClass() const;
	int32 GetBuildingID() const;

	// 재료 정보 Getter (CDO에서)
	FGameplayTag GetRequiredMaterialTag() const;
	int32 GetRequiredAmount() const;
	UTexture2D* GetMaterialIcon1() const;
	FGameplayTag GetRequiredMaterialTag2() const;
	int32 GetRequiredAmount2() const;
	UTexture2D* GetMaterialIcon2() const;
	FGameplayTag GetRequiredMaterialTag3() const;
	int32 GetRequiredAmount3() const;
	UTexture2D* GetMaterialIcon3() const;

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

private:
	// 버튼 클릭 이벤트
	UFUNCTION()
	void OnButtonClicked();

	// 재료 체크 함수
	bool HasRequiredMaterials();
	void UpdateButtonState();

	// 재료 UI 업데이트 (이미지 표시/숨김) — BuildMenu에서 일괄 호출
	void UpdateMaterialUI();

	// CDO 캐시 헬퍼
	const AInv_BuildableActor* GetCDO() const;

	// [최적화] 인벤토리 변경 시 BuildMenu에서 일괄 호출
	friend class UInv_BuildMenu;

	// === 블루프린트에서 바인딩할 위젯들 ===

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> Button_Main;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> Image_Icon;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> Text_BuildingName;

	// 재료 UI
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UHorizontalBox> HorizontalBox_Material1;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> Image_Material1;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> Text_Material1Amount;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UHorizontalBox> HorizontalBox_Material2;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> Image_Material2;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> Text_Material2Amount;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UHorizontalBox> HorizontalBox_Material3;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> Image_Material3;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> Text_Material3Amount;

	// [최적화] 캐싱된 InventoryComponent (FindComponentByClass 반복 호출 방지)
	TWeakObjectPtr<UInv_InventoryComponent> CachedInvComp;
	UInv_InventoryComponent* GetCachedInventoryComponent();

	// === 핵심: 건설 액터 클래스 참조 (이것 하나만 설정하면 됨) ===

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "건설", meta = (AllowPrivateAccess = "true",
		DisplayName = "건설 액터 클래스",
		Tooltip = "건설할 액터의 블루프린트 클래스. 모든 건설 정보(이름, 아이콘, 재료, 프리뷰)는 이 클래스의 CDO에서 자동으로 읽힙니다."))
	TSubclassOf<AInv_BuildableActor> BuildableActorClass;
};
