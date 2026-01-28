#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "HellunaLoginController.generated.h"

class UHellunaLoginWidget;

/**
 * GihyeonMap 전용 PlayerController
 * 로그인 UI 표시 및 로그인 RPC 처리
 * 로그인 성공 시 GameControllerClass로 교체됨
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
	UFUNCTION(BlueprintCallable, Category = "Login")
	void ShowLoginWidget();

	UFUNCTION(BlueprintCallable, Category = "Login")
	void HideLoginWidget();

	UFUNCTION(BlueprintCallable, Category = "Login")
	void OnLoginButtonClicked(const FString& PlayerId, const FString& Password);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Login")
	UHellunaLoginWidget* GetLoginWidget() const { return LoginWidget; }

	/** 로그인 성공 시 교체할 Controller 클래스 (BP에서 설정) */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Login")
	TSubclassOf<APlayerController> GetGameControllerClass() const { return GameControllerClass; }

	/** 로그인 UI에서 결과 표시용 */
	UFUNCTION(BlueprintCallable, Category = "Login")
	void ShowLoginResult(bool bSuccess, const FString& Message);

	// ============================================
	// RPC (DefenseGameMode에서 호출하므로 public)
	// ============================================

	UFUNCTION(Server, Reliable)
	void Server_RequestLogin(const FString& PlayerId, const FString& Password);

	UFUNCTION(Client, Reliable)
	void Client_LoginResult(bool bSuccess, const FString& ErrorMessage);

	UFUNCTION(Client, Reliable)
	void Client_PrepareControllerSwap();

protected:
	UPROPERTY(EditDefaultsOnly, Category = "Login", meta = (DisplayName = "로그인 위젯 클래스"))
	TSubclassOf<UHellunaLoginWidget> LoginWidgetClass;

	UPROPERTY()
	TObjectPtr<UHellunaLoginWidget> LoginWidget;

	/** 로그인 성공 후 교체할 Controller 클래스 (BP에서 필수 설정!) */
	UPROPERTY(EditDefaultsOnly, Category = "Login", meta = (DisplayName = "게임 Controller 클래스"))
	TSubclassOf<APlayerController> GameControllerClass;
};
