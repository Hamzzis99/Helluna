#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "HellunaLoginGameMode.generated.h"

class UHellunaAccountSaveGame;

/**
 * LoginLevel 전용 GameMode
 * IP 입력 및 서버 시작/접속만 담당
 */
UCLASS()
class HELLUNA_API AHellunaLoginGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AHellunaLoginGameMode();

protected:
	virtual void BeginPlay() override;
	virtual void PostLogin(APlayerController* NewPlayer) override;

public:
	/** 호스트가 "시작" 버튼 클릭 시 호출 */
	UFUNCTION(BlueprintCallable, Category = "Login")
	void TravelToGameMap();

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Login")
	bool IsPlayerLoggedIn(const FString& PlayerId) const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Login")
	UHellunaAccountSaveGame* GetAccountSaveGame() const { return AccountSaveGame; }

protected:
	UPROPERTY(EditDefaultsOnly, Category = "Login", meta = (DisplayName = "게임 맵"))
	TSoftObjectPtr<UWorld> GameMap;

	UPROPERTY()
	TObjectPtr<UHellunaAccountSaveGame> AccountSaveGame;
};
