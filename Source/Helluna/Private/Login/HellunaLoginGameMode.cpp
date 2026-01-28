// HellunaLoginGameMode.cpp
// 로그인 레벨 전용 GameMode 구현
// 
// ============================================
// 📌 작성자: Gihyeon
// 📌 작성일: 2025-01-23
// 📌 수정일: 2025-01-28 (Phase B)
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
	// ============================================
	PlayerControllerClass = AHellunaLoginController::StaticClass();
	PlayerStateClass = AHellunaPlayerState::StaticClass();
	DefaultPawnClass = nullptr;  // UI만 표시
	bUseSeamlessTravel = true;
}

void AHellunaLoginGameMode::BeginPlay()
{
	Super::BeginPlay();

	// 계정 데이터 로드
	AccountSaveGame = UHellunaAccountSaveGame::LoadOrCreate();

	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║         [LoginGameMode] Phase B - BeginPlay                ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║  역할: IP 접속 UI만 담당                                   ║"));
	UE_LOG(LogTemp, Warning, TEXT("║  로그인: GihyeonMap(DefenseGameMode)에서 처리              ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║  [사용법]                                                  ║"));
	UE_LOG(LogTemp, Warning, TEXT("║  • 호스트: IP 빈칸 → '시작' 버튼 → 서버 시작              ║"));
	UE_LOG(LogTemp, Warning, TEXT("║  • 클라이언트: IP 입력 → '접속' 버튼 → 서버 접속          ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	if (AccountSaveGame)
	{
		UE_LOG(LogTemp, Warning, TEXT("║  계정 데이터: %d개 로드됨                                  ║"), AccountSaveGame->GetAccountCount());
	}
	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
	UE_LOG(LogTemp, Warning, TEXT(""));
}

void AHellunaLoginGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);

	// ============================================
	// 📌 [Phase B] PostLogin에서는 아무것도 안 함!
	// 
	// UI에서 버튼 클릭 시에만 맵 이동:
	// - 호스트: "시작" 버튼 → TravelToGameMap()
	// - 클라이언트: "접속" 버튼 → open IP (서버가 GihyeonMap에 있으면 바로 이동)
	// ============================================

	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("┌────────────────────────────────────────────────────────────┐"));
	UE_LOG(LogTemp, Warning, TEXT("│ [LoginGameMode] PostLogin                                  │"));
	UE_LOG(LogTemp, Warning, TEXT("├────────────────────────────────────────────────────────────┤"));
	UE_LOG(LogTemp, Warning, TEXT("│ 접속자: %s"), *GetNameSafe(NewPlayer));
	UE_LOG(LogTemp, Warning, TEXT("│ NetMode: %d"), static_cast<int32>(GetNetMode()));
	UE_LOG(LogTemp, Warning, TEXT("├────────────────────────────────────────────────────────────┤"));
	UE_LOG(LogTemp, Warning, TEXT("│ ※ 자동 맵 이동 없음!                                      │"));
	UE_LOG(LogTemp, Warning, TEXT("│ ※ UI에서 버튼 클릭 시에만 이동                            │"));
	UE_LOG(LogTemp, Warning, TEXT("└────────────────────────────────────────────────────────────┘"));
	UE_LOG(LogTemp, Warning, TEXT(""));
}

bool AHellunaLoginGameMode::IsPlayerLoggedIn(const FString& PlayerId) const
{
	if (UMDF_GameInstance* GI = Cast<UMDF_GameInstance>(UGameplayStatics::GetGameInstance(GetWorld())))
	{
		return GI->IsPlayerLoggedIn(PlayerId);
	}
	return false;
}

void AHellunaLoginGameMode::TravelToGameMap()
{
	// ============================================
	// 📌 [Phase B] 게임 맵으로 ServerTravel
	// 호스트가 "서버 시작" 버튼 클릭 시 호출됨
	// ============================================
	
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║         [LoginGameMode] TravelToGameMap 호출됨!            ║"));
	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));

	if (GameMap.IsNull())
	{
		UE_LOG(LogTemp, Error, TEXT(""));
		UE_LOG(LogTemp, Error, TEXT("┌────────────────────────────────────────────────────────────┐"));
		UE_LOG(LogTemp, Error, TEXT("│ ❌ GameMap이 설정되지 않았습니다!                         │"));
		UE_LOG(LogTemp, Error, TEXT("│ BP_HellunaLoginGameMode에서 'Game Map' 설정 필요!         │"));
		UE_LOG(LogTemp, Error, TEXT("└────────────────────────────────────────────────────────────┘"));
		UE_LOG(LogTemp, Error, TEXT(""));
		
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 10.f, FColor::Red,
				TEXT("❌ GameMap이 설정되지 않았습니다! BP_HellunaLoginGameMode에서 설정 필요"));
		}
		return;
	}

	FString MapPath = GameMap.GetLongPackageName();
	FString TravelURL = FString::Printf(TEXT("%s?listen"), *MapPath);
	
	UE_LOG(LogTemp, Warning, TEXT("┌────────────────────────────────────────────────────────────┐"));
	UE_LOG(LogTemp, Warning, TEXT("│ ★ ServerTravel 실행!                                       │"));
	UE_LOG(LogTemp, Warning, TEXT("│ 목적지: %s"), *TravelURL);
	UE_LOG(LogTemp, Warning, TEXT("│ ※ Listen 서버로 시작 (?listen)                            │"));
	UE_LOG(LogTemp, Warning, TEXT("└────────────────────────────────────────────────────────────┘"));
	UE_LOG(LogTemp, Warning, TEXT(""));
	
	GetWorld()->ServerTravel(TravelURL);
}
