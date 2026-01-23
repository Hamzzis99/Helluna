// HellunaLoginController.h
// 로그인 레벨 전용 PlayerController
// 
// ============================================
// 📌 역할:
// - 로그인 UI 위젯 생성 및 표시
// - 서버에 로그인 요청 (Server RPC)
// - 로그인 결과 수신 (Client RPC)
// - UI 상태 전환 (IP 입력 → 로그인)
// 
// 📌 사용 위치:
// - LoginLevel에서만 사용
// - HellunaLoginGameMode에서 PlayerControllerClass로 지정
// 
// 📌 작성자: Claude & Gihyeon
// 📌 작성일: 2025-01-23
// ============================================

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "HellunaLoginController.generated.h"

class UHellunaLoginWidget;

/**
 * 로그인 레벨 전용 PlayerController
 * 로그인 UI 표시 및 서버 통신 담당
 */
UCLASS()
class HELLUNA_API AHellunaLoginController : public APlayerController
{
	GENERATED_BODY()

public:
	AHellunaLoginController();

protected:
	virtual void BeginPlay() override;

public:
	// ============================================
	// 📌 UI 관련
	// ============================================

	/**
	 * 로그인 위젯 생성 및 표시
	 * BeginPlay에서 자동 호출됨
	 */
	UFUNCTION(BlueprintCallable, Category = "Login|UI")
	void ShowLoginWidget();

	/**
	 * 로그인 위젯 숨기기
	 */
	UFUNCTION(BlueprintCallable, Category = "Login|UI")
	void HideLoginWidget();

	// ============================================
	// 📌 서버 통신 (RPC)
	// ============================================

	/**
	 * 서버에 로그인 요청
	 * @param PlayerId - 입력한 아이디
	 * @param Password - 입력한 비밀번호
	 */
	UFUNCTION(Server, Reliable, Category = "Login")
	void Server_RequestLogin(const FString& PlayerId, const FString& Password);

	/**
	 * 로그인 결과 수신 (서버 → 클라이언트)
	 * @param bSuccess - 로그인 성공 여부
	 * @param ErrorMessage - 실패 시 에러 메시지
	 */
	UFUNCTION(Client, Reliable, Category = "Login")
	void Client_LoginResult(bool bSuccess, const FString& ErrorMessage);

	// ============================================
	// 📌 UI에서 호출하는 함수 (Blueprint에서 접근 가능)
	// ============================================

	/**
	 * 로그인 버튼 클릭 시 호출
	 * UI에서 바인딩하여 사용
	 * @param PlayerId - 입력한 아이디
	 * @param Password - 입력한 비밀번호
	 */
	UFUNCTION(BlueprintCallable, Category = "Login")
	void OnLoginButtonClicked(const FString& PlayerId, const FString& Password);

	/**
	 * 로그인 위젯 참조 반환
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Login|UI")
	UHellunaLoginWidget* GetLoginWidget() const { return LoginWidget; }

protected:
	// ============================================
	// 📌 위젯 클래스 (Blueprint에서 설정)
	// ============================================

	/** 로그인 위젯 클래스 (BP에서 설정) */
	UPROPERTY(EditDefaultsOnly, Category = "Login|UI")
	TSubclassOf<UHellunaLoginWidget> LoginWidgetClass;

	/** 현재 생성된 로그인 위젯 인스턴스 */
	UPROPERTY()
	TObjectPtr<UHellunaLoginWidget> LoginWidget;
};
