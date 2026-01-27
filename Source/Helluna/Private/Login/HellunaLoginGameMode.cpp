// HellunaLoginGameMode.cpp
// 로그인 레벨 전용 GameMode 구현
// 
// ============================================
// 📌 작성자: Gihyeon
// 📌 작성일: 2025-01-23
// ============================================

#include "Login/HellunaLoginGameMode.h"
#include "Login/HellunaLoginController.h"
#include "Login/HellunaAccountSaveGame.h"
#include "Player/HellunaPlayerState.h"

AHellunaLoginGameMode::AHellunaLoginGameMode()
{
	// ============================================
	// 📌 기본 클래스 설정
	// LoginLevel에서 사용할 클래스들 지정
	// ============================================
	PlayerControllerClass = AHellunaLoginController::StaticClass();
	PlayerStateClass = AHellunaPlayerState::StaticClass();

	// Pawn은 사용하지 않음 (UI만 표시)
	DefaultPawnClass = nullptr;

	// Seamless Travel 활성화 (PlayerState 유지)
	bUseSeamlessTravel = true;
}

void AHellunaLoginGameMode::BeginPlay()
{
	Super::BeginPlay();

	// ============================================
	// 📌 계정 데이터 로드
	// 서버 시작 시 기존 계정 정보 불러오기
	// ============================================
	AccountSaveGame = UHellunaAccountSaveGame::LoadOrCreate();

	if (AccountSaveGame)
	{
		UE_LOG(LogTemp, Log, TEXT("[LoginGameMode] BeginPlay: 계정 데이터 로드 완료 (계정 %d개)"), AccountSaveGame->GetAccountCount());
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[LoginGameMode] BeginPlay: 계정 데이터 로드 실패!"));
	}
}

void AHellunaLoginGameMode::ProcessLogin(AHellunaLoginController* LoginController, const FString& PlayerId, const FString& Password)
{
	// ============================================
	// 📌 서버에서만 실행되어야 함
	// ============================================
	if (!HasAuthority())
	{
		UE_LOG(LogTemp, Warning, TEXT("[LoginGameMode] ProcessLogin: 서버에서만 호출 가능!"));
		return;
	}

	if (!LoginController)
	{
		UE_LOG(LogTemp, Error, TEXT("[LoginGameMode] ProcessLogin: LoginController가 nullptr!"));
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("[LoginGameMode] ProcessLogin: 로그인 시도 - ID: %s"), *PlayerId);

	// ============================================
	// 📌 1단계: 동시 접속 체크
	// ============================================
	if (IsPlayerLoggedIn(PlayerId))
	{
		OnLoginFailed(LoginController, TEXT("이미 접속 중인 계정입니다."));
		return;
	}

	// ============================================
	// 📌 2단계: 계정 존재 여부 확인
	// ============================================
	if (!AccountSaveGame)
	{
		OnLoginFailed(LoginController, TEXT("서버 오류: 계정 데이터를 불러올 수 없습니다."));
		return;
	}

	if (AccountSaveGame->HasAccount(PlayerId))
	{
		// ============================================
		// 📌 기존 계정: 비밀번호 검증
		// ============================================
		if (AccountSaveGame->ValidatePassword(PlayerId, Password))
		{
			// 비밀번호 일치 → 로그인 성공
			OnLoginSuccess(LoginController, PlayerId);
		}
		else
		{
			// 비밀번호 불일치 → 로그인 실패
			OnLoginFailed(LoginController, TEXT("아이디가 이미 존재합니다. 비밀번호를 확인해주세요."));
		}
	}
	else
	{
		// ============================================
		// 📌 새 계정: 자동 생성
		// ============================================
		if (AccountSaveGame->CreateAccount(PlayerId, Password))
		{
			// 계정 생성 성공 → 저장 후 로그인
			UHellunaAccountSaveGame::Save(AccountSaveGame);
			OnLoginSuccess(LoginController, PlayerId);

			UE_LOG(LogTemp, Log, TEXT("[LoginGameMode] ProcessLogin: 새 계정 생성됨 - ID: %s"), *PlayerId);
		}
		else
		{
			OnLoginFailed(LoginController, TEXT("계정 생성에 실패했습니다."));
		}
	}
}

void AHellunaLoginGameMode::ProcessLogout(const FString& PlayerId)
{
	if (LoggedInPlayerIds.Contains(PlayerId))
	{
		LoggedInPlayerIds.Remove(PlayerId);
		UE_LOG(LogTemp, Log, TEXT("[LoginGameMode] ProcessLogout: 로그아웃 - ID: %s (접속자 %d명)"), *PlayerId, LoggedInPlayerIds.Num());
	}
}

bool AHellunaLoginGameMode::IsPlayerLoggedIn(const FString& PlayerId) const
{
	return LoggedInPlayerIds.Contains(PlayerId);
}

void AHellunaLoginGameMode::OnLoginSuccess(AHellunaLoginController* LoginController, const FString& PlayerId)
{
	// ============================================
	// 📌 접속자 목록에 추가
	// ============================================
	LoggedInPlayerIds.Add(PlayerId);

	// ============================================
	// 📌 PlayerState에 로그인 정보 저장
	// Seamless Travel 시에도 유지됨
	// ============================================
	if (AHellunaPlayerState* PS = LoginController->GetPlayerState<AHellunaPlayerState>())
	{
		PS->SetLoginInfo(PlayerId);
	}

	// ============================================
	// 📌 클라이언트에 성공 알림
	// ============================================
	LoginController->Client_LoginResult(true, TEXT(""));

	UE_LOG(LogTemp, Log, TEXT("[LoginGameMode] OnLoginSuccess: 로그인 성공 - ID: %s (접속자 %d명)"), *PlayerId, LoggedInPlayerIds.Num());

	// ============================================
	// 📌 게임 맵으로 이동
	// 현재는 단일 플레이어 테스트용으로 바로 이동
	// TODO: 나중에 "준비 완료" 버튼 또는 모든 플레이어 대기 후 이동으로 변경
	// ============================================
	TravelToGameMap();
}

void AHellunaLoginGameMode::OnLoginFailed(AHellunaLoginController* LoginController, const FString& ErrorMessage)
{
	LoginController->Client_LoginResult(false, ErrorMessage);

	UE_LOG(LogTemp, Warning, TEXT("[LoginGameMode] OnLoginFailed: %s"), *ErrorMessage);
}

void AHellunaLoginGameMode::TravelToGameMap()
{
	// ============================================
	// 📌 Seamless Travel로 게임 맵 이동
	// PlayerState가 유지됨!
	// 
	// TSoftObjectPtr<UWorld>에서 맵 경로를 가져와서
	// ServerTravel 실행
	// ============================================
	if (GameMap.IsNull())
	{
		UE_LOG(LogTemp, Error, TEXT("[LoginGameMode] TravelToGameMap: GameMap이 설정되지 않았습니다! Blueprint에서 맵을 선택해주세요."));
		
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 10.f, FColor::Red,
				TEXT("❌ [LoginGameMode] GameMap이 설정되지 않았습니다! Blueprint에서 맵을 선택해주세요."));
		}
		return;
	}

	// TSoftObjectPtr에서 맵 경로 추출
	// 예: /Game/Gihyeon/GihyeonMap.GihyeonMap → /Game/Gihyeon/GihyeonMap
	FString MapPath = GameMap.GetLongPackageName();
	FString TravelURL = FString::Printf(TEXT("%s?listen"), *MapPath);
	
	UE_LOG(LogTemp, Log, TEXT("[LoginGameMode] TravelToGameMap: %s 로 이동 시작"), *TravelURL);
	
	GetWorld()->ServerTravel(TravelURL);
}
