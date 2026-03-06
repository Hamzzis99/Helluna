// ════════════════════════════════════════════════════════════════════════════════
// HellunaLobbyGameMode.cpp
// ════════════════════════════════════════════════════════════════════════════════
//
// 로비 전용 GameMode — Stash 로드/저장 + 크래시 복구
//
// ============================================================================
// 📌 Phase 4 Step 4-1: 로비 GameMode
// ============================================================================
//
// 📌 역할:
//   - 로비에 진입한 플레이어의 Stash 데이터를 SQLite에서 로드
//   - 로비에서 나가는 플레이어의 Stash+Loadout을 SQLite에 저장
//   - 크래시 복구: 비정상 종료 시 player_loadout에 남은 데이터를 Stash로 복원
//
// 📌 핵심 흐름 (서버에서만 실행됨):
//   PostLogin:
//     1) CheckAndRecoverFromCrash(PlayerId) — 이전 비정상 종료 시 Loadout→Stash 복구
//     2) LoadStashToComponent(LobbyPC, PlayerId) — SQLite → FInv_PlayerSaveData → RestoreFromSaveData
//     3) RegisterControllerPlayerId() — Logout 시 PlayerId 찾기 위한 맵 등록
//
//   Logout:
//     1) StashComp → CollectInventoryDataForSave() → SQLite SavePlayerStash
//     2) LoadoutComp에 잔존 아이템 있으면 Stash에 병합해서 저장 (데이터 유실 방지)
//
// 📌 상속 구조:
//   AGameMode → AInv_SaveGameMode → AHellunaBaseGameMode → AHellunaLobbyGameMode
//
// 📌 SQLite 테이블 사용:
//   - player_stash: 전체 보유 아이템 (로비 창고)
//   - player_loadout: 출격 장비 (Deploy 시 사용, 크래시 복구용)
//
// 작성자: Gihyeon (Claude Code 보조)
// ════════════════════════════════════════════════════════════════════════════════

#include "Lobby/GameMode/HellunaLobbyGameMode.h"
#include "Lobby/Controller/HellunaLobbyController.h"
#include "Lobby/Database/HellunaSQLiteSubsystem.h"
#include "InventoryManagement/Components/Inv_InventoryComponent.h"
#include "Persistence/Inv_SaveTypes.h"
#include "Items/Components/Inv_ItemComponent.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "MDF_Function/MDF_Instance/MDF_GameInstance.h"
#include "Login/Save/HellunaAccountSaveGame.h"

// 로그 카테고리 (공유 헤더에서 DECLARE, 여기서 DEFINE)
#include "Lobby/HellunaLobbyLog.h"
DEFINE_LOG_CATEGORY(LogHellunaLobby);

// ════════════════════════════════════════════════════════════════════════════════
// 생성자
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 로비에서는 캐릭터(Pawn)가 필요 없음 → DefaultPawnClass = nullptr
//    플레이어는 UI만 조작하며, 3D 캐릭터는 게임 맵에서만 스폰
//
// 📌 PlayerControllerClass는 BP(BP_HellunaLobbyGameMode)에서 설정
//    BP_HellunaLobbyController를 지정해야 함
//
// ════════════════════════════════════════════════════════════════════════════════
AHellunaLobbyGameMode::AHellunaLobbyGameMode()
{
	DefaultPawnClass = nullptr;

	// 로비 서버 고유 ID 생성 (GUID 기반)
	LobbyServerId = FGuid::NewGuid().ToString();

	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] ========================================"));
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] 생성자 호출 | DefaultPawnClass=nullptr | ServerId=%s"), *LobbyServerId);
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] ========================================"));
}

// ════════════════════════════════════════════════════════════════════════════════
// [Phase 12h] BeginPlay — 서버 초기화 (오래된 파티 정리)
// ════════════════════════════════════════════════════════════════════════════════

void AHellunaLobbyGameMode::BeginPlay()
{
	Super::BeginPlay();

	// SQLite 서브시스템 캐시 (PostLogin 보다 앞서 초기화)
	if (!SQLiteSubsystem)
	{
		UGameInstance* GI = GetGameInstance();
		if (GI)
		{
			SQLiteSubsystem = GI->GetSubsystem<UHellunaSQLiteSubsystem>();
		}
	}

	// 24시간 이상 된 오래된 파티 DB에서 정리
	if (SQLiteSubsystem)
	{
		SQLiteSubsystem->CleanupStaleParties(24);
		UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] BeginPlay: CleanupStaleParties(24h) 완료"));
	}

	// [Fix46-M6] 1시간 주기 파티 정리 타이머 (서버 장기 가동 시 DB 누적 방지)
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			StalePartyCleanupTimer,
			[this]()
			{
				if (SQLiteSubsystem)
				{
					const int32 Cleaned = SQLiteSubsystem->CleanupStaleParties(24);
					if (Cleaned > 0)
					{
						UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] [Fix46-M6] 주기적 CleanupStaleParties: %d개 정리"), Cleaned);
					}
				}
			},
			3600.f, // 1시간 간격
			true    // 반복
		);
	}

	// [Phase 13] AccountSaveGame 로드 (로비 로그인용)
	LobbyAccountSaveGame = UHellunaAccountSaveGame::LoadOrCreate();
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] BeginPlay: AccountSaveGame %s | 계정 수=%d"),
		LobbyAccountSaveGame ? TEXT("로드 성공") : TEXT("로드 실패"),
		LobbyAccountSaveGame ? LobbyAccountSaveGame->GetAccountCount() : 0);
}

// ════════════════════════════════════════════════════════════════════════════════
// PostLogin — 플레이어가 로비에 진입할 때 호출 (서버에서만 실행)
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 호출 시점: 클라이언트가 로비 맵에 접속 완료 직후
// 📌 실행 위치: 서버 (Dedicated Server 또는 ListenServer)
//
// 📌 처리 순서:
//   1) Cast<AHellunaLobbyController> — 올바른 PC 타입인지 확인
//   2) SQLite 서브시스템 획득 및 캐시
//   3) PlayerId 획득 (UniqueNetId 기반, 또는 디버그 모드 시 고정 ID)
//   4) CheckAndRecoverFromCrash — 이전 비정상 종료 시 Loadout→Stash 복구
//   5) LoadStashToComponent — SQLite에서 Stash 로드 → StashComp에 RestoreFromSaveData
//   6) RegisterControllerPlayerId — Logout 시 PlayerId 역추적 맵 등록
//
// 📌 주의:
//   - 이 시점에서 StashComp/LoadoutComp는 이미 생성자에서 생성됨 (CDO에서 CreateDefaultSubobject)
//   - PlayerId가 비어있고 bDebugSkipLogin=true이면 "debug_lobby_player" 사용
//
// ════════════════════════════════════════════════════════════════════════════════
void AHellunaLobbyGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);

	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] ══════════════════════════════════════"));
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] PostLogin 시작 | PC=%s"), *GetNameSafe(NewPlayer));
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] ══════════════════════════════════════"));

	if (!NewPlayer)
	{
		UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyGM] PostLogin: NewPlayer가 nullptr!"));
		return;
	}

	// ── Cast: AHellunaLobbyController 확인 ──
	// BP_HellunaLobbyGameMode의 PlayerControllerClass가 BP_HellunaLobbyController인지 확인
	AHellunaLobbyController* LobbyPC = Cast<AHellunaLobbyController>(NewPlayer);
	if (!LobbyPC)
	{
		UE_LOG(LogHellunaLobby, Warning, TEXT("[LobbyGM] PostLogin: Controller가 HellunaLobbyController가 아닙니다!"));
		UE_LOG(LogHellunaLobby, Warning, TEXT("[LobbyGM]   → 실제 클래스: %s"), *NewPlayer->GetClass()->GetName());
		UE_LOG(LogHellunaLobby, Warning, TEXT("[LobbyGM]   → BP GameMode의 PlayerControllerClass 설정을 확인하세요!"));
		return;
	}

	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] PostLogin: LobbyPC 캐스팅 성공 | StashComp=%s | LoadoutComp=%s"),
		LobbyPC->GetStashComponent() ? TEXT("O") : TEXT("X"),
		LobbyPC->GetLoadoutComponent() ? TEXT("O") : TEXT("X"));

	// ── SQLite 서브시스템 캐시 ──
	if (!SQLiteSubsystem)
	{
		UGameInstance* GI = GetGameInstance();
		SQLiteSubsystem = GI ? GI->GetSubsystem<UHellunaSQLiteSubsystem>() : nullptr;
		UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] SQLite 서브시스템 캐시: %s"),
			SQLiteSubsystem ? TEXT("성공") : TEXT("실패"));
	}

	if (!SQLiteSubsystem)
	{
		UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyGM] PostLogin: SQLiteSubsystem 없음!"));
		return;
	}

	if (!SQLiteSubsystem->IsDatabaseReady())
	{
		UE_LOG(LogHellunaLobby, Warning, TEXT("[LobbyGM] PostLogin: DB 미준비 — TryReopenDatabase 시도"));
		SQLiteSubsystem->TryReopenDatabase();
		if (!SQLiteSubsystem->IsDatabaseReady())
		{
			UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyGM] PostLogin: TryReopenDatabase 실패!"));
			return;
		}
	}

	// ── [Phase 13] 로그인 모드 분기 ──
	if (bDebugSkipLogin)
	{
		// ── 디버그 모드: 로그인 스킵 → 즉시 로비 초기화 ──
		const FString PlayerId = TEXT("DebugPlayer");
		UE_LOG(LogHellunaLobby, Warning, TEXT("[LobbyGM] [Phase 13] 디버그 모드 → 고정 ID '%s' 사용"), *PlayerId);
		InitializeLobbyForPlayer(LobbyPC, PlayerId);
	}
	else
	{
		// ── 로그인 모드: 클라이언트에 로그인 UI 표시 지시 → 로그인 대기 ──
		UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] [Phase 13] 로그인 필요 → Client_ShowLobbyLoginUI 호출"));
		LobbyPC->Client_ShowLobbyLoginUI();
	}

	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] ══════════════════════════════════════"));
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] PostLogin 완료 | PC=%s"), *GetNameSafe(NewPlayer));
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] ══════════════════════════════════════"));
}

// ════════════════════════════════════════════════════════════════════════════════
// [Phase 13] ProcessLobbyLogin — 로비 로그인 처리
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 호출 시점: Controller->Server_RequestLobbyLogin에서 호출
// 📌 처리: AccountSaveGame 검증 → 동시접속 체크 → 성공 시 InitializeLobbyForPlayer
//
// ════════════════════════════════════════════════════════════════════════════════
void AHellunaLobbyGameMode::ProcessLobbyLogin(AHellunaLobbyController* LobbyPC, const FString& PlayerId, const FString& Password)
{
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] ──────────────────────────────────────"));
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] ProcessLobbyLogin | PlayerId=%s | PC=%s"), *PlayerId, *GetNameSafe(LobbyPC));
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] ──────────────────────────────────────"));

	if (!LobbyPC)
	{
		UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyGM] ProcessLobbyLogin: LobbyPC nullptr!"));
		return;
	}

	// ── 동시 접속 체크 ──
	UMDF_GameInstance* GI = Cast<UMDF_GameInstance>(GetGameInstance());
	if (GI && GI->IsPlayerLoggedIn(PlayerId))
	{
		UE_LOG(LogHellunaLobby, Warning, TEXT("[LobbyGM] ProcessLobbyLogin: 동시 접속 거부 | PlayerId=%s"), *PlayerId);
		LobbyPC->Client_LobbyLoginResult(false, TEXT("이미 접속 중인 계정입니다."));
		return;
	}

	// ── AccountSaveGame 검증 ──
	if (!LobbyAccountSaveGame)
	{
		UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyGM] ProcessLobbyLogin: AccountSaveGame nullptr!"));
		LobbyPC->Client_LobbyLoginResult(false, TEXT("서버 오류"));
		return;
	}

	if (LobbyAccountSaveGame->HasAccount(PlayerId))
	{
		// 기존 계정: 비밀번호 검증
		if (!LobbyAccountSaveGame->ValidatePassword(PlayerId, Password))
		{
			UE_LOG(LogHellunaLobby, Warning, TEXT("[LobbyGM] ProcessLobbyLogin: 비밀번호 불일치 | PlayerId=%s"), *PlayerId);
			LobbyPC->Client_LobbyLoginResult(false, TEXT("비밀번호를 확인해주세요."));
			return;
		}
		UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] ProcessLobbyLogin: 기존 계정 로그인 성공 | PlayerId=%s"), *PlayerId);
	}
	else
	{
		// 계정 없음: 회원가입 안내
		UE_LOG(LogHellunaLobby, Warning, TEXT("[LobbyGM] ProcessLobbyLogin: 계정 없음 | PlayerId=%s"), *PlayerId);
		LobbyPC->Client_LobbyLoginResult(false, TEXT("계정이 없습니다. 회원가입해주세요."));
		return;
	}

	// ── 로그인 성공 → 등록 + 초기화 ──
	if (GI)
	{
		GI->RegisterLogin(PlayerId);
		UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] ProcessLobbyLogin: RegisterLogin 완료 | PlayerId=%s"), *PlayerId);
	}

	InitializeLobbyForPlayer(LobbyPC, PlayerId);
}

// ════════════════════════════════════════════════════════════════════════════════
// [Phase 13] ProcessLobbySignup — 로비 회원가입 처리
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 호출 시점: Controller->Server_RequestLobbySignup에서 호출
// 📌 처리: ID 중복 체크 → 계정 생성 → Save → 성공 메시지 (자동 로그인 안 함)
//
// ════════════════════════════════════════════════════════════════════════════════
void AHellunaLobbyGameMode::ProcessLobbySignup(AHellunaLobbyController* LobbyPC, const FString& PlayerId, const FString& Password)
{
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] ──────────────────────────────────────"));
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] ProcessLobbySignup | PlayerId=%s | PC=%s"), *PlayerId, *GetNameSafe(LobbyPC));
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] ──────────────────────────────────────"));

	if (!LobbyPC)
	{
		UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyGM] ProcessLobbySignup: LobbyPC nullptr!"));
		return;
	}

	// ── AccountSaveGame 검증 ──
	if (!LobbyAccountSaveGame)
	{
		UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyGM] ProcessLobbySignup: AccountSaveGame nullptr!"));
		LobbyPC->Client_LobbySignupResult(false, TEXT("서버 오류"));
		return;
	}

	// ── ID 중복 체크 ──
	if (LobbyAccountSaveGame->HasAccount(PlayerId))
	{
		UE_LOG(LogHellunaLobby, Warning, TEXT("[LobbyGM] ProcessLobbySignup: 이미 존재하는 아이디 | PlayerId=%s"), *PlayerId);
		LobbyPC->Client_LobbySignupResult(false, TEXT("이미 존재하는 아이디입니다."));
		return;
	}

	// ── 계정 생성 ──
	if (!LobbyAccountSaveGame->CreateAccount(PlayerId, Password))
	{
		UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyGM] ProcessLobbySignup: 계정 생성 실패 | PlayerId=%s"), *PlayerId);
		LobbyPC->Client_LobbySignupResult(false, TEXT("계정 생성에 실패했습니다."));
		return;
	}

	// ── 저장 ──
	UHellunaAccountSaveGame::Save(LobbyAccountSaveGame);
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] ProcessLobbySignup: 계정 생성 + 저장 완료 | PlayerId=%s"), *PlayerId);

	// ── 성공 통보 (자동 로그인 안 함 — 클라이언트에서 로그인 탭으로 전환) ──
	LobbyPC->Client_LobbySignupResult(true, TEXT("회원가입 성공! 로그인해주세요."));
}

// ════════════════════════════════════════════════════════════════════════════════
// [Phase 13] InitializeLobbyForPlayer — 로그인 성공 후 로비 초기화
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 기존 PostLogin의 Step 0~5를 추출
// 📌 bDebugSkipLogin=true 경로와 로그인 성공 경로 모두 이 함수를 호출
//
// ════════════════════════════════════════════════════════════════════════════════
void AHellunaLobbyGameMode::InitializeLobbyForPlayer(AHellunaLobbyController* LobbyPC, const FString& PlayerId)
{
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] ══════════════════════════════════════"));
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] InitializeLobbyForPlayer 시작 | PlayerId=%s"), *PlayerId);
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] ══════════════════════════════════════"));

	if (!LobbyPC || PlayerId.IsEmpty())
	{
		UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyGM] InitializeLobbyForPlayer: 유효하지 않은 파라미터!"));
		return;
	}

	// ── 0) 게임 결과 파일 처리 ──
	bool bGameResultProcessed = false;
	if (SQLiteSubsystem->HasPendingGameResultFile(PlayerId))
	{
		UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] [0] 게임 결과 파일 발견 → import 시작 | PlayerId=%s"), *PlayerId);

		bool bSurvived = false;
		bool bImportSuccess = false;
		TArray<FHellunaEquipmentSlotData> GameEquipment;
		TArray<FInv_SavedItemData> ResultItems = SQLiteSubsystem->ImportGameResultFromFile(PlayerId, bSurvived, bImportSuccess, &GameEquipment);

		if (bImportSuccess)
		{
			bGameResultProcessed = true;
			SQLiteSubsystem->SetPlayerDeployed(PlayerId, false);

			UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] [0] 게임 결과: survived=%s | 아이템=%d개 | 장착=%d슬롯 | PlayerId=%s"),
				bSurvived ? TEXT("Y") : TEXT("N"), ResultItems.Num(), GameEquipment.Num(), *PlayerId);

			if (bSurvived && ResultItems.Num() > 0)
			{
				if (GameEquipment.Num() > 0)
				{
					SQLiteSubsystem->SavePlayerEquipment(PlayerId, GameEquipment);
					UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] [0] SavePlayerEquipment 성공 | %d개 슬롯 | PlayerId=%s"),
						GameEquipment.Num(), *PlayerId);
				}

				if (SQLiteSubsystem->SavePlayerLoadout(PlayerId, ResultItems))
				{
					UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] [0] [Fix23] SavePlayerLoadout 성공 | PlayerId=%s | %d개"),
						*PlayerId, ResultItems.Num());
				}
				else
				{
					UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyGM] [0] [Fix23] SavePlayerLoadout 실패! Stash 폴백 | PlayerId=%s"), *PlayerId);
					SQLiteSubsystem->MergeGameResultToStash(PlayerId, ResultItems);
					SQLiteSubsystem->DeletePlayerLoadout(PlayerId);
					SQLiteSubsystem->DeletePlayerEquipment(PlayerId);
				}
			}
			else
			{
				SQLiteSubsystem->DeletePlayerEquipment(PlayerId);
				if (SQLiteSubsystem->DeletePlayerLoadout(PlayerId))
				{
					UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] [0] DeletePlayerLoadout 성공 (사망/아이템없음) | PlayerId=%s"), *PlayerId);
				}
				else
				{
					UE_LOG(LogHellunaLobby, Warning, TEXT("[LobbyGM] [0] DeletePlayerLoadout: 삭제할 Loadout 없음 | PlayerId=%s"), *PlayerId);
				}
			}
		}
		else
		{
			UE_LOG(LogHellunaLobby, Error,
				TEXT("[LobbyGM] [0] GameResult 파일 손상! Loadout 보존 → 크래시 복구로 전환 | PlayerId=%s"), *PlayerId);
		}
	}

	// ── 0.5) [Phase 14d] 재참가 감지 ──
	if (!bGameResultProcessed && SQLiteSubsystem->IsPlayerDeployed(PlayerId))
	{
		const int32 DeployedPort = SQLiteSubsystem->GetPlayerDeployedPort(PlayerId);
		if (DeployedPort > 0 && IsGameServerRunning(DeployedPort))
		{
			UE_LOG(LogHellunaLobby, Warning, TEXT("[LobbyGM] [Phase14] 재참가 후보 감지! | PlayerId=%s | Port=%d"),
				*PlayerId, DeployedPort);
			LobbyPC->SetReplicatedPlayerId(PlayerId);
			LobbyPC->PendingRejoinPort = DeployedPort;
			LobbyPC->Client_ShowRejoinPrompt(DeployedPort);
			// 초기화 중단 — 플레이어 결정 대기 (HandleRejoinAccepted / HandleRejoinDeclined)
			return;
		}
		else
		{
			UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] [Phase14] 게임서버 비활성 → 기존 크래시 복구 진행 | PlayerId=%s | Port=%d"),
				*PlayerId, DeployedPort);
			// fall-through to crash recovery below
		}
	}

	// ── 1) [Fix36] 크래시 복구 ──
	if (!bGameResultProcessed)
	{
		if (SQLiteSubsystem->IsPlayerDeployed(PlayerId))
		{
			UE_LOG(LogHellunaLobby, Warning, TEXT("[LobbyGM] [Fix36] [1/4] 크래시 감지 | PlayerId=%s"), *PlayerId);
			SQLiteSubsystem->SetPlayerDeployed(PlayerId, false);
			UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] [Fix36] [1/4] deploy 상태 해제 완료 | PlayerId=%s"), *PlayerId);
		}
		else
		{
			UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] [Fix36] [1/4] 크래시 아님 (미출격 상태) | PlayerId=%s"), *PlayerId);
		}
	}
	else
	{
		UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] [1/4] 크래시 복구 스킵 — 게임 결과 정상 처리됨 | PlayerId=%s"), *PlayerId);
	}

	// ── 2) Stash 로드 → StashComp에 복원 ──
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] [2/4] Stash 로드 → StashComp | PlayerId=%s"), *PlayerId);
	LoadStashToComponent(LobbyPC, PlayerId);

	// ── 2.5) [Fix23] Loadout 로드 → LoadoutComp에 복원 ──
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] [2.5/4] [Fix23] Loadout 로드 → LoadoutComp | PlayerId=%s"), *PlayerId);
	LoadLoadoutToComponent(LobbyPC, PlayerId);

	// ── 3) Controller-PlayerId 매핑 등록 ──
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] [3/4] Controller-PlayerId 매핑 등록 | PlayerId=%s"), *PlayerId);
	RegisterControllerPlayerId(LobbyPC, PlayerId);

	// ── 4) 가용 캐릭터 정보 전달 ──
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] [4/4] 가용 캐릭터 정보 전달"));
	{
		TArray<bool> UsedCharacters = GetLobbyAvailableCharacters();
		LobbyPC->Client_ShowLobbyCharacterSelectUI(UsedCharacters);
	}

	// ── ReplicatedPlayerId 설정 ──
	LobbyPC->SetReplicatedPlayerId(PlayerId);

	// ── [Phase 12d] Step 5: 파티 자동 복귀 ──
	PlayerIdToControllerMap.Add(PlayerId, LobbyPC);
	{
		const int32 ExistingPartyId = SQLiteSubsystem->GetPlayerPartyId(PlayerId);
		if (ExistingPartyId > 0)
		{
			UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] [5/5] 파티 자동 복귀 | PlayerId=%s | PartyId=%d"), *PlayerId, ExistingPartyId);

			SQLiteSubsystem->UpdateMemberReady(PlayerId, false);

			// [Fix43] 파티 DB에서 hero_type 복원 → 새 Controller의 SelectedHeroType 설정
			{
				FHellunaPartyInfo TempInfo = SQLiteSubsystem->LoadPartyInfo(ExistingPartyId);
				for (const FHellunaPartyMemberInfo& Member : TempInfo.Members)
				{
					if (Member.PlayerId == PlayerId && Member.SelectedHeroType != 3) // 3 = None
					{
						const EHellunaHeroType RestoredHero = IndexToHeroType(Member.SelectedHeroType);
						if (RestoredHero != EHellunaHeroType::None)
						{
							RegisterLobbyCharacterUse(RestoredHero, PlayerId);
							LobbyPC->ForceSetSelectedHeroType(RestoredHero);
							UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] [Fix43] 파티 복귀 시 캐릭터 복원 | PlayerId=%s | HeroType=%d"), *PlayerId, Member.SelectedHeroType);
						}
						break;
					}
				}
			}

			if (FTimerHandle* TimerPtr = PartyLeaveTimers.Find(PlayerId))
			{
				UWorld* World = GetWorld();
				if (World)
				{
					World->GetTimerManager().ClearTimer(*TimerPtr);
				}
				PartyLeaveTimers.Remove(PlayerId);
				UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] 파티 탈퇴 타이머 취소 | PlayerId=%s"), *PlayerId);
			}

			RefreshPartyCache(ExistingPartyId);
			BroadcastPartyState(ExistingPartyId);
		}
		else
		{
			UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] [5/5] 파티 미가입 상태 | PlayerId=%s"), *PlayerId);
		}
	}

	// ── 로비 UI 표시 (0.5초 딜레이 — 리플리케이션 대기) ──
	FTimerHandle UITimer;
	UWorld* World = GetWorld();
	if (World)
	{
		TWeakObjectPtr<AHellunaLobbyController> WeakPC = LobbyPC;
		World->GetTimerManager().SetTimer(UITimer, [WeakPC]()
		{
			if (WeakPC.IsValid())
			{
				WeakPC->Client_ShowLobbyUI();
			}
		}, 0.5f, false);
	}

	// ── 로그인 결과 성공 통보 ──
	LobbyPC->Client_LobbyLoginResult(true, TEXT(""));

	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] ══════════════════════════════════════"));
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] InitializeLobbyForPlayer 완료 | PlayerId=%s"), *PlayerId);
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] ══════════════════════════════════════"));
}

// ════════════════════════════════════════════════════════════════════════════════
// Logout — 플레이어가 로비에서 나갈 때 호출 (서버에서만 실행)
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 호출 시점: 클라이언트가 접속 해제 (정상 종료, ClientTravel, 비정상 종료 모두)
// 📌 실행 위치: 서버
//
// 📌 처리 내용:
//   - StashComp의 현재 상태를 SQLite player_stash에 저장
//   - LoadoutComp에 잔존 아이템이 있으면 Stash에 병합 저장 (데이터 유실 방지)
//   - PlayerId를 직접 얻지 못하면 ControllerToPlayerIdMap 폴백 사용
//
// 📌 주의:
//   - Deploy(출격)로 나간 경우: LoadoutComp 데이터는 이미 Server_Deploy에서 저장됨
//   - 비정상 종료(크래시)인 경우: 여기서 저장 못 하면 다음 PostLogin에서 복구
//
// ════════════════════════════════════════════════════════════════════════════════
void AHellunaLobbyGameMode::Logout(AController* Exiting)
{
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] ──────────────────────────────────────"));
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] Logout 시작 | Controller=%s"), *GetNameSafe(Exiting));
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] ──────────────────────────────────────"));

	if (Exiting)
	{
		AHellunaLobbyController* LobbyPC = Cast<AHellunaLobbyController>(Exiting);
		APlayerController* PC = Cast<APlayerController>(Exiting);
		const FString PlayerId = GetLobbyPlayerId(PC);

		// 캐릭터 사용 해제
		if (!PlayerId.IsEmpty())
		{
			UnregisterLobbyCharacterUse(PlayerId);
		}
		else if (const FString* CachedId = ControllerToPlayerIdMap.Find(Exiting))
		{
			UnregisterLobbyCharacterUse(*CachedId);
		}

		if (LobbyPC && !PlayerId.IsEmpty())
		{
			// 정상 경로: PlayerState에서 직접 PlayerId 획득 성공
			UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] Logout: 정상 경로 | PlayerId=%s | Stash/Loadout 저장 시작"), *PlayerId);
			SaveComponentsToDatabase(LobbyPC, PlayerId);
		}
		else
		{
			// 폴백 경로: PlayerState가 이미 정리된 경우 캐시된 ID 사용
			UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] Logout: 직접 ID 획득 실패 → ControllerToPlayerIdMap 폴백 시도"));
			if (const FString* CachedId = ControllerToPlayerIdMap.Find(Exiting))
			{
				if (LobbyPC && !CachedId->IsEmpty())
				{
					UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] Logout: 캐시 ID 발견 | PlayerId=%s"), **CachedId);
					SaveComponentsToDatabase(LobbyPC, *CachedId);
				}
				else
				{
					UE_LOG(LogHellunaLobby, Warning, TEXT("[LobbyGM] Logout: 캐시 ID는 있지만 LobbyPC=%s, CachedId='%s'"),
						LobbyPC ? TEXT("O") : TEXT("X"), CachedId ? **CachedId : TEXT("(null)"));
				}
			}
			else
			{
				UE_LOG(LogHellunaLobby, Warning, TEXT("[LobbyGM] Logout: ControllerToPlayerIdMap에서도 ID를 찾지 못함!"));
				UE_LOG(LogHellunaLobby, Warning, TEXT("[LobbyGM]   → 이 플레이어의 Stash는 저장되지 않았습니다."));
				UE_LOG(LogHellunaLobby, Warning, TEXT("[LobbyGM]   → 다음 로그인 시 이전 저장 상태로 복원됩니다."));
			}
		}
	}
	else
	{
		UE_LOG(LogHellunaLobby, Warning, TEXT("[LobbyGM] Logout: Exiting Controller가 nullptr!"));
	}

	// [Phase 12d] 파티 연결 해제 처리
	{
		FString LogoutPlayerId;
		APlayerController* LogoutPC = Cast<APlayerController>(Exiting);
		if (LogoutPC)
		{
			LogoutPlayerId = GetLobbyPlayerId(LogoutPC);
		}
		if (LogoutPlayerId.IsEmpty())
		{
			if (const FString* CachedId = ControllerToPlayerIdMap.Find(Exiting))
			{
				LogoutPlayerId = *CachedId;
			}
		}

		if (!LogoutPlayerId.IsEmpty())
		{
			PlayerIdToControllerMap.Remove(LogoutPlayerId);

			AHellunaLobbyController* LogoutLobbyPC = Cast<AHellunaLobbyController>(Exiting);
			const bool bDeploy = LogoutLobbyPC && LogoutLobbyPC->IsDeployInProgress();

			if (bDeploy)
			{
				// Deploy로 인한 Logout — 파티 탈퇴 안 함
				UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] Logout: Deploy 중 — 파티 유지 | PlayerId=%s"), *LogoutPlayerId);
			}
			else if (PlayerToPartyMap.Contains(LogoutPlayerId))
			{
				// Ready 리셋
				if (SQLiteSubsystem && SQLiteSubsystem->IsDatabaseReady())
				{
					SQLiteSubsystem->UpdateMemberReady(LogoutPlayerId, false);
					const int32* PId = PlayerToPartyMap.Find(LogoutPlayerId);
					if (PId && *PId > 0)
					{
						SQLiteSubsystem->ResetAllReadyStates(*PId);
						RefreshPartyCache(*PId);
						BroadcastPartyState(*PId);
					}
				}

				// 30초 유예 타이머 → 재접속 안 하면 자동 탈퇴
				FTimerHandle& LeaveTimer = PartyLeaveTimers.FindOrAdd(LogoutPlayerId);
				UWorld* World = GetWorld();
				if (World)
				{
					FString CapturedPlayerId = LogoutPlayerId;
					TWeakObjectPtr<AHellunaLobbyGameMode> WeakThis(this);
					World->GetTimerManager().SetTimer(LeaveTimer, [WeakThis, CapturedPlayerId]()
					{
						if (!WeakThis.IsValid()) return;
						UE_LOG(LogHellunaLobby, Warning, TEXT("[LobbyGM] 30초 유예 만료 — 파티 자동 탈퇴 | PlayerId=%s"), *CapturedPlayerId);
						WeakThis->LeavePartyForPlayer(CapturedPlayerId);
						WeakThis->PartyLeaveTimers.Remove(CapturedPlayerId);
					}, 30.f, false);

					UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] Logout: 30초 파티 탈퇴 타이머 시작 | PlayerId=%s"), *LogoutPlayerId);
				}
			}
		}
	}

	// [Phase 13] RegisterLogout — 동시접속 방지 해제
	{
		FString LogoutId;
		APlayerController* ExitingPC = Cast<APlayerController>(Exiting);
		if (ExitingPC)
		{
			LogoutId = GetLobbyPlayerId(ExitingPC);
		}
		if (LogoutId.IsEmpty())
		{
			if (const FString* CachedId = ControllerToPlayerIdMap.Find(Exiting))
			{
				LogoutId = *CachedId;
			}
		}
		if (!LogoutId.IsEmpty())
		{
			UMDF_GameInstance* GI = Cast<UMDF_GameInstance>(GetGameInstance());
			if (GI)
			{
				GI->RegisterLogout(LogoutId);
				UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] Logout: RegisterLogout 완료 | PlayerId=%s"), *LogoutId);
			}
		}
	}

	// ControllerToPlayerIdMap 정리 (댕글링 포인터 방지)
	ControllerToPlayerIdMap.Remove(Exiting);

	Super::Logout(Exiting);

	// ── 마지막 플레이어 로그아웃 시 DB 연결 해제 ──
	// PIE(로비서버)가 DB 파일을 잠그고 있으면 데디서버(게임서버)가 열 수 없음
	// → 플레이어가 없으면 DB가 불필요하므로 잠금 해제
	// → 데디서버의 TryReopenDatabase()가 성공할 수 있게 됨
	const int32 RemainingPlayers = GetNumPlayers();
	if (RemainingPlayers <= 0)
	{
		UE_LOG(LogHellunaLobby, Warning, TEXT("[LobbyGM] 마지막 플레이어 로그아웃 → DB 연결 해제 (게임서버 접근 허용)"));
		if (SQLiteSubsystem)
		{
			SQLiteSubsystem->ReleaseDatabaseConnection();
		}
	}
	else
	{
		UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] 잔여 플레이어 %d명 → DB 연결 유지"), RemainingPlayers);
	}

	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] Logout 완료"));
}

// ════════════════════════════════════════════════════════════════════════════════
// LoadStashToComponent — SQLite에서 Stash 데이터를 로드하여 StashComp에 복원
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 데이터 흐름:
//   SQLite player_stash 테이블
//     → LoadPlayerStash(PlayerId)
//     → TArray<FInv_SavedItemData> (ItemType + StackCount + GridPosition 등)
//     → FInv_PlayerSaveData로 래핑
//     → StashComp->RestoreFromSaveData(SaveData, Resolver)
//     → Resolver가 GameplayTag → UInv_ItemComponent* 로 변환
//     → FastArray에 아이템 생성 및 추가
//
// 📌 Resolver란?
//   FInv_ItemTemplateResolver는 델리게이트로, ItemType(GameplayTag)을 받아서
//   해당 아이템의 CDO(Class Default Object)에서 UInv_ItemComponent*를 반환
//   → 이 템플릿을 기반으로 새 UInv_InventoryItem을 생성
//   HellunaBaseGameMode::ResolveItemClass()를 내부적으로 사용
//
// ════════════════════════════════════════════════════════════════════════════════
void AHellunaLobbyGameMode::LoadStashToComponent(AHellunaLobbyController* LobbyPC, const FString& PlayerId)
{
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] LoadStashToComponent 시작 | PlayerId=%s"), *PlayerId);

	if (!LobbyPC || !SQLiteSubsystem || !SQLiteSubsystem->IsDatabaseReady())
	{
		UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyGM] LoadStash: 조건 미충족 | LobbyPC=%s, DB=%s"),
			LobbyPC ? TEXT("O") : TEXT("X"),
			(SQLiteSubsystem && SQLiteSubsystem->IsDatabaseReady()) ? TEXT("Ready") : TEXT("Not Ready"));
		return;
	}

	UInv_InventoryComponent* StashComp = LobbyPC->GetStashComponent();
	if (!StashComp)
	{
		UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyGM] LoadStash: StashComp가 nullptr! | PlayerId=%s"), *PlayerId);
		UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyGM]   → BP_HellunaLobbyController의 생성자에서 CreateDefaultSubobject가 실행되었는지 확인하세요"));
		return;
	}

	// ── SQLite에서 Stash 로드 ──
	TArray<FInv_SavedItemData> StashItems = SQLiteSubsystem->LoadPlayerStash(PlayerId);
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] SQLite Stash 로드 완료 | PlayerId=%s | 아이템 %d개"), *PlayerId, StashItems.Num());

	if (StashItems.Num() == 0)
	{
		// [Fix41] 빈 Stash도 정상 — 미로드(-1) 아닌 "0개 로드됨"으로 설정
		// 미설정 시 LoadedStashItemCount=-1 유지 → SaveComponentsToDatabase에서 전체 저장 차단
		LobbyPC->SetLoadedStashItemCount(0);
		UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] Stash가 비어있음 → 빈 인벤토리로 시작 | PlayerId=%s"), *PlayerId);
		UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM]   → DebugSave 콘솔 명령으로 테스트 데이터를 넣을 수 있습니다"));
		return;
	}

	// [Fix46-M7] 사전 진단: Shipping 빌드에서는 실행 불필요 (RestoreFromSaveData에서 동일 리졸브 수행)
#if !UE_BUILD_SHIPPING
	// ── 로드된 아이템 상세 로그 + 리졸브 사전 진단 ──
	int32 DiagResolveFail = 0;
	for (int32 i = 0; i < StashItems.Num(); ++i)
	{
		const FInv_SavedItemData& ItemData = StashItems[i];
		UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM]   [%d] ItemType=%s | Stack=%d | GridPos=(%d,%d) | Cat=%d"),
			i, *ItemData.ItemType.ToString(), ItemData.StackCount,
			ItemData.GridPosition.X, ItemData.GridPosition.Y, ItemData.GridCategory);

		// 사전 진단: 각 아이템이 리졸브 가능한지 미리 확인
		UInv_ItemComponent* DiagTemplate = ResolveItemTemplate(ItemData.ItemType);
		if (!DiagTemplate)
		{
			DiagResolveFail++;
			UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyGM] ◆ 사전 진단: [%d] '%s' 리졸브 실패! → 이 아이템은 복원되지 않습니다"),
				i, *ItemData.ItemType.ToString());
		}
	}

	if (DiagResolveFail > 0)
	{
		UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyGM] ◆ 사전 진단 결과: %d/%d개 아이템이 리졸브 실패! (파괴적 캐스케이드 위험)"),
			DiagResolveFail, StashItems.Num());
	}
#endif

	// ── 로드된 아이템 수 기록 (파괴적 캐스케이드 방지용) ──
	const int32 LoadedStashItemCount = StashItems.Num();

	// [Fix14] Stash 로딩 시 장착 상태 해제 — StashPanel에 EquippedGridSlots 없음
	// bEquipped=true인 아이템이 Grid에 배치되지 않는 문제 방지
	// DB에는 장착 정보가 보존되어 있으므로, 향후 자동 장착 기능에 활용 가능
	for (FInv_SavedItemData& ItemData : StashItems)
	{
		if (ItemData.bEquipped)
		{
			UE_LOG(LogHellunaLobby, Log, TEXT("[Fix14] Stash 아이템 장착 해제: %s (WeaponSlot=%d)"),
				*ItemData.ItemType.ToString(), ItemData.WeaponSlotIndex);
			ItemData.bEquipped = false;
			ItemData.WeaponSlotIndex = -1;
		}
	}

	// ── FInv_PlayerSaveData 구성 ──
	// RestoreFromSaveData()가 요구하는 포맷으로 래핑
	FInv_PlayerSaveData SaveData;
	SaveData.Items = MoveTemp(StashItems);
	SaveData.LastSaveTime = FDateTime::Now();

	// ── 템플릿 리졸버 생성 ──
	// GameplayTag → UInv_ItemComponent* 변환 (아이템 생성의 핵심)
	FInv_ItemTemplateResolver Resolver;
	Resolver.BindUObject(this, &AHellunaLobbyGameMode::ResolveItemTemplate);

	// ── StashComp에 복원 ──
	// 내부에서 각 아이템의 Manifest를 Resolver로 구성하고 FastArray에 추가
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] RestoreFromSaveData 호출 → StashComp에 %d개 아이템 복원 시작"), SaveData.Items.Num());
	StashComp->RestoreFromSaveData(SaveData, Resolver);

	// ── 복원 후 검증 ──
	const int32 RestoredCount = StashComp->CollectInventoryDataForSave().Num();
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] StashComp 복원 완료 | PlayerId=%s | DB 아이템=%d | 실제 복원=%d"),
		*PlayerId, LoadedStashItemCount, RestoredCount);

	if (RestoredCount < LoadedStashItemCount)
	{
		UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyGM] ◆◆ 아이템 유실 감지! DB=%d → 복원=%d → %d개 유실 | PlayerId=%s"),
			LoadedStashItemCount, RestoredCount, LoadedStashItemCount - RestoredCount, *PlayerId);
		UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyGM] ◆◆ 로그아웃 시 파괴적 저장을 방지합니다 (LoadedStashItemCount 기록)"));
	}

	// LobbyPC에 로드된 아이템 수를 저장 (SaveComponentsToDatabase에서 참조)
	LobbyPC->SetLoadedStashItemCount(LoadedStashItemCount);
}

// ════════════════════════════════════════════════════════════════════════════════
// [Fix23] LoadLoadoutToComponent — SQLite에서 Loadout 데이터를 로드하여 LoadoutComp에 복원
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 데이터 흐름:
//   SQLite player_loadout 테이블
//     → LoadPlayerLoadout(PlayerId)
//     → TArray<FInv_SavedItemData>
//     → FInv_PlayerSaveData로 래핑
//     → LoadoutComp->RestoreFromSaveData(SaveData, Resolver)
//
// 📌 호출 시점:
//   - PostLogin에서 LoadStashToComponent 이후
//   - 게임 생존 후 복귀 시: player_loadout에 게임 결과 아이템이 저장되어 있음
//   - 최초 로그인/사망 후: player_loadout이 비어있으므로 스킵
//
// 📌 로드 후:
//   - 복원 성공: player_loadout 삭제 (Logout 시 SaveComponentsToDatabase에서 중복 저장 방지)
//   - 복원 실패(아이템 유실): player_loadout 보존 → 다음 로그인 시 크래시 복구로 Stash에 복원
//
// ════════════════════════════════════════════════════════════════════════════════
void AHellunaLobbyGameMode::LoadLoadoutToComponent(AHellunaLobbyController* LobbyPC, const FString& PlayerId)
{
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] [Fix23] LoadLoadoutToComponent 시작 | PlayerId=%s"), *PlayerId);

	if (!LobbyPC || !SQLiteSubsystem || !SQLiteSubsystem->IsDatabaseReady())
	{
		UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyGM] [Fix23] LoadLoadout: 조건 미충족 | LobbyPC=%s, DB=%s"),
			LobbyPC ? TEXT("O") : TEXT("X"),
			(SQLiteSubsystem && SQLiteSubsystem->IsDatabaseReady()) ? TEXT("Ready") : TEXT("Not Ready"));
		return;
	}

	UInv_InventoryComponent* LoadoutComp = LobbyPC->GetLoadoutComponent();
	if (!LoadoutComp)
	{
		UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyGM] [Fix23] LoadLoadout: LoadoutComp가 nullptr! | PlayerId=%s"), *PlayerId);
		return;
	}

	// ── SQLite에서 Loadout 로드 ──
	TArray<FInv_SavedItemData> LoadoutItems = SQLiteSubsystem->LoadPlayerLoadout(PlayerId);
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] [Fix23] SQLite Loadout 로드 완료 | PlayerId=%s | 아이템 %d개"), *PlayerId, LoadoutItems.Num());

	if (LoadoutItems.Num() == 0)
	{
		// [Fix36] 빈 Loadout도 정상 — 미로드(-1) 아닌 "0개 로드됨"으로 설정
		LobbyPC->SetLoadedLoadoutItemCount(0);
		UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] [Fix23] Loadout이 비어있음 → 스킵 (최초 로그인/사망 후 복귀)"));
		return;
	}

	// ── 원본 아이템 수 기록 (파괴적 캐스케이드 방지용) ──
	const int32 LoadedLoadoutItemCount = LoadoutItems.Num();

	// ── 장착 상태 보존 (bEquipped + WeaponSlotIndex 유지) ──
	// player_equipment 테이블에서 교차 검증용 로드
	TArray<FHellunaEquipmentSlotData> EquipSlots = SQLiteSubsystem->LoadPlayerEquipment(PlayerId);
	if (EquipSlots.Num() > 0)
	{
		UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] [Fix23] player_equipment에서 %d개 장착 슬롯 로드 | PlayerId=%s"),
			EquipSlots.Num(), *PlayerId);
	}

	// 장착 아이템 로깅 (강제 해제 없음)
	for (const FInv_SavedItemData& ItemData : LoadoutItems)
	{
		if (ItemData.bEquipped)
		{
			UE_LOG(LogHellunaLobby, Log, TEXT("[Fix23] Loadout 장착 아이템 보존: %s (WeaponSlot=%d)"),
				*ItemData.ItemType.ToString(), ItemData.WeaponSlotIndex);
		}
	}

	// ── FInv_PlayerSaveData 구성 ──
	FInv_PlayerSaveData SaveData;
	SaveData.Items = MoveTemp(LoadoutItems);
	SaveData.LastSaveTime = FDateTime::Now();

	// ── 템플릿 리졸버 생성 ──
	FInv_ItemTemplateResolver Resolver;
	Resolver.BindUObject(this, &AHellunaLobbyGameMode::ResolveItemTemplate);

	// ── LoadoutComp에 복원 ──
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] [Fix23] RestoreFromSaveData → LoadoutComp에 %d개 아이템 복원 시작"), SaveData.Items.Num());
	LoadoutComp->RestoreFromSaveData(SaveData, Resolver);

	// ── 복원 후 검증 ──
	const int32 RestoredCount = LoadoutComp->CollectInventoryDataForSave().Num();
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] [Fix23] LoadoutComp 복원 완료 | PlayerId=%s | DB 아이템=%d | 실제 복원=%d"),
		*PlayerId, LoadedLoadoutItemCount, RestoredCount);

	// ── 파괴적 캐스케이드 방지 ──
	// 복원된 수가 원본보다 적으면 리졸브 실패로 아이템 유실
	// → player_loadout을 보존하여 다음 로그인 시 재시도
	// [Fix36] LoadedLoadoutItemCount = -1 유지 → SaveComponentsToDatabase에서 저장 차단
	if (RestoredCount < LoadedLoadoutItemCount)
	{
		UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyGM] [Fix23] ◆◆ Loadout 아이템 유실 감지! DB=%d → 복원=%d → %d개 유실"),
			LoadedLoadoutItemCount, RestoredCount, LoadedLoadoutItemCount - RestoredCount);
		UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyGM] [Fix36] ◆◆ player_loadout 보존 (LoadedLoadoutItemCount=-1 유지 → 저장 차단)"));

		// [Fix29-A] LoadoutComp를 비워서 부분 아이템이 저장되지 않도록 방지
		FInv_PlayerSaveData EmptySaveData;
		EmptySaveData.LastSaveTime = FDateTime::Now();
		LoadoutComp->RestoreFromSaveData(EmptySaveData, Resolver);
		UE_LOG(LogHellunaLobby, Warning, TEXT("[LobbyGM] [Fix29-A] LoadoutComp 비움 — 부분 복원 아이템 복제 방지"));
		// LoadedLoadoutItemCount 미설정 → -1 유지 → Logout 시 Loadout 저장 차단
		return;
	}

	// [Fix36] 복원 성공 → LoadedLoadoutItemCount 설정 (Logout 시 캐스케이드 검증용)
	LobbyPC->SetLoadedLoadoutItemCount(LoadedLoadoutItemCount);

	// [Fix35/Fix36] player_loadout을 DB에 유지 — 독립 영속성
	// 로비에 있는 동안 Stash+Loadout 모두 DB에 반영
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] [Fix36] player_loadout DB 유지 (독립 영속성) | PlayerId=%s | LoadedLoadoutItemCount=%d"), *PlayerId, LoadedLoadoutItemCount);
}

// ════════════════════════════════════════════════════════════════════════════════
// [Fix36] SaveComponentsToDatabase — StashComp + LoadoutComp 독립 저장
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 호출 시점: Logout (정상 종료 / 접속 해제)
//
// 📌 [Fix36] 저장 전략 (독립 영속성):
//   1) StashComp → SQLite player_stash (독립 저장)
//   2) LoadoutComp → SQLite player_loadout (독립 저장, 비어있으면 삭제)
//   → Loadout→Stash 병합 없음! 각각 독립적으로 DB에 유지
//
// 📌 주의:
//   - Deploy(출격)으로 나간 경우 LoadoutComp은 이미 Server_Deploy에서 저장됨
//   - LoadedStashItemCount 또는 LoadedLoadoutItemCount가 -1이면 미로드 → 저장 차단
//
// ════════════════════════════════════════════════════════════════════════════════
void AHellunaLobbyGameMode::SaveComponentsToDatabase(AHellunaLobbyController* LobbyPC, const FString& PlayerId)
{
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] [Fix36] SaveComponentsToDatabase 시작 | PlayerId=%s"), *PlayerId);

	// [Fix41] ReleaseDatabaseConnection 후 DB가 닫혀있을 수 있으므로 재오픈 시도
	if (SQLiteSubsystem && !SQLiteSubsystem->IsDatabaseReady())
	{
		SQLiteSubsystem->TryReopenDatabase();
	}
	if (!LobbyPC || !SQLiteSubsystem || !SQLiteSubsystem->IsDatabaseReady())
	{
		UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyGM] SaveComponents: 조건 미충족! | PC=%s, DB=%s"),
			*GetNameSafe(LobbyPC),
			(SQLiteSubsystem && SQLiteSubsystem->IsDatabaseReady()) ? TEXT("Ready") : TEXT("Not Ready"));
		return;
	}

	// ── [Fix29-D + Fix36] 미로드 상태 체크 ──
	// LoadedStashItemCount < 0 OR LoadedLoadoutItemCount < 0 = PostLogin 미완료
	// → 빈 데이터로 DB를 덮어쓰면 기존 데이터 소실 → 저장 차단
	const int32 OriginalStashCount = LobbyPC->GetLoadedStashItemCount();
	const int32 OriginalLoadoutCount = LobbyPC->GetLoadedLoadoutItemCount();

	if (OriginalStashCount < 0 || OriginalLoadoutCount < 0)
	{
		UE_LOG(LogHellunaLobby, Warning, TEXT("[LobbyGM] [Fix36] 미로드 상태에서 Logout → 저장 차단 | PlayerId=%s | StashLoaded=%d, LoadoutLoaded=%d"),
			*PlayerId, OriginalStashCount, OriginalLoadoutCount);
		return;
	}

	// ════════════════════════════════════════════════════════════════
	// 1) Stash 독립 저장
	// ════════════════════════════════════════════════════════════════
	UInv_InventoryComponent* StashComp = LobbyPC->GetStashComponent();
	if (StashComp)
	{
		TArray<FInv_SavedItemData> StashItems = StashComp->CollectInventoryDataForSave();
		UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] [Fix36] Stash 아이템 수집 | %d개"), StashItems.Num());

		// 파괴적 캐스케이드 방지 (Stash)
		if (OriginalStashCount > 0 && StashItems.Num() < OriginalStashCount)
		{
			UE_LOG(LogHellunaLobby, Error, TEXT(""));
			UE_LOG(LogHellunaLobby, Error, TEXT("╔══════════════════════════════════════════════════════════════╗"));
			UE_LOG(LogHellunaLobby, Error, TEXT("║  ◆◆ [Fix36] Stash 파괴적 캐스케이드 방지: 저장 거부!       ║"));
			UE_LOG(LogHellunaLobby, Error, TEXT("║  DB 원본=%d개 → 현재=%d개 → %d개 유실 감지              ║"),
				OriginalStashCount, StashItems.Num(), OriginalStashCount - StashItems.Num());
			UE_LOG(LogHellunaLobby, Error, TEXT("╚══════════════════════════════════════════════════════════════╝"));
			UE_LOG(LogHellunaLobby, Error, TEXT(""));
		}
		else
		{
			const bool bStashOk = SQLiteSubsystem->SavePlayerStash(PlayerId, StashItems);
			UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] [Fix36] Stash SQLite 저장 %s | %d개 | PlayerId=%s"),
				bStashOk ? TEXT("성공") : TEXT("실패"), StashItems.Num(), *PlayerId);
		}
	}
	else
	{
		UE_LOG(LogHellunaLobby, Warning, TEXT("[LobbyGM] SaveComponents: StashComp가 nullptr! | PlayerId=%s"), *PlayerId);
	}

	// ════════════════════════════════════════════════════════════════
	// 2) Loadout 독립 저장
	// ════════════════════════════════════════════════════════════════
	UInv_InventoryComponent* LoadoutComp = LobbyPC->GetLoadoutComponent();
	if (LoadoutComp)
	{
		TArray<FInv_SavedItemData> LoadoutItems = LoadoutComp->CollectInventoryDataForSave();
		UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] [Fix36] Loadout 아이템 수집 | %d개"), LoadoutItems.Num());

		if (LoadoutItems.Num() > 0)
		{
			// 파괴적 캐스케이드 방지 (Loadout)
			if (OriginalLoadoutCount > 0 && LoadoutItems.Num() < OriginalLoadoutCount)
			{
				UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyGM] [Fix36] ◆◆ Loadout 캐스케이드 방지: 저장 거부! DB 원본=%d → 현재=%d"),
					OriginalLoadoutCount, LoadoutItems.Num());
			}
			else
			{
				const bool bLoadoutOk = SQLiteSubsystem->SavePlayerLoadout(PlayerId, LoadoutItems);
				UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] [Fix36] Loadout SQLite 저장 %s | %d개 | PlayerId=%s"),
					bLoadoutOk ? TEXT("성공") : TEXT("실패"), LoadoutItems.Num(), *PlayerId);

				// 3) 장착 상태 저장 (Loadout 아이템 중 bEquipped 추출)
				TArray<FHellunaEquipmentSlotData> EquipSlots;
				for (const FInv_SavedItemData& Item : LoadoutItems)
				{
					if (Item.bEquipped && Item.WeaponSlotIndex >= 0)
					{
						FHellunaEquipmentSlotData Slot;
						Slot.SlotId = FString::Printf(TEXT("weapon_%d"), Item.WeaponSlotIndex);
						Slot.ItemType = Item.ItemType;
						EquipSlots.Add(Slot);
					}
				}
				// [Fix44-C4] Equipment 저장 반환값 검증
				const bool bEquipOk = SQLiteSubsystem->SavePlayerEquipment(PlayerId, EquipSlots);
				if (!bEquipOk)
				{
					UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyGM] [Fix44] SavePlayerEquipment 실패! Loadout/Equipment 불일치 가능 | PlayerId=%s"), *PlayerId);
				}
			}
		}
		else
		{
			// [Fix36] 빈 Loadout → player_loadout 삭제 (빈 행 정리, 크래시 감지와 무관)
			// [Fix44-C3] Delete 반환값 검증
			const bool bDelLoadout = SQLiteSubsystem->DeletePlayerLoadout(PlayerId);
			const bool bDelEquip = SQLiteSubsystem->DeletePlayerEquipment(PlayerId);
			if (!bDelLoadout || !bDelEquip)
			{
				UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyGM] [Fix44] Delete 실패: Loadout=%s Equipment=%s | PlayerId=%s"),
					bDelLoadout ? TEXT("OK") : TEXT("FAIL"), bDelEquip ? TEXT("OK") : TEXT("FAIL"), *PlayerId);
			}
			UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] [Fix36] Loadout 비어있음 → DeletePlayerLoadout (빈 행 정리) | PlayerId=%s"), *PlayerId);
		}
	}

	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] [Fix36] SaveComponentsToDatabase 완료 | PlayerId=%s"), *PlayerId);
}

// ════════════════════════════════════════════════════════════════════════════════
// ResolveItemTemplate — GameplayTag → UInv_ItemComponent* (아이템 템플릿 리졸버)
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 역할:
//   RestoreFromSaveData()에서 각 아이템을 복원할 때 호출되는 콜백
//   GameplayTag (예: "Item.Weapon.AR") → 대응하는 Actor 클래스 → CDO에서 ItemComponent 추출
//
// 📌 호출 체인:
//   RestoreFromSaveData() → Resolver.Execute(ItemType)
//     → ResolveItemTemplate(ItemType)
//       → ResolveItemClass(ItemType)     [HellunaBaseGameMode에서 상속]
//       → FindItemComponentTemplate(ActorClass)  [CDO에서 ItemComp 추출]
//
// 📌 실패 시:
//   nullptr 반환 → 해당 아이템은 복원되지 않음 (로그에서 확인 가능)
//   주로 ItemType에 대응하는 Actor 클래스가 등록되지 않았을 때 발생
//
// ════════════════════════════════════════════════════════════════════════════════
UInv_ItemComponent* AHellunaLobbyGameMode::ResolveItemTemplate(const FGameplayTag& ItemType)
{
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] ResolveItemTemplate: %s"), *ItemType.ToString());

	// 1단계: DataTable에서 ItemType → ActorClass 매핑
	TSubclassOf<AActor> ActorClass = ResolveItemClass(ItemType);
	if (!ActorClass)
	{
		UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyGM] ✗ ResolveItemTemplate 실패(1단계): ResolveItemClass → nullptr | ItemType=%s"),
			*ItemType.ToString());
		UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyGM]   → BP_HellunaLobbyGameMode의 ItemTypeMappingDataTable 설정 확인"));
		UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyGM]   → DT_ItemTypeMapping에 '%s' 행이 있는지 확인"), *ItemType.ToString());
		return nullptr;
	}

	// 2단계: ActorClass CDO에서 UInv_ItemComponent 추출
	UInv_ItemComponent* Template = FindItemComponentTemplate(ActorClass);
	if (!Template)
	{
		UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyGM] ✗ ResolveItemTemplate 실패(2단계): FindItemComponentTemplate → nullptr | ItemType=%s, ActorClass=%s"),
			*ItemType.ToString(), *ActorClass->GetName());
		UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyGM]   → %s 액터에 UInv_ItemComponent가 있는지 확인"), *ActorClass->GetName());
		return nullptr;
	}

	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] ✓ ResolveItemTemplate 성공 | %s → %s"), *ItemType.ToString(), *ActorClass->GetName());
	return Template;
}


// ════════════════════════════════════════════════════════════════════════════════
// 캐릭터 중복 방지 — 같은 로비 + SQLite 교차 체크
// ════════════════════════════════════════════════════════════════════════════════

bool AHellunaLobbyGameMode::IsLobbyCharacterAvailable(EHellunaHeroType HeroType) const
{
	// 1) 메모리 맵 체크 (같은 로비 내)
	if (LobbyUsedCharacterMap.Contains(HeroType))
	{
		UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] IsLobbyCharacterAvailable: %s → 이미 로비 내 사용중 (PlayerId=%s)"),
			*UEnum::GetValueAsString(HeroType), *LobbyUsedCharacterMap[HeroType]);
		return false;
	}

	// 2) SQLite 체크 (다른 서버 간)
	if (SQLiteSubsystem && SQLiteSubsystem->IsDatabaseReady())
	{
		TArray<bool> DbUsed = SQLiteSubsystem->GetActiveGameCharacters();
		const int32 Index = HeroTypeToIndex(HeroType);
		if (DbUsed.IsValidIndex(Index) && DbUsed[Index])
		{
			UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] IsLobbyCharacterAvailable: %s → SQLite에서 사용중"),
				*UEnum::GetValueAsString(HeroType));
			return false;
		}
	}

	return true;
}

TArray<bool> AHellunaLobbyGameMode::GetLobbyAvailableCharacters() const
{
	TArray<bool> Result;
	Result.SetNum(3);
	Result[0] = false;
	Result[1] = false;
	Result[2] = false;

	// 메모리 맵에서 사용 중인 캐릭터 마킹
	for (const auto& Pair : LobbyUsedCharacterMap)
	{
		const int32 Index = HeroTypeToIndex(Pair.Key);
		if (Index >= 0 && Index < 3)
		{
			Result[Index] = true;
		}
	}

	// SQLite에서 추가 체크 (다른 서버에서 사용 중인 캐릭터)
	if (SQLiteSubsystem && SQLiteSubsystem->IsDatabaseReady())
	{
		TArray<bool> DbUsed = SQLiteSubsystem->GetActiveGameCharacters();
		for (int32 i = 0; i < FMath::Min(3, DbUsed.Num()); ++i)
		{
			if (DbUsed[i])
			{
				Result[i] = true;
			}
		}
	}

	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] GetLobbyAvailableCharacters: Lui=%s Luna=%s Liam=%s"),
		Result[0] ? TEXT("사용중") : TEXT("가능"),
		Result[1] ? TEXT("사용중") : TEXT("가능"),
		Result[2] ? TEXT("사용중") : TEXT("가능"));

	return Result;
}

void AHellunaLobbyGameMode::RegisterLobbyCharacterUse(EHellunaHeroType HeroType, const FString& PlayerId)
{
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] RegisterLobbyCharacterUse | Hero=%s | PlayerId=%s"),
		*UEnum::GetValueAsString(HeroType), *PlayerId);

	// 기존 선택 해제 (같은 플레이어가 다른 캐릭터 재선택)
	UnregisterLobbyCharacterUse(PlayerId);

	// 메모리 맵 등록
	LobbyUsedCharacterMap.Add(HeroType, PlayerId);

	// SQLite 등록
	if (SQLiteSubsystem && SQLiteSubsystem->IsDatabaseReady())
	{
		SQLiteSubsystem->RegisterActiveGameCharacter(HeroTypeToIndex(HeroType), PlayerId, LobbyServerId);
	}
}

void AHellunaLobbyGameMode::UnregisterLobbyCharacterUse(const FString& PlayerId)
{
	// 메모리 맵에서 해당 플레이어 제거
	EHellunaHeroType FoundType = EHellunaHeroType::None;
	for (const auto& Pair : LobbyUsedCharacterMap)
	{
		if (Pair.Value == PlayerId)
		{
			FoundType = Pair.Key;
			break;
		}
	}

	if (FoundType != EHellunaHeroType::None)
	{
		LobbyUsedCharacterMap.Remove(FoundType);
		UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] UnregisterLobbyCharacterUse | Hero=%s | PlayerId=%s"),
			*UEnum::GetValueAsString(FoundType), *PlayerId);
	}

	// SQLite 해제
	if (SQLiteSubsystem && SQLiteSubsystem->IsDatabaseReady())
	{
		SQLiteSubsystem->UnregisterActiveGameCharacter(PlayerId);
	}
}


// ════════════════════════════════════════════════════════════════════════════════
// [Phase 12b] 채널 레지스트리 스캔
// ════════════════════════════════════════════════════════════════════════════════

FString AHellunaLobbyGameMode::GetRegistryDirectoryPath() const
{
	return FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("ServerRegistry")));
}

TArray<FGameChannelInfo> AHellunaLobbyGameMode::ScanAvailableChannels()
{
	TArray<FGameChannelInfo> Channels;
	const FString RegistryDir = GetRegistryDirectoryPath();

	// JSON 파일 목록 스캔
	TArray<FString> FoundFiles;
	IFileManager::Get().FindFiles(FoundFiles, *FPaths::Combine(RegistryDir, TEXT("channel_*.json")), true, false);

	const FDateTime Now = FDateTime::UtcNow();

	for (const FString& FileName : FoundFiles)
	{
		const FString FilePath = FPaths::Combine(RegistryDir, FileName);
		FString JsonString;
		if (!FFileHelper::LoadFileToString(JsonString, *FilePath))
		{
			continue;
		}

		TSharedPtr<FJsonObject> JsonObj;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
		if (!FJsonSerializer::Deserialize(Reader, JsonObj) || !JsonObj.IsValid())
		{
			continue;
		}

		FGameChannelInfo Info;
		Info.ChannelId = JsonObj->GetStringField(TEXT("channelId"));
		Info.Port = static_cast<int32>(JsonObj->GetNumberField(TEXT("port")));
		Info.CurrentPlayers = static_cast<int32>(JsonObj->GetNumberField(TEXT("currentPlayers")));
		Info.MaxPlayers = static_cast<int32>(JsonObj->GetNumberField(TEXT("maxPlayers")));
		Info.MapName = JsonObj->GetStringField(TEXT("mapName"));

		// Status 문자열 → enum
		const FString StatusStr = JsonObj->GetStringField(TEXT("status"));
		if (StatusStr == TEXT("playing"))
		{
			Info.Status = EChannelStatus::Playing;
		}
		else if (StatusStr == TEXT("empty"))
		{
			Info.Status = EChannelStatus::Empty;
		}
		else
		{
			Info.Status = EChannelStatus::Offline;
		}

		// [Phase 12h] lastUpdate 60초 초과 → Offline (비정상 종료 대비)
		FString LastUpdateStr = JsonObj->GetStringField(TEXT("lastUpdate"));
		FDateTime LastUpdate;
		if (FDateTime::ParseIso8601(*LastUpdateStr, LastUpdate))
		{
			const FTimespan Age = Now - LastUpdate;
			if (Age.GetTotalSeconds() > 60.0)
			{
				Info.Status = EChannelStatus::Offline;
				UE_LOG(LogHellunaLobby, Warning, TEXT("[LobbyGM] 채널 %s 타임아웃 (%.0f초) → Offline"),
					*Info.ChannelId, Age.GetTotalSeconds());
			}
		}

		Channels.Add(MoveTemp(Info));
	}

	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] ScanAvailableChannels: %d개 채널 발견"), Channels.Num());
	return Channels;
}

bool AHellunaLobbyGameMode::FindEmptyChannel(FGameChannelInfo& OutChannel)
{
	const TArray<FGameChannelInfo> Channels = ScanAvailableChannels();

	for (const FGameChannelInfo& Ch : Channels)
	{
		if (Ch.Status == EChannelStatus::Empty && !PendingDeployChannels.Contains(Ch.Port))
		{
			OutChannel = Ch;
			UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] FindEmptyChannel: %s (Port=%d)"), *Ch.ChannelId, Ch.Port);
			return true;
		}
	}

	UE_LOG(LogHellunaLobby, Warning, TEXT("[LobbyGM] FindEmptyChannel: 빈 채널 없음"));
	return false;
}

void AHellunaLobbyGameMode::MarkChannelAsPendingDeploy(int32 Port)
{
	PendingDeployChannels.Add(Port);

	// 30초 후 자동 해제 (Travel 실패 대비)
	FTimerHandle& TimerHandle = PendingDeployTimers.FindOrAdd(Port);
	UWorld* World = GetWorld();
	if (World)
	{
		TWeakObjectPtr<AHellunaLobbyGameMode> WeakThis(this);
		World->GetTimerManager().SetTimer(TimerHandle, [WeakThis, Port]()
		{
			if (!WeakThis.IsValid()) return;
			WeakThis->PendingDeployChannels.Remove(Port);
			WeakThis->PendingDeployTimers.Remove(Port);
			UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] PendingDeploy 타이머 해제 | Port=%d"), Port);
		}, 30.f, false);
	}

	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] MarkChannelAsPendingDeploy | Port=%d"), Port);
}


// ════════════════════════════════════════════════════════════════════════════════
// [Phase 12d] 파티 시스템 — 서버 로직
// ════════════════════════════════════════════════════════════════════════════════

FString AHellunaLobbyGameMode::GeneratePartyCode()
{
	// 문자 풀: I/O/0/1 제외 (가독성)
	static const TCHAR* CharPool = TEXT("ABCDEFGHJKLMNPQRSTUVWXYZ23456789");
	static const int32 PoolSize = 30; // strlen(CharPool)
	static const int32 CodeLength = 6;

	if (!SQLiteSubsystem || !SQLiteSubsystem->IsDatabaseReady())
	{
		UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyGM] GeneratePartyCode: DB가 준비되지 않음"));
		return FString();
	}

	for (int32 Attempt = 0; Attempt < 10; ++Attempt)
	{
		FString Code;
		Code.Reserve(CodeLength);
		for (int32 i = 0; i < CodeLength; ++i)
		{
			Code.AppendChar(CharPool[FMath::RandRange(0, PoolSize - 1)]);
		}

		if (SQLiteSubsystem->IsPartyCodeUnique(Code))
		{
			UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] GeneratePartyCode: %s (시도 %d회)"), *Code, Attempt + 1);
			return Code;
		}
	}

	UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyGM] GeneratePartyCode: 10회 시도 후 유니크 코드 생성 실패"));
	return FString();
}

void AHellunaLobbyGameMode::RefreshPartyCache(int32 PartyId)
{
	if (!SQLiteSubsystem || !SQLiteSubsystem->IsDatabaseReady() || PartyId <= 0)
	{
		return;
	}

	FHellunaPartyInfo Info = SQLiteSubsystem->LoadPartyInfo(PartyId);
	if (Info.IsValid())
	{
		ActivePartyCache.Add(PartyId, Info);

		// PlayerToPartyMap 갱신
		for (const FHellunaPartyMemberInfo& Member : Info.Members)
		{
			PlayerToPartyMap.Add(Member.PlayerId, PartyId);
		}
	}
	else
	{
		// 파티가 삭제됨
		ActivePartyCache.Remove(PartyId);
	}
}

void AHellunaLobbyGameMode::CreatePartyForPlayer(const FString& PlayerId, const FString& DisplayName)
{
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] ▶ CreatePartyForPlayer | PlayerId=%s"), *PlayerId);

	if (!SQLiteSubsystem || !SQLiteSubsystem->IsDatabaseReady())
	{
		auto* PC = PlayerIdToControllerMap.Find(PlayerId);
		if (PC && PC->IsValid())
		{
			(*PC)->Client_PartyError(TEXT("DB가 준비되지 않았습니다"));
		}
		return;
	}

	// 이미 파티에 속해있는지 확인
	if (PlayerToPartyMap.Contains(PlayerId))
	{
		auto* PC = PlayerIdToControllerMap.Find(PlayerId);
		if (PC && PC->IsValid())
		{
			(*PC)->Client_PartyError(TEXT("이미 파티에 참가 중입니다"));
		}
		return;
	}

	const FString PartyCode = GeneratePartyCode();
	if (PartyCode.IsEmpty())
	{
		auto* PC = PlayerIdToControllerMap.Find(PlayerId);
		if (PC && PC->IsValid())
		{
			(*PC)->Client_PartyError(TEXT("파티 코드 생성 실패"));
		}
		return;
	}

	const int32 PartyId = SQLiteSubsystem->CreateParty(PlayerId, DisplayName, PartyCode);
	if (PartyId <= 0)
	{
		auto* PC = PlayerIdToControllerMap.Find(PlayerId);
		if (PC && PC->IsValid())
		{
			(*PC)->Client_PartyError(TEXT("파티 생성 실패"));
		}
		return;
	}

	// [Fix43] 파티 생성 시 리더의 기존 캐릭터 선택 보존
	{
		auto* LeaderPCPtr = PlayerIdToControllerMap.Find(PlayerId);
		if (LeaderPCPtr && LeaderPCPtr->IsValid())
		{
			AHellunaLobbyController* LeaderPC = LeaderPCPtr->Get();
			const EHellunaHeroType LeaderHero = LeaderPC->GetSelectedHeroType();
			if (LeaderHero != EHellunaHeroType::None)
			{
				const int32 HeroIndex = HeroTypeToIndex(LeaderHero);
				SQLiteSubsystem->UpdateMemberHeroType(PlayerId, HeroIndex);
				UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] [Fix43] 파티 생성 시 리더 캐릭터 보존 | PlayerId=%s | HeroType=%d"), *PlayerId, HeroIndex);
			}
		}
	}

	RefreshPartyCache(PartyId);
	BroadcastPartyState(PartyId);

	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] ✓ 파티 생성 완료 | PartyId=%d | Code=%s | Leader=%s"), PartyId, *PartyCode, *PlayerId);
}

void AHellunaLobbyGameMode::JoinPartyForPlayer(const FString& PlayerId, const FString& DisplayName, const FString& PartyCode)
{
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] ▶ JoinPartyForPlayer | PlayerId=%s | Code=%s"), *PlayerId, *PartyCode);

	if (!SQLiteSubsystem || !SQLiteSubsystem->IsDatabaseReady())
	{
		auto* PC = PlayerIdToControllerMap.Find(PlayerId);
		if (PC && PC->IsValid()) { (*PC)->Client_PartyError(TEXT("DB가 준비되지 않았습니다")); }
		return;
	}

	// 이미 파티에 속해있는지
	if (PlayerToPartyMap.Contains(PlayerId))
	{
		auto* PC = PlayerIdToControllerMap.Find(PlayerId);
		if (PC && PC->IsValid()) { (*PC)->Client_PartyError(TEXT("이미 파티에 참가 중입니다")); }
		return;
	}

	// 파티 코드로 PartyId 조회
	const int32 PartyId = SQLiteSubsystem->FindPartyByCode(PartyCode);
	if (PartyId <= 0)
	{
		auto* PC = PlayerIdToControllerMap.Find(PlayerId);
		if (PC && PC->IsValid()) { (*PC)->Client_PartyError(TEXT("존재하지 않는 파티 코드입니다")); }
		return;
	}

	// 인원 체크 (최대 3명)
	const int32 MemberCount = SQLiteSubsystem->GetPartyMemberCount(PartyId);
	if (MemberCount >= 3)
	{
		auto* PC = PlayerIdToControllerMap.Find(PlayerId);
		if (PC && PC->IsValid()) { (*PC)->Client_PartyError(TEXT("파티가 가득 찼습니다 (최대 3명)")); }
		return;
	}

	if (!SQLiteSubsystem->JoinParty(PartyId, PlayerId, DisplayName))
	{
		auto* PC = PlayerIdToControllerMap.Find(PlayerId);
		if (PC && PC->IsValid()) { (*PC)->Client_PartyError(TEXT("파티 참가 실패")); }
		return;
	}

	// [Fix43] 기존 캐릭터 선택 보존 — 충돌 시에만 리셋
	{
		auto* JoiningPCPtr = PlayerIdToControllerMap.Find(PlayerId);
		if (JoiningPCPtr && JoiningPCPtr->IsValid())
		{
			AHellunaLobbyController* JoiningPC = JoiningPCPtr->Get();
			const EHellunaHeroType JoiningHero = JoiningPC->GetSelectedHeroType();

			if (JoiningHero != EHellunaHeroType::None)
			{
				// 기존 파티원들의 hero_type과 충돌 체크
				FHellunaPartyInfo TempInfo = SQLiteSubsystem->LoadPartyInfo(PartyId);
				bool bConflict = false;
				for (const FHellunaPartyMemberInfo& Member : TempInfo.Members)
				{
					if (Member.PlayerId == PlayerId) continue; // 자기 자신 제외
					if (Member.SelectedHeroType == static_cast<int32>(JoiningHero))
					{
						bConflict = true;
						break;
					}
				}

				if (!bConflict)
				{
					// 충돌 없음 → DB에 기존 캐릭터 반영
					const int32 HeroIndex = HeroTypeToIndex(JoiningHero);
					SQLiteSubsystem->UpdateMemberHeroType(PlayerId, HeroIndex);
					UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] [Fix43] 파티 참가 시 캐릭터 보존 | PlayerId=%s | HeroType=%d"), *PlayerId, HeroIndex);
				}
				else
				{
					// 충돌 → 캐릭터 리셋 (재선택 필요)
					JoiningPC->ResetSelectedHeroType();
					UnregisterLobbyCharacterUse(PlayerId);
					UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] [Fix43] 파티 캐릭터 충돌 → 리셋 | PlayerId=%s"), *PlayerId);
				}
			}
		}
	}

	// 전원 Ready 리셋
	SQLiteSubsystem->ResetAllReadyStates(PartyId);

	RefreshPartyCache(PartyId);
	BroadcastPartyState(PartyId);

	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] ✓ 파티 참가 완료 | PartyId=%d | PlayerId=%s"), PartyId, *PlayerId);
}

void AHellunaLobbyGameMode::LeavePartyForPlayer(const FString& PlayerId)
{
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] ▶ LeavePartyForPlayer | PlayerId=%s"), *PlayerId);

	if (!SQLiteSubsystem || !SQLiteSubsystem->IsDatabaseReady())
	{
		return;
	}

	const int32* PartyIdPtr = PlayerToPartyMap.Find(PlayerId);
	if (!PartyIdPtr || *PartyIdPtr <= 0)
	{
		return;
	}
	const int32 PartyId = *PartyIdPtr;

	// 리더인지 확인
	const FHellunaPartyInfo* CachedInfo = ActivePartyCache.Find(PartyId);
	const bool bIsLeader = CachedInfo && CachedInfo->LeaderId == PlayerId;

	// DB에서 탈퇴
	SQLiteSubsystem->LeaveParty(PlayerId);
	PlayerToPartyMap.Remove(PlayerId);

	// 남은 멤버 확인
	const int32 RemainingCount = SQLiteSubsystem->GetPartyMemberCount(PartyId);

	if (RemainingCount <= 0)
	{
		// 파티 해산
		ActivePartyCache.Remove(PartyId);
		PartyChatHistory.Remove(PartyId);

		// 탈퇴한 플레이어에게 해산 알림
		auto* PC = PlayerIdToControllerMap.Find(PlayerId);
		if (PC && PC->IsValid())
		{
			(*PC)->Client_PartyDisbanded(TEXT("파티가 해산되었습니다"));
		}
	}
	else
	{
		// 리더였으면 리더십 이전 (joined_at 순서로 DB에서 첫 번째)
		if (bIsLeader)
		{
			FHellunaPartyInfo Info = SQLiteSubsystem->LoadPartyInfo(PartyId);
			if (Info.Members.Num() > 0)
			{
				const FString NewLeaderId = Info.Members[0].PlayerId;
				SQLiteSubsystem->TransferLeadership(PartyId, NewLeaderId);
				UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] 리더십 이전: %s → %s"), *PlayerId, *NewLeaderId);
			}
		}

		// 전원 Ready 리셋
		SQLiteSubsystem->ResetAllReadyStates(PartyId);

		RefreshPartyCache(PartyId);
		BroadcastPartyState(PartyId);

		// 탈퇴한 플레이어에게 해산 알림
		auto* PC = PlayerIdToControllerMap.Find(PlayerId);
		if (PC && PC->IsValid())
		{
			(*PC)->Client_PartyDisbanded(TEXT("파티에서 탈퇴했습니다"));
		}
	}

	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] ✓ 파티 탈퇴 완료 | PlayerId=%s | PartyId=%d | Remaining=%d"), *PlayerId, PartyId, RemainingCount);
}

void AHellunaLobbyGameMode::KickPartyMember(const FString& RequesterId, const FString& TargetId)
{
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] ▶ KickPartyMember | Requester=%s | Target=%s"), *RequesterId, *TargetId);

	const int32* PartyIdPtr = PlayerToPartyMap.Find(RequesterId);
	if (!PartyIdPtr || *PartyIdPtr <= 0)
	{
		return;
	}
	const int32 PartyId = *PartyIdPtr;

	// 리더 확인
	const FHellunaPartyInfo* CachedInfo = ActivePartyCache.Find(PartyId);
	if (!CachedInfo || CachedInfo->LeaderId != RequesterId)
	{
		auto* PC = PlayerIdToControllerMap.Find(RequesterId);
		if (PC && PC->IsValid()) { (*PC)->Client_PartyError(TEXT("리더만 추방할 수 있습니다")); }
		return;
	}

	// 대상이 같은 파티인지
	const int32* TargetPartyPtr = PlayerToPartyMap.Find(TargetId);
	if (!TargetPartyPtr || *TargetPartyPtr != PartyId)
	{
		auto* PC = PlayerIdToControllerMap.Find(RequesterId);
		if (PC && PC->IsValid()) { (*PC)->Client_PartyError(TEXT("대상이 파티에 없습니다")); }
		return;
	}

	// 대상 DB 탈퇴
	if (SQLiteSubsystem)
	{
		SQLiteSubsystem->LeaveParty(TargetId);
	}
	PlayerToPartyMap.Remove(TargetId);

	// 추방된 플레이어에게 알림
	auto* TargetPC = PlayerIdToControllerMap.Find(TargetId);
	if (TargetPC && TargetPC->IsValid())
	{
		(*TargetPC)->Client_PartyDisbanded(TEXT("파티에서 추방되었습니다"));
	}

	// 전원 Ready 리셋 + 갱신
	if (SQLiteSubsystem)
	{
		SQLiteSubsystem->ResetAllReadyStates(PartyId);
	}
	RefreshPartyCache(PartyId);
	BroadcastPartyState(PartyId);
}

void AHellunaLobbyGameMode::SetPlayerReady(const FString& PlayerId, bool bReady)
{
	UE_LOG(LogHellunaLobby, Verbose, TEXT("[LobbyGM] SetPlayerReady | PlayerId=%s | Ready=%s"), *PlayerId, bReady ? TEXT("true") : TEXT("false"));

	if (!SQLiteSubsystem || !SQLiteSubsystem->IsDatabaseReady())
	{
		return;
	}

	SQLiteSubsystem->UpdateMemberReady(PlayerId, bReady);

	const int32* PartyIdPtr = PlayerToPartyMap.Find(PlayerId);
	if (PartyIdPtr && *PartyIdPtr > 0)
	{
		RefreshPartyCache(*PartyIdPtr);
		BroadcastPartyState(*PartyIdPtr);

		// Auto-Deploy 체크
		if (bReady)
		{
			TryAutoDeployParty(*PartyIdPtr);
		}
	}
}

void AHellunaLobbyGameMode::OnPlayerHeroChanged(const FString& PlayerId, int32 HeroType)
{
	UE_LOG(LogHellunaLobby, Verbose, TEXT("[LobbyGM] OnPlayerHeroChanged | PlayerId=%s | HeroType=%d"), *PlayerId, HeroType);

	if (!SQLiteSubsystem || !SQLiteSubsystem->IsDatabaseReady())
	{
		return;
	}

	SQLiteSubsystem->UpdateMemberHeroType(PlayerId, HeroType);

	const int32* PartyIdPtr = PlayerToPartyMap.Find(PlayerId);
	if (PartyIdPtr && *PartyIdPtr > 0)
	{
		RefreshPartyCache(*PartyIdPtr);
		BroadcastPartyState(*PartyIdPtr);
	}
}

bool AHellunaLobbyGameMode::ValidatePartyHeroDuplication(int32 PartyId)
{
	const FHellunaPartyInfo* Info = ActivePartyCache.Find(PartyId);
	if (!Info)
	{
		return false;
	}

	TSet<int32> UsedHeroes;
	for (const FHellunaPartyMemberInfo& Member : Info->Members)
	{
		// None(3)은 중복 체크 대상 아님
		if (Member.SelectedHeroType == 3)
		{
			continue;
		}
		if (UsedHeroes.Contains(Member.SelectedHeroType))
		{
			return true; // 중복 발견
		}
		UsedHeroes.Add(Member.SelectedHeroType);
	}
	return false; // 중복 없음
}

void AHellunaLobbyGameMode::TryAutoDeployParty(int32 PartyId)
{
	// [Phase 12 Fix] 값 복사 — RefreshPartyCache가 ActivePartyCache를 변경할 수 있으므로 포인터 사용 금지
	const FHellunaPartyInfo* InfoPtr = ActivePartyCache.Find(PartyId);
	if (!InfoPtr || InfoPtr->Members.Num() == 0)
	{
		return;
	}
	FHellunaPartyInfo Info = *InfoPtr; // 값 복사
	InfoPtr = nullptr; // 실수 방지

	// 전원 Ready 확인
	for (const FHellunaPartyMemberInfo& Member : Info.Members)
	{
		if (!Member.bIsReady)
		{
			return; // 아직 안 됨
		}
	}

	// 영웅 중복 체크
	if (ValidatePartyHeroDuplication(PartyId))
	{
		// 중복 있음 — 에러 전송 + Ready 리셋
		if (SQLiteSubsystem)
		{
			SQLiteSubsystem->ResetAllReadyStates(PartyId);
		}

		RefreshPartyCache(PartyId);

		for (const FHellunaPartyMemberInfo& Member : Info.Members)
		{
			auto* PC = PlayerIdToControllerMap.Find(Member.PlayerId);
			if (PC && PC->IsValid())
			{
				(*PC)->Client_PartyError(TEXT("영웅이 중복되었습니다! 캐릭터를 변경해주세요"));
			}
		}

		BroadcastPartyState(PartyId);
		return;
	}

	// 캐릭터 미선택 체크 (None = static_cast<int32>(EHellunaHeroType::None))
	for (const FHellunaPartyMemberInfo& Member : Info.Members)
	{
		if (Member.SelectedHeroType == 3) // None
		{
			auto* PC = PlayerIdToControllerMap.Find(Member.PlayerId);
			if (PC && PC->IsValid())
			{
				(*PC)->Client_PartyError(TEXT("모든 멤버가 캐릭터를 선택해야 합니다"));
			}

			if (SQLiteSubsystem)
			{
				SQLiteSubsystem->ResetAllReadyStates(PartyId);
			}
			RefreshPartyCache(PartyId);
			BroadcastPartyState(PartyId);
			return;
		}
	}

	// 전원 Ready + 중복 없음 + 캐릭터 선택 완료 → Deploy!
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] TryAutoDeployParty: 전원 Ready — Deploy 시작 | PartyId=%d"), PartyId);
	ExecutePartyDeploy(PartyId);
}

void AHellunaLobbyGameMode::ExecutePartyDeploy(int32 PartyId)
{
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] ▶ ExecutePartyDeploy | PartyId=%d"), PartyId);

	// [Phase 12 Fix] 값 복사 — RefreshPartyCache가 ActivePartyCache를 변경할 수 있으므로
	const FHellunaPartyInfo* InfoPtr = ActivePartyCache.Find(PartyId);
	if (!InfoPtr || InfoPtr->Members.Num() == 0)
	{
		UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyGM] ExecutePartyDeploy: 캐시에 파티 정보 없음 | PartyId=%d"), PartyId);
		return;
	}
	FHellunaPartyInfo Info = *InfoPtr;
	InfoPtr = nullptr;

	// Step 1: 빈 채널 선택
	FGameChannelInfo EmptyChannel;
	if (!FindEmptyChannel(EmptyChannel))
	{
		// 빈 채널 없음 → 에러 + Ready 리셋
		for (const FHellunaPartyMemberInfo& Member : Info.Members)
		{
			auto* PC = PlayerIdToControllerMap.Find(Member.PlayerId);
			if (PC && PC->IsValid())
			{
				(*PC)->Client_PartyError(TEXT("빈 채널이 없습니다. 잠시 후 다시 시도해주세요."));
			}
		}
		if (SQLiteSubsystem)
		{
			SQLiteSubsystem->ResetAllReadyStates(PartyId);
		}
		RefreshPartyCache(PartyId);
		BroadcastPartyState(PartyId);
		return;
	}

	MarkChannelAsPendingDeploy(EmptyChannel.Port);
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] ExecutePartyDeploy: 채널 배정 | Port=%d"), EmptyChannel.Port);

	// Step 2: 전원 Save
	for (const FHellunaPartyMemberInfo& Member : Info.Members)
	{
		auto* PCPtr = PlayerIdToControllerMap.Find(Member.PlayerId);
		if (!PCPtr || !PCPtr->IsValid())
		{
			UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyGM] ExecutePartyDeploy: Controller 없음 | PlayerId=%s"), *Member.PlayerId);
			continue;
		}

		AHellunaLobbyController* MemberPC = PCPtr->Get();
		UInv_InventoryComponent* LoadoutComp = MemberPC->GetLoadoutComponent();
		UInv_InventoryComponent* StashComp = MemberPC->GetStashComponent();

		if (SQLiteSubsystem && SQLiteSubsystem->IsDatabaseReady())
		{
			// [Fix29-C 순서] Loadout → ExportToFile → Stash
			if (LoadoutComp)
			{
				TArray<FInv_SavedItemData> LoadoutItems = LoadoutComp->CollectInventoryDataForSave();
				if (LoadoutItems.Num() > 0)
				{
					SQLiteSubsystem->SavePlayerLoadout(Member.PlayerId, LoadoutItems);
					SQLiteSubsystem->ExportLoadoutToFile(Member.PlayerId, LoadoutItems, Member.SelectedHeroType);
				}
			}
			if (StashComp)
			{
				TArray<FInv_SavedItemData> StashItems = StashComp->CollectInventoryDataForSave();
				SQLiteSubsystem->SavePlayerStash(Member.PlayerId, StashItems);
			}

			// [Phase 14c] 크래시 복구 + 재참가를 위한 출격 상태 설정 (포트+영웅타입 포함)
			SQLiteSubsystem->SetPlayerDeployedWithPort(Member.PlayerId, true, EmptyChannel.Port, Member.SelectedHeroType);
		}
	}

	// Step 3: 전원 Deploy
	for (const FHellunaPartyMemberInfo& Member : Info.Members)
	{
		auto* PCPtr = PlayerIdToControllerMap.Find(Member.PlayerId);
		if (!PCPtr || !PCPtr->IsValid())
		{
			continue;
		}

		AHellunaLobbyController* MemberPC = PCPtr->Get();
		MemberPC->SetDeployInProgress(true);
		MemberPC->Client_ExecutePartyDeploy(EmptyChannel.Port);

		UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] ExecutePartyDeploy: Client_ExecutePartyDeploy 전송 | PlayerId=%s | Port=%d"),
			*Member.PlayerId, EmptyChannel.Port);
	}

	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] ✓ ExecutePartyDeploy 완료 | PartyId=%d | Port=%d | Members=%d"),
		PartyId, EmptyChannel.Port, Info.Members.Num());
}

// ════════════════════════════════════════════════════════════════════════════════
// [Phase 14d] 재참가 시스템 — 로비 측 헬퍼
// ════════════════════════════════════════════════════════════════════════════════

bool AHellunaLobbyGameMode::IsGameServerRunning(int32 Port)
{
	const FString RegistryDir = GetRegistryDirectoryPath();
	const FString FilePath = FPaths::Combine(RegistryDir, FString::Printf(TEXT("channel_%d.json"), Port));

	FString JsonString;
	if (!FFileHelper::LoadFileToString(JsonString, *FilePath))
	{
		return false;
	}

	TSharedPtr<FJsonObject> JsonObj;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
	if (!FJsonSerializer::Deserialize(Reader, JsonObj) || !JsonObj.IsValid())
	{
		return false;
	}

	const FString StatusStr = JsonObj->GetStringField(TEXT("status"));
	if (StatusStr != TEXT("playing"))
	{
		return false;
	}

	// lastUpdate 60초 이내 확인
	FString LastUpdateStr = JsonObj->GetStringField(TEXT("lastUpdate"));
	FDateTime LastUpdate;
	if (FDateTime::ParseIso8601(*LastUpdateStr, LastUpdate))
	{
		const FTimespan Age = FDateTime::UtcNow() - LastUpdate;
		if (Age.GetTotalSeconds() > 60.0)
		{
			UE_LOG(LogHellunaLobby, Warning, TEXT("[LobbyGM] IsGameServerRunning: Port=%d 타임아웃 (%.0f초)"), Port, Age.GetTotalSeconds());
			return false;
		}
	}

	return true;
}

void AHellunaLobbyGameMode::HandleRejoinAccepted(AHellunaLobbyController* LobbyPC)
{
	if (!LobbyPC || !SQLiteSubsystem) return;

	const FString PlayerId = GetLobbyPlayerId(LobbyPC);
	if (PlayerId.IsEmpty()) return;

	const int32 Port = SQLiteSubsystem->GetPlayerDeployedPort(PlayerId);
	const int32 HeroTypeIdx = SQLiteSubsystem->GetPlayerDeployedHeroType(PlayerId);

	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] [Phase14] HandleRejoinAccepted | PlayerId=%s | Port=%d | HeroType=%d"),
		*PlayerId, Port, HeroTypeIdx);

	// 영웅타입 복원
	const EHellunaHeroType HeroType = IndexToHeroType(HeroTypeIdx);
	if (HeroType != EHellunaHeroType::None)
	{
		LobbyPC->ForceSetSelectedHeroType(HeroType);
	}

	// deploy_state는 true 유지 → 게임서버가 재접속을 감지
	LobbyPC->SetDeployInProgress(true);
	LobbyPC->Client_ExecutePartyDeploy(Port);
}

void AHellunaLobbyGameMode::HandleRejoinDeclined(AHellunaLobbyController* LobbyPC)
{
	if (!LobbyPC || !SQLiteSubsystem) return;

	const FString PlayerId = GetLobbyPlayerId(LobbyPC);
	if (PlayerId.IsEmpty()) return;

	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] [Phase14] HandleRejoinDeclined | PlayerId=%s"), *PlayerId);

	// 출격 상태 해제
	SQLiteSubsystem->SetPlayerDeployed(PlayerId, false);

	// Loadout 삭제 (아이템 포기)
	SQLiteSubsystem->DeletePlayerLoadout(PlayerId);
	SQLiteSubsystem->DeletePlayerEquipment(PlayerId);

	// 정상 로비 초기화 이어서 진행
	ContinueLobbyInitAfterRejoinDecision(LobbyPC, PlayerId);
}

void AHellunaLobbyGameMode::ContinueLobbyInitAfterRejoinDecision(AHellunaLobbyController* LobbyPC, const FString& PlayerId)
{
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] [Phase14] ContinueLobbyInitAfterRejoinDecision | PlayerId=%s"), *PlayerId);

	// Step 2: Stash 로드
	LoadStashToComponent(LobbyPC, PlayerId);

	// Step 2.5: Loadout 로드
	LoadLoadoutToComponent(LobbyPC, PlayerId);

	// Step 3: Controller-PlayerId 매핑
	RegisterControllerPlayerId(LobbyPC, PlayerId);

	// Step 4: 가용 캐릭터 정보 전달
	{
		TArray<bool> UsedCharacters = GetLobbyAvailableCharacters();
		LobbyPC->Client_ShowLobbyCharacterSelectUI(UsedCharacters);
	}

	// ReplicatedPlayerId 설정 (이미 Step 0.5에서 했지만 안전장치)
	LobbyPC->SetReplicatedPlayerId(PlayerId);

	// Step 5: 파티 자동 복귀
	PlayerIdToControllerMap.Add(PlayerId, LobbyPC);
	{
		const int32 ExistingPartyId = SQLiteSubsystem->GetPlayerPartyId(PlayerId);
		if (ExistingPartyId > 0)
		{
			UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] [Phase14] 파티 자동 복귀 | PlayerId=%s | PartyId=%d"), *PlayerId, ExistingPartyId);
			SQLiteSubsystem->UpdateMemberReady(PlayerId, false);
			RefreshPartyCache(ExistingPartyId);
			BroadcastPartyState(ExistingPartyId);
		}
	}

	// 로비 UI 표시
	LobbyPC->Client_ShowLobbyUI();
}

void AHellunaLobbyGameMode::BroadcastPartyState(int32 PartyId)
{
	const FHellunaPartyInfo* Info = ActivePartyCache.Find(PartyId);
	if (!Info)
	{
		return;
	}

	for (const FHellunaPartyMemberInfo& Member : Info->Members)
	{
		auto* PC = PlayerIdToControllerMap.Find(Member.PlayerId);
		if (PC && PC->IsValid())
		{
			(*PC)->Client_UpdatePartyState(*Info);
		}
	}

	UE_LOG(LogHellunaLobby, Verbose, TEXT("[LobbyGM] BroadcastPartyState | PartyId=%d | Members=%d"), PartyId, Info->Members.Num());
}

void AHellunaLobbyGameMode::BroadcastPartyChatMessage(int32 PartyId, const FHellunaPartyChatMessage& Msg)
{
	// 채팅 기록 저장 (최대 50개)
	TArray<FHellunaPartyChatMessage>& History = PartyChatHistory.FindOrAdd(PartyId);
	History.Add(Msg);
	if (History.Num() > 50)
	{
		History.RemoveAt(0);
	}

	// 파티원에게 전송
	const FHellunaPartyInfo* Info = ActivePartyCache.Find(PartyId);
	if (!Info)
	{
		return;
	}

	for (const FHellunaPartyMemberInfo& Member : Info->Members)
	{
		auto* PC = PlayerIdToControllerMap.Find(Member.PlayerId);
		if (PC && PC->IsValid())
		{
			(*PC)->Client_ReceivePartyChatMessage(Msg);
		}
	}
}
