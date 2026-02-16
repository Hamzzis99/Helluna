#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HellunaCharacterSelectSceneV2.generated.h"

class USkeletalMeshComponent;
class USceneCaptureComponent2D;
class UPointLightComponent;
class UTextureRenderTarget2D;
class USkeletalMesh;
class UHellunaPreviewAnimInstance;

/**
 * ============================================
 * AHellunaCharacterSelectSceneV2
 * ============================================
 *
 * V2 캐릭터 선택 프리뷰 씬 액터
 * 3캐릭터를 한 공간에 배치 + 카메라 1개 + RenderTarget 1개
 *
 * V1(HellunaCharacterPreviewActor)은 캐릭터별 1:1 구조였으나,
 * V2는 하나의 액터 안에서 모든 캐릭터를 관리한다.
 *
 * ============================================
 * 컴포넌트 계층:
 * ============================================
 * SceneRoot (Root)
 *   PreviewMeshes[] (동적 생성)
 *   SceneCapture (카메라 1개)
 *   MainLight
 *   FillLight
 *
 * 작성자: Gihyeon
 */
UCLASS()
class HELLUNA_API AHellunaCharacterSelectSceneV2 : public AActor
{
	GENERATED_BODY()

public:
	AHellunaCharacterSelectSceneV2();

	// ============================================
	// 공개 함수
	// ============================================

	/**
	 * 씬 초기화 - 캐릭터 메시/애님/RT 설정
	 *
	 * @param InMeshes - 캐릭터별 SkeletalMesh 배열 (Lui/Luna/Liam 순서)
	 * @param InAnimClasses - 캐릭터별 AnimInstance 클래스 배열
	 * @param InRenderTarget - 공유 RenderTarget (1개)
	 */
	UFUNCTION(BlueprintCallable, Category = "프리뷰V2")
	void InitializeScene(
		const TArray<USkeletalMesh*>& InMeshes,
		const TArray<TSubclassOf<UAnimInstance>>& InAnimClasses,
		UTextureRenderTarget2D* InRenderTarget);

	/**
	 * 호버 ON/OFF (오버레이 머티리얼 + AnimBP)
	 *
	 * @param Index - 캐릭터 인덱스 (0=Lui, 1=Luna, 2=Liam)
	 * @param bHovered - 호버 여부
	 */
	UFUNCTION(BlueprintCallable, Category = "프리뷰V2")
	void SetCharacterHovered(int32 Index, bool bHovered);

	/** RenderTarget 반환 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "프리뷰V2")
	UTextureRenderTarget2D* GetRenderTarget() const;

	/** 캐릭터 수 반환 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "프리뷰V2")
	int32 GetCharacterCount() const;

protected:
	// ============================================
	// 컴포넌트
	// ============================================

	UPROPERTY(VisibleAnywhere, Category = "프리뷰V2")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, Category = "프리뷰V2")
	TObjectPtr<USceneCaptureComponent2D> SceneCapture;

	UPROPERTY(VisibleAnywhere, Category = "프리뷰V2")
	TObjectPtr<UPointLightComponent> MainLight;

	UPROPERTY(VisibleAnywhere, Category = "프리뷰V2")
	TObjectPtr<UPointLightComponent> FillLight;

	// ============================================
	// 동적 생성 메시 배열
	// ============================================

	UPROPERTY()
	TArray<TObjectPtr<USkeletalMeshComponent>> PreviewMeshes;

	// ============================================
	// BP 설정 UPROPERTY
	// ============================================

	/** 캐릭터 간 X축 간격 */
	UPROPERTY(EditDefaultsOnly, Category = "프리뷰V2", meta = (DisplayName = "캐릭터 간격"))
	float CharacterSpacing = 200.f;

	/** 카메라 위치 (씬 중앙 기준) */
	UPROPERTY(EditDefaultsOnly, Category = "프리뷰V2", meta = (DisplayName = "카메라 오프셋"))
	FVector CameraOffset = FVector(500.f, 0.f, 90.f);

	/** 카메라 회전 */
	UPROPERTY(EditDefaultsOnly, Category = "프리뷰V2", meta = (DisplayName = "카메라 회전"))
	FRotator CameraRotation = FRotator(0.f, 180.f, 0.f);

	/** 카메라 FOV */
	UPROPERTY(EditDefaultsOnly, Category = "프리뷰V2", meta = (DisplayName = "카메라 FOV"))
	float CameraFOV = 30.f;

	/** 캐릭터별 오버레이 하이라이트 머티리얼 (Lui/Luna/Liam 순서) */
	UPROPERTY(EditDefaultsOnly, Category = "프리뷰V2", meta = (DisplayName = "하이라이트 머티리얼"))
	TArray<UMaterialInterface*> HighlightMaterials;
};
