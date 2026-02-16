#include "Login/Preview/HellunaCharacterPreviewActor.h"
#include "Helluna.h"
#include "AnimInstance/HellunaPreviewAnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/PointLightComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInterface.h"

// ============================================
// 📌 생성자
// ============================================

AHellunaCharacterPreviewActor::AHellunaCharacterPreviewActor()
{
	PrimaryActorTick.bCanEverTick = false;
	SetReplicates(false);
	bNetLoadOnClient = false;

	// ════════════════════════════════════════════
	// 📌 컴포넌트 생성 및 계층 구성
	// ════════════════════════════════════════════

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	PreviewMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("PreviewMesh"));
	PreviewMesh->SetupAttachment(SceneRoot);
	PreviewMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PreviewMesh->SetRelativeRotation(FRotator(0.f, -90.f, 0.f));  // 카메라 정면을 바라보도록 회전

	SceneCapture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("SceneCapture"));
	SceneCapture->SetupAttachment(SceneRoot);
	SceneCapture->bCaptureEveryFrame = true;
	SceneCapture->bCaptureOnMovement = false;
	SceneCapture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
	SceneCapture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;  // 알파 채널 포함

	// Lumen GI 없이도 캐릭터가 보이도록 ShowFlags 설정
	SceneCapture->ShowFlags.SetAtmosphere(false);
	SceneCapture->ShowFlags.SetFog(false);
	SceneCapture->ShowFlags.SetVolumetricFog(false);
	SceneCapture->ShowFlags.SetSkyLighting(false);
	SceneCapture->ShowFlags.SetDynamicShadows(false);
	SceneCapture->ShowFlags.SetGlobalIllumination(false);
	SceneCapture->ShowFlags.SetScreenSpaceReflections(false);
	SceneCapture->ShowFlags.SetAmbientOcclusion(false);
	SceneCapture->ShowFlags.SetReflectionEnvironment(false);

	// 기본 앰비언트 광원 확보 (ShowOnlyList에서 GI 없이도 밝게)
	SceneCapture->PostProcessSettings.bOverride_AutoExposureBias = true;
	SceneCapture->PostProcessSettings.AutoExposureBias = 3.0f;
	SceneCapture->PostProcessBlendWeight = 1.0f;

	PreviewLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("PreviewLight"));
	PreviewLight->SetupAttachment(SceneRoot);
	PreviewLight->SetIntensity(50000.f);
	PreviewLight->SetAttenuationRadius(1000.f);
	PreviewLight->SetRelativeLocation(FVector(150.f, 0.f, 200.f));

	FillLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("FillLight"));
	FillLight->SetupAttachment(SceneRoot);
	FillLight->SetIntensity(20000.f);
	FillLight->SetAttenuationRadius(1000.f);
	FillLight->SetRelativeLocation(FVector(-100.f, -150.f, 100.f));
}

// ============================================
// 📌 프리뷰 초기화
// ============================================

void AHellunaCharacterPreviewActor::InitializePreview(USkeletalMesh* InMesh, TSubclassOf<UAnimInstance> InAnimClass, UTextureRenderTarget2D* InRenderTarget)
{
	// ════════════════════════════════════════════
	// 📌 인자 유효성 검사
	// ════════════════════════════════════════════

	if (!InMesh)
	{
		UE_LOG(LogHelluna, Error, TEXT("[캐릭터프리뷰액터] 프리뷰 초기화 실패 - InMesh가 nullptr!"));
		return;
	}

	if (!InAnimClass)
	{
		UE_LOG(LogHelluna, Error, TEXT("[캐릭터프리뷰액터] 프리뷰 초기화 실패 - InAnimClass가 nullptr!"));
		return;
	}

	if (!InRenderTarget)
	{
		UE_LOG(LogHelluna, Error, TEXT("[캐릭터프리뷰액터] 프리뷰 초기화 실패 - InRenderTarget이 nullptr!"));
		return;
	}

	// ════════════════════════════════════════════
	// 📌 SkeletalMesh 및 AnimClass 세팅
	// ════════════════════════════════════════════

	PreviewMesh->SetSkeletalMeshAsset(InMesh);
	PreviewMesh->SetAnimClass(InAnimClass);

	// ════════════════════════════════════════════
	// 📌 SceneCapture 세팅
	// ════════════════════════════════════════════

	SceneCapture->TextureTarget = InRenderTarget;
	SceneCapture->SetRelativeLocation(CaptureOffset);
	SceneCapture->SetRelativeRotation(CaptureRotation);
	SceneCapture->FOVAngle = CaptureFOVAngle;

	// ShowOnlyList 설정 - 자기 자신만 캡처
	SceneCapture->ShowOnlyActors.Empty();
	SceneCapture->ShowOnlyActors.Add(this);

#if HELLUNA_DEBUG_CHARACTER_PREVIEW
	UE_LOG(LogHelluna, Warning, TEXT("[캐릭터프리뷰액터] ✅ 프리뷰 초기화 완료"));
	UE_LOG(LogHelluna, Warning, TEXT("  Mesh: %s"), *InMesh->GetName());
	UE_LOG(LogHelluna, Warning, TEXT("  AnimClass: %s"), *InAnimClass->GetName());
	UE_LOG(LogHelluna, Warning, TEXT("  RenderTarget: %s (%dx%d)"), *InRenderTarget->GetName(), InRenderTarget->SizeX, InRenderTarget->SizeY);
	UE_LOG(LogHelluna, Warning, TEXT("  CaptureOffset: %s"), *CaptureOffset.ToString());
	UE_LOG(LogHelluna, Warning, TEXT("  CaptureRotation: %s"), *CaptureRotation.ToString());
	UE_LOG(LogHelluna, Warning, TEXT("  FOV: %.1f"), CaptureFOVAngle);
#endif
}

// ============================================
// 📌 Hover 상태 설정
// ============================================

void AHellunaCharacterPreviewActor::SetHovered(bool bHovered)
{
	if (!PreviewMesh)
	{
		UE_LOG(LogHelluna, Error, TEXT("[캐릭터프리뷰액터] 호버 설정 실패 - PreviewMesh가 nullptr!"));
		return;
	}

	// ════════════════════════════════════════════
	// 📌 AnimBP 호버 상태
	// ════════════════════════════════════════════
	UHellunaPreviewAnimInstance* AnimInst = Cast<UHellunaPreviewAnimInstance>(PreviewMesh->GetAnimInstance());
	if (!AnimInst)
	{
		UE_LOG(LogHelluna, Error, TEXT("[캐릭터프리뷰액터] 호버 설정 실패 - PreviewAnimInstance를 찾을 수 없음!"));
		return;
	}

	AnimInst->bIsHovered = bHovered;

	// ════════════════════════════════════════════
	// 📌 오버레이 하이라이트 머티리얼
	// ════════════════════════════════════════════
	PreviewMesh->SetOverlayMaterial(bHovered ? HighlightMaterial : nullptr);

#if HELLUNA_DEBUG_CHARACTER_PREVIEW
	UE_LOG(LogHelluna, Warning, TEXT("[캐릭터프리뷰액터] 호버 상태 변경: %s (Highlight: %s)"),
		bHovered ? TEXT("TRUE") : TEXT("FALSE"),
		HighlightMaterial ? *HighlightMaterial->GetName() : TEXT("없음"));
#endif
}

// ============================================
// 📌 하이라이트 머티리얼 설정
// ============================================

void AHellunaCharacterPreviewActor::SetHighlightMaterial(UMaterialInterface* InMaterial)
{
	HighlightMaterial = InMaterial;

#if HELLUNA_DEBUG_CHARACTER_PREVIEW
	UE_LOG(LogHelluna, Warning, TEXT("[캐릭터프리뷰액터] 하이라이트 머티리얼 설정: %s"),
		InMaterial ? *InMaterial->GetName() : TEXT("nullptr"));
#endif
}

// ============================================
// 📌 ShowOnlyActors에 액터 추가
// ============================================

void AHellunaCharacterPreviewActor::AddShowOnlyActor(AActor* InActor)
{
	if (SceneCapture && InActor)
	{
		SceneCapture->ShowOnlyActors.AddUnique(InActor);
	}
}

// ============================================
// 📌 RenderTarget 반환
// ============================================

UTextureRenderTarget2D* AHellunaCharacterPreviewActor::GetRenderTarget() const
{
	if (SceneCapture)
	{
		return SceneCapture->TextureTarget;
	}
	return nullptr;
}
