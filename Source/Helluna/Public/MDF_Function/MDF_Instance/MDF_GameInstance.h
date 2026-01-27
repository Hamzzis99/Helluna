#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "MDF_GameInstance.generated.h"

UCLASS()
class HELLUNA_API UMDF_GameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	// ============================================
	// 📌 기존 변수
	// ============================================
	
	// 이 변수가 true면 "맵 이동 중", false면 "새 게임/재시작"
	UPROPERTY(BlueprintReadWrite, Category = "Game Flow")
	bool bIsMapTransitioning = false;

	// ============================================
	// 📌 로그인 시스템 (Seamless Travel에서도 유지)
	// ============================================

	/** 현재 접속 중인 플레이어 ID 목록 */
	UPROPERTY(BlueprintReadOnly, Category = "Login", meta = (DisplayName = "접속 중인 플레이어 목록"))
	TSet<FString> LoggedInPlayerIds;

	/** 플레이어 로그인 등록 */
	UFUNCTION(BlueprintCallable, Category = "Login")
	void RegisterLogin(const FString& PlayerId);

	/** 플레이어 로그아웃 처리 */
	UFUNCTION(BlueprintCallable, Category = "Login")
	void RegisterLogout(const FString& PlayerId);

	/** 동시 접속 체크 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Login")
	bool IsPlayerLoggedIn(const FString& PlayerId) const;

	/** 접속 중인 플레이어 수 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Login")
	int32 GetLoggedInPlayerCount() const;
};