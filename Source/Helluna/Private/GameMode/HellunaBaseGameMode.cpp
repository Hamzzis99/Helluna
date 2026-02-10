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
#include "Helluna.h"  // 전처리기 플래그
#include "GameMode/HellunaBaseGameState.h"
#include "Login/HellunaLoginController.h"
#include "Login/HellunaAccountSaveGame.h"
#include "Player/HellunaPlayerState.h"
#include "Inventory/HellunaInventorySaveGame.h"
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
	InventorySaveGame = UHellunaInventorySaveGame::LoadOrCreate();

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

	StartAutoSaveTimer();
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

		// 0.5초 딜레이: Controller 초기화 완료 대기
		FTimerHandle TimerHandle;
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
#if WITH_EDITOR
		FString DebugPlayerId = FString::Printf(TEXT("DEBUG_%s"), *FGuid::NewGuid().ToString());

		UE_LOG(LogHelluna, Warning, TEXT(""));
		UE_LOG(LogHelluna, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
		UE_LOG(LogHelluna, Warning, TEXT("║  🔧 [BaseGameMode] 개발자 모드 - 로그인 스킵              ║"));
		UE_LOG(LogHelluna, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
		UE_LOG(LogHelluna, Warning, TEXT("║ DebugPlayerId: %s"), *DebugPlayerId);
		UE_LOG(LogHelluna, Warning, TEXT("║ Controller: %s"), *GetNameSafe(NewPlayer));
		UE_LOG(LogHelluna, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));

		// 1. PlayerState에 GUID 부여
		if (PS)
		{
			PS->SetLoginInfo(DebugPlayerId);
		}

		// 2. GameInstance에 로그인 등록
		if (UMDF_GameInstance* GI = Cast<UMDF_GameInstance>(UGameplayStatics::GetGameInstance(GetWorld())))
		{
			GI->RegisterLogin(DebugPlayerId);
		}

		// 3. ControllerToPlayerIdMap 등록 (Logout/인벤토리 저장 시 필요)
		ControllerToPlayerIdMap.Add(NewPlayer, DebugPlayerId);

		// 4. Controller EndPlay 델리게이트 바인딩 (인벤토리 저장용)
		AInv_PlayerController* InvPC = Cast<AInv_PlayerController>(NewPlayer);
		if (IsValid(InvPC))
		{
			InvPC->OnControllerEndPlay.AddDynamic(this, &AHellunaBaseGameMode::OnInvControllerEndPlay);
		}

		// 5. 게임 초기화 (첫 플레이어일 때)
		if (!bGameInitialized)
		{
			InitializeGame();
		}

		// 6. 인벤토리 로드 (1초 딜레이 - 컴포넌트 초기화 대기)
		FTimerHandle InventoryLoadTimer;
		GetWorldTimerManager().SetTimer(InventoryLoadTimer, [this, NewPlayer]()
		{
			if (IsValid(NewPlayer))
			{
				LoadAndSendInventoryToClient(NewPlayer);
			}
		}, 1.0f, false);

		// 타임아웃 타이머 시작하지 않음!
#else
		// 에디터 외 빌드에서는 개발자 모드 무시 → 정상 로그인 흐름
		UE_LOG(LogHelluna, Error, TEXT("[BaseGameMode] bDebugSkipLogin은 에디터 전용입니다!"));
		FTimerHandle& TimeoutTimer = LoginTimeoutTimers.FindOrAdd(NewPlayer);
		GetWorldTimerManager().SetTimer(TimeoutTimer, [this, NewPlayer]()
		{
			if (IsValid(NewPlayer))
			{
				OnLoginTimeout(NewPlayer);
			}
		}, LoginTimeoutSeconds, false);
#endif
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

	APlayerController* NewController = GetWorld()->SpawnActor<APlayerController>(
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
			ControllerToPlayerIdMap.Add(InvPC, PlayerId);
		}
	}

#if HELLUNA_DEBUG_GAMEMODE
	UE_LOG(LogHelluna, Warning, TEXT("║ Controller 교체 완료!"));
	UE_LOG(LogHelluna, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
#endif

	// 0.3초 딜레이 후 캐릭터 소환 (Controller 초기화 완료 대기)
	FTimerHandle TimerHandle;
	GetWorldTimerManager().SetTimer(TimerHandle, [this, NewController]()
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

	APawn* NewPawn = GetWorld()->SpawnActor<APawn>(SpawnClass, SpawnLocation, SpawnRotation, SpawnParams);

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
	// 📌 인벤토리 로드 (1초 딜레이)
	// ────────────────────────────────────────────────────────────────────────────
	// 딜레이 이유: InventoryComponent 초기화 완료 대기
	// ────────────────────────────────────────────────────────────────────────────
	FTimerHandle InventoryLoadTimer;
	GetWorldTimerManager().SetTimer(InventoryLoadTimer, [this, PlayerController]()
	{
		if (IsValid(PlayerController))
		{
			LoadAndSendInventoryToClient(PlayerController);
		}
	}, 1.0f, false);
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

	if (!PlayerId.IsEmpty())
	{
		// ────────────────────────────────────────────────────────────────────────
		// 📌 인벤토리 저장
		// ────────────────────────────────────────────────────────────────────────
		APawn* Pawn = Exiting->GetPawn();
		UInv_InventoryComponent* InvComp = Pawn ? Pawn->FindComponentByClass<UInv_InventoryComponent>() : nullptr;

		if (InvComp)
		{
			// 현재 인벤토리 수집 후 저장
			TArray<FInv_SavedItemData> CollectedItems = InvComp->CollectInventoryDataForSave();
			FHellunaPlayerInventoryData SaveData;
			SaveData.LastSaveTime = FDateTime::Now();

			for (const FInv_SavedItemData& Item : CollectedItems)
			{
				FHellunaInventoryItemData DestItem;
				DestItem.ItemType = Item.ItemType;
				DestItem.StackCount = Item.StackCount;
				DestItem.GridPosition = Item.GridPosition;
				DestItem.GridCategory = Item.GridCategory;
				DestItem.EquipSlotIndex = Item.bEquipped ? Item.WeaponSlotIndex : -1;
				SaveData.Items.Add(DestItem);
			}

			if (IsValid(InventorySaveGame) && SaveData.Items.Num() > 0)
			{
				InventorySaveGame->SavePlayerInventory(PlayerId, SaveData);
				UHellunaInventorySaveGame::Save(InventorySaveGame);
			}
		}
		else
		{
			// 캐시된 데이터 저장 (InvComp 없는 경우)
			if (FHellunaPlayerInventoryData* CachedData = CachedPlayerInventoryData.Find(PlayerId))
			{
				CachedData->LastSaveTime = FDateTime::Now();
				if (IsValid(InventorySaveGame))
				{
					InventorySaveGame->SavePlayerInventory(PlayerId, *CachedData);
					UHellunaInventorySaveGame::Save(InventorySaveGame);
				}
			}
		}

		// 캐시 데이터 제거
		CachedPlayerInventoryData.Remove(PlayerId);

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

		// 로그인 상태였으면 게임 컨트롤러로 전환
		if (bSavedIsLoggedIn)
		{
			APlayerController* TraveledPC = Cast<APlayerController>(C);
			if (TraveledPC)
			{
				FTimerHandle TimerHandle;
				GetWorldTimerManager().SetTimer(TimerHandle, [this, TraveledPC, SavedPlayerId, SavedHeroType]()
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

	// 성공 알림
	if (LoginController)
	{
		LoginController->Client_CharacterSelectionResult(true, TEXT(""));
	}

	// 0.3초 후 GameController로 전환
	if (LoginController && LoginController->GetGameControllerClass())
	{
		FTimerHandle TimerHandle;
		GetWorldTimerManager().SetTimer(TimerHandle, [this, LoginController, PlayerId, HeroType]()
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
// 📌 SaveAllPlayersInventory - 모든 플레이어 인벤토리 저장
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 호출 시점:
//    - 맵 이동 전 (ServerTravel 호출 전)
//    - 외부에서 직접 호출 (예: 라운드 종료 시)
//
// 📌 처리 흐름:
//    1. 모든 PlayerController 순회
//    2. 로그인 상태 확인
//    3. InventoryComponent에서 아이템 데이터 수집
//    4. EquipmentComponent에서 장착 상태 추가
//    5. SaveInventoryFromCharacterEndPlay() 호출 → 파일 저장
//
// 📌 반환값:
//    저장된 플레이어 수
//
// 📌 저장 위치:
//    Saved/SaveGames/HellunaInventory.sav
//
// ════════════════════════════════════════════════════════════════════════════════
int32 AHellunaBaseGameMode::SaveAllPlayersInventory()
{
#if HELLUNA_DEBUG_INVENTORY_SAVE
	UE_LOG(LogHelluna, Warning, TEXT("[BaseGameMode] SaveAllPlayersInventory"));
#endif

	int32 SavedCount = 0;

	// 모든 PlayerController 순회
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (!IsValid(PC)) continue;

		// 로그인 상태 확인
		AHellunaPlayerState* PS = PC->GetPlayerState<AHellunaPlayerState>();
		if (!PS || !PS->IsLoggedIn()) continue;

		FString PlayerId = PS->GetPlayerUniqueId();
		if (PlayerId.IsEmpty()) continue;

		// InventoryComponent에서 데이터 수집
		UInv_InventoryComponent* InvComp = PC->FindComponentByClass<UInv_InventoryComponent>();
		if (!InvComp) continue;

		TArray<FInv_SavedItemData> CollectedItems = InvComp->CollectInventoryDataForSave();

		// ────────────────────────────────────────────────────────────────────────
		// 📌 장착 상태 추가
		// ────────────────────────────────────────────────────────────────────────
		// EquipmentComponent에서 장착 중인 아이템 정보를 가져와
		// 해당 아이템의 bEquipped, WeaponSlotIndex 설정
		// ────────────────────────────────────────────────────────────────────────
		UInv_EquipmentComponent* EquipComp = PC->FindComponentByClass<UInv_EquipmentComponent>();
		if (EquipComp)
		{
			const TArray<TObjectPtr<AInv_EquipActor>>& EquippedActors = EquipComp->GetEquippedActors();
			for (const TObjectPtr<AInv_EquipActor>& EquipActor : EquippedActors)
			{
				if (EquipActor.Get())
				{
					FGameplayTag ItemType = EquipActor->GetEquipmentType();
					int32 SlotIndex = EquipActor->GetWeaponSlotIndex();
					for (FInv_SavedItemData& Item : CollectedItems)
					{
						if (Item.ItemType == ItemType && !Item.bEquipped)
						{
							Item.bEquipped = true;
							Item.WeaponSlotIndex = SlotIndex;
							break;
						}
					}
				}
			}
		}

		// 파일 저장
		if (CollectedItems.Num() > 0)
		{
			SaveInventoryFromCharacterEndPlay(PlayerId, CollectedItems);
			SavedCount++;
		}
	}

	return SavedCount;
}

// ════════════════════════════════════════════════════════════════════════════════
// 📌 LoadAndSendInventoryToClient - 인벤토리 로드 후 클라이언트 전송
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 호출 시점:
//    SpawnHeroCharacter()에서 캐릭터 소환 후 (1초 딜레이)
//
// 📌 매개변수:
//    - PC: 인벤토리를 로드할 플레이어의 Controller
//
// 📌 처리 흐름:
//    1. PlayerState에서 PlayerId 가져오기
//    2. InventorySaveGame에서 저장된 데이터 로드
//    3. 아이템 액터 스폰 (서버) - ItemTypeMappingDataTable 사용
//    4. InventoryComponent에 아이템 추가
//    5. 그리드 위치 설정
//    6. 장착 상태 복원 (OnItemEquipped 브로드캐스트)
//    7. 클라이언트에 데이터 전송 (Client_ReceiveInventoryData RPC)
//
// 📌 로드 위치:
//    Saved/SaveGames/HellunaInventory.sav
//
// 📌 아이템 스폰 위치:
//    FVector(0.f, 0.f, -10000.f) - 맵 아래 안 보이는 곳
//
// ════════════════════════════════════════════════════════════════════════════════
void AHellunaBaseGameMode::LoadAndSendInventoryToClient(APlayerController* PC)
{
#if HELLUNA_DEBUG_INVENTORY_SAVE
	UE_LOG(LogHelluna, Warning, TEXT("[BaseGameMode] LoadAndSendInventoryToClient"));
#endif

	if (!HasAuthority() || !IsValid(PC)) return;

	AHellunaPlayerState* PS = PC->GetPlayerState<AHellunaPlayerState>();
	if (!IsValid(PS)) return;

	FString PlayerUniqueId = PS->GetPlayerUniqueId();
	if (PlayerUniqueId.IsEmpty()) return;

	if (!IsValid(InventorySaveGame)) return;

	// ────────────────────────────────────────────────────────────────────────
	// 📌 저장된 데이터 로드
	// ────────────────────────────────────────────────────────────────────────
	FHellunaPlayerInventoryData LoadedData;
	bool bDataFound = InventorySaveGame->LoadPlayerInventory(PlayerUniqueId, LoadedData);

	if (!bDataFound || LoadedData.Items.Num() == 0) return;

	UInv_InventoryComponent* InvComp = PC->FindComponentByClass<UInv_InventoryComponent>();
	if (!IsValid(InvComp) || !IsValid(ItemTypeMappingDataTable)) return;

	// ────────────────────────────────────────────────────────────────────────
	// 📌 아이템 액터 스폰 및 인벤토리 추가
	// ────────────────────────────────────────────────────────────────────────
	for (const FHellunaInventoryItemData& ItemData : LoadedData.Items)
	{
		if (!ItemData.ItemType.IsValid()) continue;

		// ItemType → ActorClass 변환
		TSubclassOf<AActor> ActorClass = UHellunaItemTypeMapping::GetActorClassFromItemType(
			ItemTypeMappingDataTable, ItemData.ItemType);
		if (!ActorClass) continue;

		// 아이템 액터 스폰 (맵 아래 안 보이는 곳)
		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		AActor* SpawnedActor = GetWorld()->SpawnActor<AActor>(ActorClass, FVector(0.f, 0.f, -10000.f), FRotator::ZeroRotator, SpawnParams);
		if (!IsValid(SpawnedActor)) continue;

		UInv_ItemComponent* ItemComp = SpawnedActor->FindComponentByClass<UInv_ItemComponent>();
		if (!IsValid(ItemComp))
		{
			SpawnedActor->Destroy();
			continue;
		}

		// 인벤토리에 추가
		InvComp->Server_AddNewItem(ItemComp, ItemData.StackCount, 0);

		// 그리드 위치 설정
		const int32 Columns = 8;
		int32 SavedGridIndex = ItemData.GridPosition.Y * Columns + ItemData.GridPosition.X;
		InvComp->SetLastEntryGridPosition(SavedGridIndex, ItemData.GridCategory);
	}

	// ────────────────────────────────────────────────────────────────────────
	// 📌 장착 상태 복원
	// ────────────────────────────────────────────────────────────────────────
	TSet<UInv_InventoryItem*> ServerProcessedItems;
	for (const FHellunaInventoryItemData& ItemData : LoadedData.Items)
	{
		if (ItemData.EquipSlotIndex < 0) continue;

		UInv_InventoryItem* FoundItem = InvComp->FindItemByTypeExcluding(ItemData.ItemType, ServerProcessedItems);
		if (FoundItem)
		{
			InvComp->OnItemEquipped.Broadcast(FoundItem, ItemData.EquipSlotIndex);
			ServerProcessedItems.Add(FoundItem);
		}
	}

	// ────────────────────────────────────────────────────────────────────────
	// 📌 클라이언트에 데이터 전송
	// ────────────────────────────────────────────────────────────────────────
	TArray<FInv_SavedItemData> SavedItemsForClient;
	for (const FHellunaInventoryItemData& ItemData : LoadedData.Items)
	{
		FInv_SavedItemData ClientData;
		ClientData.ItemType = ItemData.ItemType;
		ClientData.StackCount = ItemData.StackCount;
		ClientData.GridPosition = ItemData.GridPosition;
		ClientData.GridCategory = ItemData.GridCategory;
		ClientData.bEquipped = (ItemData.EquipSlotIndex >= 0);
		ClientData.WeaponSlotIndex = ItemData.EquipSlotIndex;
		SavedItemsForClient.Add(ClientData);
	}

	AInv_PlayerController* InvPC = Cast<AInv_PlayerController>(PC);
	if (IsValid(InvPC))
	{
		InvPC->Client_ReceiveInventoryData(SavedItemsForClient);
	}
}

// ════════════════════════════════════════════════════════════════════════════════
// 📌 SaveInventoryFromCharacterEndPlay - 인벤토리 저장 (내부 함수)
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 역할:
//    수집된 인벤토리 데이터를 SaveGame에 저장
//
// 📌 호출 시점:
//    - SaveAllPlayersInventory()
//    - Logout()
//    - OnInvControllerEndPlay()
//
// 📌 매개변수:
//    - PlayerId: 저장할 플레이어 아이디
//    - CollectedItems: 저장할 아이템 데이터 배열
//
// 📌 처리 흐름:
//    1. FInv_SavedItemData → FHellunaInventoryItemData 변환
//    2. InventorySaveGame.SavePlayerInventory() 호출
//    3. 파일 저장 (UHellunaInventorySaveGame::Save)
//    4. 캐시에 저장 (CachedPlayerInventoryData)
//
// ════════════════════════════════════════════════════════════════════════════════
void AHellunaBaseGameMode::SaveInventoryFromCharacterEndPlay(const FString& PlayerId, const TArray<FInv_SavedItemData>& CollectedItems)
{
	if (PlayerId.IsEmpty() || CollectedItems.Num() == 0) return;

	// 데이터 변환
	FHellunaPlayerInventoryData SaveData;
	SaveData.LastSaveTime = FDateTime::Now();

	for (const FInv_SavedItemData& Item : CollectedItems)
	{
		FHellunaInventoryItemData DestItem;
		DestItem.ItemType = Item.ItemType;
		DestItem.StackCount = Item.StackCount;
		DestItem.GridPosition = Item.GridPosition;
		DestItem.GridCategory = Item.GridCategory;
		DestItem.EquipSlotIndex = Item.bEquipped ? Item.WeaponSlotIndex : -1;
		SaveData.Items.Add(DestItem);
	}

	// 파일 저장
	if (IsValid(InventorySaveGame))
	{
		InventorySaveGame->SavePlayerInventory(PlayerId, SaveData);
		UHellunaInventorySaveGame::Save(InventorySaveGame);
	}

	// 캐시에 저장 (로그아웃 시 사용)
	CachedPlayerInventoryData.Add(PlayerId, SaveData);
}

// ════════════════════════════════════════════════════════════════════════════════
// 📌 OnPlayerInventoryStateReceived - 클라이언트로부터 인벤토리 상태 수신
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 호출 시점:
//    RequestAllPlayersInventoryState() 후 클라이언트가 응답할 때
//    (Server_SendInventoryState RPC → 이 함수 호출)
//
// 📌 매개변수:
//    - PlayerController: 응답한 플레이어의 Controller
//    - SavedItems: 클라이언트의 현재 인벤토리 상태
//
// 📌 처리 흐름:
//    1. PlayerId 가져오기
//    2. 데이터 변환 (FInv_SavedItemData → FHellunaInventoryItemData)
//    3. 캐시에 저장
//    4. 파일 저장
//
// ════════════════════════════════════════════════════════════════════════════════
void AHellunaBaseGameMode::OnPlayerInventoryStateReceived(
	AInv_PlayerController* PlayerController,
	const TArray<FInv_SavedItemData>& SavedItems)
{
	AHellunaPlayerState* PS = PlayerController->GetPlayerState<AHellunaPlayerState>();
	if (!IsValid(PS)) return;

	FString PlayerUniqueId = PS->GetPlayerUniqueId();
	if (PlayerUniqueId.IsEmpty()) return;

	// 데이터 변환
	FHellunaPlayerInventoryData PlayerData;
	PlayerData.LastSaveTime = FDateTime::Now();
	PlayerData.SaveVersion = 1;

	for (const FInv_SavedItemData& SourceItem : SavedItems)
	{
		FHellunaInventoryItemData DestItem;
		DestItem.ItemType = SourceItem.ItemType;
		DestItem.StackCount = SourceItem.StackCount;
		DestItem.GridPosition = SourceItem.GridPosition;
		DestItem.GridCategory = SourceItem.GridCategory;
		DestItem.EquipSlotIndex = SourceItem.bEquipped ? SourceItem.WeaponSlotIndex : -1;
		PlayerData.Items.Add(DestItem);
	}

	// 캐시에 저장
	CachedPlayerInventoryData.Add(PlayerUniqueId, PlayerData);

	// 파일 저장
	if (IsValid(InventorySaveGame))
	{
		InventorySaveGame->SavePlayerInventory(PlayerUniqueId, PlayerData);
		UHellunaInventorySaveGame::Save(InventorySaveGame);
	}
}

// ════════════════════════════════════════════════════════════════════════════════
// 📦 자동저장 시스템
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 동작 방식:
//    BeginPlay() → StartAutoSaveTimer() 호출
//                      ↓
//    AutoSaveIntervalSeconds(기본 300초=5분)마다 OnAutoSaveTimer() 호출
//                      ↓
//    RequestAllPlayersInventoryState() → 모든 플레이어에게 인벤토리 상태 요청
//                      ↓
//    클라이언트가 Server_SendInventoryState() RPC로 응답
//                      ↓
//    OnPlayerInventoryStateReceived() → InventorySaveGame에 저장
//
// ════════════════════════════════════════════════════════════════════════════════

// ════════════════════════════════════════════════════════════════════════════════
// 📌 StartAutoSaveTimer - 자동저장 타이머 시작
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 호출 시점:
//    BeginPlay()에서 호출
//
// 📌 처리 흐름:
//    1. AutoSaveIntervalSeconds 확인 (0 이하면 비활성화)
//    2. 기존 타이머 정리 (StopAutoSaveTimer)
//    3. 새 타이머 시작 (Looping = true)
//
// 📌 타이머 주기:
//    AutoSaveIntervalSeconds (기본 300초 = 5분)
//
// ════════════════════════════════════════════════════════════════════════════════
void AHellunaBaseGameMode::StartAutoSaveTimer()
{
	if (AutoSaveIntervalSeconds <= 0.0f) return;

	StopAutoSaveTimer();

	GetWorldTimerManager().SetTimer(
		AutoSaveTimerHandle,
		this,
		&AHellunaBaseGameMode::OnAutoSaveTimer,
		AutoSaveIntervalSeconds,
		true  // Looping
	);

#if HELLUNA_DEBUG_INVENTORY_SAVE
	UE_LOG(LogHelluna, Warning, TEXT("[BaseGameMode] AutoSave Timer Started (%.0fs)"), AutoSaveIntervalSeconds);
#endif
}

// ════════════════════════════════════════════════════════════════════════════════
// 📌 StopAutoSaveTimer - 자동저장 타이머 중지
// ════════════════════════════════════════════════════════════════════════════════
void AHellunaBaseGameMode::StopAutoSaveTimer()
{
	if (AutoSaveTimerHandle.IsValid())
	{
		GetWorldTimerManager().ClearTimer(AutoSaveTimerHandle);
	}
}

// ════════════════════════════════════════════════════════════════════════════════
// 📌 OnAutoSaveTimer - 자동저장 실행
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 호출 시점:
//    자동저장 타이머 만료 시 (기본 5분마다)
//
// 📌 처리:
//    RequestAllPlayersInventoryState() 호출
//    → 모든 플레이어에게 인벤토리 상태 요청
//
// ════════════════════════════════════════════════════════════════════════════════
void AHellunaBaseGameMode::OnAutoSaveTimer()
{
	RequestAllPlayersInventoryState();
}

// ════════════════════════════════════════════════════════════════════════════════
// 📌 RequestAllPlayersInventoryState - 모든 플레이어 인벤토리 상태 요청
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 호출 시점:
//    - OnAutoSaveTimer() (자동저장)
//    - DebugRequestSaveAllInventory() (디버그)
//
// 📌 처리 흐름:
//    1. 모든 PlayerController 순회
//    2. Inv_PlayerController인지 확인
//    3. 델리게이트 바인딩 (OnInventoryStateReceived)
//    4. Client_RequestInventoryState() RPC 호출
//
// 📌 응답 처리:
//    클라이언트가 Server_SendInventoryState() RPC로 응답
//    → OnPlayerInventoryStateReceived() 호출됨
//
// ════════════════════════════════════════════════════════════════════════════════
void AHellunaBaseGameMode::RequestAllPlayersInventoryState()
{
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (!IsValid(PC)) continue;

		AInv_PlayerController* InvPC = Cast<AInv_PlayerController>(PC);
		if (!IsValid(InvPC)) continue;

		// 델리게이트 바인딩 (중복 방지)
		if (!InvPC->OnInventoryStateReceived.IsBound())
		{
			InvPC->OnInventoryStateReceived.AddDynamic(this, &AHellunaBaseGameMode::OnPlayerInventoryStateReceived);
		}

		RequestPlayerInventoryState(PC);
	}
}

// ════════════════════════════════════════════════════════════════════════════════
// 📌 RequestPlayerInventoryState - 단일 플레이어 인벤토리 상태 요청
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 매개변수:
//    - PC: 요청할 플레이어의 Controller
//
// 📌 처리:
//    Client_RequestInventoryState() RPC 호출
//    → 클라이언트가 현재 인벤토리 상태를 수집하여 응답
//
// ════════════════════════════════════════════════════════════════════════════════
void AHellunaBaseGameMode::RequestPlayerInventoryState(APlayerController* PC)
{
	if (!IsValid(PC)) return;

	AInv_PlayerController* InvPC = Cast<AInv_PlayerController>(PC);
	if (IsValid(InvPC))
	{
		InvPC->Client_RequestInventoryState();
	}
}

// ════════════════════════════════════════════════════════════════════════════════
// 📌 OnInvControllerEndPlay - Controller EndPlay 델리게이트 핸들러
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 호출 시점:
//    Inv_PlayerController가 파괴될 때 (OnControllerEndPlay 델리게이트)
//
// 📌 역할:
//    Controller 파괴 전 인벤토리 저장
//
// 📌 매개변수:
//    - PlayerController: 파괴되는 Controller
//    - SavedItems: 컨트롤러가 수집한 인벤토리 데이터
//
// 📌 처리 흐름:
//    1. ControllerToPlayerIdMap에서 PlayerId 찾기
//    2. 장착 정보 병합 (SavedItems에 없으면 캐시에서 복원)
//    3. SaveInventoryFromCharacterEndPlay() 호출
//    4. PlayerState 로그인 정보 초기화
//    5. GameInstance.RegisterLogout() 호출
//
// ════════════════════════════════════════════════════════════════════════════════
void AHellunaBaseGameMode::OnInvControllerEndPlay(AInv_PlayerController* PlayerController, const TArray<FInv_SavedItemData>& SavedItems)
{
	if (!IsValid(PlayerController)) return;

	// ────────────────────────────────────────────────────────────────────────
	// 📌 PlayerId 찾기
	// ────────────────────────────────────────────────────────────────────────
	// ControllerToPlayerIdMap: SwapToGameController()에서 등록됨
	// EndPlay 시점에 PlayerState가 유효하지 않을 수 있어 미리 매핑해둠
	// ────────────────────────────────────────────────────────────────────────
	FString PlayerId;
	if (FString* FoundPlayerId = ControllerToPlayerIdMap.Find(PlayerController))
	{
		PlayerId = *FoundPlayerId;
		ControllerToPlayerIdMap.Remove(PlayerController);
	}
	else
	{
		AHellunaPlayerState* PS = PlayerController->GetPlayerState<AHellunaPlayerState>();
		if (IsValid(PS))
		{
			PlayerId = PS->GetPlayerUniqueId();
		}
	}

	// ────────────────────────────────────────────────────────────────────────
	// 📌 장착 정보 병합
	// ────────────────────────────────────────────────────────────────────────
	// SavedItems에 장착 정보가 없으면 캐시된 데이터에서 복원
	// (EndPlay 시점에 EquipmentComponent가 이미 파괴되어 정보 유실 가능)
	// ────────────────────────────────────────────────────────────────────────
	TArray<FInv_SavedItemData> MergedItems = SavedItems;

	int32 EquippedCount = 0;
	for (const FInv_SavedItemData& Item : MergedItems)
	{
		if (Item.bEquipped) EquippedCount++;
	}

	// 장착 정보가 없으면 캐시에서 복원
	if (EquippedCount == 0 && !PlayerId.IsEmpty())
	{
		if (FHellunaPlayerInventoryData* CachedData = CachedPlayerInventoryData.Find(PlayerId))
		{
			for (const FHellunaInventoryItemData& CachedItem : CachedData->Items)
			{
				if (CachedItem.EquipSlotIndex >= 0)
				{
					for (FInv_SavedItemData& Item : MergedItems)
					{
						if (Item.ItemType == CachedItem.ItemType && !Item.bEquipped)
						{
							Item.bEquipped = true;
							Item.WeaponSlotIndex = CachedItem.EquipSlotIndex;
							break;
						}
					}
				}
			}
		}
	}

	// 인벤토리 저장
	if (!PlayerId.IsEmpty() && MergedItems.Num() > 0)
	{
		SaveInventoryFromCharacterEndPlay(PlayerId, MergedItems);
	}

	// 로그아웃 처리
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
	}
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
	};

	int32 SuccessCount = 0;
	for (const FString& TagString : TestTags)
	{
		FGameplayTag TestTag = FGameplayTag::RequestGameplayTag(FName(*TagString), false);
		if (TestTag.IsValid())
		{
			TSubclassOf<AActor> FoundClass = UHellunaItemTypeMapping::GetActorClassFromItemType(
				ItemTypeMappingDataTable, TestTag);
			if (FoundClass) SuccessCount++;
		}
	}

#if HELLUNA_DEBUG_GAMEMODE
	UE_LOG(LogHelluna, Warning, TEXT("[BaseGameMode] ItemTypeMapping Test: %d/%d passed"), SuccessCount, TestTags.Num());
#endif
}

// ════════════════════════════════════════════════════════════════════════════════
// 📌 DebugPrintAllItemMappings - 모든 아이템 매핑 출력
// ════════════════════════════════════════════════════════════════════════════════
void AHellunaBaseGameMode::DebugPrintAllItemMappings()
{
	if (IsValid(ItemTypeMappingDataTable))
	{
		UHellunaItemTypeMapping::DebugPrintAllMappings(ItemTypeMappingDataTable);
	}
}

// ════════════════════════════════════════════════════════════════════════════════
// 📌 DebugTestInventorySaveGame - 인벤토리 SaveGame 테스트
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 역할:
//    InventorySaveGame의 저장/로드 기능 테스트
//
// 📌 테스트 내용:
//    1. 테스트 플레이어 데이터 생성 (TestPlayer_Debug)
//    2. 저장 테스트
//    3. 로드 테스트
//    4. 결과 출력
//
// ════════════════════════════════════════════════════════════════════════════════
void AHellunaBaseGameMode::DebugTestInventorySaveGame()
{
	if (!IsValid(InventorySaveGame))
	{
		UE_LOG(LogHelluna, Error, TEXT("[BaseGameMode] InventorySaveGame is nullptr!"));
		return;
	}

	const FString TestPlayerId = TEXT("TestPlayer_Debug");

	// 테스트 데이터 생성
	FHellunaPlayerInventoryData TestData;
	TestData.SaveVersion = 1;

	FHellunaInventoryItemData Item1;
	Item1.ItemType = FGameplayTag::RequestGameplayTag(FName("GameItems.Equipment.Weapons.Axe"), false);
	Item1.StackCount = 1;
	Item1.GridPosition = FIntPoint(0, 0);
	Item1.EquipSlotIndex = 0;
	TestData.Items.Add(Item1);

	// 저장 테스트
	InventorySaveGame->SavePlayerInventory(TestPlayerId, TestData);
	bool bSaveSuccess = UHellunaInventorySaveGame::Save(InventorySaveGame);

	// 로드 테스트
	FHellunaPlayerInventoryData LoadedData;
	bool bLoadSuccess = InventorySaveGame->LoadPlayerInventory(TestPlayerId, LoadedData);

#if HELLUNA_DEBUG_INVENTORY_SAVE
	UE_LOG(LogHelluna, Warning, TEXT("[BaseGameMode] SaveGame Test: Save=%s, Load=%s"),
		bSaveSuccess ? TEXT("OK") : TEXT("FAIL"),
		bLoadSuccess ? TEXT("OK") : TEXT("FAIL"));
#endif
}

// ════════════════════════════════════════════════════════════════════════════════
// 📌 DebugRequestSaveAllInventory - 디버그용 전체 인벤토리 저장 요청
// ════════════════════════════════════════════════════════════════════════════════
void AHellunaBaseGameMode::DebugRequestSaveAllInventory()
{
	RequestAllPlayersInventoryState();
}

// ════════════════════════════════════════════════════════════════════════════════
// 📌 DebugForceAutoSave - 디버그용 강제 자동저장
// ════════════════════════════════════════════════════════════════════════════════
void AHellunaBaseGameMode::DebugForceAutoSave()
{
	OnAutoSaveTimer();
}

// ════════════════════════════════════════════════════════════════════════════════
// 📌 DebugTestLoadInventory - 디버그용 인벤토리 로드 테스트
// ════════════════════════════════════════════════════════════════════════════════
void AHellunaBaseGameMode::DebugTestLoadInventory()
{
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (IsValid(PC))
		{
			LoadAndSendInventoryToClient(PC);
			return;
		}
	}
}
