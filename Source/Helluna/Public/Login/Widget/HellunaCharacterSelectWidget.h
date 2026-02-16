#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "HellunaCharacterSelectWidget.generated.h"

class UButton;
class UTextBlock;
class UMaterialInterface;

/**
 * ============================================
 * UHellunaCharacterSelectWidget (베이스 클래스)
 * ============================================
 *
 * 캐릭터 선택 UI 위젯의 공통 로직을 담당하는 베이스 클래스
 * 프리뷰 시스템(V1/V2)은 서브클래스에서 구현
 *
 * ============================================
 * 상속 구조:
 * ============================================
 * UHellunaCharacterSelectWidget (베이스 - 공통 로직)
 *   UHellunaCharSelectWidget_V1 (V1: 캐릭터별 1:1 프리뷰)
 *   UHellunaCharSelectWidget_V2 (V2: 3캐릭터 1카메라 통합 프리뷰)
 *
 * ============================================
 * 사용 흐름:
 * ============================================
 * 1. LoginWidget에서 ShowCharacterSelection() 호출
 * 2. CharacterSelectWidgetClass에 따라 V1/V2 위젯 생성
 * 3. SetAvailableCharacters()로 선택 가능 여부 설정
 * 4. LoginController에서 Cast<V1/V2>하여 프리뷰 초기화
 * 5. 버튼 호버 → OnCharacterHovered() (서브클래스 virtual)
 * 6. 버튼 클릭 → Server_SelectCharacter() RPC
 *
 * ============================================
 * BP 설정 필수 항목:
 * ============================================
 * - LuiButton, LunaButton, LiamButton: 캐릭터 선택 버튼 (BindWidget)
 * - MessageText: 상태 메시지 (BindWidgetOptional)
 * - PreviewCaptureMaterial: 프리뷰 캡처용 Material (EditDefaultsOnly)
 *
 * 작성자: Gihyeon
 */
UCLASS()
class HELLUNA_API UHellunaCharacterSelectWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;

public:
	// ============================================
	// 📌 외부 호출 함수 (공통)
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

	/** 로딩 상태 설정 (모든 버튼 비활성화) */
	UFUNCTION(BlueprintCallable, Category = "Character Select (캐릭터 선택)")
	void SetLoadingState(bool bLoading);

	/** 캐릭터 선택 결과 처리 (LoginController에서 호출) */
	UFUNCTION(BlueprintCallable, Category = "Character Select (캐릭터 선택)")
	void OnSelectionResult(bool bSuccess, const FString& ErrorMessage);

	// ============================================
	// 📌 프리뷰 virtual 인터페이스 (서브클래스에서 구현)
	// ============================================

	/** 프리뷰 정리 — 서브클래스에서 override */
	virtual void CleanupPreview() {}

protected:
	/** 캐릭터 호버 이벤트 — 서브클래스에서 override */
	virtual void OnCharacterHovered(int32 Index, bool bHovered) {}

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

	/** GameState 델리게이트 핸들러 — 다른 플레이어 캐릭터 선택 시 UI 갱신 */
	UFUNCTION()
	void OnCharacterAvailabilityChanged();

	/** GameState에서 현재 사용 가능한 캐릭터 목록 가져와서 UI 갱신 */
	void RefreshAvailableCharacters();

	// ============================================
	// 📌 프리뷰 Hover 이벤트 핸들러 (OnCharacterHovered 호출)
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
	// 📌 프리뷰 공통 설정
	// ============================================

	/** 프리뷰 캡처용 Material (BP에서 반드시 세팅! nullptr이면 프리뷰 표시 불가) */
	UPROPERTY(EditDefaultsOnly, Category = "CharacterPreview (캐릭터 프리뷰)", meta = (DisplayName = "프리뷰 캡처 머티리얼"))
	TObjectPtr<UMaterialInterface> PreviewCaptureMaterial;

	// ============================================
	// 📌 내부 상태
	// ============================================

	/** 각 캐릭터 선택 가능 여부 캐싱 */
	UPROPERTY()
	TArray<bool> CachedAvailableCharacters;

	/** 로딩 중 여부 */
	bool bIsLoading = false;
};
