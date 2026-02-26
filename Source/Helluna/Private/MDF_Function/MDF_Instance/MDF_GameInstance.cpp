// MDF_GameInstance.cpp
// 게임 인스턴스 구현
// 
// ============================================
// 📌 역할:
// - Seamless Travel에서도 유지되는 데이터 관리
// - 로그인 플레이어 목록 관리 (동시 접속 방지)
// 
// ============================================
// 📌 로그인 시스템에서의 역할:
// ============================================
// 
// LoggedInPlayerIds (TSet<FString>)
//   - 현재 접속 중인 플레이어 ID 목록
//   - 예: {"playerA", "playerB", "playerC"}
// 
// [동시 접속 방지 흐름]
// 
// 1. 플레이어 A 로그인 시도 (ID: "test123")
// 2. DefenseGameMode::ProcessLogin()에서:
//    GameInstance->IsPlayerLoggedIn("test123") 체크
//    └─ false → 로그인 진행
// 3. OnLoginSuccess()에서:
//    GameInstance->RegisterLogin("test123")
//    └─ LoggedInPlayerIds에 추가
// 
// 4. 플레이어 B가 같은 ID로 로그인 시도
// 5. IsPlayerLoggedIn("test123") → true
//    └─ 로그인 거부! "이미 접속 중인 계정입니다"
// 
// 6. 플레이어 A 로그아웃 (연결 끊김)
// 7. DefenseGameMode::Logout()에서:
//    GameInstance->RegisterLogout("test123")
//    └─ LoggedInPlayerIds에서 제거
// 
// 8. 이제 플레이어 B가 "test123"으로 로그인 가능!
// 
// ============================================
// 📌 Seamless Travel과의 관계:
// ============================================
// - GameInstance는 맵 이동해도 파괴되지 않음
// - 따라서 LoggedInPlayerIds도 맵 이동 후에도 유지
// - 맵 이동 후에도 동시 접속 방지 정상 작동
// 
// 📌 작성자: Gihyeon
// ============================================

#include "MDF_Function/MDF_Instance/MDF_GameInstance.h"
#include "Login/Widget/HellunaLoadingWidget.h"
#include "Blueprint/UserWidget.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"

// ============================================
// 🔐 RegisterLogin - 로그인 등록
// ============================================
// 📌 호출 시점: DefenseGameMode::OnLoginSuccess() 에서 호출
// 📌 역할: 접속자 목록에 PlayerId 추가
// ============================================
void UMDF_GameInstance::RegisterLogin(const FString& PlayerId)
{
	if (!PlayerId.IsEmpty())
	{
		LoggedInPlayerIds.Add(PlayerId);
		UE_LOG(LogTemp, Warning, TEXT("[GameInstance] ★ RegisterLogin: '%s' (현재 접속자 %d명)"), *PlayerId, LoggedInPlayerIds.Num());
		
		// 디버깅: 현재 접속자 목록 출력
		UE_LOG(LogTemp, Warning, TEXT("[GameInstance] 접속자 목록:"));
		for (const FString& Id : LoggedInPlayerIds)
		{
			UE_LOG(LogTemp, Warning, TEXT("[GameInstance]   - '%s'"), *Id);
		}
	}
}

// ============================================
// 🔐 RegisterLogout - 로그아웃 등록
// ============================================
// 📌 호출 시점: DefenseGameMode::Logout() 에서 호출
// 📌 역할: 접속자 목록에서 PlayerId 제거
// 📌 효과: 다른 클라이언트가 같은 ID로 로그인 가능해짐
// ============================================
void UMDF_GameInstance::RegisterLogout(const FString& PlayerId)
{
	if (LoggedInPlayerIds.Contains(PlayerId))
	{
		LoggedInPlayerIds.Remove(PlayerId);
		UE_LOG(LogTemp, Warning, TEXT("[GameInstance] ★ RegisterLogout: '%s' (현재 접속자 %d명)"), *PlayerId, LoggedInPlayerIds.Num());
	}
}

// ============================================
// 🔐 IsPlayerLoggedIn - 동시 접속 체크
// ============================================
// 📌 호출 시점: DefenseGameMode::ProcessLogin() 에서 호출
// 📌 역할: 해당 PlayerId가 이미 접속 중인지 확인
// 📌 반환값:
//    - true: 이미 접속 중 → 로그인 거부해야 함
//    - false: 접속 안 함 → 로그인 진행 가능
// ============================================
bool UMDF_GameInstance::IsPlayerLoggedIn(const FString& PlayerId) const
{
	bool bResult = LoggedInPlayerIds.Contains(PlayerId);
	UE_LOG(LogTemp, Warning, TEXT("[GameInstance] IsPlayerLoggedIn('%s') = %s (접속자 %d명)"), 
		*PlayerId, bResult ? TEXT("TRUE") : TEXT("FALSE"), LoggedInPlayerIds.Num());
	return bResult;
}

int32 UMDF_GameInstance::GetLoggedInPlayerCount() const
{
	return LoggedInPlayerIds.Num();
}

// ============================================
// 로딩 화면 (Loading Screen)
// ============================================

void UMDF_GameInstance::ShowLoadingScreen(const FString& Message)
{
	// 이미 유효한 로딩 위젯이 있으면 메시지만 갱신
	if (IsValid(LoadingWidget))
	{
		LoadingWidget->SetLoadingMessage(Message);
		UE_LOG(LogTemp, Log, TEXT("[GameInstance] 로딩 화면 메시지 갱신: '%s'"), *Message);
		return;
	}

	if (!LoadingWidgetClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("[GameInstance] LoadingWidgetClass 미설정! BP에서 설정 필요"));
		return;
	}

	// 첫 번째 로컬 플레이어 컨트롤러로 위젯 생성
	APlayerController* PC = GetFirstLocalPlayerController();
	if (!PC)
	{
		UE_LOG(LogTemp, Warning, TEXT("[GameInstance] 로딩 화면 표시 실패 - LocalPlayerController 없음"));
		return;
	}

	LoadingWidget = CreateWidget<UHellunaLoadingWidget>(PC, LoadingWidgetClass);
	if (!LoadingWidget)
	{
		UE_LOG(LogTemp, Error, TEXT("[GameInstance] 로딩 위젯 생성 실패!"));
		return;
	}

	LoadingWidget->SetLoadingMessage(Message);
	LoadingWidget->AddToViewport(9999);  // 최상위 ZOrder

	UE_LOG(LogTemp, Log, TEXT("[GameInstance] 로딩 화면 표시: '%s'"), *Message);
}

void UMDF_GameInstance::HideLoadingScreen()
{
	if (!IsValid(LoadingWidget))
	{
		// 맵 전환 등으로 이미 파괴됨 → 포인터만 정리
		LoadingWidget = nullptr;
		return;
	}

	LoadingWidget->RemoveFromParent();
	LoadingWidget = nullptr;

	UE_LOG(LogTemp, Log, TEXT("[GameInstance] 로딩 화면 숨김"));
}
