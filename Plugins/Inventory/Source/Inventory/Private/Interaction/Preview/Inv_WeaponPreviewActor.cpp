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
#include "Components/DirectionalLightComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/StaticMesh.h"

// ════════════════════════════════════════════════════════════════
// 📌 생성자 — 컴포넌트 초기 구성
// ════════════════════════════════════════════════════════════════
// 처리 흐름:
//   1. 리플리케이션 비활성화 (로컬 전용 UI 보조 액터)
//   2. SceneRoot 생성 → RootComponent
//   3. PreviewMeshComponent 생성 → Root에 부착
//   4. CameraBoom(SpringArm) 생성 → Root에 부착
//   5. SceneCapture 생성 → CameraBoom에 부착
//   6. PreviewLight 생성 → Root에 부착
//   7. SceneCapture 설정:
//      - bCaptureEveryFrame = false (수동)
//      - bCaptureOnMovement = false
//      - PrimitiveRenderMode = UseShowOnlyList
//      - ShowOnlyComponents에 PreviewMeshComponent 추가
// ════════════════════════════════════════════════════════════════
AInv_WeaponPreviewActor::AInv_WeaponPreviewActor()
{
	PrimaryActorTick.bCanEverTick = false;
	SetReplicates(false); // 프리뷰 액터는 로컬 전용 — 서버에 보낼 필요 없음

	// ── Root ──
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;

	// ── 무기 메시 ──
	PreviewMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PreviewMesh"));
	PreviewMeshComponent->SetupAttachment(SceneRoot);
	PreviewMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PreviewMeshComponent->CastShadow = false; // 그림자 비활성화 (프리뷰에 불필요)

	// ── 카메라 붐 (스프링 암) ──
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(SceneRoot);
	CameraBoom->TargetArmLength = 150.f; // 기본값, SetPreviewMesh에서 재설정
	CameraBoom->bDoCollisionTest = false; // 월드 충돌 무시
	CameraBoom->SetRelativeRotation(FRotator(-15.f, 0.f, 0.f)); // 약간 위에서 내려다보는 각도

	// ── SceneCapture ──
	SceneCapture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("SceneCapture"));
	SceneCapture->SetupAttachment(CameraBoom);

	// 핵심 설정: 매 프레임 캡처하지 않음 → CaptureScene() 수동 호출
	SceneCapture->bCaptureEveryFrame = false;
	SceneCapture->bCaptureOnMovement = false;

	// 핵심 설정: ShowOnlyList에 등록된 컴포넌트만 촬영
	// → 월드 배경, 다른 액터 제외하고 무기 메시만 찍힘
	SceneCapture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
	SceneCapture->ShowOnlyComponents.Add(PreviewMeshComponent);

	// 캡처 소스: 최종 색상 (포스트프로세싱 포함)
	SceneCapture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;

	// 프레임 간 렌더링 상태 유지 (깜빡임 방지)
	SceneCapture->bAlwaysPersistRenderingState = true;

	// ── 프리뷰 전용 조명 ──
	PreviewLight = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("PreviewLight"));
	PreviewLight->SetupAttachment(SceneRoot);
	PreviewLight->SetRelativeRotation(FRotator(-45.f, -45.f, 0.f)); // 좌상단에서 비추는 조명
	PreviewLight->Intensity = 8.f; // 프리뷰 밝기 (월드 아래쪽이라 조명이 없으므로)
	PreviewLight->CastShadows = false; // 프리뷰에서 그림자 불필요
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

	// RenderTarget 준비 및 캡처
	EnsureRenderTarget();
	CaptureNow();

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
	CaptureNow();
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
