// HellunaLoginGameMode.h
// 로그인 레벨 전용 GameMode
// 
// ============================================
// 📌 역할:
// - 로그인 검증 로직 (계정 확인, 비밀번호 체크)
// - 동시 접속 체크 (같은 ID 접속 거부)
// - 계정 자동 생성 (새 아이디면 생성)
// - 로그인 성공 시 맵 이동 (ServerTravel)
// - PlayerState, PlayerController 클래스 지정
// 
// 📌 사용 위치:
// - LoginLevel에서 GameMode Override로 지정
// 
// 📌 로그인 흐름:
// 1. 동시 접속 체크 → 이미 접속 중이면 거부
// 2. 계정 존재 여부 확인
//    - 있으면: 비밀번호 검증
//    - 없으면: 새 계정 생성
// 3. 로그인 성공 시 PlayerState에 ID 저장
// 4. 게임 맵으로 ServerTravel
// 
// 📌 작성자: Gihyeon
// 📌 작성일: 2025-01-23
// ============================================

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "HellunaLoginGameMode.generated.h"

class UHellunaAccountSaveGame;
class AHellunaLoginController;

/**
 * 로그인 레벨 전용 GameMode
 * 로그인 검증 및 계정 관리 담당
 */
UCLASS()
class HELLUNA_API AHellunaLoginGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AHellunaLoginGameMode();

protected:
	virtual void BeginPlay() override;

public:
	// ============================================
	// 📌 로그인 처리 함수 (서버에서만 호출)
	// ============================================

	/**
	 * 로그인 요청 처리
	 * LoginController에서 호출됨
	 * @param LoginController - 요청한 컨트롤러
	 * @param PlayerId - 입력한 아이디
	 * @param Password - 입력한 비밀번호
	 */
	UFUNCTION(BlueprintCallable, Category = "Login")
	void ProcessLogin(AHellunaLoginController* LoginController, const FString& PlayerId, const FString& Password);

	/**
	 * 플레이어 로그아웃 처리
	 * 접속 종료 시 호출
	 * @param PlayerId - 로그아웃할 플레이어 ID
	 */
	UFUNCTION(BlueprintCallable, Category = "Login")
	void ProcessLogout(const FString& PlayerId);

	/**
	 * 동시 접속 여부 확인
	 * @param PlayerId - 확인할 아이디
	 * @return 이미 접속 중이면 true
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Login")
	bool IsPlayerLoggedIn(const FString& PlayerId) const;

protected:
	// ============================================
	// 📌 내부 함수
	// ============================================

	/**
	 * 로그인 성공 처리
	 * PlayerState에 ID 저장 + 맵 이동
	 */
	void OnLoginSuccess(AHellunaLoginController* LoginController, const FString& PlayerId);

	/**
	 * 로그인 실패 처리
	 * 클라이언트에 에러 메시지 전송
	 */
	void OnLoginFailed(AHellunaLoginController* LoginController, const FString& ErrorMessage);

	/**
	 * 게임 맵으로 이동
	 * Seamless Travel 사용
	 */
	void TravelToGameMap();

	// ============================================
	// 📌 설정 (Blueprint에서 변경 가능)
	// ============================================

	/** 
	 * 로그인 성공 후 이동할 게임 맵
	 * Blueprint에서 드롭다운으로 선택 가능
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Login|Map", meta = (DisplayName = "게임 맵"))
	TSoftObjectPtr<UWorld> GameMap;

	// ============================================
	// 📌 데이터
	// ============================================

	/** 현재 접속 중인 플레이어 ID 목록 */
	UPROPERTY(meta = (DisplayName = "접속 중인 플레이어 목록"))
	TSet<FString> LoggedInPlayerIds;

	/** 계정 데이터 (SaveGame) */
	UPROPERTY(meta = (DisplayName = "계정 저장 데이터"))
	TObjectPtr<UHellunaAccountSaveGame> AccountSaveGame;
};
