// HellunaPlayerState.cpp
// 플레이어 고유 ID를 저장하는 PlayerState 클래스 구현
// 
// ============================================
// 📌 작성자: Gihyeon
// 📌 작성일: 2025-01-23
// ============================================

#include "Player/HellunaPlayerState.h"
#include "Net/UnrealNetwork.h"

AHellunaPlayerState::AHellunaPlayerState()
{
	// 기본값 초기화
	PlayerUniqueId = TEXT("");
	bIsLoggedIn = false;
}

void AHellunaPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// ============================================
	// 📌 Replicated 속성 등록
	// DOREPLIFETIME: 모든 클라이언트에게 동기화
	// ============================================
	DOREPLIFETIME(AHellunaPlayerState, PlayerUniqueId);
	DOREPLIFETIME(AHellunaPlayerState, bIsLoggedIn);
}

void AHellunaPlayerState::SetLoginInfo(const FString& InPlayerId)
{
	// ============================================
	// 📌 서버에서만 호출되어야 함
	// 클라이언트에서 호출하면 Replicated가 작동 안 함
	// ============================================
	if (!HasAuthority())
	{
		UE_LOG(LogTemp, Warning, TEXT("[HellunaPlayerState] SetLoginInfo는 서버에서만 호출해야 합니다!"));
		return;
	}

	PlayerUniqueId = InPlayerId;
	bIsLoggedIn = true;

	UE_LOG(LogTemp, Log, TEXT("[HellunaPlayerState] 로그인 성공: PlayerUniqueId = %s"), *PlayerUniqueId);
}

void AHellunaPlayerState::ClearLoginInfo()
{
	// ============================================
	// 📌 서버에서만 호출되어야 함
	// ============================================
	if (!HasAuthority())
	{
		UE_LOG(LogTemp, Warning, TEXT("[HellunaPlayerState] ClearLoginInfo는 서버에서만 호출해야 합니다!"));
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("[HellunaPlayerState] 로그아웃: PlayerUniqueId = %s"), *PlayerUniqueId);

	PlayerUniqueId = TEXT("");
	bIsLoggedIn = false;
}
