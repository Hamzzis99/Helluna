#include "Login/Preview/HellunaCharacterSelectSceneV2.h"
#include "Helluna.h"
#include "AnimInstance/HellunaPreviewAnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/PointLightComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/SkeletalMesh.h"

// ============================================
// 생성자
// ============================================

AHellunaCharacterSelectSceneV2::AHellunaCharacterSelectSceneV2()
{
	PrimaryActorTick.bCanEverTick = false;
	SetReplicates(false);
	bNetLoadOnClient = false;

	// ════════════════════════════════════════════
	// 컴포넌트 생성 및 계층 구성
	// ════════════════════════════════════════════

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	SceneCapture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("SceneCapture"));
	SceneCapture->SetupAttachment(SceneRoot);
	SceneCapture->bCaptureEveryFrame = true;
	SceneCapture->bCaptureOnMovement = false;
	SceneCapture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
	SceneCapture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;

	// ShowFlags (V1과 동일)
	SceneCapture->ShowFlags.SetAtmosphere(false);
	SceneCapture->ShowFlags.SetFog(false);
	SceneCapture->ShowFlags.SetVolumetricFog(false);
	SceneCapture->ShowFlags.SetSkyLighting(false);
	SceneCapture->ShowFlags.SetDynamicShadows(false);
	SceneCapture->ShowFlags.SetGlobalIllumination(false);
	SceneCapture->ShowFlags.SetScreenSpaceReflections(false);
	SceneCapture->ShowFlags.SetAmbientOcclusion(false);
	SceneCapture->ShowFlags.SetReflectionEnvironment(false);

	// AutoExposure (V1과 동일)
	SceneCapture->PostProcessSettings.bOverride_AutoExposureBias = true;
	SceneCapture->PostProcessSettings.AutoExposureBias = 3.0f;
	SceneCapture->PostProcessBlendWeight = 1.0f;

	// 메인 조명
	MainLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("MainLight"));
	MainLight->SetupAttachment(SceneRoot);
	MainLight->SetIntensity(50000.f);
	MainLight->SetAttenuationRadius(2000.f);
	MainLight->SetRelativeLocation(FVector(300.f, 0.f, 300.f));

	// 보조 조명
	FillLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("FillLight"));
	FillLight->SetupAttachment(SceneRoot);
	FillLight->SetIntensity(20000.f);
	FillLight->SetAttenuationRadius(2000.f);
	FillLight->SetRelativeLocation(FVector(-200.f, -300.f, 150.f));
}

// ============================================
// 씬 초기화
// ============================================

void AHellunaCharacterSelectSceneV2::InitializeScene(
	const TArray<USkeletalMesh*>& InMeshes,
	const TArray<TSubclassOf<UAnimInstance>>& InAnimClasses,
	UTextureRenderTarget2D* InRenderTarget)
{
	// ════════════════════════════════════════════
	// 인자 검증
	// ════════════════════════════════════════════

	if (InMeshes.Num() != InAnimClasses.Num())
	{
		UE_LOG(LogHelluna, Error, TEXT("[프리뷰V2] InitializeScene 실패 - Meshes(%d)와 AnimClasses(%d) 수 불일치!"),
			InMeshes.Num(), InAnimClasses.Num());
		return;
	}

	if (!InRenderTarget)
	{
		UE_LOG(LogHelluna, Error, TEXT("[프리뷰V2] InitializeScene 실패 - InRenderTarget이 nullptr!"));
		return;
	}

#if HELLUNA_DEBUG_CHARACTER_PREVIEW_V2
	UE_LOG(LogHelluna, Warning, TEXT(""));
	UE_LOG(LogHelluna, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogHelluna, Warning, TEXT("║  [프리뷰V2] InitializeScene                               ║"));
	UE_LOG(LogHelluna, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogHelluna, Warning, TEXT("║ 캐릭터 수: %d"), InMeshes.Num());
	UE_LOG(LogHelluna, Warning, TEXT("║ CharacterSpacing: %.1f"), CharacterSpacing);
	UE_LOG(LogHelluna, Warning, TEXT("║ CameraOffset: %s"), *CameraOffset.ToString());
	UE_LOG(LogHelluna, Warning, TEXT("║ CameraFOV: %.1f"), CameraFOV);
#endif

	const int32 Num = InMeshes.Num();

	// ════════════════════════════════════════════
	// 캐릭터 메시 동적 생성
	// ════════════════════════════════════════════

	PreviewMeshes.Empty();

	for (int32 i = 0; i < Num; i++)
	{
		if (!InMeshes[i])
		{
			UE_LOG(LogHelluna, Warning, TEXT("[프리뷰V2] InMeshes[%d]가 nullptr - 스킵"), i);
			PreviewMeshes.Add(nullptr);
			continue;
		}

		if (!InAnimClasses[i])
		{
			UE_LOG(LogHelluna, Warning, TEXT("[프리뷰V2] InAnimClasses[%d]가 nullptr - 스킵"), i);
			PreviewMeshes.Add(nullptr);
			continue;
		}

		USkeletalMeshComponent* MeshComp = NewObject<USkeletalMeshComponent>(this,
			*FString::Printf(TEXT("PreviewMesh_%d"), i));
		MeshComp->RegisterComponent();
		MeshComp->AttachToComponent(SceneRoot, FAttachmentTransformRules::KeepRelativeTransform);
		MeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);

		// ════════════════════════════════════════════
		// 위치: CharacterOffsets 우선, 없으면 CharacterSpacing 균등 배치
		// ════════════════════════════════════════════
		FVector MeshLocation;
		if (CharacterOffsets.IsValidIndex(i))
		{
			MeshLocation = CharacterOffsets[i];
		}
		else
		{
			const float XOffset = i * CharacterSpacing - (Num - 1) * CharacterSpacing * 0.5f;
			MeshLocation = FVector(XOffset, 0.f, 0.f);
		}
		MeshComp->SetRelativeLocation(MeshLocation);

		// ════════════════════════════════════════════
		// 회전: CharacterRotations 우선, 없으면 기본 -90도 (카메라 정면)
		// ════════════════════════════════════════════
		FRotator MeshRotation = CharacterRotations.IsValidIndex(i)
			? CharacterRotations[i]
			: FRotator(0.f, -90.f, 0.f);
		MeshComp->SetRelativeRotation(MeshRotation);

		// ════════════════════════════════════════════
		// 스케일: CharacterScales 우선, 없으면 기본 (1,1,1)
		// ════════════════════════════════════════════
		if (CharacterScales.IsValidIndex(i))
		{
			MeshComp->SetRelativeScale3D(CharacterScales[i]);
		}

		MeshComp->SetSkeletalMeshAsset(InMeshes[i]);
		MeshComp->SetAnimInstanceClass(InAnimClasses[i]);

		PreviewMeshes.Add(MeshComp);

#if HELLUNA_DEBUG_CHARACTER_PREVIEW_V2
		UE_LOG(LogHelluna, Warning, TEXT("║ [%d] Mesh: %s, Loc: %s, Rot: %s"),
			i, *InMeshes[i]->GetName(), *MeshLocation.ToString(), *MeshRotation.ToString());
#endif
	}

	// ════════════════════════════════════════════
	// SceneCapture 설정
	// ════════════════════════════════════════════

	SceneCapture->TextureTarget = InRenderTarget;
	SceneCapture->SetRelativeLocation(CameraOffset);
	SceneCapture->SetRelativeRotation(CameraRotation);
	SceneCapture->FOVAngle = CameraFOV;

	// ShowOnlyActors - 자기 자신 (안에 모든 메시 포함)
	SceneCapture->ShowOnlyActors.Empty();
	SceneCapture->ShowOnlyActors.Add(this);

#if HELLUNA_DEBUG_CHARACTER_PREVIEW_V2
	UE_LOG(LogHelluna, Warning, TEXT("║ SceneCapture 설정 완료 (RT: %s, %dx%d)"),
		*InRenderTarget->GetName(), InRenderTarget->SizeX, InRenderTarget->SizeY);
	UE_LOG(LogHelluna, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
	UE_LOG(LogHelluna, Warning, TEXT(""));
#endif
}

// ============================================
// 호버 ON/OFF
// ============================================

void AHellunaCharacterSelectSceneV2::SetCharacterHovered(int32 Index, bool bHovered)
{
	if (!PreviewMeshes.IsValidIndex(Index) || !PreviewMeshes[Index])
	{
#if HELLUNA_DEBUG_CHARACTER_PREVIEW_V2
		UE_LOG(LogHelluna, Warning, TEXT("[프리뷰V2] SetCharacterHovered 실패 - Index %d 범위 초과 또는 nullptr"), Index);
#endif
		return;
	}

	USkeletalMeshComponent* MeshComp = PreviewMeshes[Index];

	// 오버레이 머티리얼
	if (HighlightMaterials.IsValidIndex(Index))
	{
		MeshComp->SetOverlayMaterial(bHovered ? HighlightMaterials[Index] : nullptr);
	}

	// AnimBP 호버 상태
	UHellunaPreviewAnimInstance* AnimInst = Cast<UHellunaPreviewAnimInstance>(MeshComp->GetAnimInstance());
	if (AnimInst)
	{
		AnimInst->bIsHovered = bHovered;
	}

#if HELLUNA_DEBUG_CHARACTER_PREVIEW_V2
	UE_LOG(LogHelluna, Warning, TEXT("[프리뷰V2] SetCharacterHovered(%d, %s)"), Index, bHovered ? TEXT("true") : TEXT("false"));
#endif
}

// ============================================
// Getter
// ============================================

UTextureRenderTarget2D* AHellunaCharacterSelectSceneV2::GetRenderTarget() const
{
	if (SceneCapture)
	{
		return SceneCapture->TextureTarget;
	}
	return nullptr;
}

int32 AHellunaCharacterSelectSceneV2::GetCharacterCount() const
{
	return PreviewMeshes.Num();
}
