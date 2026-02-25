// Gihyeon's Inventory Project
//
// ════════════════════════════════════════════════════════════════════════════════
// 📌 무기 프리뷰 액터 (Weapon Preview Actor) — Phase 8 구현
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 핵심 흐름:
//    생성자 → 컴포넌트 구성
//    SetPreviewMesh → 메시 설정 + 카메라 거리 + 초기 캡처
//    RotatePreview → 회전 + 재캡처
//    Destroy → 패널 닫을 때 호출
//
// ════════════════════════════════════════════════════════════════════════════════

#include "Interaction/Preview/Inv_WeaponPreviewActor.h"
#include "Inventory.h"  // INV_DEBUG_ATTACHMENT 매크로

#include "Components/StaticMeshComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SpotLightComponent.h"
#include "Components/PointLightComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/StaticMesh.h"

// ════════════════════════════════════════════════════════════════
// 📌 생성자 — 컴포넌트 초기 구성
// ════════════════════════════════════════════════════════════════
// 처리 흐름:
//   1. 리플리케이션 비활성화 (로컬 전용 UI 보조 액터)
//   2. SceneRoot 생성 → RootComponent
//   3. PreviewMeshComponent 생성 → LightingChannel 1 전용
//   4. CameraBoom(SpringArm) 생성
//   5. SceneCapture 생성 → PRM_UseShowOnlyList + MaxViewDistance 제한
//   6. PreviewLight 생성 → LightingChannel 1 전용 (월드 조명 오염 방지)
//
// LightingChannels 격리 전략:
//   - 월드 오브젝트/라이트: Channel 0 (기본)
//   - 프리뷰 메시/라이트:  Channel 1 전용
//   - Channel이 다르면 서로 영향을 주지 않음
//   - 엔진 소스: Engine/Source/Runtime/Engine/Private/Components/LightComponent.cpp
//     → AffectsChannel()에서 LightingChannels 비트 AND 비교
// ════════════════════════════════════════════════════════════════
AInv_WeaponPreviewActor::AInv_WeaponPreviewActor()
{
	PrimaryActorTick.bCanEverTick = false;
	SetReplicates(false);

	// ── Root ──
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;

	// ── 무기 메시 ──
	PreviewMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PreviewMesh"));
	PreviewMeshComponent->SetupAttachment(SceneRoot);
	PreviewMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PreviewMeshComponent->CastShadow = false;
	// ★ Channel0(기본) 사용: 패키징 빌드에서 Channel1이 SceneCapture와
	// 호환되지 않는 문제 해결. ShowOnlyList로 월드 메시 격리는 이미 보장됨.
	PreviewMeshComponent->LightingChannels.bChannel0 = true;
	PreviewMeshComponent->LightingChannels.bChannel1 = false;

	// ── 카메라 붐 (스프링 암) ──
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(SceneRoot);
	CameraBoom->TargetArmLength = 150.f;
	CameraBoom->bDoCollisionTest = false;
	CameraBoom->SetRelativeRotation(FRotator(-15.f, 0.f, 0.f));

	// ── SceneCapture ──
	SceneCapture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("SceneCapture"));
	SceneCapture->SetupAttachment(CameraBoom);

	// ★ ShowOnlyList 방식: 무기 메시+부착물만 렌더링
	// 패키징 빌드에서 PropagateAlpha 셰이더가 달라 알파 손실되는 문제 해결.
	// ShowOnlyList에 없는 프리미티브는 렌더링 안 됨 → 빈 픽셀은 ClearColor(알파=0) 유지.
	SceneCapture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;

	// 매 프레임 캡처 (회전, 부착물 변경 실시간 반영)
	SceneCapture->bCaptureEveryFrame = true;
	SceneCapture->bCaptureOnMovement = true;

	SceneCapture->CaptureSource = ESceneCaptureSource::SCS_SceneColorHDR;
	SceneCapture->bAlwaysPersistRenderingState = true;

	// HDR 캡처: 자동 노출 비활성화 → 일관된 밝기
	SceneCapture->PostProcessSettings.bOverride_AutoExposureMethod = true;
	SceneCapture->PostProcessSettings.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
	SceneCapture->PostProcessSettings.bOverride_AutoExposureBias = true;
	SceneCapture->PostProcessSettings.AutoExposureBias = 1.0f;
	SceneCapture->ShowFlags.SetEyeAdaptation(false);

	// 시야 거리 제한: 프리뷰 메시(~100유닛) 외 원거리 오브젝트 캡처 방지
	SceneCapture->MaxViewDistanceOverride = 500.f;

	// FOV: 기본 30° (좁은 화각 → 무기가 화면 꽉 채움)
	// BP 서브클래스에서 SceneCapture 컴포넌트의 FOVAngle을 직접 변경 가능
	SceneCapture->FOVAngle = 30.f;

	// 배경을 깔끔하게 하기 위해 안개/대기 효과 제거
	SceneCapture->ShowFlags.SetFog(false);
	SceneCapture->ShowFlags.SetVolumetricFog(false);
	SceneCapture->ShowFlags.SetAtmosphere(false);
	SceneCapture->ShowFlags.SetSkyLighting(false);
	SceneCapture->ShowFlags.SetCloud(false);

	// ★ Lumen GI/반사/그림자 비활성화 (캐릭터 프리뷰와 동일)
	// ShowOnlyList 모드에서 월드 GI가 간섭하면 메시가 어둡게 보임
	SceneCapture->ShowFlags.SetDynamicShadows(false);
	SceneCapture->ShowFlags.SetGlobalIllumination(false);
	SceneCapture->ShowFlags.SetScreenSpaceReflections(false);
	SceneCapture->ShowFlags.SetAmbientOcclusion(false);
	SceneCapture->ShowFlags.SetReflectionEnvironment(false);

	// 무기 프리뷰 메시를 ShowOnlyList에 등록
	SceneCapture->ShowOnlyComponents.Add(PreviewMeshComponent);

	// ════════════════════════════════════════════════════════════════
	// 📌 3점 조명 시스템 — 물리적 범위 격리
	// ════════════════════════════════════════════════════════════════
	// ⚠️ DirectionalLight 사용 금지:
	//    Deferred Rendering에서 DirectionalLight는 LightingChannels를 무시하고
	//    전역 적용됨 → 월드 밤낮 조명을 오염시킴.
	//    SpotLight/PointLight만 사용하여 AttenuationRadius로 물리적 격리.
	//
	// 격리 보장 (2중):
	//   1차: AttenuationRadius=500 → 빛이 500유닛 밖으로 안 나감
	//   2차: Z=-10000 + 월드(Z=0) → 거리 10000 >> 500
	// ※ LightingChannels=Channel0 사용: 패키징 빌드에서 Channel1이 SceneCapture 미적용 이슈 대응
	//
	// Key Light (SpotLight): 정면 상단 → 메시 직접 조명
	// Fill Light (PointLight): 반대편 → 그림자 면 밝힘
	// Rim Light (PointLight): 뒤쪽 상단 → 실루엣 윤곽 강조

	// ── Key Light (메인 조명 — SpotLight) ──
	PreviewLight = CreateDefaultSubobject<USpotLightComponent>(TEXT("PreviewLight"));
	PreviewLight->SetupAttachment(SceneRoot);
	PreviewLight->SetRelativeLocation(FVector(150.f, -100.f, 120.f));
	PreviewLight->SetRelativeRotation(FRotator(-35.f, -30.f, 0.f));
	PreviewLight->Intensity = 8000.f;          // SpotLight 루멘 단위
	PreviewLight->AttenuationRadius = 500.f;    // 물리적 범위 제한
	PreviewLight->SetInnerConeAngle(30.f);      // 중심 밝은 영역
	PreviewLight->SetOuterConeAngle(60.f);      // 빛 감쇠 경계
	PreviewLight->CastShadows = false;
	PreviewLight->LightingChannels.bChannel0 = true;
	PreviewLight->LightingChannels.bChannel1 = false;

	// ── Fill Light (보조 조명 — 그림자 면 밝힘) ──
	FillLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("FillLight"));
	FillLight->SetupAttachment(SceneRoot);
	FillLight->SetRelativeLocation(FVector(-80.f, 100.f, 30.f));
	FillLight->Intensity = 3000.f;
	FillLight->AttenuationRadius = 500.f;
	FillLight->CastShadows = false;
	FillLight->LightingChannels.bChannel0 = true;
	FillLight->LightingChannels.bChannel1 = false;

	// ── Rim Light (윤곽 조명 — 뒤쪽 상단 실루엣 강조) ──
	RimLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("RimLight"));
	RimLight->SetupAttachment(SceneRoot);
	RimLight->SetRelativeLocation(FVector(-100.f, 0.f, 100.f));
	RimLight->Intensity = 5000.f;
	RimLight->AttenuationRadius = 500.f;
	RimLight->CastShadows = false;
	RimLight->LightingChannels.bChannel0 = true;
	RimLight->LightingChannels.bChannel1 = false;

	// ── 배경 차단 큐브 (UDS 하늘/대기 가림막) ──
	// SceneCapture의 ShowOnlyList/ShowFlags로는 UDS 같은 BP 스카이를 못 막음
	// 프리뷰 액터를 감싸는 검정 큐브로 물리적으로 하늘을 차단
	BackdropCube = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BackdropCube"));
	BackdropCube->SetupAttachment(SceneRoot);
	BackdropCube->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BackdropCube->CastShadow = false;
	// 음수 스케일 → 노멀 뒤집힘 → 내부에서 면이 보임
	BackdropCube->SetRelativeScale3D(FVector(-5.f, 5.f, 5.f));
	// 조명 채널 없음 → 어떤 라이트도 안 닿음 → 완전 검정 렌더
	// 머티리얼의 luminance 기반 Opacity에서 검정=0 → 투명 처리됨
	BackdropCube->LightingChannels.bChannel0 = false;
	BackdropCube->LightingChannels.bChannel1 = false;

	// 기본 큐브 메시 설정
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		BackdropCube->SetStaticMesh(CubeMesh.Object);
	}
}

// ════════════════════════════════════════════════════════════════
// 📌 SetPreviewMesh — 프리뷰 무기 메시 설정 및 초기 캡처
// ════════════════════════════════════════════════════════════════
// 호출 경로: AttachmentPanel::OpenForWeapon → 이 함수
// 처리 흐름:
//   1. InMesh nullptr 체크
//   2. PreviewMeshComponent->SetStaticMesh
//   3. RotationOffset → PreviewMeshComponent 상대 회전 설정
//   4. CameraDistance 설정:
//      - > 0: 직접 사용
//      - == 0: CalculateAutoDistance()로 자동 계산
//   5. EnsureRenderTarget() → SceneCapture->TextureTarget 연결
//   6. CaptureScene()으로 첫 프레임 캡처
// 실패 조건: InMesh == nullptr
// Phase 연결: Phase 8 — 무기 프리뷰 초기 설정
// ════════════════════════════════════════════════════════════════
void AInv_WeaponPreviewActor::SetPreviewMesh(UStaticMesh* InMesh, const FRotator& RotationOffset, float CameraDistance)
{
	if (!IsValid(InMesh))
	{
		UE_LOG(LogTemp, Warning, TEXT("[Weapon Preview] SetPreviewMesh 실패: InMesh가 nullptr"));
		return;
	}

	if (!IsValid(PreviewMeshComponent))
	{
		UE_LOG(LogTemp, Error, TEXT("[Weapon Preview] SetPreviewMesh 실패: PreviewMeshComponent가 유효하지 않음"));
		return;
	}

	// 메시 설정
	PreviewMeshComponent->SetStaticMesh(InMesh);

	// 초기 회전 오프셋 적용
	PreviewMeshComponent->SetRelativeRotation(RotationOffset);

	// 카메라 거리 설정 (우선순위: 아이템 개별값 > 자동 계산 > BP 설정값)
	if (IsValid(CameraBoom))
	{
		if (CameraDistance > 0.f)
		{
			// 1순위: EquipmentFragment에서 명시적으로 지정한 아이템별 거리
			CameraBoom->TargetArmLength = CameraDistance;
		}
		else if (bAutoCalculateDistance)
		{
			// 2순위: 메시 크기 기반 자동 계산 (기본 동작)
			CameraBoom->TargetArmLength = CalculateAutoDistance();
		}
		// else: 3순위 — BP에서 설정한 CameraBoom->TargetArmLength 유지 (덮어쓰지 않음)
	}

	// RenderTarget 준비 (bCaptureEveryFrame=true이므로 수동 캡처 불필요)
	EnsureRenderTarget();

#if INV_DEBUG_ATTACHMENT
	UE_LOG(LogTemp, Warning, TEXT("========== [WeaponPreview Debug] =========="));
	UE_LOG(LogTemp, Warning, TEXT("  Mesh: %s"), PreviewMeshComponent->GetStaticMesh() ? *PreviewMeshComponent->GetStaticMesh()->GetName() : TEXT("NULL"));
	UE_LOG(LogTemp, Warning, TEXT("  Mesh Visible: %s"), PreviewMeshComponent->IsVisible() ? TEXT("YES") : TEXT("NO"));
	UE_LOG(LogTemp, Warning, TEXT("  Mesh Location: %s"), *PreviewMeshComponent->GetComponentLocation().ToString());
	UE_LOG(LogTemp, Warning, TEXT("  Mesh LightingChannel0: %s, Channel1: %s"),
		PreviewMeshComponent->LightingChannels.bChannel0 ? TEXT("ON") : TEXT("OFF"),
		PreviewMeshComponent->LightingChannels.bChannel1 ? TEXT("ON") : TEXT("OFF"));

	if (IsValid(SceneCapture))
	{
		UE_LOG(LogTemp, Warning, TEXT("  SceneCapture Valid: YES"));
		UE_LOG(LogTemp, Warning, TEXT("  CaptureSource: %d"), (int32)SceneCapture->CaptureSource);
		UE_LOG(LogTemp, Warning, TEXT("  TextureTarget: %s"), SceneCapture->TextureTarget ? TEXT("SET") : TEXT("NULL"));
		UE_LOG(LogTemp, Warning, TEXT("  bCaptureEveryFrame: %s"), SceneCapture->bCaptureEveryFrame ? TEXT("YES") : TEXT("NO"));
		UE_LOG(LogTemp, Warning, TEXT("  PrimitiveRenderMode: %d"), (int32)SceneCapture->PrimitiveRenderMode);
		UE_LOG(LogTemp, Warning, TEXT("  ShowOnlyComponents Num: %d"), SceneCapture->ShowOnlyComponents.Num());
		UE_LOG(LogTemp, Warning, TEXT("  ShowOnlyActors Num: %d"), SceneCapture->ShowOnlyActors.Num());
		UE_LOG(LogTemp, Warning, TEXT("  MaxViewDistanceOverride: %.1f"), SceneCapture->MaxViewDistanceOverride);
		UE_LOG(LogTemp, Warning, TEXT("  FOVAngle: %.1f"), SceneCapture->FOVAngle);
		UE_LOG(LogTemp, Warning, TEXT("  AutoExposureBias: %.1f"), SceneCapture->PostProcessSettings.AutoExposureBias);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("  SceneCapture: INVALID!"));
	}

	if (IsValid(RenderTarget))
	{
		UE_LOG(LogTemp, Warning, TEXT("  RenderTarget: %dx%d, Format=%d"),
			RenderTarget->SizeX, RenderTarget->SizeY, (int32)RenderTarget->GetFormat());
		UE_LOG(LogTemp, Warning, TEXT("  RenderTarget ClearColor: R=%.2f G=%.2f B=%.2f A=%.2f"),
			RenderTarget->ClearColor.R, RenderTarget->ClearColor.G, RenderTarget->ClearColor.B, RenderTarget->ClearColor.A);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("  RenderTarget: INVALID!"));
	}

	if (IsValid(PreviewLight))
	{
		UE_LOG(LogTemp, Warning, TEXT("  PreviewLight Intensity: %.0f, Visible: %s, Channel0: %s"),
			PreviewLight->Intensity,
			PreviewLight->IsVisible() ? TEXT("YES") : TEXT("NO"),
			PreviewLight->LightingChannels.bChannel0 ? TEXT("ON") : TEXT("OFF"));
	}

	if (IsValid(CameraBoom))
	{
		UE_LOG(LogTemp, Warning, TEXT("  CameraBoom ArmLength: %.1f"), CameraBoom->TargetArmLength);
	}

	UE_LOG(LogTemp, Warning, TEXT("  Actor Location: %s"), *GetActorLocation().ToString());
	UE_LOG(LogTemp, Warning, TEXT("=========================================="));
	UE_LOG(LogTemp, Log, TEXT("[Weapon Preview] 메시 설정 완료: %s, ArmLength=%.1f, Rotation=%s"),
		*InMesh->GetName(),
		IsValid(CameraBoom) ? CameraBoom->TargetArmLength : -1.f,
		*RotationOffset.ToString());
#endif
}

// ════════════════════════════════════════════════════════════════
// 📌 RotatePreview — 마우스 드래그 회전
// ════════════════════════════════════════════════════════════════
// 호출 경로: AttachmentPanel::NativeTick (드래그 감지) → 이 함수
// 처리 흐름:
//   1. PreviewMeshComponent에 Yaw 회전 추가 (AddRelativeRotation)
//   2. CaptureScene() 호출로 회전 상태 즉시 반영
// Phase 연결: Phase 8 — CharacterDisplay와 동일한 드래그 회전 패턴
// ════════════════════════════════════════════════════════════════
void AInv_WeaponPreviewActor::RotatePreview(float YawDelta, float PitchDelta)
{
	if (!IsValid(PreviewMeshComponent)) return;

	// Pitch 클램프: 누적값이 ±MaxPitchAngle 범위를 벗어나지 않도록 제한
	const float NewPitch = FMath::Clamp(AccumulatedPitch + PitchDelta, -MaxPitchAngle, MaxPitchAngle);
	const float ClampedPitchDelta = NewPitch - AccumulatedPitch;
	AccumulatedPitch = NewPitch;

	// Yaw는 무제한, Pitch는 클램프된 값만 적용
	PreviewMeshComponent->AddRelativeRotation(FRotator(ClampedPitchDelta, YawDelta, 0.f));
	// bCaptureEveryFrame=true이므로 수동 캡처 불필요 — 다음 프레임에 자동 반영
}

// ════════════════════════════════════════════════════════════════
// 📌 GetRenderTarget — RenderTarget 접근
// ════════════════════════════════════════════════════════════════
UTextureRenderTarget2D* AInv_WeaponPreviewActor::GetRenderTarget() const
{
	return RenderTarget;
}

// ════════════════════════════════════════════════════════════════
// 📌 CaptureNow — 즉시 캡처 실행
// ════════════════════════════════════════════════════════════════
// 호출 경로: SetPreviewMesh / RotatePreview / 외부 → 이 함수
// 처리 흐름:
//   1. SceneCapture 유효성 체크
//   2. TextureTarget 유효성 체크
//   3. CaptureScene() 호출
// ════════════════════════════════════════════════════════════════
void AInv_WeaponPreviewActor::CaptureNow()
{
	if (!IsValid(SceneCapture))
	{
		UE_LOG(LogTemp, Warning, TEXT("[Weapon Preview] CaptureNow 실패: SceneCapture 무효"));
		return;
	}

	if (!IsValid(SceneCapture->TextureTarget))
	{
		UE_LOG(LogTemp, Warning, TEXT("[Weapon Preview] CaptureNow 실패: TextureTarget 무효"));
		return;
	}

	SceneCapture->CaptureScene();
}

// ════════════════════════════════════════════════════════════════
// 📌 EnsureRenderTarget — RenderTarget Lazy Init
// ════════════════════════════════════════════════════════════════
// 호출 경로: SetPreviewMesh → 이 함수
// 처리 흐름:
//   1. RenderTarget이 이미 있으면 스킵
//   2. 없으면 NewObject<UTextureRenderTarget2D> 생성
//   3. InitAutoFormat(Width, Height) → BP에서 지정한 해상도
//   4. SceneCapture->TextureTarget에 연결
// ════════════════════════════════════════════════════════════════
void AInv_WeaponPreviewActor::EnsureRenderTarget()
{
	if (IsValid(RenderTarget)) return;

	RenderTarget = NewObject<UTextureRenderTarget2D>(this);
	if (!IsValid(RenderTarget))
	{
		UE_LOG(LogTemp, Error, TEXT("[Weapon Preview] RenderTarget 생성 실패!"));
		return;
	}

	// ClearColor: 배경 완전 투명 (알파=0) → Material Translucent에서 배경이 사라짐
	RenderTarget->ClearColor = FLinearColor(0.f, 0.f, 0.f, 0.f);
	const int32 Width = FMath::Clamp(RenderTargetWidth, 128, 2048);
	const int32 Height = FMath::Clamp(RenderTargetHeight, 128, 2048);
	// ★ 명시적 HDR 포맷: 패키징 빌드에서 InitAutoFormat이 LDR(PF_B8G8R8A8)을
	// 선택하면 알파 채널이 손실되어 배경 투명 처리가 깨짐.
	// PF_FloatRGBA(RGBA16F)로 고정하여 어떤 빌드에서든 HDR+알파 보장.
	RenderTarget->InitCustomFormat(Width, Height, PF_FloatRGBA, false);
	RenderTarget->UpdateResourceImmediate(true);

	if (IsValid(SceneCapture))
	{
		SceneCapture->TextureTarget = RenderTarget;
	}

#if INV_DEBUG_ATTACHMENT
	UE_LOG(LogTemp, Log, TEXT("[Weapon Preview] RenderTarget 생성 완료 (%dx%d)"), Width, Height);
#endif
}

// ════════════════════════════════════════════════════════════════
// 📌 CalculateAutoDistance — 메시 Bounds 기반 카메라 거리 자동 계산
// ════════════════════════════════════════════════════════════════
// 호출 경로: SetPreviewMesh (CameraDistance == 0일 때) → 이 함수
// 처리 흐름:
//   1. PreviewMeshComponent->GetStaticMesh()->GetBounds() 가져오기
//   2. BoundingSphere 반지름의 2.5배를 카메라 거리로 사용
//   3. 최소 100, 최대 1000 클램프 (너무 가까이/멀리 방지)
// 반환: SpringArm TargetArmLength에 설정할 float 값
// ════════════════════════════════════════════════════════════════
float AInv_WeaponPreviewActor::CalculateAutoDistance() const
{
	if (!IsValid(PreviewMeshComponent)) return AutoDistanceDefault;

	UStaticMesh* Mesh = PreviewMeshComponent->GetStaticMesh();
	if (!IsValid(Mesh)) return AutoDistanceDefault;

	const FBoxSphereBounds Bounds = Mesh->GetBounds();
	const float SphereRadius = Bounds.SphereRadius;

	if (SphereRadius <= KINDA_SMALL_NUMBER) return AutoDistanceDefault;

	const float AutoDistance = SphereRadius * AutoDistanceMultiplier;
	const float ClampedDistance = FMath::Clamp(AutoDistance, AutoDistanceMin, AutoDistanceMax);

#if INV_DEBUG_ATTACHMENT
	UE_LOG(LogTemp, Log, TEXT("[Weapon Preview] 자동 거리 계산: SphereRadius=%.1f × %.1f → %.1f → Clamped=%.1f"),
		SphereRadius, AutoDistanceMultiplier, AutoDistance, ClampedDistance);
#endif

	return ClampedDistance;
}

// ════════════════════════════════════════════════════════════════
// 📌 AddAttachmentPreview — 슬롯에 부착물 3D 메시 추가
// ════════════════════════════════════════════════════════════════
// 호출 경로: AttachmentPanel::RefreshPreviewAttachments → 이 함수
// 처리 흐름:
//   1. 이미 해당 SlotIndex에 메시가 있으면 제거
//   2. NewObject<UStaticMeshComponent> 생성 (런타임이므로 CreateDefaultSubobject 불가)
//   3. 메시 설정 + LightingChannels Channel1 전용
//   4. SocketName이 유효하고 PreviewMeshComponent에 소켓이 있으면 소켓 부착
//      없으면 Offset의 Location/Rotation을 RelativeTransform으로 적용
//   5. RegisterComponent + TMap에 저장
// ════════════════════════════════════════════════════════════════
void AInv_WeaponPreviewActor::AddAttachmentPreview(int32 SlotIndex, UStaticMesh* AttachMesh, FName SocketName, const FTransform& Offset)
{
	if (!IsValid(AttachMesh))
	{
#if INV_DEBUG_ATTACHMENT
		UE_LOG(LogTemp, Warning, TEXT("[Weapon Preview] AddAttachmentPreview 실패: AttachMesh가 nullptr (SlotIndex=%d)"), SlotIndex);
#endif
		return;
	}

	if (!IsValid(PreviewMeshComponent))
	{
#if INV_DEBUG_ATTACHMENT
		UE_LOG(LogTemp, Warning, TEXT("[Weapon Preview] AddAttachmentPreview 실패: PreviewMeshComponent 무효"));
#endif
		return;
	}

	// 이미 존재하면 제거 후 재생성
	RemoveAttachmentPreview(SlotIndex);

	// 런타임 동적 생성 (CreateDefaultSubobject는 생성자 전용)
	UStaticMeshComponent* NewComp = NewObject<UStaticMeshComponent>(this,
		*FString::Printf(TEXT("AttachPreview_%d"), SlotIndex));
	if (!IsValid(NewComp)) return;

	NewComp->SetStaticMesh(AttachMesh);
	NewComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	NewComp->CastShadow = false;

	// Channel0 사용 (PreviewMeshComponent와 동일 — 패키징 빌드 채널1 미적용 이슈 대응)
	NewComp->LightingChannels.bChannel0 = true;
	NewComp->LightingChannels.bChannel1 = false;

	// 소켓 부착 시도: SocketName이 유효하고 메시에 소켓이 존재하면 소켓 부착
	const bool bHasSocket = !SocketName.IsNone()
		&& IsValid(PreviewMeshComponent->GetStaticMesh())
		&& PreviewMeshComponent->GetStaticMesh()->FindSocket(SocketName) != nullptr;

	if (bHasSocket)
	{
		NewComp->AttachToComponent(PreviewMeshComponent,
			FAttachmentTransformRules::SnapToTargetNotIncludingScale, SocketName);

		// 소켓 위치에 추가 오프셋 적용
		NewComp->SetRelativeTransform(Offset);
	}
	else
	{
		// 소켓 없음 → PreviewMeshComponent에 상대 Transform으로 부착
		NewComp->AttachToComponent(PreviewMeshComponent,
			FAttachmentTransformRules::KeepRelativeTransform);
		NewComp->SetRelativeTransform(Offset);
	}

	NewComp->RegisterComponent();

	// ShowOnlyList에 부착물 컴포넌트 추가 (SceneCapture가 렌더링하도록)
	if (IsValid(SceneCapture))
	{
		SceneCapture->ShowOnlyComponents.Add(NewComp);
	}

	AttachmentMeshComponents.Add(SlotIndex, NewComp);

#if INV_DEBUG_ATTACHMENT
	UE_LOG(LogTemp, Log, TEXT("[Weapon Preview] 부착물 프리뷰 추가: Slot=%d, Mesh=%s, Socket=%s, bSocketUsed=%s"),
		SlotIndex, *AttachMesh->GetName(),
		*SocketName.ToString(),
		bHasSocket ? TEXT("Y") : TEXT("N"));
#endif
}

// ════════════════════════════════════════════════════════════════
// 📌 RemoveAttachmentPreview — 특정 슬롯의 부착물 메시 제거
// ════════════════════════════════════════════════════════════════
void AInv_WeaponPreviewActor::RemoveAttachmentPreview(int32 SlotIndex)
{
	TObjectPtr<UStaticMeshComponent>* Found = AttachmentMeshComponents.Find(SlotIndex);
	if (Found && IsValid(*Found))
	{
		// ShowOnlyList에서 먼저 제거
		if (IsValid(SceneCapture))
		{
			SceneCapture->ShowOnlyComponents.Remove(*Found);
		}
		(*Found)->DestroyComponent();
	}
	AttachmentMeshComponents.Remove(SlotIndex);
}

// ════════════════════════════════════════════════════════════════
// 📌 ClearAllAttachmentPreviews — 모든 부착물 메시 제거
// ════════════════════════════════════════════════════════════════
void AInv_WeaponPreviewActor::ClearAllAttachmentPreviews()
{
	for (auto& Pair : AttachmentMeshComponents)
	{
		if (IsValid(Pair.Value))
		{
			// ShowOnlyList에서 먼저 제거
			if (IsValid(SceneCapture))
			{
				SceneCapture->ShowOnlyComponents.Remove(Pair.Value);
			}
			Pair.Value->DestroyComponent();
		}
	}
	AttachmentMeshComponents.Empty();
}
