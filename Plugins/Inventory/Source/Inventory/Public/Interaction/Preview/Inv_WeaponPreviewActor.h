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
class USpotLightComponent;
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

	UPROPERTY(VisibleAnywhere, Category = "상호작용|프리뷰", meta = (DisplayName = "씬 루트"))
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, Category = "상호작용|프리뷰", meta = (DisplayName = "프리뷰 메시"))
	TObjectPtr<UStaticMeshComponent> PreviewMeshComponent;

	UPROPERTY(VisibleAnywhere, Category = "상호작용|프리뷰", meta = (DisplayName = "카메라 붐"))
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, Category = "상호작용|프리뷰", meta = (DisplayName = "씬 캡처"))
	TObjectPtr<USceneCaptureComponent2D> SceneCapture;

	// Key Light: SpotLight (범위 제한 → 월드 조명 오염 방지)
	// ※ DirectionalLight는 Deferred에서 LightingChannels를 무시하므로 사용 금지
	UPROPERTY(VisibleAnywhere, Category = "상호작용|프리뷰", meta = (DisplayName = "프리뷰 조명"))
	TObjectPtr<USpotLightComponent> PreviewLight;

	// ── 프리뷰 전용 보조 조명 (Channel 1 전용) ──
	// FillLight: 메인 조명 반대편 → 그림자 면 밝힘 (반사광 역할)
	UPROPERTY(VisibleAnywhere, Category = "상호작용|프리뷰", meta = (DisplayName = "보조 조명"))
	TObjectPtr<UPointLightComponent> FillLight;

	// RimLight: 뒤쪽 상단 → 가장자리 윤곽 강조 (실루엣 분리)
	UPROPERTY(VisibleAnywhere, Category = "상호작용|프리뷰", meta = (DisplayName = "림 조명"))
	TObjectPtr<UPointLightComponent> RimLight;

	// ════════════════════════════════════════════════════════════════
	// 📌 BP에서 조정 가능한 프리뷰 설정값
	// ════════════════════════════════════════════════════════════════

	// RenderTarget 가로 해상도 (2의 제곱 권장, 높을수록 선명하지만 VRAM 증가)
	UPROPERTY(EditDefaultsOnly, Category = "상호작용|프리뷰|렌더", meta = (DisplayName = "렌더 가로 해상도", ClampMin = "128", ClampMax = "2048", ToolTip = "RenderTarget 가로 해상도. 2의 제곱 권장 (256, 512, 1024)"))
	int32 RenderTargetWidth = 512;

	// RenderTarget 세로 해상도
	UPROPERTY(EditDefaultsOnly, Category = "상호작용|프리뷰|렌더", meta = (DisplayName = "렌더 세로 해상도", ClampMin = "128", ClampMax = "2048", ToolTip = "RenderTarget 세로 해상도. 16:9 비율 = 가로 512 기준 288"))
	int32 RenderTargetHeight = 288;

	// ── 자동 카메라 거리 계산 파라미터 ──
	// SetPreviewMesh()에서 CameraDistance=0일 때 메시 Bounds 기반으로 자동 계산

	// true: 메시 크기 기반 자동 계산 (기본값, 기존 동작 유지)
	// false: BP에서 설정한 CameraBoom->TargetArmLength를 그대로 사용
	UPROPERTY(EditDefaultsOnly, Category = "상호작용|프리뷰|카메라", meta = (DisplayName = "자동 카메라 거리 계산", ToolTip = "false로 설정하면 BP에서 지정한 CameraBoom TargetArmLength를 사용합니다."))
	bool bAutoCalculateDistance = true;

	// 메시 Bounds를 가져올 수 없을 때 사용하는 폴백 거리
	UPROPERTY(EditDefaultsOnly, Category = "상호작용|프리뷰|카메라", meta = (DisplayName = "기본 카메라 거리", ClampMin = "50", ToolTip = "메시 Bounds 계산 실패 시 사용되는 기본 거리"))
	float AutoDistanceDefault = 150.f;

	// 자동 계산된 거리의 최소/최대 클램프
	UPROPERTY(EditDefaultsOnly, Category = "상호작용|프리뷰|카메라", meta = (DisplayName = "최소 카메라 거리", ClampMin = "10", ToolTip = "자동 계산 시 이 값 미만으로 내려가지 않음"))
	float AutoDistanceMin = 100.f;

	UPROPERTY(EditDefaultsOnly, Category = "상호작용|프리뷰|카메라", meta = (DisplayName = "최대 카메라 거리", ClampMin = "100", ToolTip = "자동 계산 시 이 값 초과로 올라가지 않음"))
	float AutoDistanceMax = 1000.f;

	// 메시 BoundingSphere 반지름에 곱하는 배율 (클수록 카메라가 멀어짐)
	UPROPERTY(EditDefaultsOnly, Category = "상호작용|프리뷰|카메라", meta = (DisplayName = "거리 배율", ClampMin = "1.0", ClampMax = "10.0", ToolTip = "메시 SphereRadius × 이 값 = 카메라 거리"))
	float AutoDistanceMultiplier = 2.5f;

	// ── RenderTarget ──

	UPROPERTY()
	TObjectPtr<UTextureRenderTarget2D> RenderTarget;

	// ── 내부 함수 ──

	void EnsureRenderTarget();
	float CalculateAutoDistance() const;
};
