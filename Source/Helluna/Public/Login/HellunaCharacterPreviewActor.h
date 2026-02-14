#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HellunaCharacterPreviewActor.generated.h"

class USkeletalMeshComponent;
class USceneCaptureComponent2D;
class UPointLightComponent;
class UTextureRenderTarget2D;
class USkeletalMesh;
class UHellunaPreviewAnimInstance;

/**
 * ============================================
 * 📌 HellunaCharacterPreviewActor
 * ============================================
 *
 * 캐릭터 선택 화면에서 3D 프리뷰를 표시하기 위한 클라이언트 전용 액터
 *
 * ============================================
 * 📌 역할:
 * ============================================
 * 1. SkeletalMesh로 캐릭터 3D 모델 표시
 * 2. SceneCaptureComponent2D로 RenderTarget에 캡처
 * 3. PointLight로 프리뷰 조명 제공
 * 4. Hover 상태를 AnimInstance에 전달하여 애니메이션 전환
 *
 * ============================================
 * 📌 라이프사이클:
 * ============================================
 * - LoginController::SpawnPreviewActors()에서 클라이언트 측 동적 스폰
 * - SetReplicates(false) → 서버에 존재하지 않음
 * - DestroyPreviewActors()에서 파괴
 *
 * ============================================
 * 📌 컴포넌트 계층:
 * ============================================
 * ├─ USceneComponent* SceneRoot (RootComponent)
 * ├─ USkeletalMeshComponent* PreviewMesh
 * ├─ USceneCaptureComponent2D* SceneCapture
 * └─ UPointLightComponent* PreviewLight
 *
 * ============================================
 * 📌 BP 설정 항목:
 * ============================================
 * - CaptureOffset: SceneCapture 위치 오프셋
 * - CaptureRotation: SceneCapture 회전
 * - CaptureFOVAngle: SceneCapture 시야각
 *
 * 📌 작성자: Gihyeon
 */
UCLASS()
class HELLUNA_API AHellunaCharacterPreviewActor : public AActor
{
	GENERATED_BODY()

public:
	AHellunaCharacterPreviewActor();

	// ============================================
	// 📌 공개 함수
	// ============================================

	/**
	 * 프리뷰 초기화
	 *
	 * @param InMesh - 표시할 SkeletalMesh
	 * @param InAnimClass - 프리뷰 전용 AnimInstance 클래스
	 * @param InRenderTarget - 캡처 결과를 저장할 RenderTarget
	 */
	UFUNCTION(BlueprintCallable, Category = "Preview (프리뷰)")
	void InitializePreview(USkeletalMesh* InMesh, TSubclassOf<UAnimInstance> InAnimClass, UTextureRenderTarget2D* InRenderTarget);

	/**
	 * Hover 상태 설정
	 * AnimInstance의 bIsHovered를 변경하여 State Machine 트랜지션 유발
	 *
	 * @param bHovered - Hover 여부
	 */
	UFUNCTION(BlueprintCallable, Category = "Preview (프리뷰)")
	void SetHovered(bool bHovered);

	/** 현재 RenderTarget 반환 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Preview (프리뷰)")
	UTextureRenderTarget2D* GetRenderTarget() const;

	/** SceneCapture의 ShowOnlyActors에 액터 추가 */
	UFUNCTION(BlueprintCallable, Category = "Preview (프리뷰)")
	void AddShowOnlyActor(AActor* InActor);

protected:
	// ============================================
	// 📌 컴포넌트
	// ============================================

	/** 루트 씬 컴포넌트 */
	UPROPERTY(VisibleAnywhere, Category = "Preview (프리뷰)")
	TObjectPtr<USceneComponent> SceneRoot;

	/** 캐릭터 SkeletalMesh */
	UPROPERTY(VisibleAnywhere, Category = "Preview (프리뷰)")
	TObjectPtr<USkeletalMeshComponent> PreviewMesh;

	/** 씬 캡처 컴포넌트 */
	UPROPERTY(VisibleAnywhere, Category = "Preview (프리뷰)")
	TObjectPtr<USceneCaptureComponent2D> SceneCapture;

	/** 프리뷰 조명 */
	UPROPERTY(VisibleAnywhere, Category = "Preview (프리뷰)")
	TObjectPtr<UPointLightComponent> PreviewLight;

	// ============================================
	// 📌 BP 조절 가능 파라미터
	// ============================================

	/** SceneCapture 위치 오프셋 (PreviewMesh 기준) */
	UPROPERTY(EditDefaultsOnly, Category = "Preview (프리뷰)", meta = (DisplayName = "캡처 위치 오프셋"))
	FVector CaptureOffset = FVector(0.f, 70.f, 85.f);

	/** SceneCapture 회전 */
	UPROPERTY(EditDefaultsOnly, Category = "Preview (프리뷰)", meta = (DisplayName = "캡처 회전"))
	FRotator CaptureRotation = FRotator(-10.f, 180.f, 0.f);

	/** SceneCapture 시야각 */
	UPROPERTY(EditDefaultsOnly, Category = "Preview (프리뷰)", meta = (DisplayName = "캡처 FOV"))
	float CaptureFOVAngle = 30.f;
};
