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
//   5. SceneCapture 생성 → PRM_RenderScenePrimitives + MaxViewDistance 제한
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

	// ── 무기 메시 (Channel 1 전용 → 월드 라이트 영향 안 받음) ──
	PreviewMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PreviewMesh"));
	PreviewMeshComponent->SetupAttachment(SceneRoot);
	PreviewMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PreviewMeshComponent->CastShadow = false;
	PreviewMeshComponent->LightingChannels.bChannel0 = false;
	PreviewMeshComponent->LightingChannels.bChannel1 = true;

	// ── 카메라 붐 (스프링 암) ──
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(SceneRoot);
	CameraBoom->TargetArmLength = 150.f;
	CameraBoom->bDoCollisionTest = false;
	CameraBoom->SetRelativeRotation(FRotator(-15.f, 0.f, 0.f));

	// ── SceneCapture ──
	SceneCapture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("SceneCapture"));
	SceneCapture->SetupAttachment(CameraBoom);

	// PRM_RenderScenePrimitives: 카메라 시야 내 모든 프리미티브 렌더
	// Z=-10000이므로 월드 오브젝트는 시야에 안 잡힘
	// ShowOnlyList 사용 시 라이트가 제외되어 메시가 검정으로 렌더되는 문제 해결
	SceneCapture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_RenderScenePrimitives;

	// 매 프레임 캡처 (회전, 부착물 변경 실시간 반영)
	SceneCapture->bCaptureEveryFrame = true;
	SceneCapture->bCaptureOnMovement = true;

	// SceneColorHDR: 알파 채널 보존 (메시=1, 배경=0) → Material에서 투명 배경 구현
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

	// 배경을 깔끔하게 하기 위해 안개/대기 효과 제거
	SceneCapture->ShowFlags.SetFog(false);
	SceneCapture->ShowFlags.SetVolumetricFog(false);

	// ════════════════════════════════════════════════════════════════
	// 📌 3점 조명 시스템 — 물리적 범위 격리
	// ════════════════════════════════════════════════════════════════
	// ⚠️ DirectionalLight 사용 금지:
	//    Deferred Rendering에서 DirectionalLight는 LightingChannels를 무시하고
	//    전역 적용됨 → 월드 밤낮 조명을 오염시킴.
	//    SpotLight/PointLight만 사용하여 AttenuationRadius로 물리적 격리.
	//
	// 격리 보장 (3중):
	//   1차: AttenuationRadius=500 → 빛이 500유닛 밖으로 안 나감
	//   2차: LightingChannels=Channel1 → Channel0(월드) 불간섭
	//   3차: Z=-10000 + 월드(Z=0) → 거리 10000 >> 500
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
	PreviewLight->LightingChannels.bChannel0 = false;
	PreviewLight->LightingChannels.bChannel1 = true;

	// ── Fill Light (보조 조명 — 그림자 면 밝힘) ──
	FillLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("FillLight"));
	FillLight->SetupAttachment(SceneRoot);
	FillLight->SetRelativeLocation(FVector(-80.f, 100.f, 30.f));
	FillLight->Intensity = 3000.f;
	FillLight->AttenuationRadius = 500.f;
	FillLight->CastShadows = false;
	FillLight->LightingChannels.bChannel0 = false;
	FillLight->LightingChannels.bChannel1 = true;

	// ── Rim Light (윤곽 조명 — 뒤쪽 상단 실루엣 강조) ──
	RimLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("RimLight"));
	RimLight->SetupAttachment(SceneRoot);
	RimLight->SetRelativeLocation(FVector(-100.f, 0.f, 100.f));
	RimLight->Intensity = 5000.f;
	RimLight->AttenuationRadius = 500.f;
	RimLight->CastShadows = false;
	RimLight->LightingChannels.bChannel0 = false;
	RimLight->LightingChannels.bChannel1 = true;
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

	// 카메라 거리 설정
	if (IsValid(CameraBoom))
	{
		if (CameraDistance > 0.f)
		{
			// BP에서 명시적으로 지정한 거리 사용
			CameraBoom->TargetArmLength = CameraDistance;
		}
		else
		{
			// 메시 크기 기반 자동 계산
			CameraBoom->TargetArmLength = CalculateAutoDistance();
		}
	}

	// RenderTarget 준비 (bCaptureEveryFrame=true이므로 수동 캡처 불필요)
	EnsureRenderTarget();

#if INV_DEBUG_ATTACHMENT
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
void AInv_WeaponPreviewActor::RotatePreview(float YawDelta)
{
	if (!IsValid(PreviewMeshComponent)) return;

	PreviewMeshComponent->AddRelativeRotation(FRotator(0.f, YawDelta, 0.f));
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
//   3. InitAutoFormat(512, 512) → 512x512 해상도
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

	// 512x512 해상도 — UI 프리뷰 용도로 충분
	// ClearColor: 짙은 회색 배경 (T_Pop_Up 배경과 조화, 메시 대비 확보)
	// 배경 완전 투명 (알파=0) → Material Translucent에서 배경이 사라짐
	RenderTarget->ClearColor = FLinearColor(0.f, 0.f, 0.f, 0.f);
	RenderTarget->InitAutoFormat(512, 512);
	RenderTarget->UpdateResourceImmediate(true);

	if (IsValid(SceneCapture))
	{
		SceneCapture->TextureTarget = RenderTarget;
	}

#if INV_DEBUG_ATTACHMENT
	UE_LOG(LogTemp, Log, TEXT("[Weapon Preview] RenderTarget 생성 완료 (512x512)"));
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
	constexpr float DefaultDistance = 150.f;
	constexpr float MinDistance = 100.f;
	constexpr float MaxDistance = 1000.f;
	constexpr float DistanceMultiplier = 2.5f; // 메시 크기 대비 여유 계수

	if (!IsValid(PreviewMeshComponent)) return DefaultDistance;

	UStaticMesh* Mesh = PreviewMeshComponent->GetStaticMesh();
	if (!IsValid(Mesh)) return DefaultDistance;

	const FBoxSphereBounds Bounds = Mesh->GetBounds();
	const float SphereRadius = Bounds.SphereRadius;

	if (SphereRadius <= KINDA_SMALL_NUMBER) return DefaultDistance;

	const float AutoDistance = SphereRadius * DistanceMultiplier;
	const float ClampedDistance = FMath::Clamp(AutoDistance, MinDistance, MaxDistance);

#if INV_DEBUG_ATTACHMENT
	UE_LOG(LogTemp, Log, TEXT("[Weapon Preview] 자동 거리 계산: SphereRadius=%.1f → AutoDist=%.1f → Clamped=%.1f"),
		SphereRadius, AutoDistance, ClampedDistance);
#endif

	return ClampedDistance;
}
