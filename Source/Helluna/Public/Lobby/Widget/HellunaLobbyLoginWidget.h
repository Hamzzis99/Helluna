// ════════════════════════════════════════════════════════════════════════════════
// HellunaLobbyLoginWidget.h
// ════════════════════════════════════════════════════════════════════════════════
//
// [Phase 13] 로비 전용 로그인/회원가입 위젯
//
// 📌 역할:
//    - 로그인 모드: ID/PW 입력 → Server_RequestLobbyLogin RPC 호출
//    - 회원가입 모드: ID/PW/PW확인 입력 → Server_RequestLobbySignup RPC 호출
//    - 탭 버튼으로 로그인/회원가입 모드 전환
//
// 📌 BP 바인딩 필수:
//    IDInputTextBox, PasswordInputTextBox, ConfirmPasswordInputTextBox,
//    LoginButton, LoginTabButton, SignupTabButton, MessageText
//
// 작성자: Gihyeon (Claude Code 보조)
// ════════════════════════════════════════════════════════════════════════════════

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "HellunaLobbyLoginWidget.generated.h"

class UEditableTextBox;
class UTextBlock;
class UButton;

UCLASS()
class HELLUNA_API UHellunaLobbyLoginWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 에러 메시지 표시 */
	void ShowError(const FString& Message);

	/** 성공 메시지 표시 */
	void ShowSuccess();

	/** 회원가입 결과 처리 (Controller에서 호출) */
	void HandleSignupResult(bool bSuccess, const FString& Message);

	/** 현재 입력된 ID 반환 (자격증명 캐시용) */
	FString GetEnteredPlayerId() const;

	/** 현재 입력된 PW 반환 (자격증명 캐시용) */
	FString GetEnteredPassword() const;

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeDestruct() override;

	/** 로그인/회원가입 버튼 클릭 */
	UFUNCTION()
	void OnLoginButtonClicked();

	/** 로그인 탭 클릭 */
	UFUNCTION()
	void OnLoginTabClicked();

	/** 회원가입 탭 클릭 */
	UFUNCTION()
	void OnSignupTabClicked();

	/** 로그인 모드로 전환 */
	void SwitchToLoginMode();

	/** 회원가입 모드로 전환 */
	void SwitchToSignupMode();

	// ── BP 바인딩 ──

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UEditableTextBox> IDInputTextBox;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UEditableTextBox> PasswordInputTextBox;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UEditableTextBox> ConfirmPasswordInputTextBox;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> LoginButton;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> LoginTabButton;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> SignupTabButton;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> MessageText;

	/** 현재 회원가입 모드 여부 (false = 로그인 모드) */
	bool bIsSignupMode = false;
};
