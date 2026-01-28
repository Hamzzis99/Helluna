#include "Login/HellunaLoginGameMode.h"
#include "Login/HellunaServerConnectController.h"
#include "Login/HellunaAccountSaveGame.h"
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

	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║     [LoginGameMode] BeginPlay                              ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ PlayerControllerClass: %s"), PlayerControllerClass ? *PlayerControllerClass->GetName() : TEXT("nullptr"));
	UE_LOG(LogTemp, Warning, TEXT("║ GameMap: %s"), GameMap.IsNull() ? TEXT("미설정!") : *GameMap.GetAssetName());
	UE_LOG(LogTemp, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ [사용법]                                                   ║"));
	UE_LOG(LogTemp, Warning, TEXT("║ • IP 빈칸 → '시작' 버튼 → 호스트로 서버 시작              ║"));
	UE_LOG(LogTemp, Warning, TEXT("║ • IP 입력 → '접속' 버튼 → 클라이언트로 서버 접속          ║"));
	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
	UE_LOG(LogTemp, Warning, TEXT(""));
}

void AHellunaLoginGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);

	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("┌────────────────────────────────────────────────────────────┐"));
	UE_LOG(LogTemp, Warning, TEXT("│ [LoginGameMode] PostLogin                                  │"));
	UE_LOG(LogTemp, Warning, TEXT("├────────────────────────────────────────────────────────────┤"));
	UE_LOG(LogTemp, Warning, TEXT("│ Controller: %s"), *GetNameSafe(NewPlayer));
	UE_LOG(LogTemp, Warning, TEXT("│ ControllerClass: %s"), NewPlayer ? *NewPlayer->GetClass()->GetName() : TEXT("nullptr"));
	UE_LOG(LogTemp, Warning, TEXT("│ ※ 자동 맵 이동 없음! UI에서 버튼 클릭 필요               │"));
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
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║     [LoginGameMode] TravelToGameMap                        ║"));
	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));

	if (GameMap.IsNull())
	{
		UE_LOG(LogTemp, Error, TEXT("[LoginGameMode] GameMap 미설정! BP에서 설정 필요"));
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 10.f, FColor::Red,
				TEXT("GameMap 미설정! BP_LoginGameMode에서 설정 필요"));
		}
		return;
	}

	FString MapPath = GameMap.GetLongPackageName();
	FString TravelURL = FString::Printf(TEXT("%s?listen"), *MapPath);
	
	UE_LOG(LogTemp, Warning, TEXT("[LoginGameMode] ServerTravel: %s"), *TravelURL);
	
	GetWorld()->ServerTravel(TravelURL);

	UE_LOG(LogTemp, Warning, TEXT(""));
}
