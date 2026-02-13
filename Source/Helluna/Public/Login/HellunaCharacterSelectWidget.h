#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "HellunaCharacterSelectWidget.generated.h"

class UButton;
class UTextBlock;
class UImage;
class AHellunaCharacterPreviewActor;
class UTextureRenderTarget2D;
class UMaterialInstanceDynamic;
class UMaterialInterface;

/**
 * ============================================
 * 🎭 HellunaCharacterSelectWidget
 * ============================================
 * 
 * 캐릭터 선택 UI 위젯
 * 로그인 성공 후 표시되어 플레이어가 캐릭터를 선택할 수 있게 함
 * 
 * ============================================
 * 📌 사용 흐름:
 * ============================================
 * 
 * 1. LoginWidget에서 ShowCharacterSelection() 호출
 * 2. 이 위젯 생성 및 표시
 * 3. SetAvailableCharacters()로 선택 가능 여부 설정
 * 4. 플레이어가 버튼 클릭
 * 5. OnCharacterButtonClicked() → LoginController::Server_SelectCharacter()
 * 
 * ============================================
 * 📌 BP 설정 필수 항목:
 * ============================================
 * - LuiButton, LunaButton, LiamButton: 캐릭터 선택 버튼
 * - MessageText: 상태 메시지 (선택사항)
 * 
 * 📌 작성자: Gihyeon
 */
UCLASS()
class HELLUNA_API UHellunaCharacterSelectWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;

public:
	// ============================================
	// 📌 외부 호출 함수
	// ============================================

	/**
	 * 선택 가능한 캐릭터 설정
	 * 서버에서 받은 AvailableCharacters 배열로 버튼 활성화/비활성화
	 * 
	 * @param AvailableCharacters - [0]=Lui, [1]=Luna, [2]=Liam 선택 가능 여부
	 */
	UFUNCTION(BlueprintCallable, Category = "Character Select (캐릭터 선택)")
	void SetAvailableCharacters(const TArray<bool>& AvailableCharacters);

	/**
	 * 메시지 표시
	 * @param Message - 표시할 메시지
	 * @param bIsError - 에러 메시지 여부 (빨간색)
	 */
	UFUNCTION(BlueprintCallable, Category = "Character Select (캐릭터 선택)")
	void ShowMessage(const FString& Message, bool bIsError);

	/**
	 * 로딩 상태 설정 (모든 버튼 비활성화)
	 */
	UFUNCTION(BlueprintCallable, Category = "Character Select (캐릭터 선택)")
	void SetLoadingState(bool bLoading);

	/**
	 * 캐릭터 선택 결과 처리 (LoginController에서 호출)
	 */
	UFUNCTION(BlueprintCallable, Category = "Character Select (캐릭터 선택)")
	void OnSelectionResult(bool bSuccess, const FString& ErrorMessage);

	// ============================================
	// 📌 프리뷰 시스템 공개 함수
	// ============================================

	/**
	 * 프리뷰 이미지 설정
	 * RenderTarget을 MID로 감싸서 UImage에 적용
	 *
	 * @param RenderTargets - Lui(0), Luna(1), Liam(2) 순서의 RenderTarget 배열
	 */
	UFUNCTION(BlueprintCallable, Category = "Character Select (캐릭터 선택)")
	void SetupPreviewImages(const TArray<UTextureRenderTarget2D*>& RenderTargets);

	/**
	 * 프리뷰 액터 배열 설정 및 Hover 델리게이트 바인딩
	 *
	 * @param InPreviewActors - Lui(0), Luna(1), Liam(2) 순서의 프리뷰 액터 배열
	 */
	UFUNCTION(BlueprintCallable, Category = "Character Select (캐릭터 선택)")
	void SetPreviewActors(const TArray<AHellunaCharacterPreviewActor*>& InPreviewActors);

protected:
	// ============================================
	// 📌 내부 이벤트 핸들러
	// ============================================

	UFUNCTION()
	void OnLuiButtonClicked();

	UFUNCTION()
	void OnLunaButtonClicked();

	UFUNCTION()
	void OnLiamButtonClicked();

	/** 캐릭터 선택 처리 (공통) */
	void SelectCharacter(int32 CharacterIndex);

	/** GameState 델리게이트 핸들러 - 다른 플레이어 캐릭터 선택 시 UI 갱신 */
	UFUNCTION()
	void OnCharacterAvailabilityChanged();

	/** GameState에서 현재 사용 가능한 캐릭터 목록 가져와서 UI 갱신 */
	void RefreshAvailableCharacters();

	// ============================================
	// 📌 프리뷰 Hover 이벤트 핸들러
	// ============================================

	UFUNCTION()
	void OnPreviewHovered_Lui();

	UFUNCTION()
	void OnPreviewUnhovered_Lui();

	UFUNCTION()
	void OnPreviewHovered_Luna();

	UFUNCTION()
	void OnPreviewUnhovered_Luna();

	UFUNCTION()
	void OnPreviewHovered_Liam();

	UFUNCTION()
	void OnPreviewUnhovered_Liam();

protected:
	// ============================================
	// 📌 UI 바인딩 (BP에서 동일한 이름으로 설정!)
	// ============================================

	/** Lui 캐릭터 선택 버튼 (Index 0) - 필수! */
	UPROPERTY(meta = (BindWidget, DisplayName = "루이 버튼"))
	TObjectPtr<UButton> LuiButton;

	/** Luna 캐릭터 선택 버튼 (Index 1) - 필수! */
	UPROPERTY(meta = (BindWidget, DisplayName = "루나 버튼"))
	TObjectPtr<UButton> LunaButton;

	/** Liam 캐릭터 선택 버튼 (Index 2) - 필수! */
	UPROPERTY(meta = (BindWidget, DisplayName = "리암 버튼"))
	TObjectPtr<UButton> LiamButton;

	/** 상태 메시지 텍스트 (선택사항) */
	UPROPERTY(meta = (BindWidgetOptional, DisplayName = "메시지 텍스트"))
	TObjectPtr<UTextBlock> MessageText;

	// ============================================
	// 📌 프리뷰 이미지 바인딩 (BP에서 UImage 추가 필요!)
	// ============================================

	/** Lui 프리뷰 이미지 (Index 0) - 선택사항 */
	UPROPERTY(meta = (BindWidgetOptional, DisplayName = "루이 프리뷰 이미지"))
	TObjectPtr<UImage> PreviewImage_Lui;

	/** Luna 프리뷰 이미지 (Index 1) - 선택사항 */
	UPROPERTY(meta = (BindWidgetOptional, DisplayName = "루나 프리뷰 이미지"))
	TObjectPtr<UImage> PreviewImage_Luna;

	/** Liam 프리뷰 이미지 (Index 2) - 선택사항 */
	UPROPERTY(meta = (BindWidgetOptional, DisplayName = "리암 프리뷰 이미지"))
	TObjectPtr<UImage> PreviewImage_Liam;

	// ============================================
	// 📌 프리뷰 설정
	// ============================================

	/** 프리뷰 캡처용 Material (BP에서 반드시 세팅! nullptr이면 프리뷰 표시 불가) */
	UPROPERTY(EditDefaultsOnly, Category = "CharacterPreview (캐릭터 프리뷰)", meta = (DisplayName = "프리뷰 캡처 머티리얼"))
	TObjectPtr<UMaterialInterface> PreviewCaptureMaterial;

	// ============================================
	// 📌 프리뷰 내부 상태
	// ============================================

	/** 프리뷰 액터 참조 (소유권은 LoginController) */
	UPROPERTY()
	TArray<TObjectPtr<AHellunaCharacterPreviewActor>> PreviewActors;

	/** 동적 머티리얼 인스턴스 (GC 방지) */
	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> PreviewMaterials;

	// ============================================
	// 📌 내부 상태
	// ============================================

	/** 각 캐릭터 선택 가능 여부 캐싱 */
	UPROPERTY()
	TArray<bool> CachedAvailableCharacters;

	/** 로딩 중 여부 */
	bool bIsLoading = false;
};
