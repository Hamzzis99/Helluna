// HellunaDefenseGameMode.cpp
// GihyeonMap 전용 GameMode 구현
// 
// ============================================
// 📌 파일 구조 (팀원 참고용)
// ============================================
// 
// 이 파일은 크게 두 부분으로 나뉩니다:
// 
// ┌─────────────────────────────────────────────────────────────┐
// │ 🔐 로그인 시스템 (Line ~30 ~ ~550)                          │
// │   - PostLogin() : 플레이어 접속 시 호출                     │
// │   - ProcessLogin() : 아이디/비밀번호 검증                   │
// │   - OnLoginSuccess() : 로그인 성공 처리                     │
// │   - OnLoginFailed() : 로그인 실패 처리                      │
// │   - OnLoginTimeout() : 로그인 타임아웃 처리                 │
// │   - SwapToGameController() : Controller 교체                │
// │   - SpawnHeroCharacter() : 캐릭터 소환                      │
// │   - Logout() : 로그아웃 (연결 끊김)                         │
// │   - HandleSeamlessTravelPlayer() : 맵 이동 후 로그인 유지   │
// │                                                              │
// │ 🎮 게임 로직 (Line ~550 이후)                               │
// │   - EnterDay() : 낮 시작                                    │
// │   - EnterNight() : 밤 시작                                  │
// │   - SpawnTestMonsters() : 몬스터 스폰                       │
// │   - TrySummonBoss() : 보스 소환                             │
// │   - NotifyMonsterDied() : 몬스터 사망 처리                  │
// │   - etc...                                                   │
// └─────────────────────────────────────────────────────────────┘
// 
// ============================================
// 📌 로그인 흐름 상세
// ============================================
// 
// [1] 플레이어 접속
//     PostLogin() 호출 → 로그인 타임아웃 타이머 시작 (60초)
// 
// [2] 로그인 UI에서 ID/PW 입력
//     LoginWidget → LoginController::OnLoginButtonClicked()
// 
// [3] Server RPC 호출
//     LoginController::Server_RequestLogin() → ProcessLogin()
// 
// [4] 계정 검증
//     ProcessLogin()에서:
//     ├─ GameInstance.IsPlayerLoggedIn() : 동시 접속 체크
//     ├─ AccountSaveGame.HasAccount() : 계정 존재 확인
//     ├─ AccountSaveGame.ValidatePassword() : 비밀번호 검증
//     └─ AccountSaveGame.CreateAccount() : 새 계정 생성
// 
// [5] 로그인 성공
//     OnLoginSuccess()에서:
//     ├─ GameInstance.RegisterLogin() : 접속자 목록 추가
//     ├─ PlayerState.SetLoginInfo() : PlayerUniqueId 설정
//     │   ★ 이 PlayerUniqueId가 인벤토리 저장의 키로 사용됨!
//     └─ Client_LoginResult(true) : 클라이언트에 결과 전달
// 
// [6] Controller 교체
//     SwapToGameController()에서:
//     ├─ LoginController 파괴
//     ├─ GameController 생성 (BP_InvPlayerController 등)
//     └─ SpawnHeroCharacter() 호출
// 
// [7] 캐릭터 소환
//     SpawnHeroCharacter()에서:
//     ├─ HeroCharacter 스폰
//     ├─ Controller.Possess(캐릭터)
//     └─ 첫 플레이어면 InitializeGame() (게임 시작!)
// 
// [8] 로그아웃 (연결 끊김)
//     Logout()에서:
//     ├─ PlayerState.ClearLoginInfo() : 로그인 정보 초기화
//     └─ GameInstance.RegisterLogout() : 접속자 목록에서 제거
// 
// ============================================
// 📌 관련 클래스
// ============================================
// - UHellunaAccountSaveGame : 계정 데이터 저장 (ID/PW)
// - AHellunaLoginController : 로그인 UI + RPC
// - UHellunaLoginWidget : 로그인 UI (ID/PW 입력)
// - AHellunaPlayerState : 로그인된 플레이어 ID 저장 (Replicated)
// - UMDF_GameInstance : 접속자 목록 관리 (동시접속 체크)
// 
// ============================================
// 📌 인벤토리 시스템과의 연계
// ============================================
// - 로그인 성공 시 PlayerState.PlayerUniqueId에 ID 저장
// - 인벤토리 저장/로드 시 이 ID를 키로 사용
// - 예: InventorySaveGame->SavePlayerInventory(PlayerUniqueId, Inventory)
// 
// 📌 작성자: Gihyeon
// 📌 작성일: 2025-01-23
// ============================================

#include "GameMode/HellunaDefenseGameMode.h"

#include "Engine/TargetPoint.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"

// ============================================
// 📌 인벤토리 저장 시스템 관련 헤더
// ============================================
#include "Inventory/HellunaItemTypeMapping.h"  // Phase 1: DataTable 매핑
#include "Inventory/HellunaInventorySaveGame.h" // Phase 2: SaveGame 클래스
#include "Engine/DataTable.h"                   // DataTable 사용
#include "GameplayTagContainer.h"               // FGameplayTag
#include "Items/Components/Inv_ItemComponent.h" // Phase 5: 아이템 스폰용
#include "InventoryManagement/Components/Inv_InventoryComponent.h" // Phase 5: 인벤토리 컴포넌트                 
#include "GameFramework/PlayerController.h"
#include "GameMode/HellunaDefenseGameState.h"
#include "Object/ResourceUsingObject/ResourceUsingObject_SpaceShip.h"
#include "MDF_Function/MDF_Instance/MDF_GameInstance.h"
#include "Player/HellunaPlayerState.h"
#include "Login/HellunaAccountSaveGame.h"
#include "Login/HellunaLoginController.h"
#include "GameFramework/SpectatorPawn.h"

#include "debughelper.h"

AHellunaDefenseGameMode::AHellunaDefenseGameMode()
{
	PrimaryActorTick.bCanEverTick = false;
	bUseSeamlessTravel = true;
	PlayerStateClass = AHellunaPlayerState::StaticClass();
	PlayerControllerClass = AHellunaLoginController::StaticClass();
	DefaultPawnClass = ASpectatorPawn::StaticClass();
}

void AHellunaDefenseGameMode::BeginPlay()
{
	Super::BeginPlay();

	if (!HasAuthority())
		return;

	// 계정 데이터 로드
	AccountSaveGame = UHellunaAccountSaveGame::LoadOrCreate();

	// 인벤토리 데이터 로드
	InventorySaveGame = UHellunaInventorySaveGame::LoadOrCreate();

	// 스폰 포인트 캐싱 (미리 해둠)
	CacheBossSpawnPoints();
	CacheMonsterSpawnPoints();

	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║     [DefenseGameMode] BeginPlay                            ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ PlayerControllerClass: %s"), PlayerControllerClass ? *PlayerControllerClass->GetName() : TEXT("nullptr"));
	UE_LOG(LogTemp, Warning, TEXT("║ PlayerStateClass: %s"), PlayerStateClass ? *PlayerStateClass->GetName() : TEXT("nullptr"));
	UE_LOG(LogTemp, Warning, TEXT("║ DefaultPawnClass: %s"), DefaultPawnClass ? *DefaultPawnClass->GetName() : TEXT("nullptr"));
	UE_LOG(LogTemp, Warning, TEXT("║ HeroCharacterClass: %s"), HeroCharacterClass ? *HeroCharacterClass->GetName() : TEXT("미설정!"));
	UE_LOG(LogTemp, Warning, TEXT("║ AccountCount: %d"), AccountSaveGame ? AccountSaveGame->GetAccountCount() : 0);
	UE_LOG(LogTemp, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ ※ 게임 초기화 대기 중...                                  ║"));
	UE_LOG(LogTemp, Warning, TEXT("║ ※ 첫 플레이어가 로그인 + 캐릭터 소환되면 게임 시작!       ║"));
	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
	UE_LOG(LogTemp, Warning, TEXT(""));

	// ※ EnterDay() 여기서 호출 안함!
	// ※ 첫 플레이어 캐릭터 소환 후 InitializeGame()에서 호출

	// ============================================
	// 📦 [Phase 1] DataTable 매핑 자동 테스트
	// ============================================
	// 
	// PIE 시작 시 자동으로 매핑 테스트 실행
	// Output Log에서 결과 확인!
	// 
	// ▶ 테스트 조건: 
	//    - 서버에서만 실행 (HasAuthority)
	//    - ItemTypeMappingDataTable이 설정되어 있을 때만
	// 
	// ▶ 테스트 비활성화 방법:
	//    - BP_DefenseGameMode에서 DataTable 설정 해제
	//    - 또는 아래 코드 주석 처리
	// ============================================
#if WITH_EDITOR
	if (IsValid(ItemTypeMappingDataTable))
	{
		UE_LOG(LogTemp, Warning, TEXT(""));
		UE_LOG(LogTemp, Warning, TEXT("🔧 [자동 테스트] DataTable 매핑 테스트 시작..."));
		UE_LOG(LogTemp, Warning, TEXT("   (이 메시지는 에디터에서만 표시됩니다)"));
		UE_LOG(LogTemp, Warning, TEXT(""));
		
		// 테스트 함수 호출
		DebugTestItemTypeMapping();
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT(""));
		UE_LOG(LogTemp, Warning, TEXT("⚠️ [자동 테스트] ItemTypeMappingDataTable 미설정!"));
		UE_LOG(LogTemp, Warning, TEXT("   → BP_DefenseGameMode에서 DataTable 설정 필요"));
		UE_LOG(LogTemp, Warning, TEXT("   → 설정 후 PIE 재시작하면 자동 테스트 실행됨"));
		UE_LOG(LogTemp, Warning, TEXT(""));
	}

	// ============================================
	// 📦 [Phase 2] SaveGame 자동 테스트 (비활성화됨)
	// ============================================
	// 
	// ✅ Phase 2 테스트 완료! 더미 데이터 생성 방지를 위해 비활성화
	// 
	// 다시 테스트하려면 아래 주석 해제:
	// ============================================
	/*
	if (IsValid(InventorySaveGame))
	{
		UE_LOG(LogTemp, Warning, TEXT(""));
		UE_LOG(LogTemp, Warning, TEXT("🔧 [자동 테스트] SaveGame 저장/로드 테스트 시작..."));
		UE_LOG(LogTemp, Warning, TEXT("   (이 메시지는 에디터에서만 표시됩니다)"));
		UE_LOG(LogTemp, Warning, TEXT(""));
		
		DebugTestInventorySaveGame();
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT(""));
		UE_LOG(LogTemp, Warning, TEXT("⚠️ [자동 테스트] InventorySaveGame 로드 실패!"));
		UE_LOG(LogTemp, Warning, TEXT(""));
	}
	*/
#endif

	// ============================================
	// 📦 [Phase 4] 자동저장 타이머 시작 (BeginPlay에서도!)
	// ============================================
	// 
	// ⚠️ Listen Server나 로그인 없는 테스트 환경에서는
	//    InitializeGame()이 호출되지 않을 수 있음.
	//    그래서 BeginPlay에서도 타이머를 시작함.
	// 
	// ▶ StartAutoSaveTimer()는 내부적으로 중복 시작 방지됨
	// ============================================
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("🔄 [Phase 4] BeginPlay에서 자동저장 타이머 시작..."));
	StartAutoSaveTimer();
}

void AHellunaDefenseGameMode::InitializeGame()
{
	if (bGameInitialized)
	{
		UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] 이미 초기화됨, 스킵"));
		return;
	}

	bGameInitialized = true;

	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║     [DefenseGameMode] InitializeGame 🎮                    ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ 첫 플레이어 캐릭터 소환 완료!                              ║"));
	UE_LOG(LogTemp, Warning, TEXT("║ → 게임 환경 초기화 시작...                                 ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ • 낮/밤 사이클 시작                                        ║"));
	UE_LOG(LogTemp, Warning, TEXT("║ • 몬스터 스포너 활성화                                     ║"));
	UE_LOG(LogTemp, Warning, TEXT("║ • 보스 시스템 대기                                         ║"));
	UE_LOG(LogTemp, Warning, TEXT("║ • 🔄 자동저장 타이머 시작 (Phase 4)                        ║"));
	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
	UE_LOG(LogTemp, Warning, TEXT(""));

	// 낮/밤 사이클 시작!
	EnterDay();

	// ============================================
	// 📦 [Phase 4] 자동저장 타이머 시작
	// ============================================
	StartAutoSaveTimer();
}

// ============================================
// 🔐 로그인 시스템 - PostLogin
// ============================================
// 
// 📌 호출 시점: 플레이어가 서버에 접속했을 때 (엔진에서 자동 호출)
// 
// 📌 처리 흐름:
//    1. PlayerState 확인
//    2. 이미 로그인됨? (SeamlessTravel로 이동한 경우)
//       → YES: 바로 캐릭터 소환 (SpawnHeroCharacter)
//       → NO: 로그인 타임아웃 타이머 시작
//    3. 로그인 UI가 자동으로 표시됨 (LoginController.BeginPlay에서)
// 
// 📌 주의: 
//    - PlayerControllerClass가 LoginController로 설정되어 있어야 함
//    - DefaultPawnClass는 SpectatorPawn (캐릭터는 로그인 후 소환)
// ============================================
void AHellunaDefenseGameMode::PostLogin(APlayerController* NewPlayer)
{
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║     [DefenseGameMode] PostLogin                            ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ Controller: %s"), *GetNameSafe(NewPlayer));
	UE_LOG(LogTemp, Warning, TEXT("║ ControllerClass: %s"), NewPlayer ? *NewPlayer->GetClass()->GetName() : TEXT("nullptr"));
	UE_LOG(LogTemp, Warning, TEXT("║ ControllerID: %d"), NewPlayer ? NewPlayer->GetUniqueID() : -1);
	UE_LOG(LogTemp, Warning, TEXT("║ NetConnection: %s"), (NewPlayer && NewPlayer->GetNetConnection()) ? TEXT("Valid") : TEXT("nullptr (ListenServer)"));
	UE_LOG(LogTemp, Warning, TEXT("║ GameInitialized: %s"), bGameInitialized ? TEXT("TRUE") : TEXT("FALSE"));

	if (NewPlayer)
	{
		AHellunaPlayerState* PS = NewPlayer->GetPlayerState<AHellunaPlayerState>();
		UE_LOG(LogTemp, Warning, TEXT("║ PlayerState: %s"), PS ? *PS->GetName() : TEXT("nullptr"));
		
		if (PS)
		{
			UE_LOG(LogTemp, Warning, TEXT("║   - PlayerId: '%s'"), *PS->GetPlayerUniqueId());
			UE_LOG(LogTemp, Warning, TEXT("║   - IsLoggedIn: %s"), PS->IsLoggedIn() ? TEXT("TRUE") : TEXT("FALSE"));
		}
	}
	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));

	if (!NewPlayer)
	{
		Super::PostLogin(NewPlayer);
		return;
	}

	AHellunaPlayerState* PS = NewPlayer->GetPlayerState<AHellunaPlayerState>();
	
	// 이미 로그인된 상태 (SeamlessTravel 등)
	if (PS && PS->IsLoggedIn() && !PS->GetPlayerUniqueId().IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] 이미 로그인됨! → HeroCharacter 소환"));
		
		FTimerHandle TimerHandle;
		GetWorldTimerManager().SetTimer(TimerHandle, [this, NewPlayer]()
		{
			if (IsValid(NewPlayer))
			{
				SpawnHeroCharacter(NewPlayer);
			}
		}, 0.5f, false);
	}
	else
	{
		// 로그인 필요 → 타임아웃 타이머 시작
		UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] 로그인 필요! 타임아웃: %.0f초"), LoginTimeoutSeconds);
		
		FTimerHandle& TimeoutTimer = LoginTimeoutTimers.FindOrAdd(NewPlayer);
		GetWorldTimerManager().SetTimer(TimeoutTimer, [this, NewPlayer]()
		{
			if (IsValid(NewPlayer))
			{
				OnLoginTimeout(NewPlayer);
			}
		}, LoginTimeoutSeconds, false);
	}

	UE_LOG(LogTemp, Warning, TEXT(""));

	Super::PostLogin(NewPlayer);
}

// ============================================
// 🔐 로그인 시스템 - ProcessLogin
// ============================================
// 
// 📌 호출 시점: LoginController.Server_RequestLogin() RPC에서 호출
// 
// 📌 매개변수:
//    - PlayerController: 로그인 요청한 플레이어
//    - PlayerId: 입력한 아이디
//    - Password: 입력한 비밀번호
// 
// 📌 처리 흐름:
//    1. 동시 접속 체크 (GameInstance.IsPlayerLoggedIn)
//       → 이미 접속 중인 ID면 거부
//    2. 계정 존재 확인 (AccountSaveGame.HasAccount)
//       → 있으면 비밀번호 검증
//       → 없으면 새 계정 생성
//    3. OnLoginSuccess() 또는 OnLoginFailed() 호출
// 
// 📌 계정 데이터 저장 위치:
//    Saved/SaveGames/HellunaAccounts.sav
// ============================================
void AHellunaDefenseGameMode::ProcessLogin(APlayerController* PlayerController, const FString& PlayerId, const FString& Password)
{
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║     [DefenseGameMode] ProcessLogin                         ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ PlayerId: '%s'"), *PlayerId);
	UE_LOG(LogTemp, Warning, TEXT("║ Controller: %s (ID: %d)"), *GetNameSafe(PlayerController), PlayerController ? PlayerController->GetUniqueID() : -1);
	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));

	if (!HasAuthority())
	{
		UE_LOG(LogTemp, Error, TEXT("[DefenseGameMode] 서버 권한 없음!"));
		return;
	}

	if (!PlayerController)
	{
		UE_LOG(LogTemp, Error, TEXT("[DefenseGameMode] PlayerController nullptr!"));
		return;
	}

	// 동시 접속 체크
	if (IsPlayerLoggedIn(PlayerId))
	{
		UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] 동시 접속 거부: '%s'"), *PlayerId);
		OnLoginFailed(PlayerController, TEXT("이미 접속 중인 계정입니다."));
		return;
	}

	if (!AccountSaveGame)
	{
		UE_LOG(LogTemp, Error, TEXT("[DefenseGameMode] AccountSaveGame nullptr!"));
		OnLoginFailed(PlayerController, TEXT("서버 오류"));
		return;
	}

	// 계정 검증
	if (AccountSaveGame->HasAccount(PlayerId))
	{
		if (AccountSaveGame->ValidatePassword(PlayerId, Password))
		{
			UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] 비밀번호 일치!"));
			OnLoginSuccess(PlayerController, PlayerId);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] 비밀번호 불일치!"));
			OnLoginFailed(PlayerController, TEXT("비밀번호를 확인해주세요."));
		}
	}
	else
	{
		if (AccountSaveGame->CreateAccount(PlayerId, Password))
		{
			UHellunaAccountSaveGame::Save(AccountSaveGame);
			UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] 새 계정 생성: '%s'"), *PlayerId);
			OnLoginSuccess(PlayerController, PlayerId);
		}
		else
		{
			OnLoginFailed(PlayerController, TEXT("계정 생성 실패"));
		}
	}

	UE_LOG(LogTemp, Warning, TEXT(""));
}

// ============================================
// 🔐 로그인 시스템 - OnLoginSuccess
// ============================================
// 
// 📌 호출 시점: ProcessLogin()에서 계정 검증 성공 시
// 
// 📌 처리 흐름:
//    1. 로그인 타임아웃 타이머 취소
//    2. GameInstance.RegisterLogin() - 접속자 목록에 추가
//    3. PlayerState.SetLoginInfo() - PlayerUniqueId 설정
//       ★★★ 이 PlayerUniqueId가 인벤토리 저장 키로 사용됨! ★★★
//    4. Client_LoginResult(true) RPC - 클라이언트에 성공 알림
//    5. 0.5초 후 SwapToGameController() 호출
// 
// 📌 PlayerState.PlayerUniqueId 용도:
//    - 인벤토리 저장: InventorySaveGame[PlayerUniqueId] = 인벤토리데이터
//    - 인벤토리 로드: 인벤토리데이터 = InventorySaveGame[PlayerUniqueId]
// ============================================
void AHellunaDefenseGameMode::OnLoginSuccess(APlayerController* PlayerController, const FString& PlayerId)
{
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║     [DefenseGameMode] OnLoginSuccess ✅                    ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ PlayerId: '%s'"), *PlayerId);
	UE_LOG(LogTemp, Warning, TEXT("║ Controller: %s (ID: %d)"), *GetNameSafe(PlayerController), PlayerController ? PlayerController->GetUniqueID() : -1);
	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));

	if (!PlayerController) return;

	// 타임아웃 타이머 취소
	if (FTimerHandle* Timer = LoginTimeoutTimers.Find(PlayerController))
	{
		GetWorldTimerManager().ClearTimer(*Timer);
		LoginTimeoutTimers.Remove(PlayerController);
		UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] 타임아웃 타이머 취소됨"));
	}

	// GameInstance에 등록
	if (UMDF_GameInstance* GI = Cast<UMDF_GameInstance>(UGameplayStatics::GetGameInstance(GetWorld())))
	{
		GI->RegisterLogin(PlayerId);
		UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] GameInstance에 등록됨"));
	}

	// PlayerState에 저장
	if (AHellunaPlayerState* PS = PlayerController->GetPlayerState<AHellunaPlayerState>())
	{
		PS->SetLoginInfo(PlayerId);
		UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] PlayerState에 저장됨"));
	}

	// Client RPC로 결과 전달
	AHellunaLoginController* LoginController = Cast<AHellunaLoginController>(PlayerController);
	if (LoginController)
	{
		LoginController->Client_LoginResult(true, TEXT(""));
		UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] Client_LoginResult(true) 호출됨"));
	}

	// HeroCharacter 소환 (딜레이)
	FTimerHandle TimerHandle;
	GetWorldTimerManager().SetTimer(TimerHandle, [this, PlayerController, PlayerId]()
	{
		if (IsValid(PlayerController))
		{
			// LoginController인 경우 GameController로 교체
			AHellunaLoginController* LoginController = Cast<AHellunaLoginController>(PlayerController);
			if (LoginController && LoginController->GetGameControllerClass())
			{
				SwapToGameController(LoginController, PlayerId);
			}
			else
			{
				// GameControllerClass 미설정 시 기존 방식 사용
				UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] GameControllerClass 미설정! 기존 방식으로 소환"));
				SpawnHeroCharacter(PlayerController);
			}
		}
	}, 0.5f, false);

	UE_LOG(LogTemp, Warning, TEXT(""));
}

// ============================================
// 🔐 로그인 시스템 - OnLoginFailed
// ============================================
// 📌 역할: 로그인 실패 시 클라이언트에 에러 메시지 전달
// 📌 실패 사유: "이미 접속 중", "비밀번호 불일치", "서버 오류" 등
// ============================================
void AHellunaDefenseGameMode::OnLoginFailed(APlayerController* PlayerController, const FString& ErrorMessage)
{
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║     [DefenseGameMode] OnLoginFailed ❌                     ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ ErrorMessage: '%s'"), *ErrorMessage);
	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));

	AHellunaLoginController* LoginController = Cast<AHellunaLoginController>(PlayerController);
	if (LoginController)
	{
		LoginController->Client_LoginResult(false, ErrorMessage);
	}

	UE_LOG(LogTemp, Warning, TEXT(""));
}

// ============================================
// 🔐 로그인 시스템 - OnLoginTimeout
// ============================================
// 📌 역할: 로그인 타임아웃 시 플레이어 킥
// 📌 타임아웃 시간: LoginTimeoutSeconds (기본 60초)
// 📌 타이머 시작: PostLogin()에서 로그인 필요한 경우
// 📌 타이머 취소: OnLoginSuccess()에서 로그인 성공 시
// ============================================
void AHellunaDefenseGameMode::OnLoginTimeout(APlayerController* PlayerController)
{
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║     [DefenseGameMode] OnLoginTimeout ⏰                    ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ Controller: %s"), *GetNameSafe(PlayerController));
	UE_LOG(LogTemp, Warning, TEXT("║ → 킥 처리!                                                 ║"));
	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));

	if (!PlayerController) return;

	LoginTimeoutTimers.Remove(PlayerController);

	if (PlayerController->GetNetConnection())
	{
		FString KickReason = FString::Printf(TEXT("로그인 타임아웃 (%.0f초)"), LoginTimeoutSeconds);
		PlayerController->ClientReturnToMainMenuWithTextReason(FText::FromString(KickReason));
	}

	UE_LOG(LogTemp, Warning, TEXT(""));
}

// ============================================
// 🔐 로그인 시스템 - SwapToGameController
// ============================================
// 
// 📌 역할: LoginController → GameController 교체
// 
// 📌 호출 시점: OnLoginSuccess() 0.5초 후
// 
// 📌 왜 Controller를 교체하나?
//    - LoginController는 로그인 UI만 담당
//    - 실제 게임 플레이는 GameController (BP_InvPlayerController 등)가 담당
//    - 로그인 성공 후 UI 전용 Controller를 게임용으로 교체
// 
// 📌 처리 흐름:
//    1. LoginController의 PlayerState 정리 (중복 로그아웃 방지)
//    2. 새 GameController 스폰
//    3. Client_PrepareControllerSwap() - 로그인 UI 숨김
//    4. SwapPlayerControllers() - 안전한 교체 (PlayerState 전이)
//    5. 새 Controller의 PlayerState에 PlayerId 복원
//    6. SpawnHeroCharacter() 호출
// 
// 📌 주의:
//    - LoginController.GameControllerClass가 BP에서 설정되어 있어야 함
//    - 미설정 시 Controller 교체 없이 기존 방식으로 캐릭터 소환
// ============================================
void AHellunaDefenseGameMode::SwapToGameController(AHellunaLoginController* LoginController, const FString& PlayerId)
{
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║     [DefenseGameMode] SwapToGameController 🔄              ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ PlayerId: '%s'"), *PlayerId);
	UE_LOG(LogTemp, Warning, TEXT("║ LoginController: %s"), *GetNameSafe(LoginController));

	if (!LoginController)
	{
		UE_LOG(LogTemp, Error, TEXT("║ ❌ LoginController nullptr!                                ║"));
		UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
		return;
	}

	TSubclassOf<APlayerController> GameControllerClass = LoginController->GetGameControllerClass();
	if (!GameControllerClass)
	{
		UE_LOG(LogTemp, Error, TEXT("║ ❌ GameControllerClass 미설정!                             ║"));
		UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
		SpawnHeroCharacter(LoginController);
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("║ GameControllerClass: %s"), *GameControllerClass->GetName());

	// ============================================
	// ⭐ 중요: LoginController의 PlayerState 정리
	// SwapPlayerControllers 후 LoginController가 파괴될 때
	// Logout에서 중복으로 로그아웃 처리되는 것을 방지
	// ============================================
	if (AHellunaPlayerState* OldPS = LoginController->GetPlayerState<AHellunaPlayerState>())
	{
		OldPS->ClearLoginInfo();
		UE_LOG(LogTemp, Warning, TEXT("║ LoginController PlayerState 정리됨 (중복 로그아웃 방지)   ║"));
	}

	// 1. 새 GameController 스폰
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.Owner = this;

	FVector SpawnLocation = LoginController->GetFocalLocation();
	FRotator SpawnRotation = LoginController->GetControlRotation();

	APlayerController* NewController = GetWorld()->SpawnActor<APlayerController>(
		GameControllerClass,
		SpawnLocation,
		SpawnRotation,
		SpawnParams
	);

	if (!NewController)
	{
		UE_LOG(LogTemp, Error, TEXT("║ ❌ 새 Controller 스폰 실패!                                ║"));
		UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
		SpawnHeroCharacter(LoginController);
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("║ NewController: %s"), *NewController->GetName());

	// 2. LoginController의 로그인 UI 정리
	LoginController->Client_PrepareControllerSwap();

	// 3. SwapPlayerControllers로 안전하게 교체
	// ⭐ PlayerState도 새 Controller로 전이됨!
	UE_LOG(LogTemp, Warning, TEXT("║ → SwapPlayerControllers 호출...                            ║"));
	SwapPlayerControllers(LoginController, NewController);

	UE_LOG(LogTemp, Warning, TEXT("║ ✅ Controller 교체 완료!                                   ║"));

	// 4. 새 Controller의 PlayerState에 PlayerId 복원
	if (AHellunaPlayerState* NewPS = NewController->GetPlayerState<AHellunaPlayerState>())
	{
		NewPS->SetLoginInfo(PlayerId);
		UE_LOG(LogTemp, Warning, TEXT("║ 새 Controller PlayerState에 PlayerId 복원: '%s'           ║"), *PlayerId);

		// ⭐ [Phase 4 개선] OnControllerEndPlay 델리게이트 바인딩
		AInv_PlayerController* InvPC = Cast<AInv_PlayerController>(NewController);
		if (IsValid(InvPC))
		{
			InvPC->OnControllerEndPlay.AddDynamic(this, &AHellunaDefenseGameMode::OnInvControllerEndPlay);
			UE_LOG(LogTemp, Warning, TEXT("║ ✅ OnControllerEndPlay 델리게이트 바인딩 완료                ║"));
			
			// ⭐ Controller → PlayerId 매핑 저장 (EndPlay 시점에 PlayerState가 이미 파괴될 수 있음)
			ControllerToPlayerIdMap.Add(InvPC, PlayerId);
			UE_LOG(LogTemp, Warning, TEXT("║ ✅ Controller→PlayerId 매핑 저장: '%s'                        ║"), *PlayerId);
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));

	// 5. HeroCharacter 소환 (새 Controller로)
	FTimerHandle TimerHandle;
	GetWorldTimerManager().SetTimer(TimerHandle, [this, NewController]()
	{
		if (IsValid(NewController))
		{
			SpawnHeroCharacter(NewController);
		}
	}, 0.3f, false);

	UE_LOG(LogTemp, Warning, TEXT(""));
}

// ============================================
// 🔐 로그인 시스템 - SpawnHeroCharacter
// ============================================
// 
// 📌 역할: 플레이어 캐릭터(HeroCharacter) 소환 및 Possess
// 
// 📌 호출 시점:
//    - SwapToGameController() 0.3초 후
//    - 또는 SeamlessTravel로 이미 로그인된 경우 PostLogin()에서
// 
// 📌 처리 흐름:
//    1. 기존 Pawn 제거 (SpectatorPawn 등)
//    2. PlayerStart 위치 찾기
//    3. HeroCharacter 스폰
//    4. Controller.Possess(캐릭터)
//    5. 첫 플레이어인 경우 InitializeGame() 호출 (게임 시작!)
// 
// 📌 BP 설정 필수:
//    - HeroCharacterClass가 설정되어 있어야 함
//    - 맵에 PlayerStart가 배치되어 있어야 함 (없으면 0,0,200 위치에 소환)
// 
// 📌 첫 플레이어 소환 후:
//    - InitializeGame()에서 낮/밤 사이클 시작
//    - 몬스터 스포너 활성화
// ============================================
void AHellunaDefenseGameMode::SpawnHeroCharacter(APlayerController* PlayerController)
{
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║     [DefenseGameMode] SpawnHeroCharacter                   ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ Controller: %s (ID: %d)"), *GetNameSafe(PlayerController), PlayerController ? PlayerController->GetUniqueID() : -1);

	if (!PlayerController)
	{
		UE_LOG(LogTemp, Error, TEXT("║ ❌ Controller nullptr!                                     ║"));
		UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
		return;
	}

	if (!HeroCharacterClass)
	{
		UE_LOG(LogTemp, Error, TEXT("║ ❌ HeroCharacterClass 미설정!                              ║"));
		UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 10.f, FColor::Red,
				TEXT("HeroCharacterClass 미설정! GameMode BP에서 설정 필요"));
		}
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("║ HeroCharacterClass: %s"), *HeroCharacterClass->GetName());

	// 기존 Pawn 제거
	APawn* OldPawn = PlayerController->GetPawn();
	if (OldPawn)
	{
		UE_LOG(LogTemp, Warning, TEXT("║ OldPawn 제거: %s"), *OldPawn->GetName());
		PlayerController->UnPossess();
		OldPawn->Destroy();
	}

	// 스폰 위치
	FVector SpawnLocation = FVector::ZeroVector;
	FRotator SpawnRotation = FRotator::ZeroRotator;

	AActor* PlayerStart = FindPlayerStart(PlayerController);
	if (PlayerStart)
	{
		SpawnLocation = PlayerStart->GetActorLocation();
		SpawnRotation = PlayerStart->GetActorRotation();
		UE_LOG(LogTemp, Warning, TEXT("║ SpawnLocation: PlayerStart (%s)"), *SpawnLocation.ToString());
	}
	else
	{
		SpawnLocation = FVector(0.f, 0.f, 200.f);
		UE_LOG(LogTemp, Warning, TEXT("║ SpawnLocation: Default (%s)"), *SpawnLocation.ToString());
	}

	// HeroCharacter 스폰
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	SpawnParams.Owner = PlayerController;

	APawn* NewPawn = GetWorld()->SpawnActor<APawn>(HeroCharacterClass, SpawnLocation, SpawnRotation, SpawnParams);

	if (!NewPawn)
	{
		UE_LOG(LogTemp, Error, TEXT("║ ❌ HeroCharacter 스폰 실패!                                ║"));
		UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("║ NewPawn: %s"), *NewPawn->GetName());

	// Possess
	PlayerController->Possess(NewPawn);

	// 로그인 UI 숨기기
	AHellunaLoginController* LoginController = Cast<AHellunaLoginController>(PlayerController);
	if (LoginController)
	{
		LoginController->Client_PrepareControllerSwap();
	}

	UE_LOG(LogTemp, Warning, TEXT("║ ✅ Possess 완료!                                           ║"));
	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));

	// ============================================
	// 📌 첫 플레이어 캐릭터 소환 → 게임 초기화!
	// ============================================
	if (!bGameInitialized)
	{
		UE_LOG(LogTemp, Warning, TEXT(""));
		UE_LOG(LogTemp, Warning, TEXT("┌────────────────────────────────────────────────────────────┐"));
		UE_LOG(LogTemp, Warning, TEXT("│ 🎮 첫 플레이어 캐릭터 소환 완료!                          │"));
		UE_LOG(LogTemp, Warning, TEXT("│ → 게임 초기화 시작...                                      │"));
		UE_LOG(LogTemp, Warning, TEXT("└────────────────────────────────────────────────────────────┘"));

		InitializeGame();
	}

	// ============================================
	// 📦 [Phase 5] 저장된 인벤토리 로드
	// ============================================
	// 캐릭터 소환 완료 후 약간의 딜레이를 두고 인벤토리 로드
	// (InventoryComponent 초기화 완료 대기)
	FTimerHandle InventoryLoadTimer;
	GetWorldTimerManager().SetTimer(InventoryLoadTimer, [this, PlayerController]()
	{
		if (IsValid(PlayerController))
		{
			UE_LOG(LogTemp, Warning, TEXT(""));
			UE_LOG(LogTemp, Warning, TEXT("📦 [Phase 5] 캐릭터 소환 완료 → 인벤토리 로드 시작"));
			LoadAndSendInventoryToClient(PlayerController);
		}
	}, 1.0f, false);  // 1초 딜레이 (InventoryComponent 초기화 대기)

	UE_LOG(LogTemp, Warning, TEXT(""));
}

bool AHellunaDefenseGameMode::IsPlayerLoggedIn(const FString& PlayerId) const
{
	if (UMDF_GameInstance* GI = Cast<UMDF_GameInstance>(UGameplayStatics::GetGameInstance(GetWorld())))
	{
		return GI->IsPlayerLoggedIn(PlayerId);
	}
	return false;
}

// ============================================
// 🔐 로그인 시스템 - Logout
// ============================================
// 
// 📌 호출 시점: 플레이어가 서버에서 나갈 때 (엔진에서 자동 호출)
//    - 클라이언트 종료
//    - 네트워크 연결 끊김
//    - 타임아웃 킥
// 
// 📌 처리 흐름:
//    1. 로그인 타임아웃 타이머 취소 (있는 경우)
//    2. PlayerState에서 PlayerId 가져오기
//    3. GameInstance.RegisterLogout() - 접속자 목록에서 제거
//       → 다른 클라이언트가 같은 ID로 로그인 가능해짐
// 
// 📌 TODO: 여기에 인벤토리 저장 로직 추가 예정
//    if (!PlayerId.IsEmpty())
//    {
//        // 인벤토리 저장
//        InventorySaveGame->SavePlayerInventory(PlayerId, InventoryComponent);
//    }
// 
// 📌 주의:
//    - SwapToGameController()에서 LoginController 파괴 시에도 호출됨
//    - 중복 로그아웃 방지를 위해 SwapToGameController()에서 
//      미리 PlayerState.ClearLoginInfo() 호출함
// ============================================
void AHellunaDefenseGameMode::Logout(AController* Exiting)
{
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║     [DefenseGameMode] Logout                               ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ Controller: %s"), *GetNameSafe(Exiting));
	
	if (!Exiting)
	{
		UE_LOG(LogTemp, Error, TEXT("║ ❌ Exiting Controller가 nullptr!                          ║"));
		UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
		Super::Logout(Exiting);
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("║ ControllerClass: %s"), *Exiting->GetClass()->GetName());
	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
	
	// 타임아웃 타이머 정리
	if (APlayerController* PC = Cast<APlayerController>(Exiting))
	{
		if (FTimerHandle* Timer = LoginTimeoutTimers.Find(PC))
		{
			GetWorldTimerManager().ClearTimer(*Timer);
			LoginTimeoutTimers.Remove(PC);
			UE_LOG(LogTemp, Warning, TEXT("[Logout] 타임아웃 타이머 제거됨"));
		}
	}

	// ============================================
	// ⭐ PlayerState에서 PlayerId 가져오기 시도
	// ============================================
	AHellunaPlayerState* PS = Exiting->GetPlayerState<AHellunaPlayerState>();
	
	UE_LOG(LogTemp, Warning, TEXT("[Logout] PlayerState: %s"), PS ? *PS->GetName() : TEXT("nullptr ❌"));
	
	FString PlayerId;
	
	if (PS)
	{
		PlayerId = PS->GetPlayerUniqueId();
		UE_LOG(LogTemp, Warning, TEXT("[Logout] PlayerState.PlayerId: '%s'"), *PlayerId);
		UE_LOG(LogTemp, Warning, TEXT("[Logout] PlayerState.bIsLoggedIn: %s"), PS->IsLoggedIn() ? TEXT("true") : TEXT("false"));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[Logout] ❌ PlayerState가 nullptr! (이미 파괴됨?)"));
	}

	// ============================================
	// ⭐ PlayerId가 비어있으면 CachedPlayerInventoryData에서 찾기
	// ============================================
	if (PlayerId.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("[Logout] ⚠️ PlayerId가 비어있음! 캐시에서 검색 시도..."));
		
		// Controller의 UniqueID로 매칭 시도 (최후의 수단)
		// 일단 캐시된 모든 플레이어 목록 출력
		UE_LOG(LogTemp, Warning, TEXT("[Logout] 현재 캐시된 플레이어 목록:"));
		for (const auto& Pair : CachedPlayerInventoryData)
		{
			UE_LOG(LogTemp, Warning, TEXT("[Logout]   - '%s' (%d개 아이템)"), *Pair.Key, Pair.Value.Items.Num());
		}
	}

	// ============================================
	// ⭐ RegisterLogout 호출
	// ============================================
	if (!PlayerId.IsEmpty())
	{
		// ============================================
		// ⭐⭐⭐ [Phase 4 개선] 서버에서 직접 인벤토리 수집 및 저장
		// ============================================
		// 
		// 기존 문제: 캐시에 의존 → 자동저장 전에 나가면 손실
		// 해결책: InventoryComponent에서 직접 읽어서 저장!
		// 
		UE_LOG(LogTemp, Warning, TEXT(""));
		UE_LOG(LogTemp, Warning, TEXT("▶ [Phase 4 개선] Logout 시 인벤토리 직접 수집 및 저장..."));
		UE_LOG(LogTemp, Warning, TEXT("   PlayerId: %s"), *PlayerId);

		bool bSaveSuccess = false;

		// Step 1: Pawn에서 InventoryComponent 가져오기
		APawn* Pawn = Exiting->GetPawn();
		UInv_InventoryComponent* InvComp = Pawn ? Pawn->FindComponentByClass<UInv_InventoryComponent>() : nullptr;

		if (InvComp)
		{
			UE_LOG(LogTemp, Warning, TEXT("   ✅ InventoryComponent 발견! 직접 수집 시작..."));

			// Step 2: 서버에서 직접 인벤토리 데이터 수집 (RPC 없이!)
			TArray<FInv_SavedItemData> CollectedItems = InvComp->CollectInventoryDataForSave();

			// Step 3: FInv_SavedItemData → FHellunaInventoryItemData 변환
			FHellunaPlayerInventoryData SaveData;
			SaveData.LastSaveTime = FDateTime::Now();

			for (const FInv_SavedItemData& Item : CollectedItems)
			{
				FHellunaInventoryItemData DestItem;
				DestItem.ItemType = Item.ItemType;
				DestItem.StackCount = Item.StackCount;
				DestItem.GridPosition = Item.GridPosition;
				DestItem.GridCategory = Item.GridCategory;
				DestItem.EquipSlotIndex = -1;  // TODO: Phase 6에서 장착 정보 추가
				SaveData.Items.Add(DestItem);
			}

			// Step 4: SaveGame에 저장
			if (IsValid(InventorySaveGame) && SaveData.Items.Num() > 0)
			{
				InventorySaveGame->SavePlayerInventory(PlayerId, SaveData);

				if (UHellunaInventorySaveGame::Save(InventorySaveGame))
				{
					UE_LOG(LogTemp, Warning, TEXT("   🎉 직접 수집 저장 성공! (%d개 아이템)"), CollectedItems.Num());
					bSaveSuccess = true;
				}
				else
				{
					UE_LOG(LogTemp, Error, TEXT("   ❌ 파일 저장 실패!"));
				}
			}
			else if (SaveData.Items.Num() == 0)
			{
				UE_LOG(LogTemp, Warning, TEXT("   ⚠️ 인벤토리가 비어있음 (저장할 아이템 없음)"));
				bSaveSuccess = true;  // 빈 인벤토리도 정상 처리
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("   ⚠️ InventoryComponent 없음 (Pawn: %s)"), Pawn ? *Pawn->GetName() : TEXT("nullptr"));
			
			// 캐시 폴백 (기존 로직)
			if (FHellunaPlayerInventoryData* CachedData = CachedPlayerInventoryData.Find(PlayerId))
			{
				UE_LOG(LogTemp, Warning, TEXT("   📦 캐시 폴백: 캐시된 데이터로 저장 시도 (%d개 아이템)"), CachedData->Items.Num());
				
				CachedData->LastSaveTime = FDateTime::Now();

				if (IsValid(InventorySaveGame))
				{
					InventorySaveGame->SavePlayerInventory(PlayerId, *CachedData);
					
					if (UHellunaInventorySaveGame::Save(InventorySaveGame))
					{
						UE_LOG(LogTemp, Warning, TEXT("   🎉 캐시 폴백 저장 성공!"));
						bSaveSuccess = true;
					}
					else
					{
						UE_LOG(LogTemp, Error, TEXT("   ❌ 캐시 폴백 저장 실패!"));
					}
				}
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("   ⚠️ 캐시된 데이터도 없음"));
			}
		}

		// 캐시 정리
		CachedPlayerInventoryData.Remove(PlayerId);

		// ⭐⭐⭐ 핵심: GameInstance에서 로그아웃 처리
		if (UMDF_GameInstance* GI = Cast<UMDF_GameInstance>(UGameplayStatics::GetGameInstance(GetWorld())))
		{
			GI->RegisterLogout(PlayerId);
			UE_LOG(LogTemp, Warning, TEXT("   ✅ RegisterLogout 호출됨: '%s'"), *PlayerId);
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("   ❌ GameInstance를 가져올 수 없음!"));
		}
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT(""));
		UE_LOG(LogTemp, Error, TEXT("╔════════════════════════════════════════════════════════════╗"));
		UE_LOG(LogTemp, Error, TEXT("║ ❌❌❌ RegisterLogout 호출 실패! ❌❌❌                    ║"));
		UE_LOG(LogTemp, Error, TEXT("╠════════════════════════════════════════════════════════════╣"));
		UE_LOG(LogTemp, Error, TEXT("║ 원인: PlayerId가 비어있음                                  ║"));
		UE_LOG(LogTemp, Error, TEXT("║ 결과: 다음 로그인 시 '이미 접속 중' 에러 발생 가능!       ║"));
		UE_LOG(LogTemp, Error, TEXT("╚════════════════════════════════════════════════════════════╝"));
		UE_LOG(LogTemp, Error, TEXT(""));
	}

	UE_LOG(LogTemp, Warning, TEXT(""));

	Super::Logout(Exiting);
}

// ============================================
// 🔐 로그인 시스템 - HandleSeamlessTravelPlayer
// ============================================
// 
// 📌 호출 시점: SeamlessTravel(맵 이동) 완료 후
// 
// 📌 역할: 맵 이동 후에도 로그인 상태 유지
// 
// 📌 처리 흐름:
//    1. 이전 PlayerState에서 PlayerId와 로그인 상태 저장
//    2. Super::HandleSeamlessTravelPlayer() 호출 (새 PlayerState 생성)
//    3. 새 PlayerState에 PlayerId 복원
// 
// 📌 SeamlessTravel이란?
//    - 연결 끊김 없이 맵 이동
//    - bUseSeamlessTravel = true 설정 필요
//    - PlayerState는 새로 생성되지만 로그인 정보는 복원해야 함
// ============================================
void AHellunaDefenseGameMode::HandleSeamlessTravelPlayer(AController*& C)
{
	UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] HandleSeamlessTravelPlayer"));
	
	FString SavedPlayerId;
	bool bSavedIsLoggedIn = false;
	
	if (C)
	{
		if (AHellunaPlayerState* OldPS = C->GetPlayerState<AHellunaPlayerState>())
		{
			SavedPlayerId = OldPS->GetPlayerUniqueId();
			bSavedIsLoggedIn = OldPS->IsLoggedIn();
			UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] 저장: PlayerId='%s', IsLoggedIn=%s"), 
				*SavedPlayerId, bSavedIsLoggedIn ? TEXT("TRUE") : TEXT("FALSE"));
		}
	}

	Super::HandleSeamlessTravelPlayer(C);
	
	if (C && !SavedPlayerId.IsEmpty())
	{
		if (AHellunaPlayerState* NewPS = C->GetPlayerState<AHellunaPlayerState>())
		{
			NewPS->SetLoginInfo(SavedPlayerId);
			UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] 복원: '%s' → %s"), *SavedPlayerId, *NewPS->GetName());
		}
	}
}

// ============================================
// 🎮 게임 로직 시작 (비로그인 관련)
// ============================================
// 
// 아래 함수들은 로그인과 무관한 게임 로직입니다:
// - CacheBossSpawnPoints() : 보스 스폰 포인트 캐싱
// - CacheMonsterSpawnPoints() : 몬스터 스폰 포인트 캐싱
// - SpawnTestMonsters() : 테스트 몬스터 스폰
// - TrySummonBoss() : 보스 소환
// - RestartGame() : 게임 재시작
// - SetBossReady() : 보스 준비 상태 설정
// - EnterDay() / EnterNight() : 낮/밤 사이클
// - RegisterAliveMonster() / NotifyMonsterDied() : 몬스터 카운트
// ============================================

void AHellunaDefenseGameMode::CacheBossSpawnPoints()
{
	BossSpawnPoints.Empty();
	TArray<AActor*> Found;
	UGameplayStatics::GetAllActorsOfClass(this, ATargetPoint::StaticClass(), Found);

	for (AActor* A : Found)
	{
		if (ATargetPoint* TP = Cast<ATargetPoint>(A))
		{
			if (TP->ActorHasTag(BossSpawnPointTag))
				BossSpawnPoints.Add(TP);
		}
	}
}

void AHellunaDefenseGameMode::CacheMonsterSpawnPoints()
{
	MonsterSpawnPoints.Empty();
	TArray<AActor*> Found;
	UGameplayStatics::GetAllActorsOfClass(this, ATargetPoint::StaticClass(), Found);

	for (AActor* A : Found)
	{
		if (ATargetPoint* TP = Cast<ATargetPoint>(A))
		{
			if (TP->ActorHasTag(MonsterSpawnPointTag))
				MonsterSpawnPoints.Add(TP);
		}
	}
}

void AHellunaDefenseGameMode::SpawnTestMonsters()
{
	if (!HasAuthority() || !bGameInitialized) return;

	if (!TestMonsterClass)
	{
		Debug::Print(TEXT("[Defense] TestMonsterClass is null"), FColor::Red);
		return;
	}

	if (MonsterSpawnPoints.IsEmpty())
	{
		Debug::Print(TEXT("[Defense] No MonsterSpawn TargetPoints"), FColor::Red);
		return;
	}

	for (int32 i = 0; i < TestMonsterSpawnCount; ++i)
	{
		ATargetPoint* TP = MonsterSpawnPoints[FMath::RandRange(0, MonsterSpawnPoints.Num() - 1)];
		if (!TP) continue;

		FActorSpawnParameters Param;
		Param.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

		GetWorld()->SpawnActor<APawn>(TestMonsterClass, TP->GetActorLocation(), TP->GetActorRotation(), Param);
	}
}

void AHellunaDefenseGameMode::TrySummonBoss()
{
	if (!HasAuthority() || !bGameInitialized || !BossClass || BossSpawnPoints.IsEmpty())
		return;

	ATargetPoint* TP = BossSpawnPoints[FMath::RandRange(0, BossSpawnPoints.Num() - 1)];
	const FVector SpawnLoc = TP->GetActorLocation() + FVector(0, 0, SpawnZOffset);

	FActorSpawnParameters Param;
	Param.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	APawn* Boss = GetWorld()->SpawnActor<APawn>(BossClass, SpawnLoc, TP->GetActorRotation(), Param);
	if (Boss) bBossReady = false;
}

void AHellunaDefenseGameMode::RestartGame()
{
	if (!HasAuthority()) return;
	
	bGameInitialized = false; // 리셋
	GetWorld()->ServerTravel(TEXT("/Game/Minwoo/MinwooTestMap?listen"));
}

void AHellunaDefenseGameMode::SetBossReady(bool bReady)
{
	if (!HasAuthority() || bBossReady == bReady) return;
	bBossReady = bReady;
	if (bBossReady) TrySummonBoss();
}

void AHellunaDefenseGameMode::EnterDay()
{
	if (!bGameInitialized)
	{
		UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] EnterDay 스킵 - 게임 미초기화"));
		return;
	}

	AliveMonsters.Empty();

	if (AHellunaDefenseGameState* GS = GetGameState<AHellunaDefenseGameState>())
	{
		GS->SetPhase(EDefensePhase::Day);
		GS->SetAliveMonsterCount(0);
		GS->MulticastPrintDay();
	}

	GetWorldTimerManager().ClearTimer(TimerHandle_ToNight);
	GetWorldTimerManager().SetTimer(TimerHandle_ToNight, this, &ThisClass::EnterNight, TestDayDuration, false);
}

void AHellunaDefenseGameMode::EnterNight()
{
	if (!HasAuthority() || !bGameInitialized) return;

	AliveMonsters.Empty();

	if (AHellunaDefenseGameState* GS = GetGameState<AHellunaDefenseGameState>())
	{
		GS->SetPhase(EDefensePhase::Night);
		GS->SetAliveMonsterCount(0);
	}

	int32 Current = 0, Need = 0;
	if (IsSpaceShipFullyRepaired(Current, Need))
	{
		SetBossReady(true);
		return;
	}

	SpawnTestMonsters();
}

bool AHellunaDefenseGameMode::IsSpaceShipFullyRepaired(int32& OutCurrent, int32& OutNeed) const
{
	OutCurrent = 0;
	OutNeed = 0;

	const AHellunaDefenseGameState* GS = GetGameState<AHellunaDefenseGameState>();
	if (!GS) return false;

	AResourceUsingObject_SpaceShip* Ship = GS->GetSpaceShip();
	if (!Ship) return false;

	OutCurrent = Ship->GetCurrentResource();
	OutNeed = Ship->GetNeedResource();

	return (OutNeed > 0) && (OutCurrent >= OutNeed);
}

void AHellunaDefenseGameMode::RegisterAliveMonster(AActor* Monster)
{
	if (!HasAuthority() || !Monster || !bGameInitialized) return;

	AHellunaDefenseGameState* GS = GetGameState<AHellunaDefenseGameState>();
	if (!GS || GS->GetPhase() != EDefensePhase::Night) return;

	if (AliveMonsters.Contains(Monster)) return;

	AliveMonsters.Add(Monster);
	GS->SetAliveMonsterCount(AliveMonsters.Num());
}

void AHellunaDefenseGameMode::NotifyMonsterDied(AActor* DeadMonster)
{
	if (!HasAuthority() || !DeadMonster || !bGameInitialized) return;

	AHellunaDefenseGameState* GS = GetGameState<AHellunaDefenseGameState>();
	if (!GS) return;

	AliveMonsters.Remove(TWeakObjectPtr<AActor>(DeadMonster));
	GS->SetAliveMonsterCount(AliveMonsters.Num());

	if (AliveMonsters.Num() <= 0)
	{
		GetWorldTimerManager().ClearTimer(TimerHandle_ToDay);
		GetWorldTimerManager().SetTimer(TimerHandle_ToDay, this, &ThisClass::EnterDay, TestNightFailToDayDelay, false);
	}
}

// ============================================
// ============================================
// 
// 📦 인벤토리 저장 시스템 구현
// 
// ============================================
// ============================================

// ============================================
// 📌 [Phase 1] DataTable 매핑 테스트
// ============================================

/**
 * DebugTestItemTypeMapping
 * 
 * 현재 등록된 5개 아이템의 GameplayTag → Actor 클래스 매핑이
 * 올바르게 작동하는지 테스트합니다.
 * 
 * ▶ 호출 방법 (에디터 콘솔):
 *   ~ 키 → "ke * DebugTestItemTypeMapping" 입력
 * 
 * ▶ Output Log에서 확인할 것:
 *   1. "[ItemTypeMapping] 매핑 성공: GameItems.xxx → BP_Inv_xxx" 메시지
 *   2. 5개 모두 성공해야 함
 *   3. 실패 시 DataTable에 해당 행이 있는지 확인!
 * 
 * ▶ 테스트 대상 아이템:
 *   - GameItems.Equipment.Weapons.Axe
 *   - GameItems.Consumables.Potions.Blue.Small
 *   - GameItems.Consumables.Potions.Red.Small
 *   - GameItems.Craftables.FireFernFruit
 *   - GameItems.Craftables.LuminDaisy
 */
void AHellunaDefenseGameMode::DebugTestItemTypeMapping()
{
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║   [Phase 1] DataTable 매핑 테스트 시작                     ║"));
	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));

	// ============================================
	// 1단계: DataTable 유효성 검사
	// ============================================
	if (!IsValid(ItemTypeMappingDataTable))
	{
		UE_LOG(LogTemp, Error, TEXT(""));
		UE_LOG(LogTemp, Error, TEXT("❌ [테스트 실패] ItemTypeMappingDataTable이 설정되지 않았습니다!"));
		UE_LOG(LogTemp, Error, TEXT(""));
		UE_LOG(LogTemp, Error, TEXT("📋 해결 방법:"));
		UE_LOG(LogTemp, Error, TEXT("   1. BP_DefenseGameMode 열기"));
		UE_LOG(LogTemp, Error, TEXT("   2. Details 패널에서 '아이템 타입 매핑 DataTable' 찾기"));
		UE_LOG(LogTemp, Error, TEXT("   3. DT_ItemTypeMapping 선택"));
		UE_LOG(LogTemp, Error, TEXT(""));
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("✅ DataTable 유효! (%s)"), *ItemTypeMappingDataTable->GetName());
	UE_LOG(LogTemp, Warning, TEXT(""));

	// ============================================
	// 2단계: 테스트할 GameplayTag 목록
	// ============================================
	// 
	// 이 태그들은 Inv_ItemTags.cpp에 정의되어 있음
	// 새 아이템 추가 시 여기에도 추가하면 테스트 가능!
	// 
	TArray<FString> TestTags = {
		TEXT("GameItems.Equipment.Weapons.Axe"),           // 도끼 (장비)
		TEXT("GameItems.Consumables.Potions.Blue.Small"),  // 파란 포션 (소비)
		TEXT("GameItems.Consumables.Potions.Red.Small"),   // 빨간 포션 (소비)
		TEXT("GameItems.Craftables.FireFernFruit"),        // 불꽃 열매 (재료)
		TEXT("GameItems.Craftables.LuminDaisy"),           // 빛나는 꽃 (재료)
	};

	UE_LOG(LogTemp, Warning, TEXT("📋 테스트 대상: %d개 아이템"), TestTags.Num());
	UE_LOG(LogTemp, Warning, TEXT("────────────────────────────────────────────────────────────"));

	// ============================================
	// 3단계: 각 태그별 매핑 테스트
	// ============================================
	int32 SuccessCount = 0;
	int32 FailCount = 0;

	for (const FString& TagString : TestTags)
	{
		// GameplayTag 생성
		FGameplayTag TestTag = FGameplayTag::RequestGameplayTag(FName(*TagString), false);
		
		if (!TestTag.IsValid())
		{
			UE_LOG(LogTemp, Error, TEXT("❌ [%d] 태그 생성 실패: %s"), FailCount + SuccessCount + 1, *TagString);
			UE_LOG(LogTemp, Error, TEXT("      → GameplayTag가 등록되지 않았습니다! (Inv_ItemTags.cpp 확인)"));
			FailCount++;
			continue;
		}

		// DataTable에서 Actor 클래스 조회
		TSubclassOf<AActor> FoundClass = UHellunaItemTypeMapping::GetActorClassFromItemType(
			ItemTypeMappingDataTable, 
			TestTag
		);

		if (FoundClass)
		{
			UE_LOG(LogTemp, Warning, TEXT("✅ [%d] 매핑 성공!"), SuccessCount + FailCount + 1);
			UE_LOG(LogTemp, Warning, TEXT("      태그: %s"), *TagString);
			UE_LOG(LogTemp, Warning, TEXT("      클래스: %s"), *FoundClass->GetName());
			SuccessCount++;
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("❌ [%d] 매핑 실패!"), SuccessCount + FailCount + 1);
			UE_LOG(LogTemp, Error, TEXT("      태그: %s"), *TagString);
			UE_LOG(LogTemp, Error, TEXT("      → DataTable에 이 태그의 행이 없습니다!"));
			FailCount++;
		}
	}

	// ============================================
	// 4단계: 테스트 결과 요약
	// ============================================
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("────────────────────────────────────────────────────────────"));
	UE_LOG(LogTemp, Warning, TEXT("📊 테스트 결과:"));
	UE_LOG(LogTemp, Warning, TEXT("   ✅ 성공: %d개"), SuccessCount);
	UE_LOG(LogTemp, Warning, TEXT("   ❌ 실패: %d개"), FailCount);
	UE_LOG(LogTemp, Warning, TEXT(""));

	if (FailCount == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("🎉 모든 매핑 테스트 통과! Phase 1 완료!"));
		UE_LOG(LogTemp, Warning, TEXT("   → Phase 2 (SaveGame 클래스) 진행 가능"));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("⚠️ 일부 매핑 실패! DataTable 확인 필요!"));
		UE_LOG(LogTemp, Error, TEXT(""));
		UE_LOG(LogTemp, Error, TEXT("📋 해결 방법:"));
		UE_LOG(LogTemp, Error, TEXT("   1. DT_ItemTypeMapping 열기"));
		UE_LOG(LogTemp, Error, TEXT("   2. 실패한 태그에 해당하는 행 추가"));
		UE_LOG(LogTemp, Error, TEXT("   3. ItemType에 정확한 GameplayTag 입력"));
		UE_LOG(LogTemp, Error, TEXT("   4. ItemActorClass에 해당 BP 선택"));
	}

	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║   [Phase 1] DataTable 매핑 테스트 완료                     ║"));
	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
	UE_LOG(LogTemp, Warning, TEXT(""));
}

/**
 * DebugPrintAllItemMappings
 * 
 * DataTable에 등록된 모든 매핑을 Output Log에 출력합니다.
 * 어떤 아이템들이 등록되어 있는지 한눈에 확인할 때 사용!
 * 
 * ▶ 호출 방법 (에디터 콘솔):
 *   ~ 키 → "ke * DebugPrintAllItemMappings" 입력
 */
void AHellunaDefenseGameMode::DebugPrintAllItemMappings()
{
	if (!IsValid(ItemTypeMappingDataTable))
	{
		UE_LOG(LogTemp, Error, TEXT("❌ ItemTypeMappingDataTable이 설정되지 않았습니다!"));
		return;
	}

	// 유틸리티 함수 호출 (HellunaItemTypeMapping.cpp에 구현됨)
	UHellunaItemTypeMapping::DebugPrintAllMappings(ItemTypeMappingDataTable);
}

// ============================================
// 📦 [Phase 2] SaveGame 테스트 함수
// ============================================
// 
// DebugTestInventorySaveGame
// 
// 인벤토리 SaveGame의 저장/로드 기능을 테스트합니다.
// 더미 데이터를 생성하여 저장 후 로드하여 검증합니다.
// 
// ▶ 테스트 내용:
//    1. 더미 플레이어 데이터 생성
//    2. SavePlayerInventory()로 저장
//    3. Save()로 파일에 기록
//    4. LoadPlayerInventory()로 로드
//    5. 데이터 검증
// 
// ▶ 파일 생성 위치:
//    Saved/SaveGames/HellunaInventory.sav
// 
// ▶ 호출 방법 (에디터 콘솔):
//    ~ 키 → "ke * DebugTestInventorySaveGame" 입력
// ============================================
void AHellunaDefenseGameMode::DebugTestInventorySaveGame()
{
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║   [Phase 2] SaveGame 저장/로드 테스트 시작                 ║"));
	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));

	// ============================================
	// 1단계: SaveGame 인스턴스 확인
	// ============================================
	if (!IsValid(InventorySaveGame))
	{
		UE_LOG(LogTemp, Error, TEXT("❌ InventorySaveGame이 nullptr입니다!"));
		UE_LOG(LogTemp, Error, TEXT("   → BeginPlay에서 LoadOrCreate() 확인 필요"));
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("✅ InventorySaveGame 유효!"));
	UE_LOG(LogTemp, Warning, TEXT("   현재 저장된 플레이어: %d명"), InventorySaveGame->GetPlayerCount());

	// ============================================
	// 2단계: 테스트용 더미 데이터 생성
	// ============================================
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("📦 테스트 데이터 생성 중..."));

	const FString TestPlayerId = TEXT("TestPlayer_Phase2");

	FHellunaPlayerInventoryData TestData;
	TestData.SaveVersion = 1;

	// 더미 아이템 1: 도끼
	FHellunaInventoryItemData Item1;
	Item1.ItemType = FGameplayTag::RequestGameplayTag(FName("GameItems.Equipment.Weapons.Axe"), false);
	Item1.StackCount = 1;
	Item1.GridPosition = FIntPoint(0, 0);
	Item1.EquipSlotIndex = 0;  // 장착 슬롯 0
	TestData.Items.Add(Item1);

	// 더미 아이템 2: 빨간 포션 3개
	FHellunaInventoryItemData Item2;
	Item2.ItemType = FGameplayTag::RequestGameplayTag(FName("GameItems.Consumables.Potions.Red.Small"), false);
	Item2.StackCount = 3;
	Item2.GridPosition = FIntPoint(1, 0);
	Item2.EquipSlotIndex = -1;  // 미장착
	TestData.Items.Add(Item2);

	// 더미 아이템 3: 파란 포션 5개
	FHellunaInventoryItemData Item3;
	Item3.ItemType = FGameplayTag::RequestGameplayTag(FName("GameItems.Consumables.Potions.Blue.Small"), false);
	Item3.StackCount = 5;
	Item3.GridPosition = FIntPoint(2, 0);
	Item3.EquipSlotIndex = -1;
	TestData.Items.Add(Item3);

	// 더미 아이템 4: 불꽃 과일 2개
	FHellunaInventoryItemData Item4;
	Item4.ItemType = FGameplayTag::RequestGameplayTag(FName("GameItems.Craftables.FireFernFruit"), false);
	Item4.StackCount = 2;
	Item4.GridPosition = FIntPoint(0, 1);
	Item4.EquipSlotIndex = -1;
	TestData.Items.Add(Item4);

	UE_LOG(LogTemp, Warning, TEXT("   생성된 아이템: %d개"), TestData.Items.Num());
	for (int32 i = 0; i < TestData.Items.Num(); i++)
	{
		const FHellunaInventoryItemData& Item = TestData.Items[i];
		UE_LOG(LogTemp, Warning, TEXT("   [%d] %s x%d @ (%d,%d) 장착:%d"),
			i, *Item.ItemType.ToString(), Item.StackCount,
			Item.GridPosition.X, Item.GridPosition.Y, Item.EquipSlotIndex);
	}

	// ============================================
	// 3단계: 저장 테스트
	// ============================================
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("💾 저장 테스트..."));

	InventorySaveGame->SavePlayerInventory(TestPlayerId, TestData);
	bool bSaveSuccess = UHellunaInventorySaveGame::Save(InventorySaveGame);

	if (bSaveSuccess)
	{
		UE_LOG(LogTemp, Warning, TEXT("   ✅ 파일 저장 성공!"));
		UE_LOG(LogTemp, Warning, TEXT("   📁 위치: Saved/SaveGames/HellunaInventory.sav"));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("   ❌ 파일 저장 실패!"));
		return;
	}

	// ============================================
	// 4단계: 로드 테스트
	// ============================================
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("📂 로드 테스트..."));

	FHellunaPlayerInventoryData LoadedData;
	bool bLoadSuccess = InventorySaveGame->LoadPlayerInventory(TestPlayerId, LoadedData);

	if (bLoadSuccess)
	{
		UE_LOG(LogTemp, Warning, TEXT("   ✅ 로드 성공!"));
		UE_LOG(LogTemp, Warning, TEXT("   로드된 아이템: %d개"), LoadedData.Items.Num());
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("   ❌ 로드 실패!"));
		return;
	}

	// ============================================
	// 5단계: 데이터 검증
	// ============================================
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("🔍 데이터 검증..."));

	bool bVerifySuccess = true;

	// 아이템 개수 확인
	if (LoadedData.Items.Num() != TestData.Items.Num())
	{
		UE_LOG(LogTemp, Error, TEXT("   ❌ 아이템 개수 불일치! (저장:%d, 로드:%d)"),
			TestData.Items.Num(), LoadedData.Items.Num());
		bVerifySuccess = false;
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("   ✅ 아이템 개수 일치: %d개"), LoadedData.Items.Num());
	}

	// 각 아이템 데이터 검증
	for (int32 i = 0; i < FMath::Min(TestData.Items.Num(), LoadedData.Items.Num()); i++)
	{
		const FHellunaInventoryItemData& Original = TestData.Items[i];
		const FHellunaInventoryItemData& Loaded = LoadedData.Items[i];

		bool bItemMatch = true;
		if (Original.ItemType != Loaded.ItemType)
		{
			UE_LOG(LogTemp, Error, TEXT("   ❌ [%d] ItemType 불일치"), i);
			bItemMatch = false;
		}
		if (Original.StackCount != Loaded.StackCount)
		{
			UE_LOG(LogTemp, Error, TEXT("   ❌ [%d] StackCount 불일치 (저장:%d, 로드:%d)"),
				i, Original.StackCount, Loaded.StackCount);
			bItemMatch = false;
		}
		if (Original.GridPosition != Loaded.GridPosition)
		{
			UE_LOG(LogTemp, Error, TEXT("   ❌ [%d] GridPosition 불일치"), i);
			bItemMatch = false;
		}
		if (Original.EquipSlotIndex != Loaded.EquipSlotIndex)
		{
			UE_LOG(LogTemp, Error, TEXT("   ❌ [%d] EquipSlotIndex 불일치"), i);
			bItemMatch = false;
		}

		if (bItemMatch)
		{
			UE_LOG(LogTemp, Warning, TEXT("   ✅ [%d] %s - 검증 통과"),
				i, *Loaded.ItemType.ToString());
		}
		else
		{
			bVerifySuccess = false;
		}
	}

	// ============================================
	// 6단계: 최종 결과
	// ============================================
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("────────────────────────────────────────────────────────────"));
	UE_LOG(LogTemp, Warning, TEXT("📊 Phase 2 테스트 결과:"));

	if (bSaveSuccess && bLoadSuccess && bVerifySuccess)
	{
		UE_LOG(LogTemp, Warning, TEXT(""));
		UE_LOG(LogTemp, Warning, TEXT("   🎉 모든 테스트 통과! Phase 2 완료!"));
		UE_LOG(LogTemp, Warning, TEXT("   → Phase 3 (Grid 위치 동기화 RPC) 진행 가능"));
		UE_LOG(LogTemp, Warning, TEXT("   → 또는 Phase 4 (저장 함수 구현) 바로 진행"));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT(""));
		UE_LOG(LogTemp, Error, TEXT("   ⚠️ 일부 테스트 실패!"));
		UE_LOG(LogTemp, Error, TEXT("   저장: %s"), bSaveSuccess ? TEXT("✅") : TEXT("❌"));
		UE_LOG(LogTemp, Error, TEXT("   로드: %s"), bLoadSuccess ? TEXT("✅") : TEXT("❌"));
		UE_LOG(LogTemp, Error, TEXT("   검증: %s"), bVerifySuccess ? TEXT("✅") : TEXT("❌"));
	}

	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║   [Phase 2] SaveGame 테스트 완료                           ║"));
	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
	UE_LOG(LogTemp, Warning, TEXT(""));

	// 전체 저장 데이터 출력 (디버그)
	InventorySaveGame->DebugPrintAllData();
}

// ============================================
// 📌 [Phase 4] 자동저장 시스템 구현
// ============================================

void AHellunaDefenseGameMode::StartAutoSaveTimer()
{
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔══════════════════════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║      [Phase 4] StartAutoSaveTimer - 자동저장 타이머 시작                      ║"));
	UE_LOG(LogTemp, Warning, TEXT("╚══════════════════════════════════════════════════════════════════════════════╝"));

	if (AutoSaveIntervalSeconds <= 0.0f)
	{
		UE_LOG(LogTemp, Warning, TEXT("⚠️ AutoSaveIntervalSeconds = 0 → 자동저장 비활성화"));
		return;
	}

	// 기존 타이머 정리
	StopAutoSaveTimer();

	// 새 타이머 시작
	GetWorldTimerManager().SetTimer(
		AutoSaveTimerHandle,
		this,
		&AHellunaDefenseGameMode::OnAutoSaveTimer,
		AutoSaveIntervalSeconds,
		true  // 반복
	);

	UE_LOG(LogTemp, Warning, TEXT("✅ 자동저장 타이머 시작! (주기: %.0f초)"), AutoSaveIntervalSeconds);
	UE_LOG(LogTemp, Warning, TEXT("════════════════════════════════════════════════════════════════════════════════"));
}

void AHellunaDefenseGameMode::StopAutoSaveTimer()
{
	if (AutoSaveTimerHandle.IsValid())
	{
		GetWorldTimerManager().ClearTimer(AutoSaveTimerHandle);
		UE_LOG(LogTemp, Warning, TEXT("[Phase 4] 자동저장 타이머 정지됨"));
	}
}

void AHellunaDefenseGameMode::OnAutoSaveTimer()
{
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔══════════════════════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║      [Phase 4] OnAutoSaveTimer - 자동저장 타이머 발동!                        ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠══════════════════════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ 📍 실행 위치: 서버                                                            ║"));
	UE_LOG(LogTemp, Warning, TEXT("║ 📍 목적: 모든 플레이어에게 인벤토리 상태 요청                                  ║"));
	UE_LOG(LogTemp, Warning, TEXT("╚══════════════════════════════════════════════════════════════════════════════╝"));

	RequestAllPlayersInventoryState();
}

void AHellunaDefenseGameMode::RequestAllPlayersInventoryState()
{
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("▶ 모든 플레이어에게 인벤토리 상태 요청 중..."));

	int32 RequestCount = 0;

	// 모든 PlayerController 순회
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (!IsValid(PC)) continue;

		// Inv_PlayerController로 캐스트
		AInv_PlayerController* InvPC = Cast<AInv_PlayerController>(PC);
		if (!IsValid(InvPC))
		{
			UE_LOG(LogTemp, Warning, TEXT("   ⚠️ %s: Inv_PlayerController 아님, 건너뜀"), *PC->GetName());
			continue;
		}

		// 델리게이트 바인딩 (아직 안 되어 있으면)
		if (!InvPC->OnInventoryStateReceived.IsBound())
		{
			UE_LOG(LogTemp, Warning, TEXT("   📌 %s: 델리게이트 바인딩 중..."), *PC->GetName());
			InvPC->OnInventoryStateReceived.AddDynamic(this, &AHellunaDefenseGameMode::OnPlayerInventoryStateReceived);
		}

		RequestPlayerInventoryState(PC);
		RequestCount++;
	}

	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("✅ 총 %d명에게 요청 전송 완료!"), RequestCount);
}

void AHellunaDefenseGameMode::RequestPlayerInventoryState(APlayerController* PC)
{
	if (!IsValid(PC)) return;

	AInv_PlayerController* InvPC = Cast<AInv_PlayerController>(PC);
	if (!IsValid(InvPC))
	{
		UE_LOG(LogTemp, Warning, TEXT("   ⚠️ %s: Inv_PlayerController 아님"), *PC->GetName());
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("   → %s: Client_RequestInventoryState() RPC 전송"), *PC->GetName());
	InvPC->Client_RequestInventoryState();
}

void AHellunaDefenseGameMode::OnPlayerInventoryStateReceived(
	AInv_PlayerController* PlayerController, 
	const TArray<FInv_SavedItemData>& SavedItems)
{
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔══════════════════════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║      [Phase 4] OnPlayerInventoryStateReceived - 인벤토리 데이터 수신          ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠══════════════════════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ 📍 실행 위치: 서버 (GameMode)                                                 ║"));
	UE_LOG(LogTemp, Warning, TEXT("║ 📍 플레이어: %s"), *PlayerController->GetName());
	UE_LOG(LogTemp, Warning, TEXT("║ 📍 수신된 아이템: %d개                                                        "), SavedItems.Num());
	UE_LOG(LogTemp, Warning, TEXT("╚══════════════════════════════════════════════════════════════════════════════╝"));

	// ============================================
	// Step 1: PlayerUniqueId 가져오기
	// ============================================
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("▶ [Step 1] PlayerUniqueId 가져오기..."));

	AHellunaPlayerState* PS = PlayerController->GetPlayerState<AHellunaPlayerState>();
	if (!IsValid(PS))
	{
		UE_LOG(LogTemp, Error, TEXT("   ❌ PlayerState가 없습니다!"));
		return;
	}

	FString PlayerUniqueId = PS->GetPlayerUniqueId();
	if (PlayerUniqueId.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("   ❌ PlayerUniqueId가 비어있습니다! (로그인 안 됨?)"));
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("   ✅ PlayerUniqueId: %s"), *PlayerUniqueId);

	// ============================================
	// Step 2: FInv_SavedItemData → FHellunaInventoryItemData 변환
	// ============================================
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("▶ [Step 2] 데이터 변환 (FInv_SavedItemData → FHellunaInventoryItemData)..."));

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
		DestItem.EquipSlotIndex = -1;  // TODO: 장착 정보는 Phase 6에서

		PlayerData.Items.Add(DestItem);

		UE_LOG(LogTemp, Warning, TEXT("   [%d] %s"), 
			PlayerData.Items.Num() - 1, *DestItem.ToString());
	}

	UE_LOG(LogTemp, Warning, TEXT("   ✅ 변환 완료: %d개 아이템"), PlayerData.Items.Num());

	// ============================================
	// Step 2.5: 캐시에 저장 (Logout 대비)
	// ============================================
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("▶ [Step 2.5] 캐시에 저장 (Logout 대비)..."));
	
	CachedPlayerInventoryData.Add(PlayerUniqueId, PlayerData);
	UE_LOG(LogTemp, Warning, TEXT("   ✅ 캐시 업데이트 완료 (PlayerId: %s)"), *PlayerUniqueId);

	// ============================================
	// Step 3: SaveGame에 저장
	// ============================================
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("▶ [Step 3] SaveGame에 저장..."));

	if (!IsValid(InventorySaveGame))
	{
		UE_LOG(LogTemp, Error, TEXT("   ❌ InventorySaveGame이 nullptr!"));
		return;
	}

	InventorySaveGame->SavePlayerInventory(PlayerUniqueId, PlayerData);

	// ============================================
	// Step 4: 파일에 저장
	// ============================================
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("▶ [Step 4] 파일에 저장..."));

	bool bSaveSuccess = UHellunaInventorySaveGame::Save(InventorySaveGame);

	if (bSaveSuccess)
	{
		UE_LOG(LogTemp, Warning, TEXT("   ✅ 저장 성공!"));
		UE_LOG(LogTemp, Warning, TEXT("   📁 위치: Saved/SaveGames/HellunaInventory.sav"));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("   ❌ 저장 실패!"));
	}

	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("════════════════════════════════════════════════════════════════════════════════"));
	UE_LOG(LogTemp, Warning, TEXT("🎉 [Phase 4] 플레이어 %s 인벤토리 저장 완료!"), *PlayerUniqueId);
	UE_LOG(LogTemp, Warning, TEXT("════════════════════════════════════════════════════════════════════════════════"));
}

// ============================================
// 📌 [Phase 4] 디버그 함수
// ============================================

void AHellunaDefenseGameMode::DebugRequestSaveAllInventory()
{
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("🔧 [디버그] 수동으로 모든 플레이어 인벤토리 저장 요청"));
	RequestAllPlayersInventoryState();
}

void AHellunaDefenseGameMode::DebugForceAutoSave()
{
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("🔧 [디버그] 자동저장 강제 실행"));
	OnAutoSaveTimer();
}

// ============================================
// 📌 [Phase 5] 인벤토리 로드 함수
// ============================================

void AHellunaDefenseGameMode::LoadAndSendInventoryToClient(APlayerController* PC)
{
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔══════════════════════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║      [Phase 5] LoadAndSendInventoryToClient - 인벤토리 로드 및 전송          ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠══════════════════════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ 📍 실행 위치: 서버                                                            ║"));
	UE_LOG(LogTemp, Warning, TEXT("║ 📍 PlayerController: %s"), PC ? *PC->GetName() : TEXT("nullptr"));
	UE_LOG(LogTemp, Warning, TEXT("╚══════════════════════════════════════════════════════════════════════════════╝"));

	if (!HasAuthority())
	{
		UE_LOG(LogTemp, Error, TEXT("   ❌ 서버 권한 없음!"));
		return;
	}

	if (!IsValid(PC))
	{
		UE_LOG(LogTemp, Error, TEXT("   ❌ PlayerController가 nullptr!"));
		return;
	}

	// ============================================
	// Step 1: PlayerUniqueId 가져오기
	// ============================================
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("▶ [Step 1] PlayerUniqueId 가져오기..."));

	AHellunaPlayerState* PS = PC->GetPlayerState<AHellunaPlayerState>();
	if (!IsValid(PS))
	{
		UE_LOG(LogTemp, Error, TEXT("   ❌ PlayerState가 없습니다!"));
		return;
	}

	FString PlayerUniqueId = PS->GetPlayerUniqueId();
	if (PlayerUniqueId.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("   ❌ PlayerUniqueId가 비어있습니다! (로그인 안 됨?)"));
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("   ✅ PlayerUniqueId: %s"), *PlayerUniqueId);

	// ============================================
	// Step 2: SaveGame에서 데이터 로드
	// ============================================
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("▶ [Step 2] SaveGame에서 인벤토리 데이터 로드..."));

	if (!IsValid(InventorySaveGame))
	{
		UE_LOG(LogTemp, Error, TEXT("   ❌ InventorySaveGame이 nullptr!"));
		return;
	}

	FHellunaPlayerInventoryData LoadedData;
	bool bDataFound = InventorySaveGame->LoadPlayerInventory(PlayerUniqueId, LoadedData);

	if (!bDataFound || LoadedData.Items.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("   ⚠️ 저장된 인벤토리 데이터가 없습니다. (신규 플레이어)"));
		UE_LOG(LogTemp, Warning, TEXT("════════════════════════════════════════════════════════════════════════════════"));
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("   ✅ 로드 성공! %d개 아이템"), LoadedData.Items.Num());
	UE_LOG(LogTemp, Warning, TEXT("   📅 마지막 저장: %s"), *LoadedData.LastSaveTime.ToString());

	// ============================================
	// Step 3: 아이템 스폰 및 인벤토리에 추가 (서버)
	// ============================================
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("▶ [Step 3] 아이템 스폰 및 인벤토리에 추가..."));

	// InventoryComponent 찾기
	UInv_InventoryComponent* InvComp = PC->FindComponentByClass<UInv_InventoryComponent>();
	if (!IsValid(InvComp))
	{
		UE_LOG(LogTemp, Error, TEXT("   ❌ InventoryComponent를 찾을 수 없습니다!"));
		return;
	}

	// DataTable 체크
	if (!IsValid(ItemTypeMappingDataTable))
	{
		UE_LOG(LogTemp, Error, TEXT("   ❌ ItemTypeMappingDataTable이 설정되지 않았습니다!"));
		UE_LOG(LogTemp, Error, TEXT("      → BP_DefenseGameMode에서 DataTable 설정 필요"));
		return;
	}

	int32 SpawnedCount = 0;
	int32 FailedCount = 0;

	for (const FHellunaInventoryItemData& ItemData : LoadedData.Items)
	{
		if (!ItemData.ItemType.IsValid())
		{
			UE_LOG(LogTemp, Error, TEXT("   ❌ [실패 %d] 유효하지 않은 ItemType!"), FailedCount + 1);
			UE_LOG(LogTemp, Error, TEXT("         인덱스: %d"), FailedCount + SpawnedCount);
			UE_LOG(LogTemp, Error, TEXT("         → ItemType 태그가 비어있거나 유효하지 않음"));
			FailedCount++;
			continue;
		}

		// DataTable에서 ActorClass 조회
		TSubclassOf<AActor> ActorClass = UHellunaItemTypeMapping::GetActorClassFromItemType(
			ItemTypeMappingDataTable, ItemData.ItemType);

		if (!ActorClass)
		{
			UE_LOG(LogTemp, Error, TEXT("   ❌ [실패 %d] ActorClass 조회 실패!"), FailedCount + 1);
			UE_LOG(LogTemp, Error, TEXT("         태그: %s"), *ItemData.ItemType.ToString());
			UE_LOG(LogTemp, Error, TEXT("         → DataTable에 해당 태그 매핑이 없음"));
			UE_LOG(LogTemp, Error, TEXT("         → DT_ItemTypeMapping에서 확인 필요"));
			FailedCount++;
			continue;
		}

		// 임시 위치에 Actor 스폰
		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		// 임시 위치 (월드 밖, 나중에 파괴될 예정)
		FVector TempSpawnLocation = FVector(0.f, 0.f, -10000.f);

		AActor* SpawnedActor = GetWorld()->SpawnActor<AActor>(ActorClass, TempSpawnLocation, FRotator::ZeroRotator, SpawnParams);

		if (!IsValid(SpawnedActor))
		{
			UE_LOG(LogTemp, Error, TEXT("   ❌ [실패 %d] Actor 스폰 실패!"), FailedCount + 1);
			UE_LOG(LogTemp, Error, TEXT("         태그: %s"), *ItemData.ItemType.ToString());
			UE_LOG(LogTemp, Error, TEXT("         클래스: %s"), *ActorClass->GetName());
			UE_LOG(LogTemp, Error, TEXT("         → World->SpawnActor 실패"));
			FailedCount++;
			continue;
		}

		// ItemComponent 가져오기
		UInv_ItemComponent* ItemComp = SpawnedActor->FindComponentByClass<UInv_ItemComponent>();
		if (!IsValid(ItemComp))
		{
			UE_LOG(LogTemp, Error, TEXT("   ❌ [실패 %d] ItemComponent 없음!"), FailedCount + 1);
			UE_LOG(LogTemp, Error, TEXT("         태그: %s"), *ItemData.ItemType.ToString());
			UE_LOG(LogTemp, Error, TEXT("         Actor: %s"), *SpawnedActor->GetName());
			UE_LOG(LogTemp, Error, TEXT("         → 해당 Actor에 UInv_ItemComponent가 없음"));
			UE_LOG(LogTemp, Error, TEXT("         → 아이템 BP 확인 필요"));
			SpawnedActor->Destroy();
			FailedCount++;
			continue;
		}

		// StackCount 설정 (ItemComponent에 직접 설정하거나 TryAddItem에서 처리)
		// 여기서는 서버 RPC를 사용하여 아이템 추가
		// Server_AddNewItem은 StackCount를 처리하므로 직접 호출
		InvComp->Server_AddNewItem(ItemComp, ItemData.StackCount, 0);

		// [Phase 5 Fix] Set GridIndex/GridCategory for the newly added Entry
		// This allows the client to place items at the correct position when FastArray replicates
		{
			const int32 Columns = 8;
			int32 SavedGridIndex = ItemData.GridPosition.Y * Columns + ItemData.GridPosition.X;
			InvComp->SetLastEntryGridPosition(SavedGridIndex, ItemData.GridCategory);
		}

		SpawnedCount++;
		UE_LOG(LogTemp, Warning, TEXT("   [%d] ✅ 스폰 성공!"),
			SpawnedCount);
		UE_LOG(LogTemp, Warning, TEXT("         태그: %s"), *ItemData.ItemType.ToString());
		UE_LOG(LogTemp, Warning, TEXT("         수량: %d"), ItemData.StackCount);
		UE_LOG(LogTemp, Warning, TEXT("         Grid: %d, 위치: (%d, %d)"), 
			ItemData.GridCategory, ItemData.GridPosition.X, ItemData.GridPosition.Y);
		UE_LOG(LogTemp, Warning, TEXT("         스폰된 Actor: %s"), *SpawnedActor->GetName());

		// 임시 Actor 파괴 (아이템 데이터는 이미 InventoryComponent에 복사됨)
		// ⚠️ 주의: 일부 시스템에서는 Actor를 유지해야 할 수도 있음
		// SpawnedActor->Destroy();  // 필요 시 활성화
	}

	// Step 3 결과 요약
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("  ┌─────────────────────────────────────────────────────────────┐"));
	UE_LOG(LogTemp, Warning, TEXT("  │ 📊 Step 3 결과: 아이템 스폰                                 │"));
	UE_LOG(LogTemp, Warning, TEXT("  ├─────────────────────────────────────────────────────────────┤"));
	UE_LOG(LogTemp, Warning, TEXT("  │ 요청: %3d개                                                 │"), LoadedData.Items.Num());
	UE_LOG(LogTemp, Warning, TEXT("  │ 성공: %3d개 ✅                                              │"), SpawnedCount);
	UE_LOG(LogTemp, Warning, TEXT("  │ 실패: %3d개 ❌                                              │"), FailedCount);
	UE_LOG(LogTemp, Warning, TEXT("  └─────────────────────────────────────────────────────────────┘"));

	// ============================================
	// Step 4: Client RPC로 Grid 위치 데이터 전송
	// ============================================
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("▶ [Step 4] Client RPC로 Grid 위치 데이터 전송..."));

	// FHellunaInventoryItemData → FInv_SavedItemData 변환
	TArray<FInv_SavedItemData> SavedItemsForClient;
	for (const FHellunaInventoryItemData& ItemData : LoadedData.Items)
	{
		FInv_SavedItemData ClientData;
		ClientData.ItemType = ItemData.ItemType;
		ClientData.StackCount = ItemData.StackCount;
		ClientData.GridPosition = ItemData.GridPosition;
		ClientData.GridCategory = ItemData.GridCategory;

		SavedItemsForClient.Add(ClientData);
	}

	// Inv_PlayerController로 캐스트하여 Client RPC 호출
	AInv_PlayerController* InvPC = Cast<AInv_PlayerController>(PC);
	if (IsValid(InvPC))
	{
		InvPC->Client_ReceiveInventoryData(SavedItemsForClient);
		UE_LOG(LogTemp, Warning, TEXT("   ✅ Client RPC 전송 완료!"));
		UE_LOG(LogTemp, Warning, TEXT("         전송 아이템: %d개"), SavedItemsForClient.Num());
		UE_LOG(LogTemp, Warning, TEXT("         수신자: %s"), *InvPC->GetName());
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("   ⚠️ Inv_PlayerController 아님, Grid 위치 복원 생략"));
		UE_LOG(LogTemp, Warning, TEXT("         PC 클래스: %s"), *PC->GetClass()->GetName());
	}

	// ============================================
	// 최종 결과 요약
	// ============================================
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("  ┌─────────────────────────────────────────────────────────────┐"));
	UE_LOG(LogTemp, Warning, TEXT("  │ 📊 [Phase 5] 인벤토리 로드 최종 결과                        │"));
	UE_LOG(LogTemp, Warning, TEXT("  ├─────────────────────────────────────────────────────────────┤"));
	UE_LOG(LogTemp, Warning, TEXT("  │ 플레이어: %-40s │"), *PlayerUniqueId);
	UE_LOG(LogTemp, Warning, TEXT("  │ 저장된 아이템: %3d개                                        │"), LoadedData.Items.Num());
	UE_LOG(LogTemp, Warning, TEXT("  │ 스폰 성공: %3d개 ✅                                         │"), SpawnedCount);
	UE_LOG(LogTemp, Warning, TEXT("  │ 스폰 실패: %3d개 ❌                                         │"), FailedCount);
	UE_LOG(LogTemp, Warning, TEXT("  │ Client RPC: %-42s │"), IsValid(InvPC) ? TEXT("전송됨") : TEXT("생략됨"));
	UE_LOG(LogTemp, Warning, TEXT("  └─────────────────────────────────────────────────────────────┘"));

	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("🎉 [Phase 5] 플레이어 %s 인벤토리 로드 완료!"), *PlayerUniqueId);
	UE_LOG(LogTemp, Warning, TEXT("════════════════════════════════════════════════════════════════════════════════"));
}

// ============================================
// ⭐ [Phase 4 개선] Character EndPlay에서 호출되는 저장
// ============================================
// 
// 📌 호출 시점: HeroCharacter::EndPlay() (Pawn 파괴 직전)
// 📌 목적: Logout()에서 Pawn이 이미 nullptr이므로, 미리 저장
// 
// ============================================
void AHellunaDefenseGameMode::SaveInventoryFromCharacterEndPlay(const FString& PlayerId, const TArray<FInv_SavedItemData>& CollectedItems)
{
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║ [Phase 4] SaveInventoryFromCharacterEndPlay                ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ PlayerId: %s"), *PlayerId);
	UE_LOG(LogTemp, Warning, TEXT("║ 아이템 수: %d개"), CollectedItems.Num());
	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));

	if (PlayerId.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("   ❌ PlayerId가 비어있음! 저장 취소."));
		return;
	}

	if (CollectedItems.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("   ⚠️ 인벤토리가 비어있음 (저장할 아이템 없음)"));
		// 빈 인벤토리도 저장 (기존 데이터 유지하기 위해 여기서 리턴)
		return;
	}

	// FInv_SavedItemData → FHellunaInventoryItemData 변환
	FHellunaPlayerInventoryData SaveData;
	SaveData.LastSaveTime = FDateTime::Now();

	for (const FInv_SavedItemData& Item : CollectedItems)
	{
		FHellunaInventoryItemData DestItem;
		DestItem.ItemType = Item.ItemType;
		DestItem.StackCount = Item.StackCount;
		DestItem.GridPosition = Item.GridPosition;
		DestItem.GridCategory = Item.GridCategory;
		DestItem.EquipSlotIndex = -1;  // TODO: Phase 6에서 장착 정보 추가

		SaveData.Items.Add(DestItem);

		UE_LOG(LogTemp, Warning, TEXT("   [%d] %s x%d @ Grid%d (%d,%d)"),
			SaveData.Items.Num() - 1,
			*Item.ItemType.ToString(),
			Item.StackCount,
			Item.GridCategory,
			Item.GridPosition.X, Item.GridPosition.Y);
	}

	// SaveGame에 저장
	if (IsValid(InventorySaveGame))
	{
		InventorySaveGame->SavePlayerInventory(PlayerId, SaveData);

		if (UHellunaInventorySaveGame::Save(InventorySaveGame))
		{
			UE_LOG(LogTemp, Warning, TEXT(""));
			UE_LOG(LogTemp, Warning, TEXT("   🎉 EndPlay 저장 성공! (%d개 아이템)"), CollectedItems.Num());
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("   ❌ 파일 저장 실패!"));
		}
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("   ❌ InventorySaveGame이 nullptr!"));
	}

	// 캐시도 업데이트 (혹시 Logout에서 사용할 경우 대비)
	CachedPlayerInventoryData.Add(PlayerId, SaveData);
	UE_LOG(LogTemp, Warning, TEXT("   ✅ 캐시 업데이트 완료"));
	UE_LOG(LogTemp, Warning, TEXT("════════════════════════════════════════════════════════════════════════════════"));
}

// ============================================
// ⭐ [Phase 4 개선] Inv_PlayerController EndPlay 델리게이트 핸들러
// ============================================
// 
// 📌 호출 시점: Inv_PlayerController::EndPlay() (Controller 파괴 직전)
// 📌 장점: Controller에 InventoryComponent가 있으므로 확실히 접근 가능!
// 
// ============================================
void AHellunaDefenseGameMode::OnInvControllerEndPlay(AInv_PlayerController* PlayerController, const TArray<FInv_SavedItemData>& SavedItems)
{
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║ [Phase 4] OnInvControllerEndPlay - Controller 종료 처리    ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ Controller: %s"), PlayerController ? *PlayerController->GetName() : TEXT("nullptr"));
	UE_LOG(LogTemp, Warning, TEXT("║ 아이템 수: %d개"), SavedItems.Num());
	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));

	if (!IsValid(PlayerController))
	{
		UE_LOG(LogTemp, Error, TEXT("   ❌ PlayerController가 nullptr!"));
		return;
	}

	// ⭐ 먼저 매핑에서 PlayerId 조회 (PlayerState가 이미 파괴된 경우를 위해)
	FString PlayerId;
	if (FString* FoundPlayerId = ControllerToPlayerIdMap.Find(PlayerController))
	{
		PlayerId = *FoundPlayerId;
		UE_LOG(LogTemp, Warning, TEXT("   ✅ 매핑에서 PlayerId 찾음: '%s'"), *PlayerId);
		
		// 매핑에서 삭제 (더 이상 필요 없음)
		ControllerToPlayerIdMap.Remove(PlayerController);
	}
	else
	{
		// 매핑에 없으면 PlayerState에서 시도 (폴백)
		AHellunaPlayerState* PS = PlayerController->GetPlayerState<AHellunaPlayerState>();
		if (IsValid(PS))
		{
			PlayerId = PS->GetPlayerUniqueId();
			UE_LOG(LogTemp, Warning, TEXT("   ✅ PlayerState에서 PlayerId 찾음: '%s'"), *PlayerId);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("   ⚠️ PlayerId를 찾을 수 없음 (매핑/PlayerState 모두 실패)"));
		}
	}

	// ============================================
	// Step 1: 인벤토리 저장
	// ============================================
	if (!PlayerId.IsEmpty() && SavedItems.Num() > 0)
	{
		UE_LOG(LogTemp, Warning, TEXT(""));
		UE_LOG(LogTemp, Warning, TEXT("▶ [Step 1] 인벤토리 저장 중..."));
		SaveInventoryFromCharacterEndPlay(PlayerId, SavedItems);
	}
	else if (SavedItems.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("   ⚠️ 인벤토리가 비어있음 (저장 생략)"));
	}

	// ============================================
	// Step 2: 로그아웃 처리 (GameInstance에서 제거)
	// ============================================
	if (!PlayerId.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT(""));
		UE_LOG(LogTemp, Warning, TEXT("▶ [Step 2] 로그아웃 처리 중..."));

		// PlayerState 정리 (아직 유효하면)
		AHellunaPlayerState* PS = PlayerController->GetPlayerState<AHellunaPlayerState>();
		if (IsValid(PS) && PS->IsLoggedIn())
		{
			PS->ClearLoginInfo();
			UE_LOG(LogTemp, Warning, TEXT("   ✅ PlayerState 정리 완료"));
		}

		// GameInstance에서 제거
		if (UMDF_GameInstance* GI = Cast<UMDF_GameInstance>(UGameplayStatics::GetGameInstance(GetWorld())))
		{
			GI->RegisterLogout(PlayerId);
			UE_LOG(LogTemp, Warning, TEXT("   ✅ RegisterLogout 호출됨: '%s'"), *PlayerId);
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("   ⚠️ PlayerId가 비어있어 로그아웃 처리 생략"));
	}

	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("🎉 [Phase 4] Controller EndPlay 처리 완료!"));
	UE_LOG(LogTemp, Warning, TEXT("════════════════════════════════════════════════════════════════════════════════"));
}

void AHellunaDefenseGameMode::DebugTestLoadInventory()
{
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("🔧 [디버그] 첫 번째 플레이어의 인벤토리 로드 테스트"));

	// 첫 번째 PlayerController 찾기
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (IsValid(PC))
		{
			LoadAndSendInventoryToClient(PC);
			return;
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("   ⚠️ 플레이어를 찾을 수 없습니다!"));
}
