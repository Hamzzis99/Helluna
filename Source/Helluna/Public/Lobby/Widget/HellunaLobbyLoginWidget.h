// ════════════════════════════════════════════════════════════════════════════════
// HellunaLobbyLoginWidget.h
// ════════════════════════════════════════════════════════════════════════════════
//
// [Phase 13] 로비 전용 로그인 위젯
//
// 📌 역할:
//    - ID/PW 입력 → Server_RequestLobbyLogin RPC 호출
//    - 로그인 결과 메시지 표시 (성공/실패)
//
// 📌 BP 바인딩 필수:
//    IDInputTextBox, PasswordInputTextBox, LoginButton, MessageText
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

protected:
	virtual void NativeOnInitialized() override;

	/** 로그인 버튼 클릭 */
	UFUNCTION()
	void OnLoginButtonClicked();

	// ── BP 바인딩 ──

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UEditableTextBox> IDInputTextBox;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UEditableTextBox> PasswordInputTextBox;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> LoginButton;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> MessageText;
};
