// Gihyeon's Inventory Project
// @author 김기현
//
// 건설물 프리뷰 액터 — Inv_WeaponPreviewActor 기반, 부착물 기능 제거

#include "Building/Preview/Inv_BuildingPreviewActor.h"
#include "Inventory.h"

#include "Components/StaticMeshComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SpotLightComponent.h"
#include "Components/PointLightComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/StaticMesh.h"

AInv_BuildingPreviewActor::AInv_BuildingPreviewActor()
{
	PrimaryActorTick.bCanEverTick = false;
	SetReplicates(false);

	UE_LOG(LogTemp, Warning, TEXT("[BuildingPreview] Actor 생성됨"));

	// ── Root ──
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;

	// ── 건설물 메시 ──
	PreviewMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PreviewMesh"));
	PreviewMeshComponent->SetupAttachment(SceneRoot);
	PreviewMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PreviewMeshComponent->CastShadow = false;
	PreviewMeshComponent->LightingChannels.bChannel0 = true;
	PreviewMeshComponent->LightingChannels.bChannel1 = false;

	// ── 카메라 붐 ──
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(SceneRoot);
	CameraBoom->TargetArmLength = 200.f;
	CameraBoom->bDoCollisionTest = false;
	CameraBoom->SetRelativeRotation(FRotator(-15.f, 0.f, 0.f));

	// ── SceneCapture ──
	SceneCapture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("SceneCapture"));
	SceneCapture->SetupAttachment(CameraBoom);
	SceneCapture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
	// [최적화] 매 프레임 캡처 비활성화 — SetPreviewMesh/RotatePreview에서만 수동 캡처
	SceneCapture->bCaptureEveryFrame = false;
	SceneCapture->bCaptureOnMovement = false;
	SceneCapture->CaptureSource = ESceneCaptureSource::SCS_SceneColorHDR;
	SceneCapture->bAlwaysPersistRenderingState = true;

	// HDR 캡처: 자동 노출 비활성화
	SceneCapture->PostProcessSettings.bOverride_AutoExposureMethod = true;
	SceneCapture->PostProcessSettings.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
	SceneCapture->PostProcessSettings.bOverride_AutoExposureBias = true;
	SceneCapture->PostProcessSettings.AutoExposureBias = 1.0f;
	SceneCapture->ShowFlags.SetEyeAdaptation(false);

	SceneCapture->MaxViewDistanceOverride = 1000.f;
	SceneCapture->FOVAngle = 30.f;

	// 배경 정리
	SceneCapture->ShowFlags.SetFog(false);
	SceneCapture->ShowFlags.SetVolumetricFog(false);
	SceneCapture->ShowFlags.SetAtmosphere(false);
	SceneCapture->ShowFlags.SetSkyLighting(false);
	SceneCapture->ShowFlags.SetCloud(false);
	SceneCapture->ShowFlags.SetDynamicShadows(false);
	SceneCapture->ShowFlags.SetGlobalIllumination(false);
	SceneCapture->ShowFlags.SetScreenSpaceReflections(false);
	SceneCapture->ShowFlags.SetAmbientOcclusion(false);
	SceneCapture->ShowFlags.SetReflectionEnvironment(false);

	SceneCapture->ShowOnlyComponents.Add(PreviewMeshComponent);

	// ── Key Light ──
	PreviewLight = CreateDefaultSubobject<USpotLightComponent>(TEXT("PreviewLight"));
	PreviewLight->SetupAttachment(SceneRoot);
	PreviewLight->SetRelativeLocation(FVector(200.f, -150.f, 150.f));
	PreviewLight->SetRelativeRotation(FRotator(-35.f, -30.f, 0.f));
	PreviewLight->Intensity = 10000.f;
	PreviewLight->AttenuationRadius = 800.f;
	PreviewLight->SetInnerConeAngle(30.f);
	PreviewLight->SetOuterConeAngle(60.f);
	PreviewLight->CastShadows = false;
	PreviewLight->LightingChannels.bChannel0 = true;
	PreviewLight->LightingChannels.bChannel1 = false;

	// ── Fill Light ──
	FillLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("FillLight"));
	FillLight->SetupAttachment(SceneRoot);
	FillLight->SetRelativeLocation(FVector(-120.f, 150.f, 50.f));
	FillLight->Intensity = 4000.f;
	FillLight->AttenuationRadius = 800.f;
	FillLight->CastShadows = false;
	FillLight->LightingChannels.bChannel0 = true;
	FillLight->LightingChannels.bChannel1 = false;

	// ── Rim Light ──
	RimLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("RimLight"));
	RimLight->SetupAttachment(SceneRoot);
	RimLight->SetRelativeLocation(FVector(-150.f, 0.f, 150.f));
	RimLight->Intensity = 6000.f;
	RimLight->AttenuationRadius = 800.f;
	RimLight->CastShadows = false;
	RimLight->LightingChannels.bChannel0 = true;
	RimLight->LightingChannels.bChannel1 = false;

	// ── 배경 차단 큐브 ──
	BackdropCube = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BackdropCube"));
	BackdropCube->SetupAttachment(SceneRoot);
	BackdropCube->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BackdropCube->CastShadow = false;
	BackdropCube->SetRelativeScale3D(FVector(-8.f, 8.f, 8.f));
	BackdropCube->LightingChannels.bChannel0 = false;
	BackdropCube->LightingChannels.bChannel1 = false;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		BackdropCube->SetStaticMesh(CubeMesh.Object);
	}
}

void AInv_BuildingPreviewActor::SetPreviewMesh(UStaticMesh* InMesh, const FRotator& RotationOffset, float CameraDistance)
{
	if (!IsValid(InMesh))
	{
		UE_LOG(LogTemp, Warning, TEXT("[BuildingPreview] SetPreviewMesh 실패: InMesh가 nullptr"));
		return;
	}

	if (!IsValid(PreviewMeshComponent))
	{
		UE_LOG(LogTemp, Warning, TEXT("[BuildingPreview] SetPreviewMesh 실패: PreviewMeshComponent 무효"));
		return;
	}

	PreviewMeshComponent->SetStaticMesh(InMesh);
	PreviewMeshComponent->SetRelativeRotation(RotationOffset);

	if (IsValid(CameraBoom))
	{
		// BP에서 설정한 카메라 붐 회전/오프셋 적용
		CameraBoom->SetRelativeRotation(CameraBoomRotation);
		CameraBoom->SocketOffset = CameraTargetOffset;

		if (CameraDistance > 0.f)
		{
			CameraBoom->TargetArmLength = CameraDistance;
		}
		else if (bAutoCalculateDistance)
		{
			CameraBoom->TargetArmLength = CalculateAutoDistance();
		}
	}

	EnsureRenderTarget();

	// [최적화] bCaptureEveryFrame=false이므로 수동 캡처
	CaptureNow();

	const float ActualDistance = IsValid(CameraBoom) ? CameraBoom->TargetArmLength : -1.f;
	UE_LOG(LogTemp, Warning, TEXT("[BuildingPreview] SetPreviewMesh: Mesh=%s, RotOffset=%s, CamDist=%.1f → 실제 Distance=%.1f"),
		*InMesh->GetName(), *RotationOffset.ToString(), CameraDistance, ActualDistance);
}

void AInv_BuildingPreviewActor::RotatePreview(float YawDelta, float PitchDelta)
{
	if (!IsValid(PreviewMeshComponent)) return;

	const float NewPitch = FMath::Clamp(AccumulatedPitch + PitchDelta, -MaxPitchAngle, MaxPitchAngle);
	const float ClampedPitchDelta = NewPitch - AccumulatedPitch;
	AccumulatedPitch = NewPitch;

	PreviewMeshComponent->AddRelativeRotation(FRotator(ClampedPitchDelta, YawDelta, 0.f));

	// [최적화] bCaptureEveryFrame=false이므로 회전 후 수동 캡처
	CaptureNow();
}

UTextureRenderTarget2D* AInv_BuildingPreviewActor::GetRenderTarget() const
{
	return RenderTarget;
}

void AInv_BuildingPreviewActor::CaptureNow()
{
	if (!IsValid(SceneCapture))
	{
		UE_LOG(LogTemp, Warning, TEXT("[BuildingPreview] CaptureNow 실패: SceneCapture 무효"));
		return;
	}

	if (!IsValid(SceneCapture->TextureTarget))
	{
		UE_LOG(LogTemp, Warning, TEXT("[BuildingPreview] CaptureNow 실패: TextureTarget 무효"));
		return;
	}

	SceneCapture->CaptureScene();
}

void AInv_BuildingPreviewActor::EnsureRenderTarget()
{
	if (IsValid(RenderTarget)) return;

	RenderTarget = NewObject<UTextureRenderTarget2D>(this);
	if (!IsValid(RenderTarget))
	{
		UE_LOG(LogTemp, Warning, TEXT("[BuildingPreview] RenderTarget 생성 실패!"));
		return;
	}

	RenderTarget->ClearColor = FLinearColor(0.f, 0.f, 0.f, 0.f);
	const int32 Width = FMath::Clamp(RenderTargetWidth, 128, 2048);
	const int32 Height = FMath::Clamp(RenderTargetHeight, 128, 2048);
	RenderTarget->InitCustomFormat(Width, Height, PF_FloatRGBA, false);
	RenderTarget->UpdateResourceImmediate(true);

	if (IsValid(SceneCapture))
	{
		SceneCapture->TextureTarget = RenderTarget;
	}

	UE_LOG(LogTemp, Warning, TEXT("[BuildingPreview] RenderTarget 생성: %dx%d"), Width, Height);
}

float AInv_BuildingPreviewActor::CalculateAutoDistance() const
{
	if (!IsValid(PreviewMeshComponent)) return AutoDistanceDefault;

	UStaticMesh* Mesh = PreviewMeshComponent->GetStaticMesh();
	if (!IsValid(Mesh)) return AutoDistanceDefault;

	const FBoxSphereBounds Bounds = Mesh->GetBounds();
	const float SphereRadius = Bounds.SphereRadius;

	if (SphereRadius <= KINDA_SMALL_NUMBER) return AutoDistanceDefault;

	const float AutoDistance = SphereRadius * AutoDistanceMultiplier;
	const float ClampedDistance = FMath::Clamp(AutoDistance, AutoDistanceMin, AutoDistanceMax);

	UE_LOG(LogTemp, Warning, TEXT("[BuildingPreview] AutoDistance: SphereRadius=%.1f → Distance=%.1f (clamp: %.1f~%.1f)"),
		SphereRadius, ClampedDistance, AutoDistanceMin, AutoDistanceMax);

	return ClampedDistance;
}
