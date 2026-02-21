#pragma once

#include "CoreMinimal.h"
#include "Login/Widget/HellunaCharacterSelectWidget.h"
#include "HellunaCharSelectWidget_V1.generated.h"

class UImage;
class AHellunaCharacterPreviewActor;
class UTextureRenderTarget2D;
class UMaterialInstanceDynamic;

/**
 * ============================================
 * UHellunaCharSelectWidget_V1
 * ============================================
 *
 * V1 캐릭터 선택 프리뷰 위젯
 * 캐릭터별 개별 RenderTarget + PreviewActor (3개씩)
 *
 * ============================================
 * 구조:
 * ============================================
 * - PreviewImage_Lui / Luna / Liam: 각 캐릭터 프리뷰 이미지 (BindWidgetOptional)
 * - PreviewActors[3]: 각 캐릭터 프리뷰 액터 참조
 * - PreviewMaterials[3]: 동적 머티리얼 인스턴스
 *
 * ============================================
 * BP 설정:
 * ============================================
 * - WBP_CharacterSelectWidget (기존) → 부모를 이 클래스로 Reparent
 * - PreviewImage_Lui/Luna/Liam (UImage, BindWidgetOptional)
 *
 * 작성자: Gihyeon
 */
UCLASS()
class HELLUNA_API UHellunaCharSelectWidget_V1 : public UHellunaCharacterSelectWidget
{
	GENERATED_BODY()

public:
	/**
	 * V1 프리뷰 초기화: RT 3개 + PreviewActor 3개
	 *
	 * @param InRenderTargets - Lui(0), Luna(1), Liam(2) 순서의 RenderTarget 배열
	 * @param InPreviewActors - Lui(0), Luna(1), Liam(2) 순서의 프리뷰 액터 배열
	 */
	UFUNCTION(BlueprintCallable, Category = "Character Select (캐릭터 선택)")
	void SetupPreviewV1(
		const TArray<UTextureRenderTarget2D*>& InRenderTargets,
		const TArray<AHellunaCharacterPreviewActor*>& InPreviewActors);

	virtual void CleanupPreview() override;

protected:
	virtual void OnCharacterHovered(int32 Index, bool bHovered) override;

	// ============================================
	// 📌 V1 전용 UI 바인딩
	// ============================================

	/** Lui 프리뷰 이미지 (Index 0) */
	UPROPERTY(meta = (BindWidgetOptional, DisplayName = "루이 프리뷰 이미지"))
	TObjectPtr<UImage> PreviewImage_Lui;

	/** Luna 프리뷰 이미지 (Index 1) */
	UPROPERTY(meta = (BindWidgetOptional, DisplayName = "루나 프리뷰 이미지"))
	TObjectPtr<UImage> PreviewImage_Luna;

	/** Liam 프리뷰 이미지 (Index 2) */
	UPROPERTY(meta = (BindWidgetOptional, DisplayName = "리암 프리뷰 이미지"))
	TObjectPtr<UImage> PreviewImage_Liam;

	// ============================================
	// 📌 V1 내부 상태
	// ============================================

	/** 프리뷰 액터 참조 (소유권은 LoginController) */
	UPROPERTY()
	TArray<TObjectPtr<AHellunaCharacterPreviewActor>> PreviewActors;

	/** 동적 머티리얼 인스턴스 (GC 방지) */
	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> PreviewMaterials;
};
