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
// - Blueprint에서 UI 디자인 및 바인딩
// 
// 📌 작성자: Claude & Gihyeon
// 📌 작성일: 2025-01-23
// ============================================

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "HellunaLoginWidget.generated.h"

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
	// 📌 UI 상태 전환 함수 (Blueprint에서 호출 가능)
	// ============================================

	/**
	 * 에러 메시지 표시
	 * @param Message - 표시할 메시지
	 */
	UFUNCTION(BlueprintCallable, BlueprintImplementableEvent, Category = "Login|UI")
	void ShowErrorMessage(const FString& Message);

	/**
	 * 성공 메시지 표시
	 * @param Message - 표시할 메시지
	 */
	UFUNCTION(BlueprintCallable, BlueprintImplementableEvent, Category = "Login|UI")
	void ShowSuccessMessage(const FString& Message);

	/**
	 * 로그인 UI 표시 (IP 입력 완료 후)
	 */
	UFUNCTION(BlueprintCallable, BlueprintImplementableEvent, Category = "Login|UI")
	void ShowLoginPanel();

	/**
	 * 로딩 상태 표시
	 * @param bLoading - true면 로딩 중 표시
	 */
	UFUNCTION(BlueprintCallable, BlueprintImplementableEvent, Category = "Login|UI")
	void SetLoadingState(bool bLoading);

	// ============================================
	// 📌 TODO: Phase 3에서 상세 구현 예정
	// - IP 입력 텍스트박스
	// - 서버 접속 버튼
	// - 아이디/비밀번호 입력
	// - 로그인 버튼
	// - 메시지 영역
	// ============================================
};
