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
// - HellunaLoginGameMode에서 로그인 검증 시 사용
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
