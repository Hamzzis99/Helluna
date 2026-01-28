// HellunaLoginGameMode.h
// 로그인 레벨 전용 GameMode
// 
// ============================================
// 📌 역할 (Phase B):
// - IP 접속 UI 담당 (서버 시작 또는 서버 연결)
// - UI에서 버튼 클릭 시 GihyeonMap으로 이동
// - ※ 로그인 로직은 HellunaDefenseGameMode에서 처리!
// 
// 📌 사용 위치:
// - LoginLevel에서 GameMode Override로 지정
// 
// 📌 접속 흐름 (Phase B):
// [호스트]
// 1. LoginLevel 시작 → IP 입력 UI 표시
// 2. IP 빈칸 + "시작" 버튼 클릭 → TravelToGameMap()
// 3. GihyeonMap으로 ServerTravel
// 4. GihyeonMap에서 로그인 UI 표시
// 
// [클라이언트]
// 1. LoginLevel 시작 → IP 입력 UI 표시
// 2. IP 입력 + "접속" 버튼 클릭 → open IP
// 3. 서버가 GihyeonMap에 있으면 바로 GihyeonMap으로 접속
// 4. GihyeonMap에서 로그인 UI 표시
// 
// 📌 작성자: Gihyeon
// 📌 작성일: 2025-01-23
// 📌 수정일: 2025-01-28 (Phase B)
// ============================================

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "HellunaLoginGameMode.generated.h"

class UHellunaAccountSaveGame;
class AHellunaLoginController;

/**
 * 로그인 레벨 전용 GameMode
 * Phase B: IP 접속만 담당, 로그인은 DefenseGameMode에서 처리
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
	// ============================================
	// 📌 [Phase B] 게임 맵 이동 함수
	// UI에서 호출됨 (호스트가 "서버 시작" 버튼 클릭 시)
	// ============================================

	/**
	 * 게임 맵으로 ServerTravel
	 * 호스트가 "서버 시작" 버튼 클릭 시 호출
	 */
	UFUNCTION(BlueprintCallable, Category = "Login")
	void TravelToGameMap();

	// ============================================
	// 📌 계정 관련 함수
	// ============================================

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Login")
	bool IsPlayerLoggedIn(const FString& PlayerId) const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Login")
	UHellunaAccountSaveGame* GetAccountSaveGame() const { return AccountSaveGame; }

protected:
	// ============================================
	// 📌 설정 (Blueprint에서 변경 가능)
	// ============================================

	/** 
	 * 서버 시작 시 이동할 게임 맵
	 * Blueprint에서 드롭다운으로 선택 가능
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Login|Map", meta = (DisplayName = "게임 맵"))
	TSoftObjectPtr<UWorld> GameMap;

	// ============================================
	// 📌 데이터
	// ============================================

	UPROPERTY()
	TObjectPtr<UHellunaAccountSaveGame> AccountSaveGame;
};
