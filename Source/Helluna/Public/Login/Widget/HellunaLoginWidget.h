#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "HellunaLoginWidget.generated.h"

class UEditableTextBox;
class UTextBlock;
class UButton;
class UHellunaCharacterSelectWidget;

/**
 * ============================================
 * 📌 HellunaLoginWidget
 * ============================================
 * 
 * 로그인 UI 위젯
 * ID/PW 입력 필드와 로그인 버튼을 포함
 * 
 * ============================================
 * 📌 역할:
 * ============================================
 * 1. 사용자 입력 받기 (ID, 비밀번호)
 * 2. 로그인 버튼 클릭 이벤트 처리
 * 3. 로그인 결과 메시지 표시
 * 4. 로딩 상태 관리 (버튼 비활성화)
 * 5. 로그인 성공 시 캐릭터 선택 UI로 전환
 * 
 * ============================================
 * 📌 필수 바인딩 (BP에서 설정):
 * ============================================
 * - IDInputTextBox : 아이디 입력 필드 (EditableTextBox)
 * - PasswordInputTextBox : 비밀번호 입력 필드 (EditableTextBox)
 * - LoginButton : 로그인 버튼 (Button)
 * - MessageText : 결과 메시지 텍스트 (TextBlock)
 * 
 * ============================================
 * 📌 BP에서 설정 필수 (캐릭터 선택):
 * ============================================
 * - CharacterSelectWidgetClass : 캐릭터 선택 위젯 클래스 지정
 * 
 * ============================================
 * 📌 사용 흐름:
 * ============================================
 * 
 * [위젯 생성]
 * LoginController::ShowLoginWidget()
 *   └─ CreateWidget<UHellunaLoginWidget>()
 * 
 * [로그인 버튼 클릭]
 * OnLoginButtonClicked()
 *   ├─ GetPlayerId(), GetPassword() 로 입력값 가져옴
 *   ├─ 유효성 검사 (빈 값 체크)
 *   └─ LoginController->OnLoginButtonClicked(PlayerId, Password)
 * 
 * [로그인 성공]
 * ShowCharacterSelection()
 *   ├─ 로그인 UI 숨김
 *   ├─ CharacterSelectWidgetClass로 새 위젯 생성
 *   └─ SetAvailableCharacters() 호출
 * 
 * 📌 작성자: Gihyeon
 */
UCLASS()
class HELLUNA_API UHellunaLoginWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;

public:
	// ============================================
	// 📌 외부 호출 함수 (LoginController에서 호출)
	// ============================================
	
	/**
	 * 메시지 표시
	 * 
	 * @param Message - 표시할 메시지
	 * @param bIsError - true면 빨간색, false면 흰색
	 */
	UFUNCTION(BlueprintCallable, Category = "Login")
	void ShowMessage(const FString& Message, bool bIsError);

	/**
	 * 로딩 상태 설정
	 * 
	 * @param bLoading - true면 버튼 비활성화
	 */
	UFUNCTION(BlueprintCallable, Category = "Login")
	void SetLoadingState(bool bLoading);

	/** 입력된 아이디 반환 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Login")
	FString GetPlayerId() const;

	/** 입력된 비밀번호 반환 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Login")
	FString GetPassword() const;

	// ============================================
	// 🎭 캐릭터 선택 시스템 (Phase 3)
	// ============================================
	
	/**
	 * 캐릭터 선택 UI 표시
	 * 로그인 성공 후 서버에서 Client_ShowCharacterSelectUI RPC로 호출됨
	 * CharacterSelectWidgetClass로 새 위젯을 생성하여 표시
	 * 
	 * @param AvailableCharacters - 각 캐릭터의 선택 가능 여부
	 *                              Index 0: Lui, 1: Luna, 2: Liam
	 *                              true: 선택 가능, false: 다른 플레이어가 사용 중
	 */
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Login|Character Select (캐릭터 선택)")
	void ShowCharacterSelection(const TArray<bool>& AvailableCharacters);

	/**
	 * 현재 활성화된 캐릭터 선택 위젯 반환
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Login|Character Select (캐릭터 선택)")
	UHellunaCharacterSelectWidget* GetCharacterSelectWidget() const { return CharacterSelectWidget; }

protected:
	// ============================================
	// 📌 내부 이벤트 핸들러
	// ============================================
	
	/** 로그인 버튼 클릭 시 호출 */
	UFUNCTION()
	void OnLoginButtonClicked();

protected:
	// ============================================
	// 📌 UI 바인딩 (BP에서 동일한 이름으로 설정 필수!)
	// ============================================
	
	/** 아이디 입력 필드 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UEditableTextBox> IDInputTextBox;

	/** 비밀번호 입력 필드 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UEditableTextBox> PasswordInputTextBox;

	/** 로그인 버튼 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> LoginButton;

	/** 결과 메시지 텍스트 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> MessageText;

	// ============================================
	// 🎭 캐릭터 선택 위젯 설정
	// ============================================

	/**
	 * 캐릭터 선택 위젯 클래스 (BP에서 설정!)
	 * 로그인 성공 후 이 위젯이 생성됨
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Login|Character Select (캐릭터 선택)", meta = (DisplayName = "캐릭터 선택 위젯 클래스"))
	TSubclassOf<UHellunaCharacterSelectWidget> CharacterSelectWidgetClass;

	/** 현재 활성화된 캐릭터 선택 위젯 */
	UPROPERTY()
	TObjectPtr<UHellunaCharacterSelectWidget> CharacterSelectWidget;
};
