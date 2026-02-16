#pragma once

#include "CoreMinimal.h"
#include "Login/Widget/HellunaCharacterSelectWidget.h"
#include "HellunaCharSelectWidget_V2.generated.h"

class UImage;
class AHellunaCharacterSelectSceneV2;
class UTextureRenderTarget2D;
class UMaterialInstanceDynamic;

/**
 * ============================================
 * UHellunaCharSelectWidget_V2
 * ============================================
 *
 * V2 캐릭터 선택 프리뷰 위젯
 * 3캐릭터 1카메라 통합 씬 + RenderTarget 1개
 *
 * ============================================
 * 구조:
 * ============================================
 * - PreviewImage_V2: 전체 장면 프리뷰 이미지 (BindWidgetOptional)
 * - PreviewSceneV2: V2 씬 액터 참조
 * - PreviewMaterialV2: 동적 머티리얼 인스턴스
 *
 * ============================================
 * BP 설정:
 * ============================================
 * - WBP_CharacterSelectWidget_V2 (신규 생성) → 부모: 이 클래스
 * - PreviewImage_V2 (UImage, BindWidgetOptional)
 *
 * 작성자: Gihyeon
 */
UCLASS()
class HELLUNA_API UHellunaCharSelectWidget_V2 : public UHellunaCharacterSelectWidget
{
	GENERATED_BODY()

public:
	/**
	 * V2 프리뷰 초기화: RT 1개 + SceneV2 1개
	 *
	 * @param InRenderTarget - 공유 RenderTarget
	 * @param InScene - V2 씬 액터
	 */
	UFUNCTION(BlueprintCallable, Category = "Character Select (캐릭터 선택)")
	void SetupPreviewV2(
		UTextureRenderTarget2D* InRenderTarget,
		AHellunaCharacterSelectSceneV2* InScene);

	virtual void CleanupPreview() override;

protected:
	virtual void OnCharacterHovered(int32 Index, bool bHovered) override;

	// ============================================
	// 📌 V2 전용 UI 바인딩
	// ============================================

	/** V2 전체 장면 프리뷰 이미지 (3캐릭터 한 장면) */
	UPROPERTY(meta = (BindWidgetOptional, DisplayName = "V2 프리뷰 이미지"))
	TObjectPtr<UImage> PreviewImage_V2;

	// ============================================
	// 📌 V2 내부 상태
	// ============================================

	/** V2 씬 액터 참조 */
	UPROPERTY()
	TObjectPtr<AHellunaCharacterSelectSceneV2> PreviewSceneV2;

	/** V2 머티리얼 인스턴스 */
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> PreviewMaterialV2;
};
