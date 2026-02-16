#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "HellunaServerConnectController.generated.h"

class UHellunaServerConnectWidget;

/**
 * LoginLevel 전용 PlayerController
 * IP 입력 및 서버 접속/시작만 담당
 */
UCLASS()
class HELLUNA_API AHellunaServerConnectController : public APlayerController
{
	GENERATED_BODY()

public:
	AHellunaServerConnectController();

protected:
	virtual void BeginPlay() override;

public:
	UFUNCTION(BlueprintCallable, Category = "ServerConnect")
	void ShowConnectWidget();

	UFUNCTION(BlueprintCallable, Category = "ServerConnect")
	void HideConnectWidget();

	/** IP 빈칸이면 호스트, IP 있으면 클라이언트로 접속 */
	UFUNCTION(BlueprintCallable, Category = "ServerConnect")
	void OnConnectButtonClicked(const FString& IPAddress);

protected:
	UPROPERTY(EditDefaultsOnly, Category = "ServerConnect", meta = (DisplayName = "서버접속 위젯 클래스"))
	TSubclassOf<UHellunaServerConnectWidget> ConnectWidgetClass;

	UPROPERTY()
	TObjectPtr<UHellunaServerConnectWidget> ConnectWidget;
};
