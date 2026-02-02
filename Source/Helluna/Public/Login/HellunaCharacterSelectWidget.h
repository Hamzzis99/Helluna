#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "HellunaCharacterSelectWidget.generated.h"

class UButton;
class UTextBlock;

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
	UFUNCTION(BlueprintCallable, Category = "CharacterSelect")
	void SetAvailableCharacters(const TArray<bool>& AvailableCharacters);

	/**
	 * 메시지 표시
	 * @param Message - 표시할 메시지
	 * @param bIsError - 에러 메시지 여부 (빨간색)
	 */
	UFUNCTION(BlueprintCallable, Category = "CharacterSelect")
	void ShowMessage(const FString& Message, bool bIsError);

	/**
	 * 로딩 상태 설정 (모든 버튼 비활성화)
	 */
	UFUNCTION(BlueprintCallable, Category = "CharacterSelect")
	void SetLoadingState(bool bLoading);

	/**
	 * 캐릭터 선택 결과 처리 (LoginController에서 호출)
	 */
	UFUNCTION(BlueprintCallable, Category = "CharacterSelect")
	void OnSelectionResult(bool bSuccess, const FString& ErrorMessage);

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

protected:
	// ============================================
	// 📌 UI 바인딩 (BP에서 동일한 이름으로 설정!)
	// ============================================

	/** Lui 캐릭터 선택 버튼 (Index 0) - 필수! */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> LuiButton;

	/** Luna 캐릭터 선택 버튼 (Index 1) - 필수! */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> LunaButton;

	/** Liam 캐릭터 선택 버튼 (Index 2) - 필수! */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> LiamButton;

	/** 상태 메시지 텍스트 (선택사항) */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> MessageText;

	// ============================================
	// 📌 내부 상태
	// ============================================

	/** 각 캐릭터 선택 가능 여부 캐싱 */
	UPROPERTY()
	TArray<bool> CachedAvailableCharacters;

	/** 로딩 중 여부 */
	bool bIsLoading = false;
};
