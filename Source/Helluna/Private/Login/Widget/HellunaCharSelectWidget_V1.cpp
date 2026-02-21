#include "Login/Widget/HellunaCharSelectWidget_V1.h"
#include "Login/Preview/HellunaCharacterPreviewActor.h"
#include "Helluna.h"
#include "Components/Image.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

// ════════════════════════════════════════════════════════════════════════════════
// 📌 V1 프리뷰 초기화
// ════════════════════════════════════════════════════════════════════════════════

void UHellunaCharSelectWidget_V1::SetupPreviewV1(
	const TArray<UTextureRenderTarget2D*>& InRenderTargets,
	const TArray<AHellunaCharacterPreviewActor*>& InPreviewActors)
{
#if HELLUNA_DEBUG_CHARACTER_PREVIEW
	UE_LOG(LogHelluna, Warning, TEXT(""));
	UE_LOG(LogHelluna, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogHelluna, Warning, TEXT("║  [캐릭터선택위젯V1] SetupPreviewV1                         ║"));
	UE_LOG(LogHelluna, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogHelluna, Warning, TEXT("║ RenderTargets: %d, PreviewActors: %d"), InRenderTargets.Num(), InPreviewActors.Num());
	UE_LOG(LogHelluna, Warning, TEXT("║ PreviewCaptureMaterial: %s"), PreviewCaptureMaterial ? *PreviewCaptureMaterial->GetName() : TEXT("nullptr"));
#endif

	// ════════════════════════════════════════════
	// 📌 액터 참조 저장
	// ════════════════════════════════════════════
	PreviewActors.Empty();
	for (AHellunaCharacterPreviewActor* Actor : InPreviewActors)
	{
		PreviewActors.Add(Actor);
	}

	// ════════════════════════════════════════════
	// 📌 RenderTarget → MID → UImage 적용
	// ════════════════════════════════════════════
	PreviewMaterials.Empty();

	TArray<UImage*> PreviewImages = { PreviewImage_Lui, PreviewImage_Luna, PreviewImage_Liam };

	for (int32 i = 0; i < PreviewImages.Num(); i++)
	{
		UImage* TargetImage = PreviewImages[i];
		if (!TargetImage)
		{
#if HELLUNA_DEBUG_CHARACTER_PREVIEW
			UE_LOG(LogHelluna, Warning, TEXT("[캐릭터선택위젯V1] PreviewImage[%d]가 nullptr - 스킵"), i);
#endif
			PreviewMaterials.Add(nullptr);
			continue;
		}

		if (!InRenderTargets.IsValidIndex(i) || !InRenderTargets[i])
		{
#if HELLUNA_DEBUG_CHARACTER_PREVIEW
			UE_LOG(LogHelluna, Warning, TEXT("[캐릭터선택위젯V1] RenderTarget[%d]가 없음 - 스킵"), i);
#endif
			PreviewMaterials.Add(nullptr);
			continue;
		}

		if (!PreviewCaptureMaterial)
		{
			UE_LOG(LogHelluna, Error, TEXT("[캐릭터선택위젯V1] 프리뷰 캡처 머티리얼 미설정!"));
			PreviewMaterials.Add(nullptr);
			continue;
		}

		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(PreviewCaptureMaterial, this);
		if (!MID)
		{
			UE_LOG(LogHelluna, Error, TEXT("[캐릭터선택위젯V1] MID 생성 실패 [%d]"), i);
			PreviewMaterials.Add(nullptr);
			continue;
		}

		MID->SetTextureParameterValue(TEXT("Texture"), InRenderTargets[i]);
		TargetImage->SetBrushFromMaterial(MID);
		PreviewMaterials.Add(MID);

#if HELLUNA_DEBUG_CHARACTER_PREVIEW
		UE_LOG(LogHelluna, Warning, TEXT("║ [%d] MID 적용 완료 (RT: %s)"), i, *InRenderTargets[i]->GetName());
#endif
	}

#if HELLUNA_DEBUG_CHARACTER_PREVIEW
	UE_LOG(LogHelluna, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
#endif
}

// ════════════════════════════════════════════════════════════════════════════════
// 📌 V1 호버 처리
// ════════════════════════════════════════════════════════════════════════════════

void UHellunaCharSelectWidget_V1::OnCharacterHovered(int32 Index, bool bHovered)
{
	if (PreviewActors.IsValidIndex(Index) && IsValid(PreviewActors[Index]))
	{
		PreviewActors[Index]->SetHovered(bHovered);
	}
}

// ════════════════════════════════════════════════════════════════════════════════
// 📌 V1 정리
// ════════════════════════════════════════════════════════════════════════════════

void UHellunaCharSelectWidget_V1::CleanupPreview()
{
#if HELLUNA_DEBUG_CHARACTER_PREVIEW
	UE_LOG(LogHelluna, Warning, TEXT("[캐릭터선택위젯V1] CleanupPreview"));
#endif

	PreviewActors.Empty();
	PreviewMaterials.Empty();
}
