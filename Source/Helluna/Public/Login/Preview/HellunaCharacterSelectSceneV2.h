#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HellunaCharacterSelectSceneV2.generated.h"

class USkeletalMeshComponent;
class USceneCaptureComponent2D;
class UPointLightComponent;
class USpotLightComponent;
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

	/**
	 * 캐릭터 선택 상태 설정 — 선택된 캐릭터는 앞으로 나오고 밝아짐, 나머지는 뒤로/어둡게
	 *
	 * @param SelectedIndex - 선택된 캐릭터 인덱스 (-1 = 미선택, 모두 원래 위치)
	 */
	UFUNCTION(BlueprintCallable, Category = "프리뷰V2")
	void SetCharacterSelected(int32 SelectedIndex);

	/** RenderTarget 반환 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "프리뷰V2")
	UTextureRenderTarget2D* GetRenderTarget() const;

	/** 캐릭터 수 반환 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "프리뷰V2")
	int32 GetCharacterCount() const;

	// ============================================
	// Solo 모드 (Play 탭: 캐릭터 1개만 표시)
	// ============================================

	/** Solo 모드 — 지정 캐릭터만 표시, 나머지 숨김 (Play 탭용) */
	UFUNCTION(BlueprintCallable, Category = "프리뷰V2|Solo",
		meta = (DisplayName = "Set Solo Character (솔로 캐릭터 설정)"))
	void SetSoloCharacter(int32 CharacterIndex);

	/** Solo 모드 해제 — 전체 캐릭터 표시 복원 (CHARACTER 탭 복귀용) */
	UFUNCTION(BlueprintCallable, Category = "프리뷰V2|Solo",
		meta = (DisplayName = "Clear Solo Mode (솔로 모드 해제)"))
	void ClearSoloMode();

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

	/** 캐릭터 간 X축 간격 (CharacterTransforms 미설정 시 사용) */
	UPROPERTY(EditDefaultsOnly, Category = "프리뷰V2|배치", meta = (DisplayName = "캐릭터 간격 (기본)"))
	float CharacterSpacing = 200.f;

	/**
	 * 캐릭터별 개별 위치 오프셋 (씬 중앙 기준)
	 * 배열 크기가 캐릭터 수와 일치하면 CharacterSpacing 대신 이 값 사용
	 * Index 0=Lui, 1=Luna, 2=Liam
	 */
	UPROPERTY(EditDefaultsOnly, Category = "프리뷰V2|배치", meta = (DisplayName = "캐릭터별 위치 오프셋"))
	TArray<FVector> CharacterOffsets;

	/**
	 * 캐릭터별 개별 회전
	 * 배열 크기가 캐릭터 수와 일치하면 이 값 사용, 아니면 기본 -90도(카메라 정면)
	 * Index 0=Lui, 1=Luna, 2=Liam
	 */
	UPROPERTY(EditDefaultsOnly, Category = "프리뷰V2|배치", meta = (DisplayName = "캐릭터별 회전"))
	TArray<FRotator> CharacterRotations;

	/**
	 * 캐릭터별 스케일
	 * 배열 크기가 캐릭터 수와 일치하면 이 값 사용, 아니면 기본 (1,1,1)
	 * Index 0=Lui, 1=Luna, 2=Liam
	 */
	UPROPERTY(EditDefaultsOnly, Category = "프리뷰V2|배치", meta = (DisplayName = "캐릭터별 스케일"))
	TArray<FVector> CharacterScales;

	/** 카메라 위치 (씬 중앙 기준) */
	UPROPERTY(EditDefaultsOnly, Category = "프리뷰V2|카메라", meta = (DisplayName = "카메라 오프셋"))
	FVector CameraOffset = FVector(500.f, 0.f, 90.f);

	/** 카메라 회전 */
	UPROPERTY(EditDefaultsOnly, Category = "프리뷰V2|카메라", meta = (DisplayName = "카메라 회전"))
	FRotator CameraRotation = FRotator(0.f, 180.f, 0.f);

	/** 카메라 FOV */
	UPROPERTY(EditDefaultsOnly, Category = "프리뷰V2|카메라", meta = (DisplayName = "카메라 FOV"))
	float CameraFOV = 30.f;

	/** 캐릭터별 오버레이 하이라이트 머티리얼 (Lui/Luna/Liam 순서) */
	UPROPERTY(EditDefaultsOnly, Category = "프리뷰V2|하이라이트", meta = (DisplayName = "하이라이트 머티리얼"))
	TArray<UMaterialInterface*> HighlightMaterials;

	// ============================================
	// 선택 연출 설정
	// ============================================

	/** 선택 시 캐릭터가 앞으로 나오는 거리 (Y축) */
	UPROPERTY(EditDefaultsOnly, Category = "프리뷰V2|선택연출", meta = (DisplayName = "선택 시 전진 거리"))
	float SelectedForwardOffset = 50.f;

	/** 비선택 캐릭터 어두움 정도 (0=완전어둡 ~ 1=원래밝기) */
	UPROPERTY(EditDefaultsOnly, Category = "프리뷰V2|선택연출", meta = (DisplayName = "비선택 밝기 비율", ClampMin = "0.0", ClampMax = "1.0"))
	float UnselectedBrightnessRatio = 0.3f;

	/** 캐릭터별 스포트라이트 (InitializeScene에서 동적 생성) */
	UPROPERTY()
	TArray<TObjectPtr<USpotLightComponent>> CharacterSpotLights;

	/** 캐릭터별 원래 위치 (선택 해제 시 복원용) */
	TArray<FVector> OriginalLocations;

	/** 선택된 캐릭터 인덱스 (-1 = 미선택) */
	int32 CurrentSelectedIndex = -1;

	// ============================================
	// Solo 모드 상태
	// ============================================

	/** Solo 모드 활성화 여부 */
	bool bSoloMode = false;

	/** Solo 모드에서 표시 중인 캐릭터 인덱스 */
	int32 SoloCharacterIndex = -1;
};
