// HellunaLoginGameMode.h
// 로그인 레벨 전용 GameMode
// 
// ============================================
// 📌 역할 (Phase B 변경):
// - IP 접속만 담당 (서버 연결)
// - 서버 접속 성공 시 바로 GihyeonMap으로 이동
// - ※ 로그인 로직은 HellunaDefenseGameMode로 이동됨!
// 
// 📌 사용 위치:
// - LoginLevel에서 GameMode Override로 지정
// 
// 📌 접속 흐름 (Phase B):
// 1. 클라이언트가 IP 입력 후 서버에 접속
// 2. LoginGameMode::PostLogin() 호출됨
// 3. 바로 GihyeonMap으로 ServerTravel
// 4. GihyeonMap에서 로그인 UI 표시 (DefenseGameMode)
// 
// 📌 작성자: Gihyeon
// 📌 작성일: 2025-01-23
// 📌 수정일: 2025-01-28 (Phase B - 로그인 로직을 DefenseGameMode로 이동)
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

	// ============================================
	// 📌 플레이어 접속 시 바로 GihyeonMap으로 이동
	// Phase B: 로그인 없이 바로 게임 맵으로!
	// ============================================
	virtual void PostLogin(APlayerController* NewPlayer) override;

public:
	// ============================================
	// 📌 [Phase B 유지] 계정 관련 함수
	// DefenseGameMode에서 사용하기 위해 유지
	// 나중에 DefenseGameMode로 완전히 이동 가능
	// ============================================

	/**
	 * 동시 접속 여부 확인 (GameInstance에서 확인)
	 * @param PlayerId - 확인할 아이디
	 * @return 이미 접속 중이면 true
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Login")
	bool IsPlayerLoggedIn(const FString& PlayerId) const;

	/**
	 * 계정 SaveGame 가져오기
	 * DefenseGameMode에서 계정 검증용으로 사용 가능
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Login")
	UHellunaAccountSaveGame* GetAccountSaveGame() const { return AccountSaveGame; }

protected:
	// ============================================
	// 📌 내부 함수
	// ============================================

	/**
	 * 게임 맵으로 이동
	 * Seamless Travel 사용
	 */
	void TravelToGameMap();

	// ============================================
	// 📌 설정 (Blueprint에서 변경 가능)
	// ============================================

	/** 
	 * 서버 접속 후 이동할 게임 맵
	 * Blueprint에서 드롭다운으로 선택 가능
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Login|Map", meta = (DisplayName = "게임 맵"))
	TSoftObjectPtr<UWorld> GameMap;

	// ============================================
	// 📌 데이터
	// ============================================

	/** 
	 * 계정 데이터 (SaveGame)
	 * 아이디/비밀번호 저장
	 * ※ DefenseGameMode에서도 접근 가능하도록 유지
	 */
	UPROPERTY(meta = (DisplayName = "계정 저장 데이터"))
	TObjectPtr<UHellunaAccountSaveGame> AccountSaveGame;

	/** 첫 번째 플레이어 접속 여부 (맵 이동 트리거) */
	bool bHasFirstPlayerJoined = false;
};
