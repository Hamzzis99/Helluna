// HellunaLoginGameMode.cpp
// 로그인 레벨 전용 GameMode 구현
// 
// ============================================
// 📌 작성자: Gihyeon
// 📌 작성일: 2025-01-23
// 📌 수정일: 2025-01-28 (Phase B - 로그인 로직을 DefenseGameMode로 이동)
// ============================================

#include "Login/HellunaLoginGameMode.h"
#include "Login/HellunaLoginController.h"
#include "Login/HellunaAccountSaveGame.h"
#include "Player/HellunaPlayerState.h"
#include "MDF_Function/MDF_Instance/MDF_GameInstance.h"
#include "Kismet/GameplayStatics.h"

AHellunaLoginGameMode::AHellunaLoginGameMode()
{
	// ============================================
	// 📌 기본 클래스 설정
	// LoginLevel에서 사용할 클래스들 지정
	// ============================================
	PlayerControllerClass = AHellunaLoginController::StaticClass();
	PlayerStateClass = AHellunaPlayerState::StaticClass();

	// ============================================
	// 📌 [Phase B] Pawn 설정
	// LoginLevel에서는 Pawn 사용 안 함 (IP 입력 UI만)
	// ============================================
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
	// ※ DefenseGameMode에서도 사용할 수 있도록 유지
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

	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("========================================"));
	UE_LOG(LogTemp, Warning, TEXT("[LoginGameMode] Phase B: IP 접속 전용 모드"));
	UE_LOG(LogTemp, Warning, TEXT("[LoginGameMode] 클라이언트 접속 시 바로 GihyeonMap으로 이동"));
	UE_LOG(LogTemp, Warning, TEXT("[LoginGameMode] 로그인은 GihyeonMap에서 처리됨!"));
	UE_LOG(LogTemp, Warning, TEXT("========================================"));
	UE_LOG(LogTemp, Warning, TEXT(""));
}

void AHellunaLoginGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);

	// ============================================
	// 📌 [Phase B] 플레이어 접속 시 바로 게임 맵으로 이동
	// 
	// 흐름:
	// 1. 클라이언트가 IP로 서버에 접속
	// 2. PostLogin 호출됨
	// 3. 첫 번째 플레이어일 경우 ServerTravel로 GihyeonMap 이동
	// 4. 이후 플레이어는 GihyeonMap으로 직접 접속됨
	// 5. GihyeonMap에서 로그인 UI 표시 (DefenseGameMode)
	// ============================================

	if (!HasAuthority())
		return;

	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("========================================"));
	UE_LOG(LogTemp, Warning, TEXT("[LoginGameMode] ★ PostLogin 호출됨!"));
	UE_LOG(LogTemp, Warning, TEXT("[LoginGameMode] 접속자: %s"), *GetNameSafe(NewPlayer));
	UE_LOG(LogTemp, Warning, TEXT("========================================"));

	// 첫 번째 플레이어 접속 시에만 맵 이동
	if (!bHasFirstPlayerJoined)
	{
		bHasFirstPlayerJoined = true;
		
		UE_LOG(LogTemp, Warning, TEXT("[LoginGameMode] ★ 첫 번째 플레이어 접속!"));
		UE_LOG(LogTemp, Warning, TEXT("[LoginGameMode] → GihyeonMap으로 ServerTravel 시작"));
		
		// 약간의 딜레이 후 맵 이동 (클라이언트가 완전히 로드될 때까지 대기)
		FTimerHandle TimerHandle;
		GetWorldTimerManager().SetTimer(TimerHandle, this, &AHellunaLoginGameMode::TravelToGameMap, 0.5f, false);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[LoginGameMode] 추가 플레이어 접속 - 이미 맵 이동 예정"));
	}

	UE_LOG(LogTemp, Warning, TEXT("========================================"));
	UE_LOG(LogTemp, Warning, TEXT(""));
}

bool AHellunaLoginGameMode::IsPlayerLoggedIn(const FString& PlayerId) const
{
	// GameInstance에서 확인
	if (UMDF_GameInstance* GI = Cast<UMDF_GameInstance>(UGameplayStatics::GetGameInstance(GetWorld())))
	{
		return GI->IsPlayerLoggedIn(PlayerId);
	}
	return false;
}

void AHellunaLoginGameMode::TravelToGameMap()
{
	// ============================================
	// 📌 [Phase B] Seamless Travel로 게임 맵 이동
	// 
	// ※ 로그인 없이 바로 이동!
	// ※ 로그인은 GihyeonMap에서 처리됨!
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
	FString MapPath = GameMap.GetLongPackageName();
	FString TravelURL = FString::Printf(TEXT("%s?listen"), *MapPath);
	
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("========================================"));
	UE_LOG(LogTemp, Warning, TEXT("[LoginGameMode] ★ ServerTravel 실행!"));
	UE_LOG(LogTemp, Warning, TEXT("[LoginGameMode] 목적지: %s"), *TravelURL);
	UE_LOG(LogTemp, Warning, TEXT("[LoginGameMode] ※ 로그인 없이 이동! (로그인은 GihyeonMap에서)"));
	UE_LOG(LogTemp, Warning, TEXT("========================================"));
	UE_LOG(LogTemp, Warning, TEXT(""));
	
	GetWorld()->ServerTravel(TravelURL);
}
