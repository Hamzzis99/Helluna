#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "MDF_GameInstance.generated.h"

class UHellunaLoadingWidget;

/**
 * ============================================
 * 📌 MDF_GameInstance
 * ============================================
 * 
 * 게임 전역에서 유지되는 데이터를 관리하는 GameInstance
 * 
 * ============================================
 * 📌 역할:
 * ============================================
 * 1. 맵 이동(Seamless Travel) 시에도 유지되는 데이터 관리
 * 2. 현재 접속 중인 플레이어 목록 관리 (동시 접속 방지)
 * 
 * ============================================
 * 📌 로그인 시스템에서의 역할:
 * ============================================
 * 
 * [동시 접속 방지]
 * - LoggedInPlayerIds에 현재 접속 중인 플레이어 ID 저장
 * - 로그인 시도 시 IsPlayerLoggedIn()으로 중복 체크
 * - 이미 접속 중인 ID면 "이미 접속 중인 계정입니다" 에러
 * 
 * [사용 흐름]
 * ┌─────────────────────────────────────────────────────────┐
 * │ 로그인 시도                                              │
 * │   ↓                                                      │
 * │ DefenseGameMode::ProcessLogin()                          │
 * │   ├─ GameInstance->IsPlayerLoggedIn(PlayerId)            │
 * │   │   └─ true면 → 로그인 거부                           │
 * │   └─ false면 → 로그인 진행                              │
 * │                                                          │
 * │ 로그인 성공                                              │
 * │   ↓                                                      │
 * │ DefenseGameMode::OnLoginSuccess()                        │
 * │   └─ GameInstance->RegisterLogin(PlayerId)               │
 * │       → LoggedInPlayerIds에 추가                         │
 * │                                                          │
 * │ 로그아웃 / 연결 끊김                                     │
 * │   ↓                                                      │
 * │ DefenseGameMode::Logout()                                │
 * │   └─ GameInstance->RegisterLogout(PlayerId)              │
 * │       → LoggedInPlayerIds에서 제거                       │
 * └─────────────────────────────────────────────────────────┘
 * 
 * ============================================
 * 📌 주의사항:
 * ============================================
 * - GameInstance는 서버에서만 유효함 (Dedicated Server 기준)
 * - Seamless Travel 시에도 GameInstance는 유지됨
 * - 게임 재시작(RestartGame) 시에도 유지됨 (필요 시 수동 초기화)
 * 
 * 📌 작성자: Gihyeon
 */
UCLASS()
class HELLUNA_API UMDF_GameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	// ============================================
	// 📌 맵 이동 관련
	// ============================================
	
	/**
	 * 맵 이동 중 플래그
	 * - true: 맵 이동 중 (MoveMapActor에서 설정)
	 * - false: 새 게임 또는 재시작
	 * 
	 * MDF_SaveActor에서 이 값을 체크하여 데이터 복원 여부 결정
	 */
	UPROPERTY(BlueprintReadWrite, Category = "Game Flow")
	bool bIsMapTransitioning = false;

	// ============================================
	// 📌 로그인 시스템 (동시 접속 방지)
	// ============================================

	/**
	 * 현재 접속 중인 플레이어 ID 목록
	 * 
	 * 예시:
	 * - 플레이어 A 로그인 → {"playerA"}
	 * - 플레이어 B 로그인 → {"playerA", "playerB"}
	 * - 플레이어 A 로그아웃 → {"playerB"}
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Login", meta = (DisplayName = "접속 중인 플레이어 목록"))
	TSet<FString> LoggedInPlayerIds;

	/**
	 * 플레이어 로그인 등록
	 * 
	 * @param PlayerId - 로그인한 플레이어 ID
	 * 
	 * 호출 위치: DefenseGameMode::OnLoginSuccess()
	 */
	UFUNCTION(BlueprintCallable, Category = "Login")
	void RegisterLogin(const FString& PlayerId);

	/**
	 * 플레이어 로그아웃 처리
	 * 
	 * @param PlayerId - 로그아웃할 플레이어 ID
	 * 
	 * 호출 위치: DefenseGameMode::Logout()
	 */
	UFUNCTION(BlueprintCallable, Category = "Login")
	void RegisterLogout(const FString& PlayerId);

	/**
	 * 동시 접속 체크
	 * 
	 * @param PlayerId - 확인할 플레이어 ID
	 * @return true면 이미 접속 중 (로그인 거부해야 함)
	 * 
	 * 호출 위치: DefenseGameMode::ProcessLogin()
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Login")
	bool IsPlayerLoggedIn(const FString& PlayerId) const;

	/**
	 * 현재 접속 중인 플레이어 수
	 * 
	 * @return 접속자 수
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Login")
	int32 GetLoggedInPlayerCount() const;

	// ============================================
	// 로딩 화면 (Loading Screen)
	// ============================================

	/**
	 * 로딩 화면 표시
	 * 전환 구간(서버 접속, 로그인, 캐릭터 선택 등)에서 호출
	 * 이미 표시 중이면 메시지만 갱신
	 *
	 * @param Message - 표시할 로딩 메시지
	 */
	UFUNCTION(BlueprintCallable, Category = "Loading")
	void ShowLoadingScreen(const FString& Message);

	/**
	 * 로딩 화면 숨김
	 * 전환 완료 후 호출
	 */
	UFUNCTION(BlueprintCallable, Category = "Loading")
	void HideLoadingScreen();

protected:
	/** 로딩 위젯 클래스 (BP에서 설정: WBP_HellunaLoadingWidget) */
	UPROPERTY(EditDefaultsOnly, Category = "Loading", meta = (DisplayName = "Loading Widget Class (로딩 위젯 클래스)"))
	TSubclassOf<UHellunaLoadingWidget> LoadingWidgetClass;

	/** 현재 활성화된 로딩 위젯 인스턴스 */
	UPROPERTY()
	TObjectPtr<UHellunaLoadingWidget> LoadingWidget;
};