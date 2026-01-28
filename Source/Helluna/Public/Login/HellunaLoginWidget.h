#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "HellunaLoginWidget.generated.h"

class UEditableTextBox;
class UTextBlock;
class UButton;

/**
 * GihyeonMap 전용 로그인 위젯
 * ID/PW 입력 및 로그인 버튼
 */
UCLASS()
class HELLUNA_API UHellunaLoginWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;

public:
	UFUNCTION(BlueprintCallable, Category = "Login")
	void ShowMessage(const FString& Message, bool bIsError);

	UFUNCTION(BlueprintCallable, Category = "Login")
	void SetLoadingState(bool bLoading);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Login")
	FString GetPlayerId() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Login")
	FString GetPassword() const;

protected:
	UFUNCTION()
	void OnLoginButtonClicked();

protected:
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UEditableTextBox> IDInputTextBox;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UEditableTextBox> PasswordInputTextBox;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> LoginButton;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> MessageText;
};
