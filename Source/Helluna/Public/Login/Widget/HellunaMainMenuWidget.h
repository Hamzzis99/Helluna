#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "HellunaMainMenuWidget.generated.h"

class UButton;

/**
 * LoginLevel 첫 화면 메인 메뉴 위젯 (_00)
 * - GAME START 클릭 → 서버접속창(WBP_ServerConnectWidget) 표시
 * - QUIT 클릭 → 게임 종료
 *
 * 필수 바인딩 (BP에서 동일 이름):
 *   - GameStartButton (Button)
 *   - QuitButton (Button)
 */
UCLASS()
class HELLUNA_API UHellunaMainMenuWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	/** GAME START 클릭 → 서버접속창 표시 + 메뉴 숨김 */
	UFUNCTION()
	void OnGameStartClicked();

	/** QUIT 클릭 → 게임 종료 */
	UFUNCTION()
	void OnQuitClicked();

protected:
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> GameStartButton;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> QuitButton;
};
