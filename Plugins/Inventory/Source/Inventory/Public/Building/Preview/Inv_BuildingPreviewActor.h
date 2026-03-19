// Gihyeon's Inventory Project
// @author 김기현
//
// ════════════════════════════════════════════════════════════════════════════════
// 건설물 프리뷰 액터 (Building Preview Actor)
// ════════════════════════════════════════════════════════════════════════════════
//
// Inv_WeaponPreviewActor를 기반으로 건설물 3D 프리뷰 전용으로 단순화.
// 부착물(Attachment) 관련 함수 제거, 나머지 구조 동일.
//
// 컴포넌트 구성:
//    AInv_BuildingPreviewActor
//     ├─ USceneComponent (SceneRoot)
//     ├─ UStaticMeshComponent (PreviewMeshComponent) ← ShowOnlyComponent 대상
//     ├─ USpringArmComponent (CameraBoom)
//     │   └─ USceneCaptureComponent2D (SceneCapture)
//     ├─ USpotLightComponent (PreviewLight) ← Key light
//     ├─ UPointLightComponent (FillLight) ← 보조
//     ├─ UPointLightComponent (RimLight) ← 윤곽
//     └─ UStaticMeshComponent (BackdropCube) ← 배경 차단
//
// 네트워크: SetReplicates(false) — 로컬 전용
// ════════════════════════════════════════════════════════════════════════════════

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Inv_BuildingPreviewActor.generated.h"

class UStaticMeshComponent;
class USpringArmComponent;
class USceneCaptureComponent2D;
class USpotLightComponent;
class UPointLightComponent;
class UTextureRenderTarget2D;
class UStaticMesh;

UCLASS()
class INVENTORY_API AInv_BuildingPreviewActor : public AActor
{
	GENERATED_BODY()

public:
	AInv_BuildingPreviewActor();

	// 프리뷰할 건설물 메시 설정 및 초기 캡처
	void SetPreviewMesh(UStaticMesh* InMesh, const FRotator& RotationOffset, float CameraDistance);

	// 마우스 드래그에 의한 회전
	void RotatePreview(float YawDelta, float PitchDelta = 0.f);

	// 누적 Pitch (상하 회전 제한용)
	float AccumulatedPitch = 0.f;

	// Pitch 제한 각도
	UPROPERTY(EditDefaultsOnly, Category = "건설|프리뷰|카메라", meta = (DisplayName = "상하 회전 제한 각도", ClampMin = "0", ClampMax = "90"))
	float MaxPitchAngle = 60.f;

	// RenderTarget 접근 (UMG Image에 연결용)
	UTextureRenderTarget2D* GetRenderTarget() const;

	// 즉시 캡처 요청
	void CaptureNow();

protected:
	// ── 컴포넌트 (BP에서 위치/회전 조정 가능) ──

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "건설|프리뷰", meta = (DisplayName = "씬 루트"))
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "건설|프리뷰", meta = (DisplayName = "프리뷰 메시"))
	TObjectPtr<UStaticMeshComponent> PreviewMeshComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "건설|프리뷰|카메라", meta = (DisplayName = "카메라 붐"))
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "건설|프리뷰|카메라", meta = (DisplayName = "씬 캡처"))
	TObjectPtr<USceneCaptureComponent2D> SceneCapture;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "건설|프리뷰|조명", meta = (DisplayName = "프리뷰 조명 (Key)"))
	TObjectPtr<USpotLightComponent> PreviewLight;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "건설|프리뷰|조명", meta = (DisplayName = "보조 조명 (Fill)"))
	TObjectPtr<UPointLightComponent> FillLight;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "건설|프리뷰|조명", meta = (DisplayName = "림 조명 (Rim)"))
	TObjectPtr<UPointLightComponent> RimLight;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "건설|프리뷰", meta = (DisplayName = "배경 차단 큐브"))
	TObjectPtr<UStaticMeshComponent> BackdropCube;

	// ── 카메라 붐 설정 (BP에서 조정 가능) ──

	// 카메라 붐 회전 (프리뷰 카메라 각도)
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "건설|프리뷰|카메라",
		meta = (DisplayName = "카메라 붐 회전", Tooltip = "SpringArm의 회전. Pitch로 위/아래 각도, Yaw로 좌/우 회전 조정."))
	FRotator CameraBoomRotation = FRotator(-20.f, 0.f, 0.f);

	// 카메라 붐 소켓 오프셋 (프리뷰 메시 중심에서 카메라 타겟 이동)
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "건설|프리뷰|카메라",
		meta = (DisplayName = "카메라 타겟 오프셋", Tooltip = "SpringArm의 SocketOffset. 프리뷰 중심점을 미세 조정."))
	FVector CameraTargetOffset = FVector::ZeroVector;

	// ── BP 설정값 ──

	UPROPERTY(EditDefaultsOnly, Category = "건설|프리뷰|렌더", meta = (DisplayName = "렌더 가로 해상도", ClampMin = "128", ClampMax = "2048"))
	int32 RenderTargetWidth = 512;

	UPROPERTY(EditDefaultsOnly, Category = "건설|프리뷰|렌더", meta = (DisplayName = "렌더 세로 해상도", ClampMin = "128", ClampMax = "2048"))
	int32 RenderTargetHeight = 288;

	UPROPERTY(EditDefaultsOnly, Category = "건설|프리뷰|카메라", meta = (DisplayName = "자동 카메라 거리 계산"))
	bool bAutoCalculateDistance = true;

	UPROPERTY(EditDefaultsOnly, Category = "건설|프리뷰|카메라", meta = (DisplayName = "기본 카메라 거리", ClampMin = "50"))
	float AutoDistanceDefault = 200.f;

	UPROPERTY(EditDefaultsOnly, Category = "건설|프리뷰|카메라", meta = (DisplayName = "최소 카메라 거리", ClampMin = "10"))
	float AutoDistanceMin = 150.f;

	UPROPERTY(EditDefaultsOnly, Category = "건설|프리뷰|카메라", meta = (DisplayName = "최대 카메라 거리", ClampMin = "100"))
	float AutoDistanceMax = 2000.f;

	UPROPERTY(EditDefaultsOnly, Category = "건설|프리뷰|카메라", meta = (DisplayName = "거리 배율", ClampMin = "1.0", ClampMax = "10.0"))
	float AutoDistanceMultiplier = 3.0f;

	// ── RenderTarget ──

	UPROPERTY()
	TObjectPtr<UTextureRenderTarget2D> RenderTarget;

	// ── 내부 함수 ──

	void EnsureRenderTarget();
	float CalculateAutoDistance() const;
};
