// HellunaLoginWidget.h
// 로그인 UI 위젯 (C++ 베이스 클래스)
// 
// ============================================
// 📌 역할:
// - IP 입력 + 서버 접속 버튼
// - 아이디/비밀번호 입력 + 로그인 버튼
// - 상태 메시지 표시
// - 순차 표시 (IP 입력 → 로그인)
// 
// 📌 사용 방법:
// - 이 클래스를 상속받아 WBP_LoginWidget 블루프린트 생성
// - Blueprint에서 동일한 이름의 위젯을 배치해야 함!
//   (BindWidget으로 지정된 것들은 필수)
// 
// 📌 작성자: Gihyeon
// 📌 작성일: 2025-01-23
// ============================================

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "HellunaLoginWidget.generated.h"

class UEditableTextBox;
class UTextBlock;
class UButton;
class UCanvasPanel;

/**
 * 로그인 UI 위젯 베이스 클래스
 * Blueprint에서 상속받아 UI 디자인
 */
UCLASS()
class HELLUNA_API UHellunaLoginWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// ============================================
	// 📌 초기화
	// ============================================
	virtual void NativeConstruct() override;

protected:
	// ============================================
	// 📌 [1단계] 서버 접속 UI (BindWidget = 필수!)
	// Blueprint에서 동일한 이름으로 위젯 배치 필요
	// ============================================

	/** 서버 접속 패널 (IP 입력 영역) */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget, DisplayName = "서버 접속 패널"), Category = "Login|UI")
	TObjectPtr<UCanvasPanel> ServerConnectPanel;

	/** IP 입력 텍스트박스 */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget, DisplayName = "IP 입력창"), Category = "Login|UI")
	TObjectPtr<UEditableTextBox> IPInputTextBox;

	/** 서버 접속 버튼 */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget, DisplayName = "서버 접속 버튼"), Category = "Login|UI")
	TObjectPtr<UButton> ConnectButton;

	// ============================================
	// 📌 [2단계] 로그인 UI (BindWidget = 필수!)
	// ============================================

	/** 로그인 패널 (ID/PW 입력 영역) */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget, DisplayName = "로그인 패널"), Category = "Login|UI")
	TObjectPtr<UCanvasPanel> LoginPanel;

	/** 아이디 입력 텍스트박스 */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget, DisplayName = "아이디 입력창"), Category = "Login|UI")
	TObjectPtr<UEditableTextBox> IDInputTextBox;

	/** 비밀번호 입력 텍스트박스 */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget, DisplayName = "비밀번호 입력창"), Category = "Login|UI")
	TObjectPtr<UEditableTextBox> PasswordInputTextBox;

	/** 로그인 버튼 */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget, DisplayName = "로그인 버튼"), Category = "Login|UI")
	TObjectPtr<UButton> LoginButton;

	// ============================================
	// 📌 공통 UI (BindWidget = 필수!)
	// ============================================

	/** 상태 메시지 텍스트 */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget, DisplayName = "메시지 텍스트"), Category = "Login|UI")
	TObjectPtr<UTextBlock> MessageText;

	// ============================================
	// 📌 버튼 클릭 이벤트 핸들러
	// ============================================

	/** 서버 접속 버튼 클릭 */
	UFUNCTION()
	void OnConnectButtonClicked();

	/** 로그인 버튼 클릭 */
	UFUNCTION()
	void OnLoginButtonClicked();

public:
	// ============================================
	// 📌 UI 상태 전환 함수
	// ============================================

	/**
	 * 메시지 표시
	 * @param Message - 표시할 메시지
	 * @param bIsError - true면 빨간색, false면 초록색
	 */
	UFUNCTION(BlueprintCallable, Category = "Login|UI")
	void ShowMessage(const FString& Message, bool bIsError = false);

	/**
	 * 로그인 패널 표시 (서버 접속 성공 후)
	 */
	UFUNCTION(BlueprintCallable, Category = "Login|UI")
	void ShowLoginPanel();

	/**
	 * 서버 접속 패널 표시 (초기 상태)
	 */
	UFUNCTION(BlueprintCallable, Category = "Login|UI")
	void ShowServerConnectPanel();

	/**
	 * 로딩 상태 설정 (버튼 비활성화 등)
	 * @param bLoading - true면 로딩 중
	 */
	UFUNCTION(BlueprintCallable, Category = "Login|UI")
	void SetLoadingState(bool bLoading);

	/**
	 * 입력된 IP 주소 반환
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Login|UI")
	FString GetIPAddress() const;

	/**
	 * 입력된 아이디 반환
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Login|UI")
	FString GetPlayerId() const;

	/**
	 * 입력된 비밀번호 반환
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Login|UI")
	FString GetPassword() const;
};
