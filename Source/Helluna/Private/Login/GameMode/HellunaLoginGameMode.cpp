#include "Login/GameMode/HellunaLoginGameMode.h"
#include "Helluna.h"  // 전처리기 플래그
#include "Login/Controller/HellunaServerConnectController.h"
#include "Login/Save/HellunaAccountSaveGame.h"
#include "Player/HellunaPlayerState.h"
#include "MDF_Function/MDF_Instance/MDF_GameInstance.h"
#include "Kismet/GameplayStatics.h"

AHellunaLoginGameMode::AHellunaLoginGameMode()
{
	PlayerControllerClass = AHellunaServerConnectController::StaticClass();
	PlayerStateClass = AHellunaPlayerState::StaticClass();
	DefaultPawnClass = nullptr;
	bUseSeamlessTravel = true;
}

void AHellunaLoginGameMode::BeginPlay()
{
	Super::BeginPlay();

	AccountSaveGame = UHellunaAccountSaveGame::LoadOrCreate();

#if HELLUNA_DEBUG_SERVERCONNECTION
	UE_LOG(LogHelluna, Warning, TEXT(""));
	UE_LOG(LogHelluna, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogHelluna, Warning, TEXT("║     [LoginGameMode] BeginPlay                              ║"));
	UE_LOG(LogHelluna, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogHelluna, Warning, TEXT("║ PlayerControllerClass: %s"), PlayerControllerClass ? *PlayerControllerClass->GetName() : TEXT("nullptr"));
	UE_LOG(LogHelluna, Warning, TEXT("║ GameMap: %s"), GameMap.IsNull() ? TEXT("미설정!") : *GameMap.GetAssetName());
	UE_LOG(LogHelluna, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogHelluna, Warning, TEXT("║ [사용법] (클라이언트 전용)                                 ║"));
	UE_LOG(LogHelluna, Warning, TEXT("║ • IP 빈칸 → '시작' 버튼 → 호스트로 서버 시작              ║"));
	UE_LOG(LogHelluna, Warning, TEXT("║ • IP 입력 → '접속' 버튼 → 클라이언트로 서버 접속          ║"));
	UE_LOG(LogHelluna, Warning, TEXT("║                                                            ║"));
	UE_LOG(LogHelluna, Warning, TEXT("║ ※ Dedicated Server는 GihyeonMap에서 바로 시작합니다      ║"));
	UE_LOG(LogHelluna, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
	UE_LOG(LogHelluna, Warning, TEXT(""));
#endif
}

void AHellunaLoginGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);

#if HELLUNA_DEBUG_SERVERCONNECTION
	UE_LOG(LogHelluna, Warning, TEXT(""));
	UE_LOG(LogHelluna, Warning, TEXT("┌────────────────────────────────────────────────────────────┐"));
	UE_LOG(LogHelluna, Warning, TEXT("│ [LoginGameMode] PostLogin                                  │"));
	UE_LOG(LogHelluna, Warning, TEXT("├────────────────────────────────────────────────────────────┤"));
	UE_LOG(LogHelluna, Warning, TEXT("│ Controller: %s"), *GetNameSafe(NewPlayer));
	UE_LOG(LogHelluna, Warning, TEXT("│ ControllerClass: %s"), NewPlayer ? *NewPlayer->GetClass()->GetName() : TEXT("nullptr"));
	UE_LOG(LogHelluna, Warning, TEXT("│ ※ 자동 맵 이동 없음! UI에서 버튼 클릭 필요               │"));
	UE_LOG(LogHelluna, Warning, TEXT("└────────────────────────────────────────────────────────────┘"));
	UE_LOG(LogHelluna, Warning, TEXT(""));
#endif
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
#if HELLUNA_DEBUG_SERVERCONNECTION
	UE_LOG(LogHelluna, Warning, TEXT(""));
	UE_LOG(LogHelluna, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogHelluna, Warning, TEXT("║     [LoginGameMode] TravelToGameMap                        ║"));
	UE_LOG(LogHelluna, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
#endif

	if (GameMap.IsNull())
	{
		UE_LOG(LogHelluna, Error, TEXT("[LoginGameMode] GameMap 미설정! BP에서 설정 필요"));
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 10.f, FColor::Red,
				TEXT("GameMap 미설정! BP_LoginGameMode에서 설정 필요"));
		}
		return;
	}

	FString MapPath = GameMap.GetLongPackageName();
	FString TravelURL = FString::Printf(TEXT("%s?listen"), *MapPath);

#if HELLUNA_DEBUG_SERVERCONNECTION
	UE_LOG(LogHelluna, Warning, TEXT("[LoginGameMode] ServerTravel: %s"), *TravelURL);
#endif

	GetWorld()->ServerTravel(TravelURL);

#if HELLUNA_DEBUG_SERVERCONNECTION
	UE_LOG(LogHelluna, Warning, TEXT(""));
#endif
}
