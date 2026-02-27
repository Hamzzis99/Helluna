#include "Login/Preview/HellunaCharacterSelectSceneV2.h"
#include "Helluna.h"
#include "AnimInstance/HellunaPreviewAnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
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
	// OriginalLocations 저장 + 캐릭터별 스포트라이트 생성
	// ════════════════════════════════════════════

	OriginalLocations.Empty();
	CharacterSpotLights.Empty();

	for (int32 i = 0; i < PreviewMeshes.Num(); i++)
	{
		if (PreviewMeshes[i])
		{
			OriginalLocations.Add(PreviewMeshes[i]->GetRelativeLocation());
		}
		else
		{
			OriginalLocations.Add(FVector::ZeroVector);
		}

		// 캐릭터 머리 위에서 아래로 비추는 스포트라이트
		USpotLightComponent* Spot = NewObject<USpotLightComponent>(this,
			*FString::Printf(TEXT("CharSpotLight_%d"), i));
		Spot->RegisterComponent();
		Spot->AttachToComponent(SceneRoot, FAttachmentTransformRules::KeepRelativeTransform);

		// 위치: 캐릭터 위 + 약간 앞에서 아래로 비춤
		FVector SpotLoc = OriginalLocations[i] + FVector(100.f, 0.f, 350.f);
		Spot->SetRelativeLocation(SpotLoc);
		Spot->SetRelativeRotation(FRotator(-70.f, 0.f, 0.f)); // 위에서 아래로

		Spot->SetIntensity(80000.f);
		Spot->SetAttenuationRadius(1000.f);
		Spot->SetInnerConeAngle(15.f);
		Spot->SetOuterConeAngle(35.f);

		CharacterSpotLights.Add(Spot);
	}

	// ════════════════════════════════════════════
	// Solo 센터 메시 생성 (Play 탭용 — 항상 카메라 정중앙)
	// ════════════════════════════════════════════
	if (!SoloCenterMesh)
	{
		SoloCenterMesh = NewObject<USkeletalMeshComponent>(this, TEXT("SoloCenterMesh"));
		SoloCenterMesh->RegisterComponent();
		SoloCenterMesh->AttachToComponent(SceneRoot, FAttachmentTransformRules::KeepRelativeTransform);
		SoloCenterMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		SoloCenterMesh->SetRelativeLocation(SoloCenterOffset);
		SoloCenterMesh->SetRelativeRotation(SoloCenterRotation);
		SoloCenterMesh->SetRelativeScale3D(SoloCenterScale);
		SoloCenterMesh->SetVisibility(false); // 초기엔 숨김
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

// ============================================
// 선택 상태 연출
// ============================================

void AHellunaCharacterSelectSceneV2::SetCharacterSelected(int32 SelectedIndex)
{
	CurrentSelectedIndex = SelectedIndex;

	for (int32 i = 0; i < PreviewMeshes.Num(); i++)
	{
		if (!PreviewMeshes[i]) continue;

		const bool bIsSelected = (i == SelectedIndex);
		const bool bNoSelection = (SelectedIndex < 0);

		// 위치: 선택된 캐릭터는 카메라 쪽(Y+)으로 전진, 나머지는 원래 자리
		FVector TargetLoc = OriginalLocations.IsValidIndex(i) ? OriginalLocations[i] : FVector::ZeroVector;
		if (bIsSelected)
		{
			TargetLoc.Y += SelectedForwardOffset;
		}
		PreviewMeshes[i]->SetRelativeLocation(TargetLoc);

		// 스포트라이트: 선택된 캐릭터는 밝게, 나머지는 어둡게 (미선택 시 모두 밝게)
		if (CharacterSpotLights.IsValidIndex(i) && CharacterSpotLights[i])
		{
			const float FullIntensity = 80000.f;
			if (bNoSelection)
			{
				CharacterSpotLights[i]->SetIntensity(FullIntensity);
			}
			else
			{
				CharacterSpotLights[i]->SetIntensity(bIsSelected ? FullIntensity : FullIntensity * UnselectedBrightnessRatio);
			}
		}
	}

#if HELLUNA_DEBUG_CHARACTER_PREVIEW_V2
	UE_LOG(LogHelluna, Warning, TEXT("[프리뷰V2] SetCharacterSelected(%d)"), SelectedIndex);
#endif
}

// ============================================
// Solo 모드 — Play 탭에서 캐릭터 1개만 표시
// ============================================

void AHellunaCharacterSelectSceneV2::SetSoloCharacter(int32 CharacterIndex)
{
	if (CharacterIndex < 0 || CharacterIndex >= PreviewMeshes.Num())
	{
		UE_LOG(LogHelluna, Warning, TEXT("[프리뷰V2] SetSoloCharacter: 잘못된 인덱스 %d (캐릭터 수=%d)"),
			CharacterIndex, PreviewMeshes.Num());
		return;
	}

	bSoloMode = true;
	SoloCharacterIndex = CharacterIndex;

	// ── 기존 PreviewMeshes + 스포트라이트 전체 숨김 ──
	for (int32 i = 0; i < PreviewMeshes.Num(); ++i)
	{
		if (PreviewMeshes[i])
		{
			PreviewMeshes[i]->SetVisibility(false);
		}
		if (CharacterSpotLights.IsValidIndex(i) && CharacterSpotLights[i])
		{
			CharacterSpotLights[i]->SetVisibility(false);
		}
	}

	// ── SoloCenterMesh에 선택 캐릭터 복사 ──
	if (SoloCenterMesh && PreviewMeshes[CharacterIndex])
	{
		USkeletalMeshComponent* SourceMesh = PreviewMeshes[CharacterIndex];

		// 메시 복사
		SoloCenterMesh->SetSkeletalMesh(SourceMesh->GetSkeletalMeshAsset());

		// 애님 클래스 복사
		SoloCenterMesh->SetAnimInstanceClass(SourceMesh->GetAnimClass());

		// 머티리얼 복사 (오버레이 등)
		for (int32 MatIdx = 0; MatIdx < SourceMesh->GetNumMaterials(); ++MatIdx)
		{
			SoloCenterMesh->SetMaterial(MatIdx, SourceMesh->GetMaterial(MatIdx));
		}

		// 가시성 ON
		SoloCenterMesh->SetVisibility(true);
	}

#if HELLUNA_DEBUG_CHARACTER_PREVIEW_V2
	UE_LOG(LogHelluna, Warning, TEXT("[프리뷰V2] SetSoloCharacter(%d) — SoloCenterMesh에 복사, 중앙 표시"), CharacterIndex);
#endif
}

void AHellunaCharacterSelectSceneV2::ClearSoloMode()
{
	if (!bSoloMode) return;

	bSoloMode = false;
	SoloCharacterIndex = -1;

	// ── SoloCenterMesh 숨김 ──
	if (SoloCenterMesh)
	{
		SoloCenterMesh->SetVisibility(false);
	}

	// ── 기존 PreviewMeshes + 스포트라이트 전체 복원 ──
	for (int32 i = 0; i < PreviewMeshes.Num(); ++i)
	{
		if (PreviewMeshes[i])
		{
			PreviewMeshes[i]->SetVisibility(true);
		}
		if (CharacterSpotLights.IsValidIndex(i) && CharacterSpotLights[i])
		{
			CharacterSpotLights[i]->SetVisibility(true);
		}
	}

	// 기존 Selected 상태 복원 (위치/밝기)
	SetCharacterSelected(CurrentSelectedIndex);

#if HELLUNA_DEBUG_CHARACTER_PREVIEW_V2
	UE_LOG(LogHelluna, Warning, TEXT("[프리뷰V2] ClearSoloMode — SoloCenterMesh 숨김, 전체 표시 복원"));
#endif
}
