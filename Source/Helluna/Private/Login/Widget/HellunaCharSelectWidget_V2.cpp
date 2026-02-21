#include "Login/Widget/HellunaCharSelectWidget_V2.h"
#include "Login/Preview/HellunaCharacterSelectSceneV2.h"
#include "Helluna.h"
#include "Components/Image.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

// ════════════════════════════════════════════════════════════════════════════════
// 📌 V2 프리뷰 초기화
// ════════════════════════════════════════════════════════════════════════════════

void UHellunaCharSelectWidget_V2::SetupPreviewV2(
	UTextureRenderTarget2D* InRenderTarget,
	AHellunaCharacterSelectSceneV2* InScene)
{
#if HELLUNA_DEBUG_CHARACTER_PREVIEW_V2
	UE_LOG(LogHelluna, Warning, TEXT(""));
	UE_LOG(LogHelluna, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogHelluna, Warning, TEXT("║  [캐릭터선택위젯V2] SetupPreviewV2                         ║"));
	UE_LOG(LogHelluna, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogHelluna, Warning, TEXT("║ InRenderTarget: %s"), InRenderTarget ? *InRenderTarget->GetName() : TEXT("nullptr"));
	UE_LOG(LogHelluna, Warning, TEXT("║ InScene: %s"), InScene ? *InScene->GetName() : TEXT("nullptr"));
	UE_LOG(LogHelluna, Warning, TEXT("║ PreviewCaptureMaterial: %s"), PreviewCaptureMaterial ? *PreviewCaptureMaterial->GetName() : TEXT("nullptr"));
#endif

	if (!InRenderTarget)
	{
		UE_LOG(LogHelluna, Error, TEXT("[캐릭터선택위젯V2] SetupPreviewV2 실패 - InRenderTarget이 nullptr!"));
		return;
	}

	if (!PreviewCaptureMaterial)
	{
		UE_LOG(LogHelluna, Error, TEXT("[캐릭터선택위젯V2] SetupPreviewV2 실패 - PreviewCaptureMaterial 미설정!"));
		return;
	}

	// ════════════════════════════════════════════
	// 📌 씬 참조 저장
	// ════════════════════════════════════════════
	PreviewSceneV2 = InScene;

	// ════════════════════════════════════════════
	// 📌 RenderTarget → MID → UImage 적용
	// ════════════════════════════════════════════
	if (!PreviewImage_V2)
	{
		UE_LOG(LogHelluna, Error, TEXT("[캐릭터선택위젯V2] SetupPreviewV2 실패 - PreviewImage_V2가 nullptr!"));
		return;
	}

	PreviewMaterialV2 = UMaterialInstanceDynamic::Create(PreviewCaptureMaterial, this);
	if (!PreviewMaterialV2)
	{
		UE_LOG(LogHelluna, Error, TEXT("[캐릭터선택위젯V2] MID 생성 실패!"));
		return;
	}

	PreviewMaterialV2->SetTextureParameterValue(TEXT("Texture"), InRenderTarget);
	PreviewImage_V2->SetBrushFromMaterial(PreviewMaterialV2);
	PreviewImage_V2->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

#if HELLUNA_DEBUG_CHARACTER_PREVIEW_V2
	UE_LOG(LogHelluna, Warning, TEXT("║ V2 프리뷰 이미지 설정 완료"));
	UE_LOG(LogHelluna, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
#endif
}

// ════════════════════════════════════════════════════════════════════════════════
// 📌 V2 호버 처리
// ════════════════════════════════════════════════════════════════════════════════

void UHellunaCharSelectWidget_V2::OnCharacterHovered(int32 Index, bool bHovered)
{
	if (IsValid(PreviewSceneV2))
	{
		PreviewSceneV2->SetCharacterHovered(Index, bHovered);
	}
}

// ════════════════════════════════════════════════════════════════════════════════
// 📌 V2 정리
// ════════════════════════════════════════════════════════════════════════════════

void UHellunaCharSelectWidget_V2::CleanupPreview()
{
#if HELLUNA_DEBUG_CHARACTER_PREVIEW_V2
	UE_LOG(LogHelluna, Warning, TEXT("[캐릭터선택위젯V2] CleanupPreview"));
#endif

	PreviewSceneV2 = nullptr;
	PreviewMaterialV2 = nullptr;
}
