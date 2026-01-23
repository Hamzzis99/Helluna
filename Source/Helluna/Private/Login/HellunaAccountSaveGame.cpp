// HellunaAccountSaveGame.cpp
// 계정 정보를 저장하는 SaveGame 클래스 구현
// 
// ============================================
// 📌 작성자: Gihyeon
// 📌 작성일: 2025-01-23
// ============================================

#include "Login/HellunaAccountSaveGame.h"
#include "Kismet/GameplayStatics.h"

// ============================================
// 📌 상수 정의
// ============================================
const FString UHellunaAccountSaveGame::SaveSlotName = TEXT("HellunaAccounts");
const int32 UHellunaAccountSaveGame::UserIndex = 0;

UHellunaAccountSaveGame::UHellunaAccountSaveGame()
{
	// 기본 생성자
}

bool UHellunaAccountSaveGame::HasAccount(const FString& PlayerId) const
{
	return Accounts.Contains(PlayerId);
}

bool UHellunaAccountSaveGame::ValidatePassword(const FString& PlayerId, const FString& Password) const
{
	// 계정이 존재하지 않으면 false
	if (!Accounts.Contains(PlayerId))
	{
		UE_LOG(LogTemp, Warning, TEXT("[AccountSaveGame] ValidatePassword: 계정이 존재하지 않음 - %s"), *PlayerId);
		return false;
	}

	// 비밀번호 비교
	const FHellunaAccountData& AccountData = Accounts[PlayerId];
	bool bMatch = AccountData.Password.Equals(Password);

	if (bMatch)
	{
		UE_LOG(LogTemp, Log, TEXT("[AccountSaveGame] ValidatePassword: 비밀번호 일치 - %s"), *PlayerId);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[AccountSaveGame] ValidatePassword: 비밀번호 불일치 - %s"), *PlayerId);
	}

	return bMatch;
}

bool UHellunaAccountSaveGame::CreateAccount(const FString& PlayerId, const FString& Password)
{
	// 이미 존재하는 계정이면 생성 실패
	if (Accounts.Contains(PlayerId))
	{
		UE_LOG(LogTemp, Warning, TEXT("[AccountSaveGame] CreateAccount: 이미 존재하는 계정 - %s"), *PlayerId);
		return false;
	}

	// 새 계정 추가
	FHellunaAccountData NewAccount(PlayerId, Password);
	Accounts.Add(PlayerId, NewAccount);

	UE_LOG(LogTemp, Log, TEXT("[AccountSaveGame] CreateAccount: 계정 생성 성공 - %s (총 %d개)"), *PlayerId, Accounts.Num());

	return true;
}

UHellunaAccountSaveGame* UHellunaAccountSaveGame::LoadOrCreate()
{
	UHellunaAccountSaveGame* LoadedSaveGame = nullptr;

	// 기존 저장 파일이 있는지 확인
	if (UGameplayStatics::DoesSaveGameExist(SaveSlotName, UserIndex))
	{
		// 기존 파일 로드
		LoadedSaveGame = Cast<UHellunaAccountSaveGame>(
			UGameplayStatics::LoadGameFromSlot(SaveSlotName, UserIndex)
		);

		if (LoadedSaveGame)
		{
			UE_LOG(LogTemp, Log, TEXT("[AccountSaveGame] LoadOrCreate: 기존 계정 데이터 로드 성공 (계정 %d개)"), LoadedSaveGame->Accounts.Num());
		}
	}

	// 로드 실패하거나 파일이 없으면 새로 생성
	if (!LoadedSaveGame)
	{
		LoadedSaveGame = Cast<UHellunaAccountSaveGame>(
			UGameplayStatics::CreateSaveGameObject(UHellunaAccountSaveGame::StaticClass())
		);

		UE_LOG(LogTemp, Log, TEXT("[AccountSaveGame] LoadOrCreate: 새 계정 데이터 생성"));
	}

	return LoadedSaveGame;
}

bool UHellunaAccountSaveGame::Save(UHellunaAccountSaveGame* AccountSaveGame)
{
	if (!AccountSaveGame)
	{
		UE_LOG(LogTemp, Error, TEXT("[AccountSaveGame] Save: AccountSaveGame이 nullptr입니다!"));
		return false;
	}

	bool bSuccess = UGameplayStatics::SaveGameToSlot(AccountSaveGame, SaveSlotName, UserIndex);

	if (bSuccess)
	{
		UE_LOG(LogTemp, Log, TEXT("[AccountSaveGame] Save: 저장 성공 (계정 %d개)"), AccountSaveGame->Accounts.Num());
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[AccountSaveGame] Save: 저장 실패!"));
	}

	return bSuccess;
}
