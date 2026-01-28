// HellunaAccountSaveGame.h
// 계정 정보를 저장하는 SaveGame 클래스
// 
// ============================================
// 📌 역할:
// - 모든 계정 정보를 서버에 저장 (.sav 파일)
// - 아이디 + 비밀번호 관리
// - 계정 존재 여부 확인
// - 비밀번호 검증
// - 새 계정 생성
// 
// 📌 저장 위치:
// Saved/SaveGames/HellunaAccounts.sav
// 
// 📌 사용 위치:
// - HellunaDefenseGameMode::BeginPlay() 에서 LoadOrCreate()로 로드
// - HellunaDefenseGameMode::ProcessLogin() 에서 계정 검증/생성
// 
// ============================================
// 📌 로그인 시스템 전체 흐름:
// ============================================
// 
// [1단계: 클라이언트 → 서버 로그인 요청]
// ┌─────────────────────────────────────────────────────────────┐
// │ LoginWidget (UI)                                             │
// │   ↓ 사용자가 ID/PW 입력 후 로그인 버튼 클릭                 │
// │ LoginController::OnLoginButtonClicked()                      │
// │   ↓ Server RPC 호출                                          │
// │ LoginController::Server_RequestLogin()  ──────────────────→  │
// └─────────────────────────────────────────────────────────────┘
// 
// [2단계: 서버에서 계정 검증]
// ┌─────────────────────────────────────────────────────────────┐
// │ DefenseGameMode::ProcessLogin()                              │
// │   ├─ AccountSaveGame->HasAccount() : 계정 존재 확인          │
// │   ├─ AccountSaveGame->ValidatePassword() : 비밀번호 검증     │
// │   └─ AccountSaveGame->CreateAccount() : 새 계정 생성         │
// │                                                               │
// │ 로그인 성공 시:                                               │
// │   ├─ GameInstance->RegisterLogin() : 접속자 목록에 추가      │
// │   ├─ PlayerState->SetLoginInfo() : ID를 PlayerState에 저장   │
// │   └─ Client RPC로 결과 전달                                   │
// └─────────────────────────────────────────────────────────────┘
// 
// [3단계: 캐릭터 소환]
// ┌─────────────────────────────────────────────────────────────┐
// │ DefenseGameMode::SwapToGameController()                      │
// │   ↓ LoginController → GameController 교체                   │
// │ DefenseGameMode::SpawnHeroCharacter()                        │
// │   ↓ 캐릭터 스폰 및 Possess                                   │
// │ DefenseGameMode::InitializeGame()                            │
// │   ↓ 첫 플레이어 소환 후 게임 시작                            │
// └─────────────────────────────────────────────────────────────┘
// 
// [로그아웃 시]
// ┌─────────────────────────────────────────────────────────────┐
// │ DefenseGameMode::Logout()                                    │
// │   ├─ PlayerState->ClearLoginInfo() : 로그인 정보 초기화      │
// │   └─ GameInstance->RegisterLogout() : 접속자 목록에서 제거   │
// └─────────────────────────────────────────────────────────────┘
// 
// ============================================
// 📌 관련 클래스:
// ============================================
// - UHellunaAccountSaveGame : 계정 데이터 저장 (이 파일)
// - AHellunaLoginController : 로그인 UI + RPC 처리
// - UHellunaLoginWidget : ID/PW 입력 UI
// - AHellunaPlayerState : 로그인된 ID 저장 (Replicated)
// - UMDF_GameInstance : 현재 접속자 목록 관리
// - AHellunaDefenseGameMode : 로그인 검증 + 캐릭터 소환
// 
// ============================================
// 📌 인벤토리 세이브 시스템과의 연계:
// ============================================
// - PlayerState->PlayerUniqueId 를 키로 사용하여 인벤토리 저장/로드
// - 동일한 SaveGame 패턴 사용 (LoadOrCreate, Save)
// - 저장 파일: HellunaInventory.sav (별도 파일)
// 
// 📌 작성자: Gihyeon
// 📌 작성일: 2025-01-23
// ============================================

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "HellunaAccountSaveGame.generated.h"

/**
 * 단일 계정 데이터 구조체
 */
USTRUCT(BlueprintType)
struct FHellunaAccountData
{
	GENERATED_BODY()

	FHellunaAccountData()
		: PlayerId(TEXT(""))
		, Password(TEXT(""))
	{
	}

	FHellunaAccountData(const FString& InPlayerId, const FString& InPassword)
		: PlayerId(InPlayerId)
		, Password(InPassword)
	{
	}

	/** 플레이어 아이디 (고유 식별자) */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Account", meta = (DisplayName = "플레이어 아이디"))
	FString PlayerId;

	/** 비밀번호 (평문 저장 - 졸업작품용) */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Account", meta = (DisplayName = "비밀번호"))
	FString Password;
};

/**
 * 계정 정보를 저장하는 SaveGame 클래스
 * 서버에서만 사용됨
 */
UCLASS()
class HELLUNA_API UHellunaAccountSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	UHellunaAccountSaveGame();

	// ============================================
	// 📌 저장 슬롯 이름 (상수)
	// ============================================
	
	/** SaveGame 슬롯 이름 */
	static const FString SaveSlotName;
	
	/** 사용자 인덱스 (싱글 서버이므로 0 고정) */
	static const int32 UserIndex;

	// ============================================
	// 📌 계정 데이터
	// ============================================

	/**
	 * 전체 계정 목록
	 * Key: 플레이어 아이디
	 * Value: 계정 데이터 (아이디, 비밀번호)
	 */
	UPROPERTY(SaveGame)
	TMap<FString, FHellunaAccountData> Accounts;

	// ============================================
	// 📌 계정 관리 함수
	// ============================================

	/**
	 * 계정 존재 여부 확인
	 * @param PlayerId - 확인할 아이디
	 * @return 존재하면 true
	 */
	UFUNCTION(BlueprintCallable, Category = "Account")
	bool HasAccount(const FString& PlayerId) const;

	/**
	 * 비밀번호 검증
	 * @param PlayerId - 아이디
	 * @param Password - 확인할 비밀번호
	 * @return 비밀번호가 일치하면 true
	 */
	UFUNCTION(BlueprintCallable, Category = "Account")
	bool ValidatePassword(const FString& PlayerId, const FString& Password) const;

	/**
	 * 새 계정 생성
	 * @param PlayerId - 새 아이디
	 * @param Password - 새 비밀번호
	 * @return 생성 성공하면 true (이미 존재하면 false)
	 */
	UFUNCTION(BlueprintCallable, Category = "Account")
	bool CreateAccount(const FString& PlayerId, const FString& Password);

	/**
	 * 계정 개수 반환
	 * @return 등록된 계정 수
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Account")
	int32 GetAccountCount() const { return Accounts.Num(); }

	// ============================================
	// 📌 저장/로드 유틸리티 (Static 함수)
	// ============================================

	/**
	 * 계정 데이터 로드 (없으면 새로 생성)
	 * @return 로드된 AccountSaveGame 인스턴스
	 */
	static UHellunaAccountSaveGame* LoadOrCreate();

	/**
	 * 계정 데이터 저장
	 * @param AccountSaveGame - 저장할 인스턴스
	 * @return 저장 성공하면 true
	 */
	static bool Save(UHellunaAccountSaveGame* AccountSaveGame);
};
