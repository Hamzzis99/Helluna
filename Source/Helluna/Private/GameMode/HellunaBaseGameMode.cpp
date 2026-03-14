// ════════════════════════════════════════════════════════════════════════════════
// HellunaBaseGameMode.cpp
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 이 파일의 역할:
//    로그인/인벤토리 시스템을 담당하는 Base GameMode
//    모든 게임 관련 GameMode의 부모 클래스
//
// 📌 주요 시스템:
//    🔐 로그인: PostLogin, ProcessLogin, OnLoginSuccess, SwapToGameController
//    🎭 캐릭터 선택: ProcessCharacterSelection, RegisterCharacterUse
//    📦 인벤토리: SaveAllPlayersInventory, LoadAndSendInventoryToClient
//
// 📌 상속 구조:
//    AGameMode → AHellunaBaseGameMode → AHellunaDefenseGameMode (게임 로직)
//
// 📌 저장 파일 위치:
//    - 계정 정보: Saved/SaveGames/HellunaAccounts.sav
//    - 인벤토리: Saved/SaveGames/HellunaInventory.sav
//
// 📌 작성자: Gihyeon
// ════════════════════════════════════════════════════════════════════════════════

#include "GameMode/HellunaBaseGameMode.h"
#include "GameMode/HellunaDefenseGameMode.h"  // [Phase 14] 재접속 분기
#include "Helluna.h"  // 전처리기 플래그
#include "GameMode/HellunaBaseGameState.h"
#include "Login/Controller/HellunaLoginController.h"
#include "Login/Save/HellunaAccountSaveGame.h"
#include "Player/HellunaPlayerState.h"
#include "Inventory/HellunaItemTypeMapping.h"
#include "MDF_Function/MDF_Instance/MDF_GameInstance.h"
#include "Player/Inv_PlayerController.h"
#include "InventoryManagement/Components/Inv_InventoryComponent.h"
#include "EquipmentManagement/Components/Inv_EquipmentComponent.h"
#include "EquipmentManagement/EquipActor/Inv_EquipActor.h"
#include "Items/Components/Inv_ItemComponent.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/SpectatorPawn.h"
#include "Engine/DataTable.h"
#include "GameplayTagContainer.h"
#include "debughelper.h"

// [투표 시스템] 플레이어 퇴장 시 투표 처리 (김기현)
#include "Utils/Vote/VoteManagerComponent.h"

// [Phase 3] SQLite 저장/로드 전환
#include "Lobby/Database/HellunaSQLiteSubsystem.h"

// ════════════════════════════════════════════════════════════════════════════════
// 📌 팀원 가이드 - 이 파일 전체 구조
// ════════════════════════════════════════════════════════════════════════════════
//
// ⚠️ 주의: 로그인/인벤토리 시스템은 복잡하게 연결되어 있습니다!
//          수정 전 반드시 아래 흐름도를 이해하세요!
//
// ════════════════════════════════════════════════════════════════════════════════
// 📌 플레이어 접속 ~ 게임 시작 전체 흐름
// ════════════════════════════════════════════════════════════════════════════════
//
//   [1] 플레이어 접속
//          ↓
//   [2] PostLogin() ← 엔진이 자동 호출
//          │
//          ├─→ 이미 로그인됨? (SeamlessTravel)
//          │         ↓ YES
//          │   SwapToGameController() 또는 SpawnHeroCharacter()
//          │
//          └─→ 로그인 필요?
//                    ↓ YES
//              타임아웃 타이머 시작 (60초)
//              LoginController.BeginPlay()에서 로그인 UI 표시
//                    ↓
//   [3] 로그인 버튼 클릭
//          ↓
//   [4] ProcessLogin() ← LoginController.Server_RequestLogin() RPC에서 호출
//          │
//          ├─→ 동시 접속? → OnLoginFailed("이미 접속 중")
//          │
//          ├─→ 계정 있음? → 비밀번호 검증
//          │         │
//          │         ├─→ 일치 → OnLoginSuccess()
//          │         └─→ 불일치 → OnLoginFailed("비밀번호 확인")
//          │
//          └─→ 계정 없음? → 새 계정 생성 → OnLoginSuccess()
//                    ↓
//   [5] OnLoginSuccess()
//          │
//          ├─→ GameInstance.RegisterLogin() - 접속자 목록에 추가
//          ├─→ PlayerState.SetLoginInfo() - PlayerId 저장
//          ├─→ Client_LoginResult(true) - 클라이언트에 성공 알림
//          └─→ Client_ShowCharacterSelectUI() - 캐릭터 선택 UI 표시
//                    ↓
//   [6] 캐릭터 선택 버튼 클릭
//          ↓
//   [7] ProcessCharacterSelection() ← LoginController.Server_SelectCharacter() RPC에서 호출
//          │
//          ├─→ 이미 사용 중? → Client_CharacterSelectionResult(false)
//          │
//          └─→ 사용 가능? → RegisterCharacterUse() → UsedCharacterMap에 등록
//                    ↓
//   [8] SwapToGameController()
//          │
//          ├─→ 새 GameController 스폰 (BP_InvPlayerController)
//          ├─→ Client_PrepareControllerSwap() - 로그인 UI 숨김
//          └─→ SwapPlayerControllers() - 안전한 교체
//                    ↓
//   [9] SpawnHeroCharacter()
//          │
//          ├─→ HeroCharacterMap에서 캐릭터 클래스 찾기
//          ├─→ 캐릭터 스폰 및 Possess
//          └─→ InitializeGame() ⭐ (첫 플레이어일 때만, DefenseGameMode에서 override)
//                    ↓
//   [10] LoadAndSendInventoryToClient() - 저장된 인벤토리 로드
//          ↓
//        🎮 게임 시작!
//
// ════════════════════════════════════════════════════════════════════════════════
// 📌 인벤토리 저장 시점
// ════════════════════════════════════════════════════════════════════════════════
//
//   ✅ 자동 저장 (5분마다)
//      OnAutoSaveTimer() → RequestAllPlayersInventoryState()
//                               ↓
//      클라이언트가 Server_SendInventoryState() RPC로 응답
//                               ↓
//      OnPlayerInventoryStateReceived() → InventorySaveGame에 저장
//
//   ✅ 로그아웃 시
//      Logout() → 인벤토리 수집 → SaveInventoryFromCharacterEndPlay()
//
//   ✅ 맵 이동 전
//      (외부에서 호출) SaveAllPlayersInventory()
//
//   ✅ Controller EndPlay 시
//      OnInvControllerEndPlay() → SaveInventoryFromCharacterEndPlay()
//
// ════════════════════════════════════════════════════════════════════════════════

// ════════════════════════════════════════════════════════════════════════════════
// 🏗️ 생성자 & 초기화
// ════════════════════════════════════════════════════════════════════════════════

// ════════════════════════════════════════════════════════════════════════════════
// 📌 AHellunaBaseGameMode - 생성자
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 역할:
//    GameMode의 기본 클래스들을 설정하는 생성자
//
// 📌 설정되는 클래스들:
//    - PlayerStateClass: AHellunaPlayerState (플레이어 정보 저장)
//    - PlayerControllerClass: AHellunaLoginController (로그인 화면용 컨트롤러)
//    - DefaultPawnClass: ASpectatorPawn (로그인 전 관전 모드)
//
// 📌 중요 설정:
//    - bUseSeamlessTravel = true: 맵 이동 시 연결 끊김 방지
//    - PrimaryActorTick.bCanEverTick = false: Tick 비활성화 (성능 최적화)
//
// ⚠️ 주의:
//    PlayerControllerClass가 LoginController로 설정되어 있어서
//    플레이어 접속 시 자동으로 로그인 UI가 표시됩니다!
//
// ════════════════════════════════════════════════════════════════════════════════
AHellunaBaseGameMode::AHellunaBaseGameMode()
{
	PrimaryActorTick.bCanEverTick = false;
	bUseSeamlessTravel = true;
	PlayerStateClass = AHellunaPlayerState::StaticClass();
	PlayerControllerClass = AHellunaLoginController::StaticClass();  // ⭐ 기존처럼 C++에서 직접 설정!
	DefaultPawnClass = ASpectatorPawn::StaticClass();

	// Phase 3: SaveAllPlayersInventoryDirect가 SaveCollectedItems를 우회하므로
	// Direct 경로를 비활성화하여 모든 자동저장이 SQLite 경로(SaveCollectedItems)를 타도록 강제
	bUseServerDirectSave = false;
}

// ════════════════════════════════════════════════════════════════════════════════
// 📌 BeginPlay - 서버 초기화
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 호출 시점:
//    맵 로드 완료 후 엔진이 자동 호출
//
// 📌 처리 흐름:
//    1. 서버 권한 체크 (HasAuthority)
//    2. AccountSaveGame 로드 (계정 정보)
//    3. InventorySaveGame 로드 (인벤토리 정보)
//    4. 자동저장 타이머 시작 (5분 주기)
//
// 📌 SaveGame 로드 위치:
//    - AccountSaveGame: Saved/SaveGames/HellunaAccounts.sav
//    - InventorySaveGame: Saved/SaveGames/HellunaInventory.sav
//
// ⚠️ 주의:
//    클라이언트에서는 실행되지 않음! (HasAuthority 체크)
//
// ════════════════════════════════════════════════════════════════════════════════
void AHellunaBaseGameMode::BeginPlay()
{
	Super::BeginPlay();

	if (!HasAuthority())
		return;

	AccountSaveGame = UHellunaAccountSaveGame::LoadOrCreate();

#if HELLUNA_DEBUG_GAMEMODE
	UE_LOG(LogHelluna, Warning, TEXT(""));
	UE_LOG(LogHelluna, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogHelluna, Warning, TEXT("║     [BaseGameMode] BeginPlay                               ║"));
	UE_LOG(LogHelluna, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogHelluna, Warning, TEXT("║ PlayerControllerClass: %s"), PlayerControllerClass ? *PlayerControllerClass->GetName() : TEXT("nullptr"));
	UE_LOG(LogHelluna, Warning, TEXT("║ PlayerStateClass: %s"), PlayerStateClass ? *PlayerStateClass->GetName() : TEXT("nullptr"));
	UE_LOG(LogHelluna, Warning, TEXT("║ DefaultPawnClass: %s"), DefaultPawnClass ? *DefaultPawnClass->GetName() : TEXT("nullptr"));
	UE_LOG(LogHelluna, Warning, TEXT("║ HeroCharacterClass: %s"), HeroCharacterClass ? *HeroCharacterClass->GetName() : TEXT("미설정!"));
	UE_LOG(LogHelluna, Warning, TEXT("║ AccountCount: %d"), AccountSaveGame ? AccountSaveGame->GetAccountCount() : 0);
	UE_LOG(LogHelluna, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogHelluna, Warning, TEXT("║ HeroCharacterMap: %d개 매핑됨"), HeroCharacterMap.Num());
	UE_LOG(LogHelluna, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
	UE_LOG(LogHelluna, Warning, TEXT(""));
#endif

#if WITH_EDITOR
	if (IsValid(ItemTypeMappingDataTable))
	{
		DebugTestItemTypeMapping();
	}
#endif

}

// ════════════════════════════════════════════════════════════════════════════════
// 📌 EndPlay — 부모(AInv_SaveGameMode)에게 인벤토리 강제저장 위임
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 부모(AInv_SaveGameMode::EndPlay)가 처리하는 것:
//    - 리슨서버 종료 감지 시 SaveAllPlayersInventory() 자동 호출
//    - 자동저장 타이머 정리 (StopAutoSave)
//
// 📌 이 클래스에서 추가할 것이 없으면 Super::EndPlay()만 호출
//
// ════════════════════════════════════════════════════════════════════════════════
void AHellunaBaseGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 타이머 전부 정리 (fire-and-forget 타이머 포함)
	GetWorldTimerManager().ClearAllTimersForObject(this);

	// [Fix26] 람다 기반 타이머는 ClearAllTimersForObject로 해제되지 않으므로 개별 해제
	for (FTimerHandle& Handle : LambdaTimerHandles)
	{
		GetWorldTimerManager().ClearTimer(Handle);
	}
	LambdaTimerHandles.Empty();

	// 캐시 맵 정리
	PreCachedInventoryMap.Empty();
	PendingLobbyDeployMap.Empty();
	LoginTimeoutTimers.Empty();

	Super::EndPlay(EndPlayReason);
}

// ════════════════════════════════════════════════════════════════════════════════
// 📌 InitializeGame - 게임 초기화 (Virtual)
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 호출 시점:
//    SpawnHeroCharacter()에서 첫 번째 플레이어 캐릭터 소환 시
//
// 📌 역할:
//    게임 시작 시 필요한 초기화 수행
//    → BaseGameMode에서는 빈 구현 (로그만 출력)
//    → DefenseGameMode에서 override하여 웨이브 시스템 등 초기화
//
// ⚠️ 주의:
//    bGameInitialized는 이 함수 내부에서 true로 설정해야 함!
//    SpawnHeroCharacter()에서 미리 설정하면 자식 클래스의 InitializeGame()이 스킵됨!
//
// ════════════════════════════════════════════════════════════════════════════════
void AHellunaBaseGameMode::InitializeGame()
{
#if HELLUNA_DEBUG_GAMEMODE
	UE_LOG(LogHelluna, Warning, TEXT("[BaseGameMode] InitializeGame - 기본 구현 (override 필요)"));
#endif
}

// ════════════════════════════════════════════════════════════════════════════════
// 🔐 로그인 시스템
// ════════════════════════════════════════════════════════════════════════════════

// ════════════════════════════════════════════════════════════════════════════════
// 📌 [Phase 6] InitNewPlayer — URL Options에서 로비 배포 정보 파싱
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 호출 시점:
//    플레이어가 서버에 접속할 때 PostLogin보다 먼저 호출됨 (엔진 내부)
//
// 📌 역할:
//    ClientTravel URL의 Options에서 PlayerId & HeroType 파싱
//    → 둘 다 존재하면 PendingLobbyDeployMap에 등록
//    → PostLogin에서 이 맵을 확인하여 로비 배포 분기 처리
//
// ════════════════════════════════════════════════════════════════════════════════
bool AHellunaBaseGameMode::ShouldEnforceLobbyDeployAdmission() const
{
	// bDebugSkipLogin 시 deploy options 검증 비활성화 → PostLogin에서 디버그 흐름 처리
	return !bDebugSkipLogin && GetNetMode() == NM_DedicatedServer && Cast<AHellunaDefenseGameMode>(this) != nullptr;
}

bool AHellunaBaseGameMode::ParseLobbyDeployOptions(const FString& Options, FString& OutPlayerId, int32& OutHeroTypeIndex) const
{
	OutPlayerId = UGameplayStatics::ParseOption(Options, TEXT("PlayerId"));
	const FString OptionHeroType = UGameplayStatics::ParseOption(Options, TEXT("HeroType"));
	OutHeroTypeIndex = INDEX_NONE;

	if (OutPlayerId.IsEmpty() || OutPlayerId.Len() > 64)
	{
		return false;
	}

	if (OptionHeroType.IsEmpty() || !OptionHeroType.IsNumeric())
	{
		return false;
	}

	OutHeroTypeIndex = FCString::Atoi(*OptionHeroType);
	return AHellunaBaseGameMode::IndexToHeroType(OutHeroTypeIndex) != EHellunaHeroType::None;
}

bool AHellunaBaseGameMode::ValidateLobbyDeployAdmission(const FString& PlayerId, int32 HeroTypeIndex, FString& OutErrorMessage) const
{
	OutErrorMessage.Reset();

	if (PlayerId.IsEmpty() || PlayerId.Len() > 64)
	{
		OutErrorMessage = TEXT("INVALID_PLAYER_ID");
		return false;
	}

	const EHellunaHeroType ParsedHeroType = AHellunaBaseGameMode::IndexToHeroType(HeroTypeIndex);
	if (ParsedHeroType == EHellunaHeroType::None)
	{
		OutErrorMessage = TEXT("INVALID_HERO_TYPE");
		return false;
	}

	const UWorld* World = GetWorld();
	const int32 CurrentServerPort = World ? World->URL.Port : 0;
	if (CurrentServerPort <= 0)
	{
		OutErrorMessage = TEXT("SERVER_PORT_UNAVAILABLE");
		return false;
	}

	UE_LOG(LogHelluna, Verbose,
		TEXT("[DeployGate] ValidateLobbyDeployAdmission passed | PlayerId=%s | HeroType=%d | ServerPort=%d"),
		*PlayerId, HeroTypeIndex, CurrentServerPort);

	return true;
}

void AHellunaBaseGameMode::PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage)
{
	Super::PreLogin(Options, Address, UniqueId, ErrorMessage);
	if (!ErrorMessage.IsEmpty())
	{
		return;
	}

	if (!ShouldEnforceLobbyDeployAdmission())
	{
		return;
	}

	FString PlayerId;
	int32 HeroTypeIndex = INDEX_NONE;
	if (!ParseLobbyDeployOptions(Options, PlayerId, HeroTypeIndex))
	{
		ErrorMessage = TEXT("INVALID_DEPLOY_OPTIONS");
		UE_LOG(LogHelluna, Warning, TEXT("[DeployGate] PreLogin rejected | Reason=%s | Address=%s"), *ErrorMessage, *Address);
		return;
	}

	UE_LOG(LogHelluna, Log, TEXT("[DeployGate] PreLogin accepted | Address=%s | PlayerId=%s | HeroType=%d"),
		*Address, *PlayerId, HeroTypeIndex);
}

FString AHellunaBaseGameMode::InitNewPlayer(APlayerController* NewPlayerController,
	const FUniqueNetIdRepl& UniqueId, const FString& Options,
	const FString& Portal)
{
	FString ErrorMessage = Super::InitNewPlayer(NewPlayerController, UniqueId, Options, Portal);
	if (!ErrorMessage.IsEmpty())
	{
		return ErrorMessage;
	}

	FString OptionPlayerId;
	int32 OptionHeroTypeIndex = INDEX_NONE;
	const bool bHasValidDeployOptions = ParseLobbyDeployOptions(Options, OptionPlayerId, OptionHeroTypeIndex);

	if (ShouldEnforceLobbyDeployAdmission())
	{
		if (!bHasValidDeployOptions)
		{
			ErrorMessage = TEXT("INVALID_DEPLOY_OPTIONS");
			UE_LOG(LogHelluna, Warning, TEXT("[DeployGate] InitNewPlayer rejected | Reason=%s"), *ErrorMessage);
			return ErrorMessage;
		}

		FString AdmissionError;
		if (!ValidateLobbyDeployAdmission(OptionPlayerId, OptionHeroTypeIndex, AdmissionError))
		{
			ErrorMessage = AdmissionError.IsEmpty() ? TEXT("DEPLOY_ADMISSION_FAILED") : AdmissionError;
			const int32 CurrentServerPort = GetWorld() ? GetWorld()->URL.Port : 0;
			UE_LOG(LogHelluna, Warning,
				TEXT("[DeployGate] InitNewPlayer rejected | Reason=%s | PlayerId=%s | HeroType=%d | ServerPort=%d"),
				*ErrorMessage, *OptionPlayerId, OptionHeroTypeIndex, CurrentServerPort);
			return ErrorMessage;
		}
	}

	if (bHasValidDeployOptions)
	{
		FLobbyDeployInfo DeployInfo;
		DeployInfo.PlayerId = OptionPlayerId;
		DeployInfo.HeroType = IndexToHeroType(OptionHeroTypeIndex);

		PendingLobbyDeployMap.Add(NewPlayerController, DeployInfo);

		const int32 CurrentServerPort = GetWorld() ? GetWorld()->URL.Port : 0;
		UE_LOG(LogHelluna, Log,
			TEXT("[DeployGate] InitNewPlayer accepted | PlayerId=%s | HeroType=%d | ServerPort=%d"),
			*OptionPlayerId, OptionHeroTypeIndex, CurrentServerPort);
	}

	return ErrorMessage;
}

// ════════════════════════════════════════════════════════════════════════════════
// 📌 PostLogin - 플레이어 접속 시 호출
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 호출 시점:
//    플레이어가 서버에 접속했을 때 (엔진 자동 호출)
//
// 📌 매개변수:
//    - NewPlayer: 접속한 플레이어의 PlayerController
//
// 📌 처리 흐름:
//    1. PlayerState 확인 및 로그 출력
//    2. 이미 로그인됨? (SeamlessTravel로 맵 이동 후 재접속한 경우)
//       → YES: 0.5초 후 SwapToGameController() 또는 SpawnHeroCharacter()
//       → NO: 로그인 타임아웃 타이머 시작 (기본 60초)
//    3. LoginController.BeginPlay()에서 로그인 UI 자동 표시
//
// 📌 타임아웃 처리:
//    - LoginTimeoutSeconds(기본 60초) 내에 로그인하지 않으면
//    - OnLoginTimeout() 호출 → 플레이어 킥
//
// ⚠️ 주의:
//    SeamlessTravel 시 PlayerState의 로그인 정보가 유지되어
//    자동으로 GameController로 전환됩니다!
//
// ════════════════════════════════════════════════════════════════════════════════
void AHellunaBaseGameMode::PostLogin(APlayerController* NewPlayer)
{
#if HELLUNA_DEBUG_GAMEMODE
	UE_LOG(LogHelluna, Warning, TEXT(""));
	UE_LOG(LogHelluna, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogHelluna, Warning, TEXT("║     [BaseGameMode] PostLogin                               ║"));
	UE_LOG(LogHelluna, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogHelluna, Warning, TEXT("║ Controller: %s"), *GetNameSafe(NewPlayer));
	UE_LOG(LogHelluna, Warning, TEXT("║ ControllerClass: %s"), NewPlayer ? *NewPlayer->GetClass()->GetName() : TEXT("nullptr"));
	UE_LOG(LogHelluna, Warning, TEXT("║ GameInitialized: %s"), bGameInitialized ? TEXT("TRUE") : TEXT("FALSE"));

	if (NewPlayer)
	{
		AHellunaPlayerState* PS = NewPlayer->GetPlayerState<AHellunaPlayerState>();
		UE_LOG(LogHelluna, Warning, TEXT("║ PlayerState: %s"), PS ? *PS->GetName() : TEXT("nullptr"));
		if (PS)
		{
			UE_LOG(LogHelluna, Warning, TEXT("║   - PlayerId: '%s'"), *PS->GetPlayerUniqueId());
			UE_LOG(LogHelluna, Warning, TEXT("║   - IsLoggedIn: %s"), PS->IsLoggedIn() ? TEXT("TRUE") : TEXT("FALSE"));
		}
	}
	UE_LOG(LogHelluna, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
#endif

	if (!NewPlayer)
	{
		Super::PostLogin(NewPlayer);
		return;
	}

	AHellunaPlayerState* PS = NewPlayer->GetPlayerState<AHellunaPlayerState>();

	// ────────────────────────────────────────────────────────────────────────────
	// 📌 [Phase 6] 로비 배포 분기 — InitNewPlayer에서 등록된 플레이어
	// ────────────────────────────────────────────────────────────────────────────
	// 로비에서 출격하여 ClientTravel로 접속한 경우
	// → 로그인/캐릭터선택 UI 스킵, Loadout에서 인벤토리 로드
	// ────────────────────────────────────────────────────────────────────────────
	if (FLobbyDeployInfo* DeployInfo = PendingLobbyDeployMap.Find(NewPlayer))
	{
		UE_LOG(LogHelluna, Warning, TEXT("[Phase6] PostLogin — 로비 배포 감지! PlayerId=%s, HeroType=%d"),
			*DeployInfo->PlayerId, static_cast<int32>(DeployInfo->HeroType));

		const FString DeployPlayerId = DeployInfo->PlayerId;
		const EHellunaHeroType DeployHeroType = DeployInfo->HeroType;
		PendingLobbyDeployMap.Remove(NewPlayer);

		// 1. PlayerState 설정
		if (PS)
		{
			PS->SetLoginInfo(DeployPlayerId);
			PS->SetSelectedHeroType(DeployHeroType);
		}

		// 2. GameInstance에 로그인 등록
		if (UMDF_GameInstance* GI = Cast<UMDF_GameInstance>(UGameplayStatics::GetGameInstance(GetWorld())))
		{
			GI->RegisterLogin(DeployPlayerId);
		}

		// 3. 캐릭터 사용 등록
		RegisterCharacterUse(DeployHeroType, DeployPlayerId);

		// 4. Controller → PlayerId 매핑 등록
		RegisterControllerPlayerId(NewPlayer, DeployPlayerId);

		// [Phase 14] 재접속 체크 — DisconnectedPlayers에 있으면 상태 복원
		{
			AHellunaDefenseGameMode* DefenseGM = Cast<AHellunaDefenseGameMode>(this);
			if (DefenseGM && DefenseGM->HasDisconnectedPlayer(DeployPlayerId))
			{
				UE_LOG(LogHelluna, Warning, TEXT("[Phase14] PostLogin — 재접속 감지! → RestoreReconnectedPlayer | PlayerId=%s"), *DeployPlayerId);

				AInv_PlayerController* InvPC = Cast<AInv_PlayerController>(NewPlayer);
				if (IsValid(InvPC))
				{
					InvPC->OnControllerEndPlay.AddDynamic(this, &AHellunaBaseGameMode::OnInvControllerEndPlay);
				}

				DefenseGM->RestoreReconnectedPlayer(NewPlayer, DeployPlayerId);

				Super::PostLogin(NewPlayer);
				return;
			}
		}

		// Phase 6: 크래시 복구 체크 (이전 게임 세션의 Loadout 잔존 → Stash 복귀)
		CheckAndRecoverFromCrash(DeployPlayerId);

		// 4.5 인벤토리 사전 로드 (디스크 I/O를 스폰 전에 완료)
		PreCacheInventoryForPlayer(DeployPlayerId);

		// 5. SwapToGameController → SpawnHeroCharacter → LoadAndSendInventoryToClient
		AHellunaLoginController* LC = Cast<AHellunaLoginController>(NewPlayer);
		if (LC && LC->GetGameControllerClass())
		{
			// [Fix26] 로컬 핸들 → LambdaTimerHandles 등록 (EndPlay에서 해제)
			FTimerHandle& SwapTimer = LambdaTimerHandles.AddDefaulted_GetRef();
			GetWorldTimerManager().SetTimer(SwapTimer, [this, LC, DeployPlayerId, DeployHeroType]()
			{
				if (IsValid(LC))
				{
					SwapToGameController(LC, DeployPlayerId, DeployHeroType);
				}
			}, 0.5f, false);
		}
		else
		{
			// LoginController가 아닌 경우 (직접 GameController로 접속) → 바로 스폰
			AInv_PlayerController* InvPC = Cast<AInv_PlayerController>(NewPlayer);
			if (IsValid(InvPC))
			{
				InvPC->OnControllerEndPlay.AddDynamic(this, &AHellunaBaseGameMode::OnInvControllerEndPlay);
			}

			if (!bGameInitialized)
			{
				InitializeGame();
			}

			// [Fix26] 로컬 핸들 → LambdaTimerHandles 등록
			FTimerHandle& SpawnTimer = LambdaTimerHandles.AddDefaulted_GetRef();
			GetWorldTimerManager().SetTimer(SpawnTimer, [this, NewPlayer]()
			{
				if (IsValid(NewPlayer))
				{
					SpawnHeroCharacter(NewPlayer);
				}
			}, 0.3f, false);
		}

		Super::PostLogin(NewPlayer);
		return;
	}

	// ────────────────────────────────────────────────────────────────────────────
	// 📌 이미 로그인된 상태 (SeamlessTravel 후 재접속)
	// ────────────────────────────────────────────────────────────────────────────
	// SeamlessTravel로 맵 이동 시 PlayerState의 로그인 정보가 유지됨
	// → 로그인 과정 생략하고 바로 게임 컨트롤러로 전환
	// ────────────────────────────────────────────────────────────────────────────
	if (PS && PS->IsLoggedIn() && !PS->GetPlayerUniqueId().IsEmpty())
	{
#if HELLUNA_DEBUG_GAMEMODE
		UE_LOG(LogHelluna, Warning, TEXT("[BaseGameMode] 이미 로그인됨! → Controller 확인 후 처리"));
#endif
		FString PlayerId = PS->GetPlayerUniqueId();

		// 인벤토리 사전 로드 (디스크 I/O를 스폰 전에 완료)
		PreCacheInventoryForPlayer(PlayerId);

		// 0.5초 딜레이: Controller 초기화 완료 대기
		// [Fix26] 로컬 핸들 → LambdaTimerHandles 등록
		FTimerHandle& TimerHandle = LambdaTimerHandles.AddDefaulted_GetRef();
		GetWorldTimerManager().SetTimer(TimerHandle, [this, NewPlayer, PlayerId]()
		{
			if (IsValid(NewPlayer))
			{
				AHellunaLoginController* LoginController = Cast<AHellunaLoginController>(NewPlayer);
				if (LoginController && LoginController->GetGameControllerClass())
				{
					int32 CharIdx = -1;
					if (AHellunaPlayerState* TempPS = NewPlayer->GetPlayerState<AHellunaPlayerState>())
					{
						CharIdx = TempPS->GetSelectedCharacterIndex();
					}
					SwapToGameController(LoginController, PlayerId, IndexToHeroType(CharIdx));
				}
				else
				{
					SpawnHeroCharacter(NewPlayer);
				}
			}
		}, 0.5f, false);
	}
	// ────────────────────────────────────────────────────────────────────────────
	// 📌 개발자 모드: 로그인 스킵
	// ────────────────────────────────────────────────────────────────────────────
	// bDebugSkipLogin == true일 때:
	//   디버그 GUID 자동 부여 → 타임아웃 없이 바로 게임 시작
	//   OnLoginSuccess()가 하는 핵심 작업을 인라인으로 재현
	// ────────────────────────────────────────────────────────────────────────────
	else if (bDebugSkipLogin)
	{
		FString DebugPlayerId = FString::Printf(TEXT("DEBUG_%s"), *FGuid::NewGuid().ToString());

		UE_LOG(LogHelluna, Warning, TEXT(""));
		UE_LOG(LogHelluna, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
		UE_LOG(LogHelluna, Warning, TEXT("║  [BaseGameMode] 개발자 모드 - 로그인/캐릭터선택 스킵      ║"));
		UE_LOG(LogHelluna, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
		UE_LOG(LogHelluna, Warning, TEXT("║ DebugPlayerId: %s"), *DebugPlayerId);
		UE_LOG(LogHelluna, Warning, TEXT("║ DebugHeroType: %d"), static_cast<int32>(DebugHeroType));
		UE_LOG(LogHelluna, Warning, TEXT("║ Controller: %s"), *GetNameSafe(NewPlayer));
		UE_LOG(LogHelluna, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));

		// 1. PlayerState에 GUID + 캐릭터 타입 부여
		if (PS)
		{
			PS->SetLoginInfo(DebugPlayerId);
			PS->SetSelectedHeroType(DebugHeroType);
		}

		// 2. GameInstance에 로그인 등록
		if (UMDF_GameInstance* GI = Cast<UMDF_GameInstance>(UGameplayStatics::GetGameInstance(GetWorld())))
		{
			GI->RegisterLogin(DebugPlayerId);
		}

		// 3. 캐릭터 사용 등록
		RegisterCharacterUse(DebugHeroType, DebugPlayerId);

		// 4. ControllerToPlayerIdMap 등록 (Logout/인벤토리 저장 시 필요)
		RegisterControllerPlayerId(NewPlayer, DebugPlayerId);

		// 5. 크래시 복구 체크 (비정상 종료 시 Loadout → Stash 복구)
		CheckAndRecoverFromCrash(DebugPlayerId);

		// 6. 인벤토리 사전 로드 (디스크 I/O를 스폰 전에 완료)
		PreCacheInventoryForPlayer(DebugPlayerId);

		// 7. LoginController → GameController 스왑 또는 직접 스폰
		AHellunaLoginController* LC = Cast<AHellunaLoginController>(NewPlayer);
		if (LC && LC->GetGameControllerClass())
		{
			// LoginController인 경우: GameController로 스왑 → 스왑 내부에서 SpawnHeroCharacter 호출
			FTimerHandle& SwapTimer = LambdaTimerHandles.AddDefaulted_GetRef();
			GetWorldTimerManager().SetTimer(SwapTimer, [this, LC, DebugPlayerId, DebugHeroType = this->DebugHeroType]()
			{
				if (IsValid(LC))
				{
					SwapToGameController(LC, DebugPlayerId, DebugHeroType);
				}
			}, 0.5f, false);
		}
		else
		{
			// LoginController가 아닌 경우 (이미 GameController) → 직접 스폰
			AInv_PlayerController* InvPC = Cast<AInv_PlayerController>(NewPlayer);
			if (IsValid(InvPC))
			{
				InvPC->OnControllerEndPlay.AddDynamic(this, &AHellunaBaseGameMode::OnInvControllerEndPlay);
			}

			if (!bGameInitialized)
			{
				InitializeGame();
			}

			FTimerHandle& SpawnTimer = LambdaTimerHandles.AddDefaulted_GetRef();
			GetWorldTimerManager().SetTimer(SpawnTimer, [this, NewPlayer]()
			{
				if (IsValid(NewPlayer))
				{
					SpawnHeroCharacter(NewPlayer);
				}
			}, 0.3f, false);
		}

		// 타임아웃 타이머 시작하지 않음!
	}
	// ────────────────────────────────────────────────────────────────────────────
	// 📌 로그인 필요 (일반 접속)
	// ────────────────────────────────────────────────────────────────────────────
	// 타임아웃 타이머 시작 → 60초 내 로그인하지 않으면 킥
	// LoginController.BeginPlay()에서 로그인 UI 자동 표시됨
	// ────────────────────────────────────────────────────────────────────────────
	else
	{
#if HELLUNA_DEBUG_GAMEMODE
		UE_LOG(LogHelluna, Warning, TEXT("[BaseGameMode] 로그인 필요! 타임아웃: %.0f초"), LoginTimeoutSeconds);
#endif
		FTimerHandle& TimeoutTimer = LoginTimeoutTimers.FindOrAdd(NewPlayer);
		GetWorldTimerManager().SetTimer(TimeoutTimer, [this, NewPlayer]()
		{
			if (IsValid(NewPlayer))
			{
				OnLoginTimeout(NewPlayer);
			}
		}, LoginTimeoutSeconds, false);
	}

	Debug::Print(TEXT("[BaseGameMode] Login"), FColor::Yellow);
	Super::PostLogin(NewPlayer);
}

// ════════════════════════════════════════════════════════════════════════════════
// 📌 ProcessLogin - 로그인 처리 (아이디/비밀번호 검증)
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 호출 시점:
//    LoginController.Server_RequestLogin() RPC에서 호출
//    (클라이언트가 로그인 버튼 클릭 시)
//
// 📌 매개변수:
//    - PlayerController: 로그인 요청한 플레이어의 Controller
//    - PlayerId: 입력한 아이디
//    - Password: 입력한 비밀번호
//
// 📌 처리 흐름:
//    1. 서버 권한 체크 (HasAuthority)
//    2. 동시 접속 체크 (IsPlayerLoggedIn)
//       → 이미 접속 중이면 거부
//    3. 계정 존재 확인 (AccountSaveGame.HasAccount)
//       → 있으면: 비밀번호 검증
//          → 일치: OnLoginSuccess()
//          → 불일치: OnLoginFailed()
//       → 없으면: 새 계정 생성 → OnLoginSuccess()
//
// 📌 계정 저장 위치:
//    Saved/SaveGames/HellunaAccounts.sav
//
// ⚠️ 주의:
//    - 서버에서만 실행됨 (HasAuthority 체크)
//    - 비밀번호는 해시되어 저장됨 (AccountSaveGame 참조)
//
// ════════════════════════════════════════════════════════════════════════════════
void AHellunaBaseGameMode::ProcessLogin(APlayerController* PlayerController, const FString& PlayerId, const FString& Password)
{
	Debug::Print(TEXT("[BaseGameMode] ProcessLogin"), FColor::Yellow);

#if HELLUNA_DEBUG_LOGIN
	UE_LOG(LogHelluna, Warning, TEXT(""));
	UE_LOG(LogHelluna, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogHelluna, Warning, TEXT("║     [BaseGameMode] ProcessLogin                            ║"));
	UE_LOG(LogHelluna, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogHelluna, Warning, TEXT("║ PlayerId: '%s'"), *PlayerId);
	UE_LOG(LogHelluna, Warning, TEXT("║ Controller: %s"), *GetNameSafe(PlayerController));
	UE_LOG(LogHelluna, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
#endif

	// 서버 권한 체크
	if (!HasAuthority())
	{
		UE_LOG(LogHelluna, Error, TEXT("[BaseGameMode] 서버 권한 없음!"));
		return;
	}

	if (!PlayerController)
	{
		UE_LOG(LogHelluna, Error, TEXT("[BaseGameMode] PlayerController nullptr!"));
		return;
	}

	// ────────────────────────────────────────────────────────────────────────────
	// 📌 동시 접속 체크
	// ────────────────────────────────────────────────────────────────────────────
	// 같은 아이디로 이미 접속 중인 플레이어가 있으면 거부
	// GameInstance의 LoggedInPlayers TSet으로 관리됨
	// ────────────────────────────────────────────────────────────────────────────
	if (IsPlayerLoggedIn(PlayerId))
	{
#if HELLUNA_DEBUG_LOGIN
		UE_LOG(LogHelluna, Warning, TEXT("[BaseGameMode] 동시 접속 거부: '%s'"), *PlayerId);
#endif
		OnLoginFailed(PlayerController, TEXT("이미 접속 중인 계정입니다."));
		return;
	}

	if (!AccountSaveGame)
	{
		UE_LOG(LogHelluna, Error, TEXT("[BaseGameMode] AccountSaveGame nullptr!"));
		OnLoginFailed(PlayerController, TEXT("서버 오류"));
		return;
	}

	// ────────────────────────────────────────────────────────────────────────────
	// 📌 계정 존재 여부에 따른 분기
	// ────────────────────────────────────────────────────────────────────────────
	if (AccountSaveGame->HasAccount(PlayerId))
	{
		// 기존 계정: 비밀번호 검증
		if (AccountSaveGame->ValidatePassword(PlayerId, Password))
		{
#if HELLUNA_DEBUG_LOGIN
			UE_LOG(LogHelluna, Warning, TEXT("[BaseGameMode] 비밀번호 일치!"));
#endif
			OnLoginSuccess(PlayerController, PlayerId);
		}
		else
		{
#if HELLUNA_DEBUG_LOGIN
			UE_LOG(LogHelluna, Warning, TEXT("[BaseGameMode] 비밀번호 불일치!"));
#endif
			OnLoginFailed(PlayerController, TEXT("비밀번호를 확인해주세요."));
		}
	}
	else
	{
		// 새 계정: 자동 생성
		if (AccountSaveGame->CreateAccount(PlayerId, Password))
		{
			UHellunaAccountSaveGame::Save(AccountSaveGame);
#if HELLUNA_DEBUG_LOGIN
			UE_LOG(LogHelluna, Warning, TEXT("[BaseGameMode] 새 계정 생성: '%s'"), *PlayerId);
#endif
			OnLoginSuccess(PlayerController, PlayerId);
		}
		else
		{
			OnLoginFailed(PlayerController, TEXT("계정 생성 실패"));
		}
	}
}

// ════════════════════════════════════════════════════════════════════════════════
// 📌 OnLoginSuccess - 로그인 성공 처리
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 호출 시점:
//    ProcessLogin()에서 로그인/계정생성 성공 시
//
// 📌 매개변수:
//    - PlayerController: 로그인 성공한 플레이어의 Controller
//    - PlayerId: 로그인한 아이디
//
// 📌 처리 흐름:
//    1. 로그인 타임아웃 타이머 취소 (더 이상 킥하지 않음)
//    2. GameInstance.RegisterLogin() - 접속자 목록(TSet)에 추가
//    3. PlayerState.SetLoginInfo() - PlayerId 저장 (Replicated)
//    4. Client_LoginResult(true) - 클라이언트에 성공 알림 (RPC)
//    5. Client_ShowCharacterSelectUI() - 캐릭터 선택 UI 표시 (RPC)
//
// 📌 다음 단계:
//    → 클라이언트에서 캐릭터 선택 UI 표시됨
//    → 캐릭터 선택 시 ProcessCharacterSelection() 호출됨
//
// ════════════════════════════════════════════════════════════════════════════════
void AHellunaBaseGameMode::OnLoginSuccess(APlayerController* PlayerController, const FString& PlayerId)
{
	Debug::Print(TEXT("[BaseGameMode] Login Success"), FColor::Yellow);

#if HELLUNA_DEBUG_LOGIN
	UE_LOG(LogHelluna, Warning, TEXT(""));
	UE_LOG(LogHelluna, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogHelluna, Warning, TEXT("║     [BaseGameMode] OnLoginSuccess                          ║"));
	UE_LOG(LogHelluna, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogHelluna, Warning, TEXT("║ PlayerId: '%s'"), *PlayerId);
	UE_LOG(LogHelluna, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
#endif

	if (!PlayerController) return;

	// 타임아웃 타이머 취소
	if (FTimerHandle* Timer = LoginTimeoutTimers.Find(PlayerController))
	{
		GetWorldTimerManager().ClearTimer(*Timer);
		LoginTimeoutTimers.Remove(PlayerController);
	}

	// GameInstance에 로그인 등록 (동시 접속 체크용)
	if (UMDF_GameInstance* GI = Cast<UMDF_GameInstance>(UGameplayStatics::GetGameInstance(GetWorld())))
	{
		GI->RegisterLogin(PlayerId);
	}

	// Phase 3: 크래시 복구 체크 (비정상 종료 시 Loadout → Stash 복구)
	CheckAndRecoverFromCrash(PlayerId);

	// PlayerState에 로그인 정보 저장 (Replicated)
	if (AHellunaPlayerState* PS = PlayerController->GetPlayerState<AHellunaPlayerState>())
	{
		PS->SetLoginInfo(PlayerId);
	}

	// 클라이언트에 결과 알림 및 캐릭터 선택 UI 표시
	AHellunaLoginController* LoginController = Cast<AHellunaLoginController>(PlayerController);
	if (LoginController)
	{
		LoginController->Client_LoginResult(true, TEXT(""));
		TArray<bool> AvailableCharacters = GetAvailableCharacters();
		LoginController->Client_ShowCharacterSelectUI(AvailableCharacters);
	}
}

// ════════════════════════════════════════════════════════════════════════════════
// 📌 OnLoginFailed - 로그인 실패 처리
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 호출 시점:
//    ProcessLogin()에서 로그인 실패 시 (동시 접속, 비밀번호 불일치 등)
//
// 📌 매개변수:
//    - PlayerController: 로그인 실패한 플레이어의 Controller
//    - ErrorMessage: 실패 사유 메시지
//
// 📌 처리 흐름:
//    Client_LoginResult(false, ErrorMessage) - 클라이언트에 실패 알림 (RPC)
//    → 클라이언트에서 에러 메시지 표시
//
// ════════════════════════════════════════════════════════════════════════════════
void AHellunaBaseGameMode::OnLoginFailed(APlayerController* PlayerController, const FString& ErrorMessage)
{
#if HELLUNA_DEBUG_LOGIN
	UE_LOG(LogHelluna, Warning, TEXT(""));
	UE_LOG(LogHelluna, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogHelluna, Warning, TEXT("║     [BaseGameMode] OnLoginFailed                           ║"));
	UE_LOG(LogHelluna, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogHelluna, Warning, TEXT("║ ErrorMessage: '%s'"), *ErrorMessage);
	UE_LOG(LogHelluna, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
#endif

	AHellunaLoginController* LoginController = Cast<AHellunaLoginController>(PlayerController);
	if (LoginController)
	{
		LoginController->Client_LoginResult(false, ErrorMessage);
	}
}

// ════════════════════════════════════════════════════════════════════════════════
// 📌 OnLoginTimeout - 로그인 타임아웃 처리
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 호출 시점:
//    PostLogin()에서 설정한 타이머가 만료될 때 (기본 60초)
//
// 📌 매개변수:
//    - PlayerController: 타임아웃된 플레이어의 Controller
//
// 📌 처리 흐름:
//    1. LoginTimeoutTimers에서 타이머 제거
//    2. ClientReturnToMainMenuWithTextReason() - 메인 메뉴로 강제 이동 (킥)
//
// 📌 킥 메시지:
//    "로그인 타임아웃 (60초)"
//
// ════════════════════════════════════════════════════════════════════════════════
void AHellunaBaseGameMode::OnLoginTimeout(APlayerController* PlayerController)
{
#if HELLUNA_DEBUG_LOGIN
	UE_LOG(LogHelluna, Warning, TEXT(""));
	UE_LOG(LogHelluna, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogHelluna, Warning, TEXT("║     [BaseGameMode] OnLoginTimeout                          ║"));
	UE_LOG(LogHelluna, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogHelluna, Warning, TEXT("║ Controller: %s"), *GetNameSafe(PlayerController));
	UE_LOG(LogHelluna, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
#endif

	if (!PlayerController) return;
	LoginTimeoutTimers.Remove(PlayerController);

	// 메인 메뉴로 강제 이동 (킥)
	if (PlayerController->GetNetConnection())
	{
		FString KickReason = FString::Printf(TEXT("로그인 타임아웃 (%.0f초)"), LoginTimeoutSeconds);
		PlayerController->ClientReturnToMainMenuWithTextReason(FText::FromString(KickReason));
	}
}

// ════════════════════════════════════════════════════════════════════════════════
// 📌 SwapToGameController - Controller 교체
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 역할:
//    LoginController → GameController 교체
//    (로그인 전용 컨트롤러에서 실제 게임 플레이 컨트롤러로 전환)
//
// 📌 왜 Controller를 교체하나?
//    - LoginController: 로그인 UI만 담당하는 간단한 컨트롤러
//    - GameController: 실제 게임 플레이 담당 (BP_InvPlayerController)
//      → 인벤토리, 장비, 캐릭터 조작 등 복잡한 기능 포함
//
// 📌 호출 시점:
//    - ProcessCharacterSelection()에서 캐릭터 선택 완료 시
//    - PostLogin()에서 SeamlessTravel 후 재접속 시
//
// 📌 매개변수:
//    - LoginController: 교체할 기존 LoginController
//    - PlayerId: 플레이어 아이디
//    - SelectedHeroType: 선택한 캐릭터 타입
//
// 📌 처리 흐름:
//    1. GameControllerClass 확인 (LoginController에서 가져옴)
//    2. 기존 PlayerState의 로그인 정보 초기화
//    3. 새 GameController 스폰
//    4. Client_PrepareControllerSwap() - 로그인 UI 숨김 (RPC)
//    5. SwapPlayerControllers() - 안전한 교체 (엔진 함수)
//    6. 새 PlayerState에 로그인 정보 설정
//    7. Controller EndPlay 델리게이트 바인딩 (인벤토리 저장용)
//    8. 0.3초 후 SpawnHeroCharacter() 호출
//
// ⚠️ 주의:
//    - SwapPlayerControllers()는 엔진 내부 함수로 복잡한 처리 수행
//    - 딜레이 없이 바로 SpawnHeroCharacter() 호출하면 타이밍 이슈 발생 가능
//
// ════════════════════════════════════════════════════════════════════════════════
void AHellunaBaseGameMode::SwapToGameController(AHellunaLoginController* LoginController, const FString& PlayerId, EHellunaHeroType SelectedHeroType)
{
#if HELLUNA_DEBUG_GAMEMODE
	UE_LOG(LogHelluna, Warning, TEXT(""));
	UE_LOG(LogHelluna, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogHelluna, Warning, TEXT("║     [BaseGameMode] SwapToGameController                    ║"));
	UE_LOG(LogHelluna, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogHelluna, Warning, TEXT("║ PlayerId: '%s'"), *PlayerId);
	UE_LOG(LogHelluna, Warning, TEXT("║ LoginController: %s"), *GetNameSafe(LoginController));
#endif

	if (!LoginController)
	{
		UE_LOG(LogHelluna, Error, TEXT("[BaseGameMode] SwapToGameController - LoginController nullptr!"));
#if HELLUNA_DEBUG_GAMEMODE
		UE_LOG(LogHelluna, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
#endif
		return;
	}

	// GameControllerClass 확인 (BP에서 설정됨)
	TSubclassOf<APlayerController> GameControllerClass = LoginController->GetGameControllerClass();
	if (!GameControllerClass)
	{
		UE_LOG(LogHelluna, Error, TEXT("[BaseGameMode] SwapToGameController - GameControllerClass 미설정!"));
#if HELLUNA_DEBUG_GAMEMODE
		UE_LOG(LogHelluna, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
#endif
		// 교체 불가 → LoginController로 캐릭터 소환 (fallback)
		SpawnHeroCharacter(LoginController);
		return;
	}

	// 기존 PlayerState 로그인 정보 초기화 (새 Controller에서 다시 설정됨)
	if (AHellunaPlayerState* OldPS = LoginController->GetPlayerState<AHellunaPlayerState>())
	{
		OldPS->ClearLoginInfo();
	}

	// 새 GameController 스폰
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.Owner = this;

	FVector SpawnLocation = LoginController->GetFocalLocation();
	FRotator SpawnRotation = LoginController->GetControlRotation();

	// [Fix46-M8] GetWorld() null 체크
	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogHelluna, Error, TEXT("[BaseGameMode] SwapToGameController - GetWorld() nullptr!"));
		return;
	}

	APlayerController* NewController = World->SpawnActor<APlayerController>(
		GameControllerClass, SpawnLocation, SpawnRotation, SpawnParams);

	if (!NewController)
	{
		UE_LOG(LogHelluna, Error, TEXT("[BaseGameMode] SwapToGameController - 새 Controller 스폰 실패!"));
#if HELLUNA_DEBUG_GAMEMODE
		UE_LOG(LogHelluna, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
#endif
		SpawnHeroCharacter(LoginController);
		return;
	}

	// 로그인 UI 숨김 (클라이언트 RPC)
	LoginController->Client_PrepareControllerSwap();

	// ────────────────────────────────────────────────────────────────────────────
	// 📌 Controller 교체 (엔진 함수)
	// ────────────────────────────────────────────────────────────────────────────
	// SwapPlayerControllers()는 다음을 수행:
	// - 네트워크 연결 이전
	// - PlayerState 이전
	// - 입력 상태 이전
	// - 기존 Controller 파괴
	// ────────────────────────────────────────────────────────────────────────────
	SwapPlayerControllers(LoginController, NewController);

	// 새 PlayerState에 로그인 정보 설정
	if (AHellunaPlayerState* NewPS = NewController->GetPlayerState<AHellunaPlayerState>())
	{
		NewPS->SetLoginInfo(PlayerId);
		NewPS->SetSelectedHeroType(SelectedHeroType);

		// ────────────────────────────────────────────────────────────────────────
		// 📌 Controller EndPlay 델리게이트 바인딩
		// ────────────────────────────────────────────────────────────────────────
		// Controller가 파괴될 때 인벤토리 저장하기 위한 델리게이트
		// ControllerToPlayerIdMap: Controller → PlayerId 매핑 (EndPlay 시 사용)
		// ────────────────────────────────────────────────────────────────────────
		AInv_PlayerController* InvPC = Cast<AInv_PlayerController>(NewController);
		if (IsValid(InvPC))
		{
			InvPC->OnControllerEndPlay.AddDynamic(this, &AHellunaBaseGameMode::OnInvControllerEndPlay);
			RegisterControllerPlayerId(InvPC, PlayerId);
		}
	}

#if HELLUNA_DEBUG_GAMEMODE
	UE_LOG(LogHelluna, Warning, TEXT("║ Controller 교체 완료!"));
	UE_LOG(LogHelluna, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
#endif

	// 0.3초 딜레이 후 캐릭터 소환 (Controller 초기화 완료 대기)
	// [Fix26] 로컬 핸들 → LambdaTimerHandles 등록
	FTimerHandle& SpawnTimerHandle = LambdaTimerHandles.AddDefaulted_GetRef();
	GetWorldTimerManager().SetTimer(SpawnTimerHandle, [this, NewController]()
	{
		if (IsValid(NewController))
		{
			SpawnHeroCharacter(NewController);
		}
	}, 0.3f, false);
}

// ════════════════════════════════════════════════════════════════════════════════
// 📌 SpawnHeroCharacter - 캐릭터 소환
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 호출 시점:
//    - SwapToGameController() 완료 후 (0.3초 딜레이)
//    - PostLogin()에서 이미 GameController인 경우
//
// 📌 매개변수:
//    - PlayerController: 캐릭터를 소환할 플레이어의 Controller
//
// 📌 처리 흐름:
//    1. PlayerState에서 선택한 캐릭터 인덱스 가져오기
//    2. HeroCharacterMap에서 해당 클래스 찾기
//       → 없으면 기본 HeroCharacterClass 사용
//    3. 기존 Pawn 제거 (SpectatorPawn 등)
//    4. PlayerStart 위치 찾기
//    5. 캐릭터 스폰 및 Possess
//    6. 첫 플레이어면 InitializeGame() 호출 → 게임 시작!
//    7. 1초 후 인벤토리 로드 (LoadAndSendInventoryToClient)
//
// 📌 캐릭터 클래스 결정 순서:
//    1. HeroCharacterMap[SelectedHeroType] (BP에서 설정)
//    2. HeroCharacterClass (기본값)
//
// ⚠️ 주의:
//    - bGameInitialized는 InitializeGame() 내부에서 설정됨!
//    - 여기서 미리 설정하면 자식 클래스의 InitializeGame()이 스킵됨!
//
// ════════════════════════════════════════════════════════════════════════════════
void AHellunaBaseGameMode::SpawnHeroCharacter(APlayerController* PlayerController)
{
#if HELLUNA_DEBUG_GAMEMODE
	UE_LOG(LogHelluna, Warning, TEXT(""));
	UE_LOG(LogHelluna, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogHelluna, Warning, TEXT("║     [BaseGameMode] SpawnHeroCharacter                      ║"));
	UE_LOG(LogHelluna, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogHelluna, Warning, TEXT("║ Controller: %s"), *GetNameSafe(PlayerController));
#endif

	if (!PlayerController)
	{
		UE_LOG(LogHelluna, Error, TEXT("[BaseGameMode] SpawnHeroCharacter - Controller nullptr!"));
#if HELLUNA_DEBUG_GAMEMODE
		UE_LOG(LogHelluna, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
#endif
		return;
	}

	// ────────────────────────────────────────────────────────────────────────────
	// 📌 스폰할 캐릭터 클래스 결정
	// ────────────────────────────────────────────────────────────────────────────
	TSubclassOf<APawn> SpawnClass = nullptr;
	int32 CharacterIndex = -1;

	if (AHellunaPlayerState* PS = PlayerController->GetPlayerState<AHellunaPlayerState>())
	{
		CharacterIndex = PS->GetSelectedCharacterIndex();
	}

	// HeroCharacterMap에서 찾기 (BP에서 설정)
	if (CharacterIndex >= 0 && HeroCharacterMap.Contains(IndexToHeroType(CharacterIndex)))
	{
		SpawnClass = HeroCharacterMap[IndexToHeroType(CharacterIndex)];
	}
	// 기본 HeroCharacterClass 사용
	else if (HeroCharacterClass)
	{
		SpawnClass = HeroCharacterClass;
	}

	if (!SpawnClass)
	{
		UE_LOG(LogHelluna, Error, TEXT("[BaseGameMode] SpawnHeroCharacter - SpawnClass nullptr!"));
#if HELLUNA_DEBUG_GAMEMODE
		UE_LOG(LogHelluna, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
#endif
		return;
	}

	// ────────────────────────────────────────────────────────────────────────────
	// 📌 기존 Pawn 제거
	// ────────────────────────────────────────────────────────────────────────────
	APawn* OldPawn = PlayerController->GetPawn();
	if (OldPawn)
	{
		PlayerController->UnPossess();
		OldPawn->Destroy();
	}

	// ────────────────────────────────────────────────────────────────────────────
	// 📌 스폰 위치 결정
	// ────────────────────────────────────────────────────────────────────────────
	FVector SpawnLocation = FVector(0.f, 0.f, 200.f);  // 기본값
	FRotator SpawnRotation = FRotator::ZeroRotator;

	AActor* PlayerStart = FindPlayerStart(PlayerController);
	if (PlayerStart)
	{
		SpawnLocation = PlayerStart->GetActorLocation();
		SpawnRotation = PlayerStart->GetActorRotation();
	}

	// ────────────────────────────────────────────────────────────────────────────
	// 📌 캐릭터 스폰
	// ────────────────────────────────────────────────────────────────────────────
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	SpawnParams.Owner = PlayerController;

	// [Fix46-M8] GetWorld() null 체크
	UWorld* SpawnWorld = GetWorld();
	if (!SpawnWorld)
	{
		UE_LOG(LogHelluna, Error, TEXT("[BaseGameMode] SpawnHeroCharacter - GetWorld() nullptr!"));
		return;
	}

	APawn* NewPawn = SpawnWorld->SpawnActor<APawn>(SpawnClass, SpawnLocation, SpawnRotation, SpawnParams);

	if (!NewPawn)
	{
		UE_LOG(LogHelluna, Error, TEXT("[BaseGameMode] SpawnHeroCharacter - HeroCharacter 스폰 실패!"));
#if HELLUNA_DEBUG_GAMEMODE
		UE_LOG(LogHelluna, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
#endif
		return;
	}

	// Possess (Controller가 Pawn을 조종)
	PlayerController->Possess(NewPawn);

	// LoginController인 경우 UI 숨김
	AHellunaLoginController* LoginController = Cast<AHellunaLoginController>(PlayerController);
	if (LoginController)
	{
		LoginController->Client_PrepareControllerSwap();
	}

#if HELLUNA_DEBUG_GAMEMODE
	UE_LOG(LogHelluna, Warning, TEXT("║ Possess 완료!"));
	UE_LOG(LogHelluna, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
#endif

	// ════════════════════════════════════════════════════════════════════════════════
	// 📌 첫 플레이어 캐릭터 소환 → 게임 초기화!
	// ════════════════════════════════════════════════════════════════════════════════
	// ⚠️ 주의: bGameInitialized는 InitializeGame() 내부에서 설정됨!
	//          여기서 미리 설정하면 자식 클래스의 InitializeGame()이 스킵됨!
	// ════════════════════════════════════════════════════════════════════════════════
	if (!bGameInitialized)
	{
		InitializeGame();  // InitializeGame() 내부에서 bGameInitialized = true 설정
	}

	// ────────────────────────────────────────────────────────────────────────────
	// 📌 인벤토리 복원 — Pre-Cache 시 즉시, 아니면 1초 딜레이
	// ────────────────────────────────────────────────────────────────────────────
	FString SpawnPlayerId;
	if (AHellunaPlayerState* SpawnPS = PlayerController->GetPlayerState<AHellunaPlayerState>())
	{
		SpawnPlayerId = SpawnPS->GetPlayerUniqueId();
	}

	const bool bHasPreCache = !SpawnPlayerId.IsEmpty() && PreCachedInventoryMap.Contains(SpawnPlayerId);
	const float InventoryLoadDelay = bHasPreCache ? 0.1f : 1.0f;

	UE_LOG(LogHelluna, Log, TEXT("[SpawnHero] 인벤토리 로드 딜레이=%.1f초 | PreCache=%s | PlayerId=%s"),
		InventoryLoadDelay, bHasPreCache ? TEXT("Y") : TEXT("N"), *SpawnPlayerId);

	// [Fix26] 로컬 핸들 → LambdaTimerHandles 등록
	FTimerHandle& InventoryLoadTimer = LambdaTimerHandles.AddDefaulted_GetRef();
	GetWorldTimerManager().SetTimer(InventoryLoadTimer, [this, PlayerController]()
	{
		if (IsValid(PlayerController))
		{
			LoadAndSendInventoryToClient(PlayerController);
		}
	}, InventoryLoadDelay, false);
}

// ════════════════════════════════════════════════════════════════════════════════
// 📌 IsPlayerLoggedIn - 동시 접속 체크
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 역할:
//    같은 아이디로 이미 접속 중인 플레이어가 있는지 확인
//
// 📌 매개변수:
//    - PlayerId: 확인할 아이디
//
// 📌 반환값:
//    - true: 이미 접속 중
//    - false: 접속 가능
//
// 📌 구현:
//    GameInstance의 LoggedInPlayers TSet에서 확인
//
// ════════════════════════════════════════════════════════════════════════════════
bool AHellunaBaseGameMode::IsPlayerLoggedIn(const FString& PlayerId) const
{
	if (UMDF_GameInstance* GI = Cast<UMDF_GameInstance>(UGameplayStatics::GetGameInstance(GetWorld())))
	{
		return GI->IsPlayerLoggedIn(PlayerId);
	}
	return false;
}

// ════════════════════════════════════════════════════════════════════════════════
// 📌 Logout - 플레이어 로그아웃 (연결 끊김)
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 호출 시점:
//    플레이어 연결 끊김 시 (엔진 자동 호출)
//    - 게임 종료
//    - 네트워크 끊김
//    - 킥
//
// 📌 매개변수:
//    - Exiting: 로그아웃하는 플레이어의 Controller
//
// 📌 처리 흐름:
//    1. 로그인 타임아웃 타이머 정리
//    2. PlayerId 가져오기
//    3. 인벤토리 저장
//       → InventoryComponent 있음: 현재 인벤토리 수집 후 저장
//       → InventoryComponent 없음: 캐시된 데이터 저장
//    4. 캐시 데이터 제거
//    5. GameInstance.RegisterLogout() - 접속자 목록에서 제거
//    6. UnregisterCharacterUse() - 캐릭터 사용 해제
//
// 📌 인벤토리 저장 위치:
//    Saved/SaveGames/HellunaInventory.sav
//
// ════════════════════════════════════════════════════════════════════════════════
void AHellunaBaseGameMode::Logout(AController* Exiting)
{
#if HELLUNA_DEBUG_GAMEMODE
	UE_LOG(LogHelluna, Warning, TEXT(""));
	UE_LOG(LogHelluna, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogHelluna, Warning, TEXT("║     [BaseGameMode] Logout                                  ║"));
	UE_LOG(LogHelluna, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogHelluna, Warning, TEXT("║ Controller: %s"), *GetNameSafe(Exiting));
	UE_LOG(LogHelluna, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
#endif

	if (!Exiting)
	{
		Super::Logout(Exiting);
		return;
	}

	// [Fix50] LoginController 스왑 감지 — 실제 이탈이 아니면 스킵
	if (AHellunaLoginController* ExitingLC = Cast<AHellunaLoginController>(Exiting))
	{
		AHellunaPlayerState* PS = ExitingLC->GetPlayerState<AHellunaPlayerState>();
		bool bIsControllerSwap = (!PS || PS->GetPlayerUniqueId().IsEmpty());
		if (bIsControllerSwap)
		{
			UE_LOG(LogHelluna, Log, TEXT("[Fix50] BaseGameMode: LoginController 스왑 감지 — Logout 처리 스킵"));
			Super::Logout(Exiting);
			return;
		}
	}

	// 타임아웃 타이머 정리
	if (APlayerController* PC = Cast<APlayerController>(Exiting))
	{
		if (FTimerHandle* Timer = LoginTimeoutTimers.Find(PC))
		{
			GetWorldTimerManager().ClearTimer(*Timer);
			LoginTimeoutTimers.Remove(PC);
		}
	}

	// PlayerId 가져오기
	AHellunaPlayerState* PS = Exiting->GetPlayerState<AHellunaPlayerState>();
	FString PlayerId;
	if (PS)
	{
		PlayerId = PS->GetPlayerUniqueId();
	}

	// Phase 6: 미소비 PreCache 정리 (접속 끊김 시 메모리 누수 방지)
	if (!PlayerId.IsEmpty())
	{
		PreCachedInventoryMap.Remove(PlayerId);
	}
	if (APlayerController* ExitingPC_ForDeploy = Cast<APlayerController>(Exiting))
	{
		PendingLobbyDeployMap.Remove(ExitingPC_ForDeploy);
	}

	if (!PlayerId.IsEmpty())
	{
		// ────────────────────────────────────────────────────────────────────────
		// 📌 인벤토리 저장 (부모에게 위임)
		// ────────────────────────────────────────────────────────────────────────
		APlayerController* ExitingPC = Cast<APlayerController>(Exiting);
		OnPlayerInventoryLogout(PlayerId, ExitingPC);

		// GameInstance에서 로그아웃 처리
		if (UMDF_GameInstance* GI = Cast<UMDF_GameInstance>(UGameplayStatics::GetGameInstance(GetWorld())))
		{
			GI->RegisterLogout(PlayerId);
		}

		// 캐릭터 사용 해제
		UnregisterCharacterUse(PlayerId);
	}

	// =========================================================================================
	// [투표 시스템] 퇴장 플레이어 투표 처리 (김기현)
	// =========================================================================================
	// 투표 진행 중 플레이어가 퇴장하면 DisconnectPolicy에 따라 처리:
	// - ExcludeAndContinue: 해당 플레이어 제외 후 남은 인원으로 재판정
	// - CancelVote: 투표 취소
	// =========================================================================================
	{
		APlayerState* ExitingPS = Exiting->GetPlayerState<APlayerState>();
		if (ExitingPS)
		{
			if (AHellunaBaseGameState* GS = GetGameState<AHellunaBaseGameState>())
			{
				if (UVoteManagerComponent* VoteMgr = GS->VoteManagerComponent)
				{
					if (VoteMgr->IsVoteInProgress())
					{
						VoteMgr->HandlePlayerDisconnect(ExitingPS);
					}
				}
			}
		}
	}

	Super::Logout(Exiting);
}

// ════════════════════════════════════════════════════════════════════════════════
// 📌 HandleSeamlessTravelPlayer - 맵 이동 후 로그인 상태 유지
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 호출 시점:
//    SeamlessTravel로 맵 이동 후 (엔진 자동 호출)
//
// 📌 매개변수:
//    - C: 맵 이동한 플레이어의 Controller (참조로 전달)
//
// 📌 역할:
//    맵 이동 시 PlayerState의 로그인 정보가 유실되지 않도록 복원
//
// 📌 처리 흐름:
//    1. 기존 PlayerState에서 로그인 정보 백업
//       - PlayerId
//       - SelectedHeroType
//       - IsLoggedIn
//    2. Super::HandleSeamlessTravelPlayer() 호출 (엔진 처리)
//    3. 새 PlayerState에 로그인 정보 복원
//    4. 로그인 상태였으면 0.5초 후 SwapToGameController() 또는 SpawnHeroCharacter()
//
// ════════════════════════════════════════════════════════════════════════════════
void AHellunaBaseGameMode::HandleSeamlessTravelPlayer(AController*& C)
{
#if HELLUNA_DEBUG_GAMEMODE
	UE_LOG(LogHelluna, Warning, TEXT(""));
	UE_LOG(LogHelluna, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogHelluna, Warning, TEXT("║     [BaseGameMode] HandleSeamlessTravelPlayer              ║"));
	UE_LOG(LogHelluna, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
#endif

	// 로그인 정보 백업
	FString SavedPlayerId;
	EHellunaHeroType SavedHeroType = EHellunaHeroType::None;
	bool bSavedIsLoggedIn = false;

	if (C)
	{
		if (AHellunaPlayerState* OldPS = C->GetPlayerState<AHellunaPlayerState>())
		{
			SavedPlayerId = OldPS->GetPlayerUniqueId();
			SavedHeroType = OldPS->GetSelectedHeroType();
			bSavedIsLoggedIn = OldPS->IsLoggedIn();
		}
	}

	// 엔진 처리
	Super::HandleSeamlessTravelPlayer(C);

	// 로그인 정보 복원
	if (C && !SavedPlayerId.IsEmpty())
	{
		if (AHellunaPlayerState* NewPS = C->GetPlayerState<AHellunaPlayerState>())
		{
			NewPS->SetLoginInfo(SavedPlayerId);
			NewPS->SetSelectedHeroType(SavedHeroType);
		}

		// SeamlessTravel: 캐릭터 사용 재등록 + 인벤토리 사전 로드
		RegisterCharacterUse(SavedHeroType, SavedPlayerId);
		PreCacheInventoryForPlayer(SavedPlayerId);

		// 로그인 상태였으면 게임 컨트롤러로 전환
		if (bSavedIsLoggedIn)
		{
			APlayerController* TraveledPC = Cast<APlayerController>(C);
			if (TraveledPC)
			{
				// [Fix26] 로컬 핸들 → LambdaTimerHandles 등록
				FTimerHandle& TravelTimerHandle = LambdaTimerHandles.AddDefaulted_GetRef();
				GetWorldTimerManager().SetTimer(TravelTimerHandle, [this, TraveledPC, SavedPlayerId, SavedHeroType]()
				{
					if (IsValid(TraveledPC))
					{
						AHellunaLoginController* LoginController = Cast<AHellunaLoginController>(TraveledPC);
						if (LoginController && LoginController->GetGameControllerClass())
						{
							SwapToGameController(LoginController, SavedPlayerId, SavedHeroType);
						}
						else
						{
							SpawnHeroCharacter(TraveledPC);
						}
					}
				}, 0.5f, false);
			}
		}
	}
}

// ════════════════════════════════════════════════════════════════════════════════
// 🎭 캐릭터 선택 시스템
// ════════════════════════════════════════════════════════════════════════════════

// ════════════════════════════════════════════════════════════════════════════════
// 📌 ProcessCharacterSelection - 캐릭터 선택 처리
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 호출 시점:
//    LoginController.Server_SelectCharacter() RPC에서 호출
//    (클라이언트가 캐릭터 선택 버튼 클릭 시)
//
// 📌 매개변수:
//    - PlayerController: 캐릭터 선택한 플레이어의 Controller
//    - HeroType: 선택한 캐릭터 타입 (Lui, Luna, Liam 등)
//
// 📌 처리 흐름:
//    1. HeroType 유효성 체크
//       → 유효하지 않음: Client_CharacterSelectionResult(false)
//    2. 중복 선택 체크 (IsCharacterInUse)
//       → 사용 중: Client_CharacterSelectionResult(false, "다른 플레이어가 사용 중")
//    3. PlayerState.SetSelectedHeroType() - 선택 정보 저장
//    4. RegisterCharacterUse() - UsedCharacterMap에 등록
//    5. Client_CharacterSelectionResult(true) - 성공 알림
//    6. 0.3초 후 SwapToGameController() 호출
//
// 📌 캐릭터 중복 방지:
//    - UsedCharacterMap: (EHellunaHeroType → PlayerId)
//    - 같은 캐릭터를 2명 이상 선택할 수 없음
//
// ════════════════════════════════════════════════════════════════════════════════
void AHellunaBaseGameMode::ProcessCharacterSelection(APlayerController* PlayerController, EHellunaHeroType HeroType)
{
#if HELLUNA_DEBUG_CHARACTER_SELECT
	UE_LOG(LogHelluna, Warning, TEXT(""));
	UE_LOG(LogHelluna, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogHelluna, Warning, TEXT("║  [BaseGameMode] ProcessCharacterSelection                  ║"));
	UE_LOG(LogHelluna, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogHelluna, Warning, TEXT("║ HeroType: %s"), *UEnum::GetValueAsString(HeroType));
	UE_LOG(LogHelluna, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
#endif

	if (!PlayerController) return;

	AHellunaLoginController* LoginController = Cast<AHellunaLoginController>(PlayerController);

	// HeroType 유효성 체크
	if (HeroType == EHellunaHeroType::None || !HeroCharacterMap.Contains(HeroType))
	{
		if (LoginController)
		{
			LoginController->Client_CharacterSelectionResult(false, TEXT("유효하지 않은 캐릭터입니다."));
		}
		return;
	}

	// 중복 선택 체크
	if (IsCharacterInUse(HeroType))
	{
		if (LoginController)
		{
			LoginController->Client_CharacterSelectionResult(false, TEXT("다른 플레이어가 사용 중인 캐릭터입니다."));
		}
		return;
	}

	// PlayerState에 선택 정보 저장
	FString PlayerId;
	AHellunaPlayerState* PS = PlayerController->GetPlayerState<AHellunaPlayerState>();
	if (PS)
	{
		PlayerId = PS->GetPlayerUniqueId();
		PS->SetSelectedHeroType(HeroType);
	}

	if (PlayerId.IsEmpty())
	{
		if (LoginController)
		{
			LoginController->Client_CharacterSelectionResult(false, TEXT("로그인 정보 오류"));
		}
		return;
	}

	// 캐릭터 사용 등록
	RegisterCharacterUse(HeroType, PlayerId);

	// 인벤토리 사전 로드 (디스크 I/O를 스폰 전에 완료)
	PreCacheInventoryForPlayer(PlayerId);

	// 성공 알림
	if (LoginController)
	{
		LoginController->Client_CharacterSelectionResult(true, TEXT(""));
	}

	// 0.3초 후 GameController로 전환
	if (LoginController && LoginController->GetGameControllerClass())
	{
		// [Fix26] 로컬 핸들 → LambdaTimerHandles 등록
		FTimerHandle& CharSelectTimerHandle = LambdaTimerHandles.AddDefaulted_GetRef();
		GetWorldTimerManager().SetTimer(CharSelectTimerHandle, [this, LoginController, PlayerId, HeroType]()
		{
			if (IsValid(LoginController))
			{
				SwapToGameController(LoginController, PlayerId, HeroType);
			}
		}, 0.3f, false);
	}
	else
	{
		SpawnHeroCharacter(PlayerController);
	}
}

// ════════════════════════════════════════════════════════════════════════════════
// 📌 RegisterCharacterUse - 캐릭터 사용 등록
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 역할:
//    캐릭터 중복 선택 방지를 위한 등록
//
// 📌 매개변수:
//    - HeroType: 등록할 캐릭터 타입
//    - PlayerId: 사용하는 플레이어 아이디
//
// 📌 처리 흐름:
//    1. 기존 캐릭터 사용 해제 (UnregisterCharacterUse)
//       → 같은 플레이어가 다른 캐릭터 선택 시 기존 선택 해제
//    2. UsedCharacterMap에 등록 (HeroType → PlayerId)
//    3. GameState에 알림 → 모든 클라이언트 UI 실시간 갱신
//
// 📌 데이터 구조:
//    - UsedCharacterMap: TMap<EHellunaHeroType, FString>
//      → 캐릭터 타입을 키로 사용하는 플레이어 ID 저장
//
// ════════════════════════════════════════════════════════════════════════════════
void AHellunaBaseGameMode::RegisterCharacterUse(EHellunaHeroType HeroType, const FString& PlayerId)
{
	if (!HasAuthority()) return;

	// 기존 선택 해제 (같은 플레이어가 다른 캐릭터 선택한 경우)
	UnregisterCharacterUse(PlayerId);

	// 새 캐릭터 등록
	UsedCharacterMap.Add(HeroType, PlayerId);

	// GameState에 알림 (클라이언트 UI 갱신용)
	if (AHellunaBaseGameState* GS = GetGameState<AHellunaBaseGameState>())
	{
		GS->AddUsedCharacter(HeroType);
	}
}

// ════════════════════════════════════════════════════════════════════════════════
// 📌 UnregisterCharacterUse - 캐릭터 사용 해제
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 역할:
//    플레이어가 사용 중인 캐릭터 등록 해제
//
// 📌 호출 시점:
//    - Logout() - 플레이어 로그아웃 시
//    - RegisterCharacterUse() - 다른 캐릭터 선택 시 기존 선택 해제
//
// 📌 매개변수:
//    - PlayerId: 해제할 플레이어 아이디
//
// 📌 처리 흐름:
//    1. UsedCharacterMap에서 해당 PlayerId로 등록된 캐릭터 찾기
//    2. 찾으면 제거
//    3. GameState에 알림 → 모든 클라이언트 UI 갱신 (선택 가능해짐)
//
// ════════════════════════════════════════════════════════════════════════════════
void AHellunaBaseGameMode::UnregisterCharacterUse(const FString& PlayerId)
{
	if (!HasAuthority()) return;
	if (PlayerId.IsEmpty()) return;

	// PlayerId로 등록된 캐릭터 찾기
	EHellunaHeroType FoundType = EHellunaHeroType::None;
	for (const auto& Pair : UsedCharacterMap)
	{
		if (Pair.Value == PlayerId)
		{
			FoundType = Pair.Key;
			break;
		}
	}

	// 찾으면 제거
	if (FoundType != EHellunaHeroType::None)
	{
		UsedCharacterMap.Remove(FoundType);

		// GameState에 알림 (클라이언트 UI 갱신용)
		if (AHellunaBaseGameState* GS = GetGameState<AHellunaBaseGameState>())
		{
			GS->RemoveUsedCharacter(FoundType);
		}
	}
}

// ════════════════════════════════════════════════════════════════════════════════
// 📌 IsCharacterInUse - 캐릭터 사용 중 확인
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 매개변수:
//    - HeroType: 확인할 캐릭터 타입
//
// 📌 반환값:
//    - true: 다른 플레이어가 사용 중
//    - false: 선택 가능
//
// ════════════════════════════════════════════════════════════════════════════════
bool AHellunaBaseGameMode::IsCharacterInUse(EHellunaHeroType HeroType) const
{
	return UsedCharacterMap.Contains(HeroType);
}

// ════════════════════════════════════════════════════════════════════════════════
// 📌 GetAvailableCharacters - 선택 가능한 캐릭터 목록 (배열)
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 역할:
//    캐릭터 선택 UI에서 사용할 선택 가능 여부 배열 반환
//
// 📌 반환값:
//    TArray<bool> - 인덱스별 선택 가능 여부
//    - [0]: Lui 선택 가능?
//    - [1]: Luna 선택 가능?
//    - [2]: Liam 선택 가능?
//
// ════════════════════════════════════════════════════════════════════════════════
TArray<bool> AHellunaBaseGameMode::GetAvailableCharacters() const
{
	TArray<bool> Result;
	for (int32 i = 0; i < 3; i++)
	{
		Result.Add(!IsCharacterInUse(IndexToHeroType(i)));
	}
	return Result;
}

// ════════════════════════════════════════════════════════════════════════════════
// 📌 GetAvailableCharactersMap - 선택 가능한 캐릭터 목록 (맵)
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 역할:
//    캐릭터 타입별 선택 가능 여부 맵 반환
//
// 📌 반환값:
//    TMap<EHellunaHeroType, bool> - 캐릭터 타입별 선택 가능 여부
//
// ════════════════════════════════════════════════════════════════════════════════
TMap<EHellunaHeroType, bool> AHellunaBaseGameMode::GetAvailableCharactersMap() const
{
	TMap<EHellunaHeroType, bool> Result;
	Result.Add(EHellunaHeroType::Lui, !IsCharacterInUse(EHellunaHeroType::Lui));
	Result.Add(EHellunaHeroType::Luna, !IsCharacterInUse(EHellunaHeroType::Luna));
	Result.Add(EHellunaHeroType::Liam, !IsCharacterInUse(EHellunaHeroType::Liam));
	return Result;
}

// ════════════════════════════════════════════════════════════════════════════════
// 📌 GetHeroCharacterClass - 캐릭터 클래스 가져오기
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 매개변수:
//    - HeroType: 가져올 캐릭터 타입
//
// 📌 반환값:
//    - HeroCharacterMap에 있으면 해당 클래스
//    - 없으면 기본 HeroCharacterClass
//
// ════════════════════════════════════════════════════════════════════════════════
TSubclassOf<APawn> AHellunaBaseGameMode::GetHeroCharacterClass(EHellunaHeroType HeroType) const
{
	if (HeroCharacterMap.Contains(HeroType))
	{
		return HeroCharacterMap[HeroType];
	}
	return HeroCharacterClass;
}

// ════════════════════════════════════════════════════════════════════════════════
// 📌 IndexToHeroType - 인덱스 → EHellunaHeroType 변환
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 매개변수:
//    - Index: 캐릭터 인덱스 (0, 1, 2)
//
// 📌 반환값:
//    - 0: EHellunaHeroType::Lui
//    - 1: EHellunaHeroType::Luna
//    - 2: EHellunaHeroType::Liam
//    - 그 외: EHellunaHeroType::None
//
// ════════════════════════════════════════════════════════════════════════════════
EHellunaHeroType AHellunaBaseGameMode::IndexToHeroType(int32 Index)
{
	if (Index < 0 || Index > 2) return EHellunaHeroType::None;
	return static_cast<EHellunaHeroType>(Index);
}

// ════════════════════════════════════════════════════════════════════════════════
// 📦 인벤토리 시스템
// ════════════════════════════════════════════════════════════════════════════════

// ════════════════════════════════════════════════════════════════════════════════
// 📌 [Phase 3] SaveCollectedItems — SQLite 저장 (실패 시 .sav 폴백)
// ════════════════════════════════════════════════════════════════════════════════
bool AHellunaBaseGameMode::SaveCollectedItems(const FString& PlayerId, const TArray<FInv_SavedItemData>& Items)
{
	if (PlayerId.IsEmpty())
	{
		return false;
	}

	if (Items.Num() == 0)
	{
		UE_LOG(LogHelluna, Log, TEXT("[SaveCollectedItems] 저장할 아이템 0개 — 정상 처리 | PlayerId=%s"), *PlayerId);
		return true;
	}

	UGameInstance* GI = GetGameInstance();
	UHellunaSQLiteSubsystem* DB = GI ? GI->GetSubsystem<UHellunaSQLiteSubsystem>() : nullptr;

	if (DB && DB->IsDatabaseReady())
	{
		const bool bSuccess = DB->SavePlayerStash(PlayerId, Items);

		// 캐시도 갱신 (Logout↔EndPlay 타이밍 문제 대응 — 부모와 동일)
		FInv_PlayerSaveData SaveData;
		SaveData.Items = Items;
		SaveData.LastSaveTime = FDateTime::Now();
		CachePlayerData(PlayerId, SaveData);

		if (bSuccess)
		{
			UE_LOG(LogHelluna, Log, TEXT("[Phase3] SaveCollectedItems: SQLite 저장 성공 | PlayerId=%s | %d개"), *PlayerId, Items.Num());
		}
		else
		{
			UE_LOG(LogHelluna, Error, TEXT("[Phase3] SaveCollectedItems: SQLite 저장 실패 — .sav 폴백 | PlayerId=%s"), *PlayerId);
			return Super::SaveCollectedItems(PlayerId, Items);
		}

		return bSuccess;
	}

	// [Fix16] bFileTransferOnly 모드(게임서버)에서는 .sav 폴백 금지
	// — .sav에 저장하면 다음 세션에서 오래된 데이터를 로드하여 아이템 복제 발생
	// — 게임서버의 저장 경로는 ExportGameResultToFile (JSON) 전용
	if (DB && DB->IsFileTransferOnly())
	{
		UE_LOG(LogHelluna, Warning, TEXT("[Fix16] SaveCollectedItems: 파일 전송 전용 모드 — .sav 폴백 차단 | PlayerId=%s"), *PlayerId);
		return false;
	}

	// SQLite 미준비 → .sav 폴백
	UE_LOG(LogHelluna, Warning, TEXT("[Phase3] SaveCollectedItems: SQLite 미준비 — .sav 폴백 | PlayerId=%s"), *PlayerId);
	return Super::SaveCollectedItems(PlayerId, Items);
}

// ════════════════════════════════════════════════════════════════════════════════
// 📌 PreCacheInventoryForPlayer — 인벤토리 데이터 사전 로드
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 역할:
//    캐릭터 스폰 전에 디스크 I/O(JSON 파일 / SQLite)를 미리 완료하여
//    스폰 후 즉시 인벤토리를 복원할 수 있도록 캐싱
//
// 📌 호출 시점:
//    PostLogin() / SwapToGameController() 등 SpawnHeroCharacter 이전
//
// 📌 데이터 소비:
//    LoadAndSendInventoryToClient()에서 PreCachedInventoryMap을 우선 확인
//
// ════════════════════════════════════════════════════════════════════════════════
void AHellunaBaseGameMode::PreCacheInventoryForPlayer(const FString& PlayerId)
{
	if (PlayerId.IsEmpty())
	{
		return;
	}

	// 이미 캐시됨
	if (PreCachedInventoryMap.Contains(PlayerId))
	{
		UE_LOG(LogHelluna, Log, TEXT("[PreCache] 이미 캐시됨 | PlayerId=%s"), *PlayerId);
		return;
	}

	UGameInstance* GI = GetGameInstance();
	UHellunaSQLiteSubsystem* DB = GI ? GI->GetSubsystem<UHellunaSQLiteSubsystem>() : nullptr;

	FInv_PlayerSaveData LoadedData;
	bool bLoaded = false;

	// ── [1] 파일 기반 Loadout 우선 체크 (DB 잠금 회피) ──
	if (DB && DB->HasPendingLoadoutFile(PlayerId))
	{
		int32 FileHeroType = 0;
		TArray<FInv_SavedItemData> Items = DB->ImportLoadoutFromFile(PlayerId, FileHeroType);
		UE_LOG(LogHelluna, Warning, TEXT("[PreCache] JSON 파일에서 Loadout 로드 | PlayerId=%s | %d개 | HeroType=%d"),
			*PlayerId, Items.Num(), FileHeroType);

		if (Items.Num() > 0)
		{
			LoadedData.Items = MoveTemp(Items);
			LoadedData.LastSaveTime = FDateTime::Now();
			bLoaded = true;
		}

		// DB Loadout도 정리
		if (DB->IsDatabaseReady() && DB->HasPendingLoadout(PlayerId))
		{
			DB->DeletePlayerLoadout(PlayerId);
			UE_LOG(LogHelluna, Log, TEXT("[PreCache] DB Loadout도 삭제 완료"));
		}
	}

	// ── [2] 파일 없으면 DB에서 로드 시도 ──
	if (!bLoaded)
	{
		if (DB && !DB->IsDatabaseReady())
		{
			DB->TryReopenDatabase();
		}

		if (DB && DB->IsDatabaseReady())
		{
			// DB Loadout 우선
			if (DB->HasPendingLoadout(PlayerId))
			{
				TArray<FInv_SavedItemData> Items = DB->LoadPlayerLoadout(PlayerId);
				if (Items.Num() > 0)
				{
					LoadedData.Items = MoveTemp(Items);
					LoadedData.LastSaveTime = FDateTime::Now();
					bLoaded = true;
				}
				DB->DeletePlayerLoadout(PlayerId);
				UE_LOG(LogHelluna, Log, TEXT("[PreCache] DB Loadout 로드+삭제 | PlayerId=%s | %d개"), *PlayerId, LoadedData.Items.Num());
			}

			// Loadout 없으면 Stash
			if (!bLoaded)
			{
				TArray<FInv_SavedItemData> Items = DB->LoadPlayerStash(PlayerId);
				if (Items.Num() > 0)
				{
					LoadedData.Items = MoveTemp(Items);
					LoadedData.LastSaveTime = FDateTime::Now();
					bLoaded = true;
					UE_LOG(LogHelluna, Log, TEXT("[PreCache] Stash 로드 | PlayerId=%s | %d개"), *PlayerId, LoadedData.Items.Num());
				}
			}
		}
	}

	if (bLoaded && !LoadedData.IsEmpty())
	{
		PreCachedInventoryMap.Add(PlayerId, MoveTemp(LoadedData));
		UE_LOG(LogHelluna, Warning, TEXT("[PreCache] ✓ 캐시 완료 | PlayerId=%s | %d개"),
			*PlayerId, PreCachedInventoryMap[PlayerId].Items.Num());
	}
	else
	{
		UE_LOG(LogHelluna, Log, TEXT("[PreCache] 데이터 없음 (신규 유저 또는 빈 인벤토리) | PlayerId=%s"), *PlayerId);
	}
}

// ════════════════════════════════════════════════════════════════════════════════
// 📌 [Phase 3] LoadAndSendInventoryToClient — SQLite 로드 (실패 시 .sav 폴백)
// ════════════════════════════════════════════════════════════════════════════════
//
// ※ 부모의 InventorySaveGame이 private이라 Super 호출로 데이터 주입 불가
// ※ 부모 로직 복제 + 데이터 소스만 SQLite로 교체
// ════════════════════════════════════════════════════════════════════════════════
void AHellunaBaseGameMode::LoadAndSendInventoryToClient(APlayerController* PC)
{
	if (!HasAuthority() || !IsValid(PC))
	{
		return;
	}

	const FString PlayerId = GetPlayerSaveId(PC);
	if (PlayerId.IsEmpty())
	{
		return;
	}

	FInv_PlayerSaveData LoadedData;
	bool bLoaded = false;

	// ══════════════════════════════════════════════════════════════
	// [Pre-Cache] 사전 로드된 데이터 우선 사용
	// ══════════════════════════════════════════════════════════════
	if (FInv_PlayerSaveData* CachedData = PreCachedInventoryMap.Find(PlayerId))
	{
		LoadedData = MoveTemp(*CachedData);
		PreCachedInventoryMap.Remove(PlayerId);
		bLoaded = true;
		UE_LOG(LogHelluna, Warning, TEXT("[LoadInv] ✓ Pre-Cache 데이터 사용 | PlayerId=%s | %d개"),
			*PlayerId, LoadedData.Items.Num());
	}

	// ══════════════════════════════════════════════════════════════
	// [Fallback] 캐시 없으면 기존 경로 (디스크 I/O)
	// ══════════════════════════════════════════════════════════════
	if (!bLoaded)
	{
		UGameInstance* GI = GetGameInstance();
		UHellunaSQLiteSubsystem* DB = GI ? GI->GetSubsystem<UHellunaSQLiteSubsystem>() : nullptr;

		// [Phase 6 File] 파일 기반 Loadout 체크
		if (DB && DB->HasPendingLoadoutFile(PlayerId))
		{
			int32 FileHeroType = 0;
			TArray<FInv_SavedItemData> Items = DB->ImportLoadoutFromFile(PlayerId, FileHeroType);
			UE_LOG(LogHelluna, Warning, TEXT("[Phase6-File] LoadAndSendInventoryToClient: JSON 파일에서 Loadout 로드 | PlayerId=%s | %d개 | HeroType=%d"),
				*PlayerId, Items.Num(), FileHeroType);

			if (Items.Num() > 0)
			{
				LoadedData.Items = MoveTemp(Items);
				LoadedData.LastSaveTime = FDateTime::Now();
				bLoaded = true;
			}

			if (DB->IsDatabaseReady() && DB->HasPendingLoadout(PlayerId))
			{
				DB->DeletePlayerLoadout(PlayerId);
				UE_LOG(LogHelluna, Log, TEXT("[Phase6-File] DB Loadout도 삭제 완료"));
			}
		}

		// [Phase 6 DB] DB에서 로드 시도
		if (!bLoaded)
		{
			if (DB && !DB->IsDatabaseReady())
			{
				UE_LOG(LogHelluna, Warning, TEXT("[Phase3] LoadAndSendInventoryToClient: DB 미준비 → TryReopenDatabase 시도"));
				DB->TryReopenDatabase();
			}

			if (DB && DB->IsDatabaseReady())
			{
				if (DB->HasPendingLoadout(PlayerId))
				{
					TArray<FInv_SavedItemData> Items = DB->LoadPlayerLoadout(PlayerId);
					UE_LOG(LogHelluna, Log, TEXT("[Phase6] LoadAndSendInventoryToClient: DB Loadout 로드 | PlayerId=%s | %d개"),
						*PlayerId, Items.Num());

					if (Items.Num() > 0)
					{
						LoadedData.Items = MoveTemp(Items);
						LoadedData.LastSaveTime = FDateTime::Now();
						bLoaded = true;
					}

					DB->DeletePlayerLoadout(PlayerId);
					UE_LOG(LogHelluna, Log, TEXT("[Phase6] LoadAndSendInventoryToClient: DB Loadout 삭제 완료 | PlayerId=%s"), *PlayerId);
				}

				if (!bLoaded)
				{
					TArray<FInv_SavedItemData> Items = DB->LoadPlayerStash(PlayerId);
					if (Items.Num() > 0)
					{
						LoadedData.Items = MoveTemp(Items);
						LoadedData.LastSaveTime = FDateTime::Now();
						bLoaded = true;
						UE_LOG(LogHelluna, Log, TEXT("[Phase3] LoadAndSendInventoryToClient: SQLite Stash 로드 성공 | PlayerId=%s | %d개"),
							*PlayerId, LoadedData.Items.Num());
					}
					else
					{
						UE_LOG(LogHelluna, Log, TEXT("[Phase3] LoadAndSendInventoryToClient: SQLite 데이터 없음 (신규 유저) | PlayerId=%s"), *PlayerId);
					}
				}
			}
			else
			{
				// [Fix16] bFileTransferOnly 모드(게임서버)에서는 .sav 폴백 금지
				// — .sav에 이전 세션의 오래된 데이터가 있으면 아이템 복제 발생
				// — JSON Loadout 파일이 없으면 빈손 출격이므로 빈 인벤토리로 시작
				if (DB && DB->IsFileTransferOnly())
				{
					UE_LOG(LogHelluna, Warning, TEXT("[Fix16] LoadAndSendInventoryToClient: 파일 전송 전용 모드 — .sav 폴백 차단, 빈 인벤토리로 시작 | PlayerId=%s"), *PlayerId);
					return;
				}

				UE_LOG(LogHelluna, Warning, TEXT("[Phase3] LoadAndSendInventoryToClient: SQLite 미준비 (재오픈도 실패) — .sav 폴백 | PlayerId=%s"), *PlayerId);
				Super::LoadAndSendInventoryToClient(PC);
				return;
			}
		}
	}

	// 데이터가 없으면 종료 (신규 유저는 빈 인벤토리로 시작)
	if (!bLoaded || LoadedData.IsEmpty())
	{
		return;
	}

	// ── InvComp에 아이템 복원 ──
	UInv_InventoryComponent* InvComp = PC->FindComponentByClass<UInv_InventoryComponent>();
	if (!IsValid(InvComp))
	{
		return;
	}

	// Fix 10: 장착 아이템 GridPosition 정리 (부모와 동일)
	for (FInv_SavedItemData& LoadItem : LoadedData.Items)
	{
		if (LoadItem.bEquipped && LoadItem.GridPosition != FIntPoint(-1, -1))
		{
			LoadItem.GridPosition = FIntPoint(-1, -1);
		}
	}

	FInv_ItemTemplateResolver Resolver;
	Resolver.BindLambda([this](const FGameplayTag& ItemType) -> UInv_ItemComponent*
	{
		TSubclassOf<AActor> ActorClass = ResolveItemClass(ItemType);
		if (!ActorClass)
		{
			return nullptr;
		}
		return FindItemComponentTemplate(ActorClass);
	});

	InvComp->RestoreFromSaveData(LoadedData, Resolver);

	// ── 클라이언트에 데이터 전송 (청크 분할, 부모와 동일) ──
	AInv_PlayerController* InvPC = Cast<AInv_PlayerController>(PC);
	if (!IsValid(InvPC))
	{
		return;
	}

	const TArray<FInv_SavedItemData>& AllItems = LoadedData.Items;
	constexpr int32 ChunkSize = 5;

	if (AllItems.Num() <= ChunkSize)
	{
		InvPC->Client_ReceiveInventoryData(AllItems);
	}
	else
	{
		const int32 TotalItems = AllItems.Num();
		for (int32 StartIdx = 0; StartIdx < TotalItems; StartIdx += ChunkSize)
		{
			const int32 EndIdx = FMath::Min(StartIdx + ChunkSize, TotalItems);
			const bool bIsLast = (EndIdx >= TotalItems);

			TArray<FInv_SavedItemData> Chunk;
			Chunk.Reserve(EndIdx - StartIdx);
			for (int32 i = StartIdx; i < EndIdx; ++i)
			{
				Chunk.Add(AllItems[i]);
			}

			InvPC->Client_ReceiveInventoryDataChunk(Chunk, bIsLast);
		}
	}
}

// ════════════════════════════════════════════════════════════════════════════════
// 📌 [Phase 3 / Fix36] CheckAndRecoverFromCrash — PostLogin 시 크래시 복구 체크
// [Fix36] deploy_state 기반 크래시 감지. Loadout 존재 ≠ 크래시.
// 크래시 시 Loadout은 출격 전 상태 그대로 보존 (Stash 이동 없음)
// ════════════════════════════════════════════════════════════════════════════════
void AHellunaBaseGameMode::CheckAndRecoverFromCrash(const FString& PlayerId)
{
	if (PlayerId.IsEmpty())
	{
		return;
	}

	UGameInstance* GI = GetGameInstance();
	UHellunaSQLiteSubsystem* DB = GI ? GI->GetSubsystem<UHellunaSQLiteSubsystem>() : nullptr;
	if (!DB || !DB->IsDatabaseReady())
	{
		return;
	}

	// [Fix36] deploy_state 기반 크래시 감지
	if (DB->IsPlayerDeployed(PlayerId))
	{
		UE_LOG(LogHelluna, Warning, TEXT("[Fix36] 크래시 감지: 출격 상태인데 게임서버에서 복귀하지 않음 | PlayerId=%s"), *PlayerId);
		DB->SetPlayerDeployed(PlayerId, false);
		UE_LOG(LogHelluna, Log, TEXT("[Fix36] deploy 상태 해제 완료 — Loadout은 출격 전 상태 보존 | PlayerId=%s"), *PlayerId);
	}
}



// ════════════════════════════════════════════════════════════════════════════════
// 📌 OnInvControllerEndPlay — 부모 + 게임별 로직 분리
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 변경 전:
//    저장 + 캐시 병합 + 로그아웃 처리 (~60줄) 전부 여기
//
// 📌 변경 후:
//    저장+캐시 병합 → Super::OnInventoryControllerEndPlay()
//    로그아웃 처리(PlayerState, GameInstance) → 여기서 직접
//
// ════════════════════════════════════════════════════════════════════════════════
void AHellunaBaseGameMode::OnInvControllerEndPlay(
	AInv_PlayerController* PlayerController,
	const TArray<FInv_SavedItemData>& SavedItems)
{
	// ── 인벤토리 저장 (부모가 캐시 병합 + 디스크 저장 전부 처리) ──
	OnInventoryControllerEndPlay(PlayerController, SavedItems);

	// ── 게임별 로그아웃 처리 ──
	FString PlayerId = GetPlayerSaveId(PlayerController);
	if (!PlayerId.IsEmpty())
	{
		AHellunaPlayerState* PS = PlayerController->GetPlayerState<AHellunaPlayerState>();
		if (IsValid(PS) && PS->IsLoggedIn())
		{
			PS->ClearLoginInfo();
		}

		if (UMDF_GameInstance* GI = Cast<UMDF_GameInstance>(UGameplayStatics::GetGameInstance(GetWorld())))
		{
			GI->RegisterLogout(PlayerId);
		}

		// 캐릭터 사용 해제 (Logout 안 거칠 수 있음 — 이중 호출 안전)
		UnregisterCharacterUse(PlayerId);
	}
}

// ════════════════════════════════════════════════════════════════════════════════
// 📌 ResolveItemClass — DataTable로 아이템 클래스 결정
// ════════════════════════════════════════════════════════════════════════════════
TSubclassOf<AActor> AHellunaBaseGameMode::ResolveItemClass(const FGameplayTag& ItemType)
{
	if (!IsValid(ItemTypeMappingDataTable))
	{
		UE_LOG(LogHelluna, Error, TEXT("[ItemTypeMapping] ItemTypeMappingDataTable이 설정되지 않음!"));
		return nullptr;
	}

	TSubclassOf<AActor> Result = UHellunaItemTypeMapping::GetActorClassFromItemType(ItemTypeMappingDataTable, ItemType);
	if (!Result)
	{
		UE_LOG(LogTemp, Error, TEXT("[ItemTypeMapping] '%s' 매핑 실패! DT_ItemTypeMapping에 행 추가 필요"),
			*ItemType.ToString());
	}
	return Result;
}

// ════════════════════════════════════════════════════════════════════════════════
// 📌 GetPlayerSaveId — HellunaPlayerState에서 고유 ID 가져오기
// ════════════════════════════════════════════════════════════════════════════════
FString AHellunaBaseGameMode::GetPlayerSaveId(APlayerController* PC) const
{
	if (!IsValid(PC)) return FString();

	// 1순위: HellunaPlayerState에서 로그인 ID
	if (AHellunaPlayerState* PS = PC->GetPlayerState<AHellunaPlayerState>())
	{
		FString Id = PS->GetPlayerUniqueId();
		if (!Id.IsEmpty()) return Id;
	}

	// 2순위: 미리 등록된 맵에서 검색
	if (const FString* Found = ControllerToPlayerIdMap.Find(PC))
	{
		return *Found;
	}

	return FString();
}

// ════════════════════════════════════════════════════════════════════════════════
// 🔧 디버그 함수들
// ════════════════════════════════════════════════════════════════════════════════

// ════════════════════════════════════════════════════════════════════════════════
// 📌 DebugTestItemTypeMapping - ItemType 매핑 테스트
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 역할:
//    ItemTypeMappingDataTable이 올바르게 설정되었는지 테스트
//
// 📌 테스트 항목:
//    - GameItems.Equipment.Weapons.Axe
//    - GameItems.Consumables.Potions.Blue.Small
//    - GameItems.Consumables.Potions.Red.Small
//    - GameItems.Craftables.FireFernFruit
//    - GameItems.Craftables.LuminDaisy
//
// ════════════════════════════════════════════════════════════════════════════════
void AHellunaBaseGameMode::DebugTestItemTypeMapping()
{
	if (!IsValid(ItemTypeMappingDataTable))
	{
		UE_LOG(LogHelluna, Error, TEXT("[BaseGameMode] ItemTypeMappingDataTable not set!"));
		return;
	}

	TArray<FString> TestTags = {
		TEXT("GameItems.Equipment.Weapons.Axe"),
		TEXT("GameItems.Consumables.Potions.Blue.Small"),
		TEXT("GameItems.Consumables.Potions.Red.Small"),
		TEXT("GameItems.Craftables.FireFernFruit"),
		TEXT("GameItems.Craftables.LuminDaisy"),
		TEXT("GameItems.Equipment.Attachments.Muzzle"),
	};

	int32 SuccessCount = 0;
	for (const FString& TagString : TestTags)
	{
		FGameplayTag TestTag = FGameplayTag::RequestGameplayTag(FName(*TagString), false);
		if (TestTag.IsValid())
		{
			TSubclassOf<AActor> FoundClass = UHellunaItemTypeMapping::GetActorClassFromItemType(
				ItemTypeMappingDataTable, TestTag);
			if (FoundClass)
			{
				SuccessCount++;
			}
			else
			{
				UE_LOG(LogHelluna, Error, TEXT("[ItemTypeMapping] 매핑 실패: %s — DataTable에 행 추가 필요!"), *TagString);
			}
		}
	}

#if HELLUNA_DEBUG_GAMEMODE
	UE_LOG(LogHelluna, Warning, TEXT("[BaseGameMode] ItemTypeMapping Test: %d/%d passed"), SuccessCount, TestTags.Num());
#endif
}

// ════════════════════════════════════════════════════════════════════════════════
// 📌 DebugPrintAllItemMappings - 모든 아이템 매핑 출력
// ════════════════════════════════════════════════════════════════════════════════
// [Step3] Shipping 빌드에서 디버그 전용 함수 비활성화
// UFUNCTION 선언은 유지 (BP 참조 보호), 내부 로직만 #if 가드
void AHellunaBaseGameMode::DebugPrintAllItemMappings()
{
#if !UE_BUILD_SHIPPING
	if (IsValid(ItemTypeMappingDataTable))
	{
		UHellunaItemTypeMapping::DebugPrintAllMappings(ItemTypeMappingDataTable);
	}
#endif
}

// ════════════════════════════════════════════════════════════════════════════════
// 📌 DebugTestInventorySaveGame - 인벤토리 SaveGame 테스트
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 역할:
//    부모 클래스의 SaveCollectedItems → InventorySaveGame 저장 흐름 테스트
//
// ════════════════════════════════════════════════════════════════════════════════
void AHellunaBaseGameMode::DebugTestInventorySaveGame()
{
#if !UE_BUILD_SHIPPING
	const FString TestPlayerId = TEXT("TestPlayer_Debug");

	// 테스트 데이터 생성
	FInv_SavedItemData TestItem;
	TestItem.ItemType = FGameplayTag::RequestGameplayTag(FName("GameItems.Equipment.Weapons.Axe"), false);
	TestItem.StackCount = 1;
	TestItem.GridPosition = FIntPoint(0, 0);
	TestItem.bEquipped = true;
	TestItem.WeaponSlotIndex = 0;

	TArray<FInv_SavedItemData> TestItems;
	TestItems.Add(TestItem);

	// 부모의 SaveCollectedItems로 저장 테스트
	SaveCollectedItems(TestPlayerId, TestItems);

	UE_LOG(LogHelluna, Warning, TEXT("[BaseGameMode] DebugTestInventorySaveGame: SaveCollectedItems 호출 완료 (PlayerId=%s, Items=%d)"),
		*TestPlayerId, TestItems.Num());
#endif
}

// ================================================================
// [Step3 O-06] 중복 제거: DebugRequestSaveAllInventory는 DebugForceAutoSave의 래퍼
// 두 함수 모두 BlueprintCallable이므로 선언은 유지하되, 구현을 통합
// ================================================================
void AHellunaBaseGameMode::DebugRequestSaveAllInventory()
{
#if !UE_BUILD_SHIPPING
	DebugForceAutoSave();
#endif
}

// ════════════════════════════════════════════════════════════════════════════════
// 📌 DebugForceAutoSave - 디버그용 강제 자동저장
// ════════════════════════════════════════════════════════════════════════════════
void AHellunaBaseGameMode::DebugForceAutoSave()
{
#if !UE_BUILD_SHIPPING
	ForceAutoSave();
#endif
}

// ════════════════════════════════════════════════════════════════════════════════
// 📌 DebugTestLoadInventory - 디버그용 인벤토리 로드 테스트
// ════════════════════════════════════════════════════════════════════════════════
void AHellunaBaseGameMode::DebugTestLoadInventory()
{
#if !UE_BUILD_SHIPPING
	UWorld* DebugWorld = GetWorld();
	if (!DebugWorld) { return; }
	for (FConstPlayerControllerIterator It = DebugWorld->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (IsValid(PC))
		{
			LoadAndSendInventoryToClient(PC);
			return;
		}
	}
#endif
}
