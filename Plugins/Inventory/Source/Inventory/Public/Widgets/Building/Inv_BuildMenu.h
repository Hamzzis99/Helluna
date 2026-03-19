// Gihyeon's Inventory Project

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Inv_BuildMenu.generated.h"

class UHorizontalBox;
class UInv_BuildingButton;
class UWidgetSwitcher;
class UWrapBox;
class UButton;
class UCanvasPanel;
class UOverlay;
class UImage;
class UTextBlock;
class UVerticalBox;
class AInv_BuildingPreviewActor;

/**
 * 빌드 메뉴 메인 위젯
 * 지원 / 보조 / 건설 3개 탭으로 분류
 * 건설 카드 클릭 시 디테일 패널 (3D 프리뷰 + 정보) 표시
 * 블루프린트에서 UI 디자인 후 이 클래스를 상속받아 사용
 */
UCLASS()
class INVENTORY_API UInv_BuildMenu : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// ── 마우스 이벤트 (드래그 회전) ──
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnMouseLeave(const FPointerEvent& InMouseEvent) override;

private:
	// === 탭 전환 함수 ===

	UFUNCTION()
	void ShowSupport();

	UFUNCTION()
	void ShowAuxiliary();

	UFUNCTION()
	void ShowConstruction();

	/** 선택된 탭 활성화 — SpatialInventory::SetActiveGrid 패턴 참고 */
	void SetActiveTab(UWidget* Content, UButton* ActiveButton);

	// === 디테일 패널 함수 ===

	/** 건설 카드 클릭 시 디테일 패널 열기 (토글) */
	void OpenDetailPanel(UInv_BuildingButton* BuildingButton);

	/** 디테일 패널 닫기 */
	void CloseDetailPanel();

	/** 3D 프리뷰 설정 (AttachmentPanel::SetupWeaponPreview 패턴) */
	void SetupBuildingPreview();

	/** 3D 프리뷰 정리 */
	void CleanupBuildingPreview();

	/** 재료 목록 UI 구성 */
	void PopulateDetailMaterials();

	/** 건설 버튼 클릭 */
	UFUNCTION()
	void OnBuildButtonClicked();

	/** 건설 카드 클릭 콜백 (BuildingButton의 OnCardClicked 델리게이트) */
	UFUNCTION()
	void OnCardClicked(UInv_BuildingButton* ClickedButton);

	// === 블루프린트에서 바인딩할 위젯들 — 탭 ===

	// 탭 버튼
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> Button_Support;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> Button_Auxiliary;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> Button_Construction;

	// 탭 콘텐츠 전환기
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UWidgetSwitcher> Switcher;

	// 각 탭의 건물 버튼 컨테이너
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UWrapBox> WrapBox_Support;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UWrapBox> WrapBox_Auxiliary;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UWrapBox> WrapBox_Construction;

	// 기존 컨테이너 — 하위 호환
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UHorizontalBox> HorizontalBox_Buildings;

	// === 블루프린트에서 바인딩할 위젯들 — 디테일 패널 ===

	// 디테일 패널 래퍼 (배경 + 콘텐츠, Collapsed 기본, Overlay 하단에 배치)
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UOverlay> Overlay_Detail;

	// 3D 프리뷰 표시
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> Image_BuildingPreview;

	// 건설물 이름
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_DetailName;

	// 건설물 설명
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_DetailDesc;

	// 재료 목록 컨테이너
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UVerticalBox> VerticalBox_DetailMaterials;

	// 건설하기 버튼
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> Button_Build;

	// === 프리뷰 액터 ===

	// 프리뷰 액터 참조
	UPROPERTY()
	TWeakObjectPtr<AInv_BuildingPreviewActor> BuildingPreviewActor;

	// 프리뷰 액터 BP 클래스 (EditDefaultsOnly)
	UPROPERTY(EditDefaultsOnly, Category = "건설|프리뷰", meta = (DisplayName = "프리뷰 액터 클래스"))
	TSubclassOf<AInv_BuildingPreviewActor> BuildingPreviewActorClass;

	// 현재 선택된 BuildingButton
	TWeakObjectPtr<UInv_BuildingButton> SelectedBuildingButton;

	// 드래그 회전
	bool bIsDragging = false;
	FVector2D DragCurrentPosition = FVector2D::ZeroVector;
	FVector2D DragLastPosition = FVector2D::ZeroVector;

	// 프리뷰 스폰 위치
	static constexpr float PreviewSpawnZ = -10000.f;

	// 캐싱된 프리뷰 이미지 사이즈
	FVector2D CachedPreviewImageSize = FVector2D::ZeroVector;

	// BuildingButton 델리게이트 바인딩 + BuildCategory별 WrapBox 동적 배치
	void BindBuildingButtonDelegates();

	// WidgetTree에서 BuildingButton들을 찾아서 BuildCategory에 따라 WrapBox에 재배치
	void DistributeBuildingButtonsToWrapBoxes();
};
