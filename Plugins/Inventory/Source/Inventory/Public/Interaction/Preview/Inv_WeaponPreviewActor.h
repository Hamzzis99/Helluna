// Gihyeon's Inventory Project
//
// ════════════════════════════════════════════════════════════════════════════════
// 📌 무기 프리뷰 액터 (Weapon Preview Actor) — Phase 8
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 이 파일의 역할:
//    부착물 패널 중앙에 표시할 무기 3D 프리뷰를 촬영하는 액터.
//    SceneCaptureComponent2D로 StaticMesh를 RenderTarget에 캡처하여
//    UMG Image 위젯에 Material로 표시한다.
//
// 📌 생명주기:
//    - AttachmentPanel::OpenForWeapon() 시 동적 스폰 (Z = -10000)
//    - AttachmentPanel::ClosePanel() 시 Destroy
//    - 부착물 패널은 자주 열고 닫는 UI가 아니므로 스폰/파괴 비용 무시 가능
//
// 📌 동작 흐름:
//    1. SpawnActor<AInv_WeaponPreviewActor>(SpawnParams)
//    2. SetPreviewMesh(SM, RotOffset, CamDist)
//       → StaticMeshComponent에 메시 설정
//       → SpringArm 길이 조정 (CamDist > 0이면 사용, 아니면 자동 계산)
//       → 초기 회전 적용
//       → CaptureScene() 1회 호출
//    3. 사용자 마우스 드래그 → RotatePreview(YawDelta)
//       → 메시 회전 → CaptureScene() 호출
//    4. 패널 닫기 → Destroy()
//
// 📌 컴포넌트 구성:
//    AInv_WeaponPreviewActor
//     ├─ USceneComponent (Root)
//     ├─ UStaticMeshComponent (PreviewMeshComponent)
//     │     └─ 무기 메시 표시, ShowOnlyComponent 대상
//     ├─ USpringArmComponent (CameraBoom)
//     │     ├─ TargetArmLength = 카메라 거리
//     │     ├─ bDoCollisionTest = false
//     │     └─ USceneCaptureComponent2D (SceneCapture)
//     │           ├─ bCaptureEveryFrame = false (수동 캡처)
//     │           ├─ bCaptureOnMovement = false
//     │           ├─ ShowOnlyComponent = PreviewMeshComponent
//     │           └─ TextureTarget = 동적 생성 RenderTarget
//     └─ UDirectionalLightComponent (PreviewLight)
//           └─ 프리뷰 전용 조명 (월드 조명과 무관하게 일정한 밝기)
//
// 📌 네트워크:
//    SetReplicates(false) — 프리뷰 액터는 로컬 전용 (서버에 보낼 필요 없음)
//    각 클라이언트가 자기 화면에서만 보는 UI 보조 액터
//
// ════════════════════════════════════════════════════════════════════════════════

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Inv_WeaponPreviewActor.generated.h"

class UStaticMeshComponent;
class USpringArmComponent;
class USceneCaptureComponent2D;
class UDirectionalLightComponent;
class UPointLightComponent;
class UTextureRenderTarget2D;
class UStaticMesh;

UCLASS()
class INVENTORY_API AInv_WeaponPreviewActor : public AActor
{
	GENERATED_BODY()

public:
	AInv_WeaponPreviewActor();

	// ════════════════════════════════════════════════════════════════
	// 📌 SetPreviewMesh — 프리뷰할 무기 메시 설정 및 초기 캡처
	// ════════════════════════════════════════════════════════════════
	// 호출 경로: AttachmentPanel::OpenForWeapon → 이 함수
	// 처리 흐름:
	//   1. StaticMeshComponent에 메시 설정
	//   2. RotationOffset 적용 (메시의 RelativeRotation)
	//   3. CameraDistance > 0이면 SpringArm 길이 설정
	//      CameraDistance == 0이면 메시 Bounds 기반 자동 계산
	//   4. CaptureScene() 호출하여 RenderTarget에 첫 프레임 캡처
	// 실패 조건: InMesh가 nullptr → 로그 출력 후 리턴
	// ════════════════════════════════════════════════════════════════
	void SetPreviewMesh(UStaticMesh* InMesh, const FRotator& RotationOffset, float CameraDistance);

	// ════════════════════════════════════════════════════════════════
	// 📌 RotatePreview — 마우스 드래그에 의한 무기 회전
	// ════════════════════════════════════════════════════════════════
	// 호출 경로: AttachmentPanel::NativeTick (드래그 감지) → 이 함수
	// 처리 흐름:
	//   1. PreviewMeshComponent에 Yaw 회전 추가
	//   2. CaptureScene() 호출하여 회전된 상태 캡처
	// ════════════════════════════════════════════════════════════════
	void RotatePreview(float YawDelta);

	// ════════════════════════════════════════════════════════════════
	// 📌 GetRenderTarget — RenderTarget 접근 (UMG Image에 연결용)
	// ════════════════════════════════════════════════════════════════
	UTextureRenderTarget2D* GetRenderTarget() const;

	// ════════════════════════════════════════════════════════════════
	// 📌 CaptureNow — 즉시 캡처 요청 (외부에서 명시적 호출용)
	// ════════════════════════════════════════════════════════════════
	void CaptureNow();

private:
	// ── 컴포넌트 ──

	UPROPERTY(VisibleAnywhere, Category = "Preview")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, Category = "Preview")
	TObjectPtr<UStaticMeshComponent> PreviewMeshComponent;

	UPROPERTY(VisibleAnywhere, Category = "Preview")
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, Category = "Preview")
	TObjectPtr<USceneCaptureComponent2D> SceneCapture;

	UPROPERTY(VisibleAnywhere, Category = "Preview")
	TObjectPtr<UDirectionalLightComponent> PreviewLight;

	// ── 프리뷰 전용 보조 조명 (Channel 1 전용) ──
	// FillLight: 메인 조명 반대편 → 그림자 면 밝힘 (반사광 역할)
	UPROPERTY(VisibleAnywhere, Category = "Preview")
	TObjectPtr<UPointLightComponent> FillLight;

	// RimLight: 뒤쪽 상단 → 가장자리 윤곽 강조 (실루엣 분리)
	UPROPERTY(VisibleAnywhere, Category = "Preview")
	TObjectPtr<UPointLightComponent> RimLight;

	// ── RenderTarget ──

	UPROPERTY()
	TObjectPtr<UTextureRenderTarget2D> RenderTarget;

	// ── 내부 함수 ──

	void EnsureRenderTarget();
	float CalculateAutoDistance() const;
};
