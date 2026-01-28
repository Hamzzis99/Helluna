// HellunaPlayerState.cpp
// 플레이어 고유 ID를 저장하는 PlayerState 클래스 구현
// 
// ============================================
// 📌 역할:
// 플레이어의 로그인 정보를 저장하고 네트워크 동기화
// 
// ============================================
// 📌 핵심 변수:
// ============================================
// 
// FString PlayerUniqueId (Replicated)
//   - 로그인한 플레이어의 고유 ID
//   - 로그인 전: "" (빈 문자열)
//   - 로그인 후: 사용자가 입력한 아이디 (예: "test123")
// 
// bool bIsLoggedIn (Replicated)
//   - 로그인 상태 플래그
//   - 로그인 전: false
//   - 로그인 후: true
// 
// ============================================
// 📌 PlayerUniqueId의 활용:
// ============================================
// 
// 1️⃣ 인벤토리 저장
//    FString PlayerId = PlayerState->GetPlayerUniqueId();
//    InventorySaveGame->SavePlayerInventory(PlayerId, InventoryComponent);
// 
// 2️⃣ 인벤토리 로드
//    FString PlayerId = PlayerState->GetPlayerUniqueId();
//    InventorySaveGame->LoadPlayerInventory(PlayerId, InventoryComponent);
// 
// 3️⃣ 동시 접속 체크 (GameInstance)
//    GameInstance->IsPlayerLoggedIn(PlayerId);
// 
// ============================================
// 📌 주의사항:
// ============================================
// - SetLoginInfo(), ClearLoginInfo()는 서버에서만 호출!
// - Replicated이므로 클라이언트에서 값 변경해도 서버에 반영 안 됨
// - HasAuthority() 체크 후 호출할 것
// 
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

// ============================================
// 🔐 SetLoginInfo - 로그인 정보 설정
// ============================================
// 
// 📌 호출 시점: DefenseGameMode::OnLoginSuccess() 에서 호출
// 
// 📌 매개변수:
//    - InPlayerId: 로그인한 플레이어 아이디 (예: "test123")
// 
// 📌 처리 내용:
//    1. PlayerUniqueId = InPlayerId
//    2. bIsLoggedIn = true
//    3. Replicated → 모든 클라이언트에 자동 동기화
// 
// 📌 ★★★ 중요 ★★★
//    이 PlayerUniqueId가 인벤토리 저장의 키로 사용됩니다!
//    예: InventorySaveGame->SavePlayerInventory(PlayerUniqueId, Inventory)
// ============================================
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

// ============================================
// 🔐 ClearLoginInfo - 로그인 정보 초기화
// ============================================
// 
// 📌 호출 시점:
//    1. DefenseGameMode::Logout() - 플레이어 연결 끊김 시
//    2. DefenseGameMode::SwapToGameController() - Controller 교체 전
//       (중복 로그아웃 방지를 위해)
// 
// 📌 처리 내용:
//    1. PlayerUniqueId = "" (빈 문자열)
//    2. bIsLoggedIn = false
//    3. Replicated → 모든 클라이언트에 자동 동기화
// 
// 📌 주의: 
//    인벤토리 저장은 이 함수 호출 전에 해야 합니다!
//    PlayerUniqueId가 초기화되면 저장 키를 알 수 없음
// ============================================
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
