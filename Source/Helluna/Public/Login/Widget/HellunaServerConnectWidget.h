#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "HellunaServerConnectWidget.generated.h"

class UEditableTextBox;
class UTextBlock;
class UButton;

/**
 * LoginLevel 전용 위젯
 * IP 입력 및 서버 접속/시작 UI
 */
UCLASS()
class HELLUNA_API UHellunaServerConnectWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;

public:
	UFUNCTION(BlueprintCallable, Category = "ServerConnect")
	void ShowMessage(const FString& Message, bool bIsError);

	UFUNCTION(BlueprintCallable, Category = "ServerConnect")
	void SetLoadingState(bool bLoading);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "ServerConnect")
	FString GetIPAddress() const;

protected:
	UFUNCTION()
	void OnConnectButtonClicked();

protected:
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UEditableTextBox> IPInputTextBox;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> ConnectButton;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> MessageText;
};
