// MDF_GameInstance.cpp
// 게임 인스턴스 구현
// 
// ============================================
// 📌 역할:
// - Seamless Travel에서도 유지되는 데이터 관리
// - 로그인 플레이어 목록 관리
// 
// 📌 작성자: Gihyeon
// ============================================

#include "MDF_Function/MDF_Instance/MDF_GameInstance.h"

void UMDF_GameInstance::RegisterLogin(const FString& PlayerId)
{
	if (!PlayerId.IsEmpty())
	{
		LoggedInPlayerIds.Add(PlayerId);
		UE_LOG(LogTemp, Log, TEXT("[GameInstance] RegisterLogin: %s (접속자 %d명)"), *PlayerId, LoggedInPlayerIds.Num());
	}
}

void UMDF_GameInstance::RegisterLogout(const FString& PlayerId)
{
	if (LoggedInPlayerIds.Contains(PlayerId))
	{
		LoggedInPlayerIds.Remove(PlayerId);
		UE_LOG(LogTemp, Log, TEXT("[GameInstance] RegisterLogout: %s (접속자 %d명)"), *PlayerId, LoggedInPlayerIds.Num());
	}
}

bool UMDF_GameInstance::IsPlayerLoggedIn(const FString& PlayerId) const
{
	return LoggedInPlayerIds.Contains(PlayerId);
}

int32 UMDF_GameInstance::GetLoggedInPlayerCount() const
{
	return LoggedInPlayerIds.Num();
}
