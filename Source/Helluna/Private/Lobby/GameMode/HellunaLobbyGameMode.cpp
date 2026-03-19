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
#include "Lobby/ServerManager/HellunaGameServerManager.h"
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

namespace
{
void BuildEquipmentSlotsFromLoadout(
	const TArray<FInv_SavedItemData>& LoadoutItems,
	TArray<FHellunaEquipmentSlotData>& OutSlots)
{
	OutSlots.Reset();

	for (const FInv_SavedItemData& Item : LoadoutItems)
	{
		if (!Item.bEquipped || Item.WeaponSlotIndex < 0)
		{
			continue;
		}

		FHellunaEquipmentSlotData Slot;
		Slot.SlotId = FString::Printf(TEXT("weapon_%d"), Item.WeaponSlotIndex);
		Slot.ItemType = Item.ItemType;
		OutSlots.Add(MoveTemp(Slot));
	}
}

FString NormalizeLobbyMapIdentifier(const FString& InValue)
{
	FString Normalized = InValue;
	Normalized.TrimStartAndEndInline();

	if (Normalized.IsEmpty())
	{
		return Normalized;
	}

	if (Normalized.Contains(TEXT("/")) || Normalized.Contains(TEXT("\\")))
	{
		Normalized = FPaths::GetBaseFilename(Normalized);
	}

	return Normalized;
}

bool DoesLobbyRegistryMapMatch(const FString& RegistryMapName, const FString& RequestedMapIdentifier)
{
	const FString NormalizedRegistry = NormalizeLobbyMapIdentifier(RegistryMapName);
	const FString NormalizedRequested = NormalizeLobbyMapIdentifier(RequestedMapIdentifier);

	return !NormalizedRegistry.IsEmpty()
		&& !NormalizedRequested.IsEmpty()
		&& NormalizedRegistry.Equals(NormalizedRequested, ESearchCase::IgnoreCase);
}
}

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

	FString CmdLobbyReturnURL;
	if (FParse::Value(FCommandLine::Get(), TEXT("-LobbyReturnURL="), CmdLobbyReturnURL))
	{
		LobbyReturnURL = CmdLobbyReturnURL;
		UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] BeginPlay: LobbyReturnURL override = '%s'"), *LobbyReturnURL);
	}

	if (LobbyReturnURL.IsEmpty())
	{
		UE_LOG(LogHellunaLobby, Warning,
			TEXT("[LobbyGM] BeginPlay: LobbyReturnURL is empty. Spawned game servers will rely on the client-side cached lobby address."));
	}

	// SQLite 서브시스템 캐시 (PostLogin 보다 앞서 초기화)
	if (!SQLiteSubsystem)
	{
		UGameInstance* GI = GetGameInstance();
		if (GI)
		{
			SQLiteSubsystem = GI->GetSubsystem<UHellunaSQLiteSubsystem>();
		}
	}

	// 24시간 이상 된 오래된 파티 DB에서 정리 + stale 캐릭터 등록 해제
	if (SQLiteSubsystem)
	{
		SQLiteSubsystem->CleanupStaleParties(24);
		UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] BeginPlay: CleanupStaleParties(24h) 완료"));

		// 서버 시작 시 아무도 접속하지 않은 상태 → 이전 세션의 잔존 캐릭터 등록 전부 정리
		SQLiteSubsystem->ClearAllActiveGameCharacters();
		UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] BeginPlay: ClearAllActiveGameCharacters 완료"));
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

	// [Phase 16] GameServerManager 초기화
	GameServerManager = NewObject<UHellunaGameServerManager>(this);
	GameServerManager->Initialize(GetWorld(), GetRegistryDirectoryPath(), LobbyReturnURL);
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] BeginPlay: GameServerManager 초기화 완료"));
}

// ════════════════════════════════════════════════════════════════════════════════
// [Phase 16] EndPlay
// ════════════════════════════════════════════════════════════════════════════════

void AHellunaLobbyGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(StalePartyCleanupTimer);
		World->GetTimerManager().ClearTimer(MatchmakingTickTimer);

		for (auto& Pair : WaitAndDeployTimers)
		{
			World->GetTimerManager().ClearTimer(Pair.Value);
		}
		WaitAndDeployTimers.Empty();

		for (auto& Pair : PendingDeployTimers)
		{
			World->GetTimerManager().ClearTimer(Pair.Value);
		}
		PendingDeployTimers.Empty();

		for (auto& Pair : PartyLeaveTimers)
		{
			World->GetTimerManager().ClearTimer(Pair.Value);
		}
		PartyLeaveTimers.Empty();
	}

	PendingDeployChannels.Empty();

	// 서버 매니저 정리
	if (GameServerManager)
	{
		GameServerManager->ShutdownAll();
	}

	Super::EndPlay(EndPlayReason);
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
		const FString PlayerId = CreateDebugLobbyPlayerId(LobbyPC);
		if (UMDF_GameInstance* GI = Cast<UMDF_GameInstance>(GetGameInstance()))
		{
			GI->RegisterLogin(PlayerId);
		}
		UE_LOG(LogHellunaLobby, Warning, TEXT("[LobbyGM] [Phase 13] 디버그 모드 → 고유 ID '%s' 사용"), *PlayerId);
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
		TArray<bool> UsedCharacters = GetLobbyAvailableCharacters(PlayerId);
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
			// [Phase 15] 매칭 큐에서 제거
			LeaveMatchmakingQueue(LogoutPlayerId);

			// [Phase 17] 카운트다운 중이면 취소 + 나머지 큐 재진입
			HandleCountdownPlayerDisconnect(LogoutPlayerId);

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
// 캐릭터 중복 방지 — 같은 파티 내에서만 제한
// ════════════════════════════════════════════════════════════════════════════════

bool AHellunaLobbyGameMode::IsLobbyCharacterAvailable(EHellunaHeroType HeroType, const FString& RequestingPlayerId) const
{
	// 파티에 속해있지 않으면 → 항상 가능 (매칭 시 ResolveHeroDuplication이 처리)
	const int32* MyPartyIdPtr = PlayerToPartyMap.Find(RequestingPlayerId);
	if (!MyPartyIdPtr || *MyPartyIdPtr <= 0)
	{
		return true;
	}

	// 같은 파티원 중 이 캐릭터를 쓰는 사람이 있는지 체크
	const int32 MyPartyId = *MyPartyIdPtr;
	for (const auto& Pair : PlayerSelectedHeroMap)
	{
		if (Pair.Key == RequestingPlayerId) continue; // 자기 자신은 제외
		if (Pair.Value != HeroType) continue;         // 다른 캐릭터면 무관

		// 같은 파티인지 체크
		const int32* OtherPartyIdPtr = PlayerToPartyMap.Find(Pair.Key);
		if (OtherPartyIdPtr && *OtherPartyIdPtr == MyPartyId)
		{
			UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] IsLobbyCharacterAvailable: %s → 같은 파티원(%s)이 사용중"),
				*UEnum::GetValueAsString(HeroType), *Pair.Key);
			return false;
		}
	}

	return true;
}

TArray<bool> AHellunaLobbyGameMode::GetLobbyAvailableCharacters(const FString& RequestingPlayerId) const
{
	// 기본: 전부 가능 (false = 미사용)
	TArray<bool> Result;
	Result.SetNum(3);
	Result[0] = false;
	Result[1] = false;
	Result[2] = false;

	// 파티에 속해있지 않으면 → 전부 가능
	const int32* MyPartyIdPtr = PlayerToPartyMap.Find(RequestingPlayerId);
	if (!MyPartyIdPtr || *MyPartyIdPtr <= 0)
	{
		UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] GetLobbyAvailableCharacters: 솔로 → 전부 가능"));
		return Result;
	}

	// 같은 파티원의 선택 캐릭터만 사용중으로 마킹
	const int32 MyPartyId = *MyPartyIdPtr;
	for (const auto& Pair : PlayerSelectedHeroMap)
	{
		if (Pair.Key == RequestingPlayerId) continue;

		const int32* OtherPartyIdPtr = PlayerToPartyMap.Find(Pair.Key);
		if (OtherPartyIdPtr && *OtherPartyIdPtr == MyPartyId)
		{
			const int32 Index = HeroTypeToIndex(Pair.Value);
			if (Index >= 0 && Index < 3)
			{
				Result[Index] = true;
			}
		}
	}

	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] GetLobbyAvailableCharacters(%s): Lui=%s Luna=%s Liam=%s"),
		*RequestingPlayerId,
		Result[0] ? TEXT("사용중") : TEXT("가능"),
		Result[1] ? TEXT("사용중") : TEXT("가능"),
		Result[2] ? TEXT("사용중") : TEXT("가능"));

	return Result;
}

void AHellunaLobbyGameMode::RegisterLobbyCharacterUse(EHellunaHeroType HeroType, const FString& PlayerId)
{
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] RegisterLobbyCharacterUse | Hero=%s | PlayerId=%s"),
		*UEnum::GetValueAsString(HeroType), *PlayerId);

	// 플레이어별 맵에 등록 (이전 선택 자동 덮어쓰기)
	PlayerSelectedHeroMap.Add(PlayerId, HeroType);

	// SQLite 등록 (Deploy 시 게임서버가 참조)
	if (SQLiteSubsystem && SQLiteSubsystem->IsDatabaseReady())
	{
		SQLiteSubsystem->RegisterActiveGameCharacter(HeroTypeToIndex(HeroType), PlayerId, LobbyServerId);
	}
}

void AHellunaLobbyGameMode::UnregisterLobbyCharacterUse(const FString& PlayerId)
{
	// 플레이어별 맵에서 제거
	if (PlayerSelectedHeroMap.Remove(PlayerId) > 0)
	{
		UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] UnregisterLobbyCharacterUse | PlayerId=%s"), *PlayerId);
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

// ════════════════════════════════════════════════════════════════════════════════
// [Phase 16] FindEmptyChannelForMap
// ════════════════════════════════════════════════════════════════════════════════

bool AHellunaLobbyGameMode::FindEmptyChannelForMap(const FString& MapKey, FGameChannelInfo& OutChannel)
{
	const bool bUseNormalizedMapMatching = FPlatformTime::Cycles64() >= 0;
	if (bUseNormalizedMapMatching)
	{
		const TArray<FGameChannelInfo> Channels = ScanAvailableChannels();
		const FString ResolvedMapPath = GetMapPathByKey(MapKey);
		const FString RequestedMapIdentifier = NormalizeLobbyMapIdentifier(ResolvedMapPath.IsEmpty() ? MapKey : ResolvedMapPath);

		if (RequestedMapIdentifier.IsEmpty())
		{
			UE_LOG(LogHellunaLobby, Warning,
				TEXT("[LobbyGM] FindEmptyChannelForMap: normalized map identifier is empty | MapKey=%s"),
				*MapKey);
			return false;
		}

		for (const FGameChannelInfo& Ch : Channels)
		{
			if (Ch.Status == EChannelStatus::Empty && !PendingDeployChannels.Contains(Ch.Port))
			{
				if (DoesLobbyRegistryMapMatch(Ch.MapName, RequestedMapIdentifier))
				{
					OutChannel = Ch;
					UE_LOG(LogHellunaLobby, Log,
						TEXT("[LobbyGM] FindEmptyChannelForMap: %s (Port=%d, RegistryMap=%s, RequestedMap=%s)"),
						*Ch.ChannelId, Ch.Port, *Ch.MapName, *RequestedMapIdentifier);
					return true;
				}
			}
		}

		UE_LOG(LogHellunaLobby, Log,
			TEXT("[LobbyGM] FindEmptyChannelForMap: no empty channel for MapKey=%s (RequestedMap=%s)"),
			*MapKey, *RequestedMapIdentifier);
		return false;
	}

	const TArray<FGameChannelInfo> Channels = ScanAvailableChannels();

	for (const FGameChannelInfo& Ch : Channels)
	{
		if (Ch.Status == EChannelStatus::Empty && !PendingDeployChannels.Contains(Ch.Port))
		{
			// MapName 필드에 MapKey가 포함되어 있는지 확인
			if (Ch.MapName.Contains(MapKey))
			{
				OutChannel = Ch;
				UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] FindEmptyChannelForMap: %s (Port=%d, Map=%s)"),
					*Ch.ChannelId, Ch.Port, *Ch.MapName);
				return true;
			}
		}
	}

	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] FindEmptyChannelForMap: 맵 '%s'의 빈 채널 없음"), *MapKey);
	return false;
}

// ════════════════════════════════════════════════════════════════════════════════
// [Phase 16] GetMapPathByKey
// ════════════════════════════════════════════════════════════════════════════════

FString AHellunaLobbyGameMode::GetMapPathByKey(const FString& MapKey) const
{
	for (const FHellunaGameMapInfo& Info : AvailableMapConfigs)
	{
		if (Info.MapKey == MapKey)
		{
			return Info.MapPath;
		}
	}

	UE_LOG(LogHellunaLobby, Warning, TEXT("[LobbyGM] GetMapPathByKey: 맵 키 '%s' 미발견, DefaultMapKey 사용"), *MapKey);
	// DefaultMapKey로 재시도
	if (MapKey != DefaultMapKey)
	{
		for (const FHellunaGameMapInfo& Info : AvailableMapConfigs)
		{
			if (Info.MapKey == DefaultMapKey)
			{
				return Info.MapPath;
			}
		}
	}

	return FString();
}

bool AHellunaLobbyGameMode::IsValidConfiguredMapKey(const FString& MapKey) const
{
	if (MapKey.IsEmpty())
	{
		return false;
	}

	for (const FHellunaGameMapInfo& Info : AvailableMapConfigs)
	{
		if (Info.MapKey == MapKey)
		{
			return true;
		}
	}

	return false;
}

bool AHellunaLobbyGameMode::ValidateRequestedGameModeForPlayer(
	const FString& PlayerId,
	ELobbyGameMode Mode,
	FString& OutError) const
{
	if (PlayerId.IsEmpty())
	{
		OutError = TEXT("플레이어 식별자가 없습니다.");
		return false;
	}

	if (static_cast<uint8>(Mode) > static_cast<uint8>(ELobbyGameMode::Squad))
	{
		OutError = TEXT("지원하지 않는 게임 모드입니다.");
		return false;
	}

	const int32* PartyIdPtr = PlayerToPartyMap.Find(PlayerId);
	if (!PartyIdPtr || *PartyIdPtr <= 0)
	{
		return true;
	}

	const FHellunaPartyInfo* Info = ActivePartyCache.Find(*PartyIdPtr);
	if (!Info || !Info->IsValid())
	{
		OutError = TEXT("파티 정보를 확인할 수 없습니다.");
		return false;
	}

	if (Info->LeaderId != PlayerId)
	{
		OutError = TEXT("파티 리더만 게임 모드를 변경할 수 있습니다.");
		return false;
	}

	if (Info->Members.Num() > GetModeCapacity(Mode))
	{
		OutError = TEXT("현재 파티 인원과 맞지 않는 게임 모드입니다.");
		return false;
	}

	return true;
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

	TArray<FString> PreviousMembers;
	if (const FHellunaPartyInfo* ExistingInfo = ActivePartyCache.Find(PartyId))
	{
		for (const FHellunaPartyMemberInfo& Member : ExistingInfo->Members)
		{
			PreviousMembers.Add(Member.PlayerId);
		}
	}

	FHellunaPartyInfo Info = SQLiteSubsystem->LoadPartyInfo(PartyId);
	if (Info.IsValid())
	{
		ActivePartyCache.Add(PartyId, Info);

		TSet<FString> CurrentMembers;

		// PlayerToPartyMap 갱신
		for (const FHellunaPartyMemberInfo& Member : Info.Members)
		{
			CurrentMembers.Add(Member.PlayerId);
			PlayerToPartyMap.Add(Member.PlayerId, PartyId);
		}

		for (const FString& PlayerId : PreviousMembers)
		{
			if (CurrentMembers.Contains(PlayerId))
			{
				continue;
			}

			const int32* CachedPartyId = PlayerToPartyMap.Find(PlayerId);
			if (CachedPartyId && *CachedPartyId == PartyId)
			{
				PlayerToPartyMap.Remove(PlayerId);
			}
		}
	}
	else
	{
		// 파티가 삭제됨
		for (const FString& PlayerId : PreviousMembers)
		{
			const int32* CachedPartyId = PlayerToPartyMap.Find(PlayerId);
			if (CachedPartyId && *CachedPartyId == PartyId)
			{
				PlayerToPartyMap.Remove(PlayerId);
			}
		}

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

	// [Phase 15] 큐에 있으면 전체 엔트리 제거
	LeaveMatchmakingQueue(PlayerId);

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

	// [Phase 15] 큐에 있으면 전체 엔트리 제거
	LeaveMatchmakingQueue(TargetId);

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

	// [Phase 15] 큐 엔트리 영웅 타입 갱신
	UpdateQueueEntryHeroType(PlayerId, HeroType);
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

	// [Phase 18] 리더의 게임 모드에 따라 직접 Deploy vs 매칭 큐 분기
	auto* LeaderPCPtr = PlayerIdToControllerMap.Find(Info.LeaderId);
	ELobbyGameMode LeaderMode = ELobbyGameMode::Squad; // 기본값: 안전하게 Squad
	if (LeaderPCPtr && LeaderPCPtr->IsValid())
	{
		LeaderMode = LeaderPCPtr->Get()->SelectedGameMode;
	}

	const int32 ModeCapacity = GetModeCapacity(LeaderMode);
	const int32 PartySize = Info.Members.Num();

	FString ModeError;
	if (!ValidateRequestedGameModeForPlayer(Info.LeaderId, LeaderMode, ModeError))
	{
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
				(*PC)->Client_PartyError(ModeError);
			}
		}
		BroadcastPartyState(PartyId);
		return;
	}

	for (const FHellunaPartyMemberInfo& Member : Info.Members)
	{
		if (IsPlayerOnlineInLobby(Member.PlayerId))
		{
			continue;
		}

		if (SQLiteSubsystem)
		{
			SQLiteSubsystem->ResetAllReadyStates(PartyId);
		}

		RefreshPartyCache(PartyId);
		for (const FHellunaPartyMemberInfo& NotifyMember : Info.Members)
		{
			auto* PC = PlayerIdToControllerMap.Find(NotifyMember.PlayerId);
			if (PC && PC->IsValid())
			{
				(*PC)->Client_PartyError(TEXT("로비에 연결되지 않은 파티원이 있어 출격할 수 없습니다."));
			}
		}
		BroadcastPartyState(PartyId);
		return;
	}

	if (PartySize >= ModeCapacity)
	{
		// 파티가 모드 정원 이상 → 직접 Deploy (매칭 큐 불필요)
		UE_LOG(LogHellunaLobby, Log,
			TEXT("[LobbyGM] [Phase18] TryAutoDeployParty: 파티 정원 충족 → 직접 Deploy | PartyId=%d | Mode=%d | PartySize=%d | Capacity=%d"),
			PartyId, static_cast<int32>(LeaderMode), PartySize, ModeCapacity);

		// 리더의 맵 키 가져오기
		FString MapKey;
		if (LeaderPCPtr && LeaderPCPtr->IsValid())
		{
			MapKey = LeaderPCPtr->Get()->SelectedMapKey;
		}
		if (MapKey.IsEmpty())
		{
			MapKey = DefaultMapKey;
		}

		// 합성 큐 엔트리 생성 (ExecuteMatchedDeploy 재활용)
		FMatchmakingQueueEntry SyntheticEntry;
		SyntheticEntry.EntryId = NextQueueEntryId++;
		SyntheticEntry.PartyId = PartyId;
		SyntheticEntry.QueueEnterTime = FPlatformTime::Seconds();
		SyntheticEntry.SelectedMapKey = MapKey;
		SyntheticEntry.GameMode = LeaderMode;

		for (const FHellunaPartyMemberInfo& Member : Info.Members)
		{
			SyntheticEntry.PlayerIds.Add(Member.PlayerId);
			SyntheticEntry.HeroTypes.Add(Member.SelectedHeroType);
		}

		TArray<FMatchmakingQueueEntry> DirectMatched;
		DirectMatched.Add(MoveTemp(SyntheticEntry));
		ExecuteMatchedDeploy(DirectMatched);

		// Ready 리셋 (Deploy 완료 후)
		if (SQLiteSubsystem)
		{
			SQLiteSubsystem->ResetAllReadyStates(PartyId);
		}
		RefreshPartyCache(PartyId);
		BroadcastPartyState(PartyId);
	}
	else
	{
		// 파티가 모드 정원 미달 → 매칭 큐 진입 (추가 플레이어 필요)
		UE_LOG(LogHellunaLobby, Log,
			TEXT("[LobbyGM] [Phase18] TryAutoDeployParty: 정원 미달 → 매칭 큐 진입 | PartyId=%d | Mode=%d | PartySize=%d | Capacity=%d"),
			PartyId, static_cast<int32>(LeaderMode), PartySize, ModeCapacity);
		EnterMatchmakingQueue(Info.LeaderId);
	}
}

void AHellunaLobbyGameMode::ExecutePartyDeploy(int32 PartyId)
{
	const bool bUseSafePartyDeployFlow = FPlatformTime::Cycles64() >= 0;
	if (bUseSafePartyDeployFlow)
	{
		UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] ExecutePartyDeploy | PartyId=%d"), PartyId);

		const FHellunaPartyInfo* SafeInfoPtr = ActivePartyCache.Find(PartyId);
		if (!SafeInfoPtr || SafeInfoPtr->Members.Num() == 0)
		{
			UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyGM] ExecutePartyDeploy: missing party cache | PartyId=%d"), PartyId);
			return;
		}

		const FHellunaPartyInfo SafeInfo = *SafeInfoPtr;
		TArray<FString> PreparedPlayerIds;

		FGameChannelInfo SafeEmptyChannel;
		if (!FindEmptyChannel(SafeEmptyChannel))
		{
			for (const FHellunaPartyMemberInfo& Member : SafeInfo.Members)
			{
				const TWeakObjectPtr<AHellunaLobbyController>* NotifyPC = PlayerIdToControllerMap.Find(Member.PlayerId);
				if (NotifyPC && NotifyPC->IsValid())
				{
					(*NotifyPC)->Client_PartyError(TEXT("빈 채널이 없습니다. 잠시 후 다시 시도해주세요."));
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

		MarkChannelAsPendingDeploy(SafeEmptyChannel.Port);

		for (const FHellunaPartyMemberInfo& Member : SafeInfo.Members)
		{
			const TWeakObjectPtr<AHellunaLobbyController>* PCPtr = PlayerIdToControllerMap.Find(Member.PlayerId);
			if (!PCPtr || !PCPtr->IsValid())
			{
				RollbackDeployStateForPlayers(PreparedPlayerIds, SafeEmptyChannel.Port);
				if (SQLiteSubsystem)
				{
					SQLiteSubsystem->ResetAllReadyStates(PartyId);
				}
				RefreshPartyCache(PartyId);

				for (const FHellunaPartyMemberInfo& NotifyMember : SafeInfo.Members)
				{
					const TWeakObjectPtr<AHellunaLobbyController>* NotifyPC = PlayerIdToControllerMap.Find(NotifyMember.PlayerId);
					if (NotifyPC && NotifyPC->IsValid())
					{
						(*NotifyPC)->Client_PartyError(TEXT("로비 컨트롤러가 유효하지 않아 출격을 중단했습니다."));
					}
				}

				BroadcastPartyState(PartyId);
				return;
			}

			FString PersistError;
			if (!PersistDeployDataForPlayer(PCPtr->Get(), Member.PlayerId, Member.SelectedHeroType, SafeEmptyChannel.Port, PersistError))
			{
				RollbackDeployStateForPlayer(Member.PlayerId, SafeEmptyChannel.Port);
				RollbackDeployStateForPlayers(PreparedPlayerIds, SafeEmptyChannel.Port);
				if (SQLiteSubsystem)
				{
					SQLiteSubsystem->ResetAllReadyStates(PartyId);
				}
				RefreshPartyCache(PartyId);

				for (const FHellunaPartyMemberInfo& NotifyMember : SafeInfo.Members)
				{
					const TWeakObjectPtr<AHellunaLobbyController>* NotifyPC = PlayerIdToControllerMap.Find(NotifyMember.PlayerId);
					if (NotifyPC && NotifyPC->IsValid())
					{
						(*NotifyPC)->Client_PartyError(PersistError);
					}
				}

				BroadcastPartyState(PartyId);
				return;
			}

			PreparedPlayerIds.Add(Member.PlayerId);
		}

		for (const FHellunaPartyMemberInfo& Member : SafeInfo.Members)
		{
			const TWeakObjectPtr<AHellunaLobbyController>* PCPtr = PlayerIdToControllerMap.Find(Member.PlayerId);
			if (!PCPtr || !PCPtr->IsValid())
			{
				continue;
			}

			AHellunaLobbyController* MemberPC = PCPtr->Get();
			MemberPC->SetDeployInProgress(true);
			MemberPC->Client_ExecutePartyDeploy(SafeEmptyChannel.Port);
		}

		return;
	}

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
	TArray<FString> PreparedPlayerIds;

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
	const bool bUseSafeRejoinAcceptFlow = FPlatformTime::Cycles64() >= 0;
	if (bUseSafeRejoinAcceptFlow)
	{
	if (!LobbyPC || !SQLiteSubsystem)
	{
		return;
	}

	const FString PlayerId = GetLobbyPlayerId(LobbyPC);
	if (PlayerId.IsEmpty())
	{
		LobbyPC->Client_DeployFailed(TEXT("플레이어 식별자를 확인할 수 없습니다."));
		return;
	}

	const int32 Port = SQLiteSubsystem->GetPlayerDeployedPort(PlayerId);
	const int32 HeroTypeIdx = SQLiteSubsystem->GetPlayerDeployedHeroType(PlayerId);

	if (!SQLiteSubsystem->IsPlayerDeployed(PlayerId) || Port <= 0 || LobbyPC->PendingRejoinPort <= 0 || LobbyPC->PendingRejoinPort != Port)
	{
		UE_LOG(LogHellunaLobby, Warning,
			TEXT("[LobbyGM] HandleRejoinAccepted rejected | PlayerId=%s | PendingPort=%d | StoredPort=%d"),
			*PlayerId, LobbyPC->PendingRejoinPort, Port);

		RollbackDeployStateForPlayer(PlayerId, Port);
		LobbyPC->Client_DeployFailed(TEXT("재참가 가능한 세션이 없습니다."));
		ContinueLobbyInitAfterRejoinDecision(LobbyPC, PlayerId);
		return;
	}

	if (!IsGameServerRunning(Port))
	{
		UE_LOG(LogHellunaLobby, Warning,
			TEXT("[LobbyGM] HandleRejoinAccepted: stale server | PlayerId=%s | Port=%d"),
			*PlayerId, Port);

		RollbackDeployStateForPlayer(PlayerId, Port);
		CheckAndRecoverFromCrash(PlayerId);
		LobbyPC->Client_DeployFailed(TEXT("재참가 가능한 게임 서버가 더 이상 없습니다."));
		ContinueLobbyInitAfterRejoinDecision(LobbyPC, PlayerId);
		return;
	}

	const EHellunaHeroType HeroType = IndexToHeroType(HeroTypeIdx);
	if (HeroType != EHellunaHeroType::None)
	{
		LobbyPC->ForceSetSelectedHeroType(HeroType);
	}

	LobbyPC->PendingRejoinPort = 0;
	LobbyPC->SetDeployInProgress(true);
	LobbyPC->Client_ExecutePartyDeploy(Port);
	return;
	}

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
	const bool bUseSafeRejoinDeclineFlow = FPlatformTime::Cycles64() >= 0;
	if (bUseSafeRejoinDeclineFlow)
	{
	if (!LobbyPC || !SQLiteSubsystem)
	{
		return;
	}

	const FString PlayerId = GetLobbyPlayerId(LobbyPC);
	if (PlayerId.IsEmpty())
	{
		LobbyPC->Client_DeployFailed(TEXT("플레이어 식별자를 확인할 수 없습니다."));
		return;
	}

	const int32 Port = SQLiteSubsystem->GetPlayerDeployedPort(PlayerId);
	const bool bValidPendingRejoin =
		SQLiteSubsystem->IsPlayerDeployed(PlayerId) &&
		Port > 0 &&
		LobbyPC->PendingRejoinPort > 0 &&
		LobbyPC->PendingRejoinPort == Port;

	if (!bValidPendingRejoin)
	{
		UE_LOG(LogHellunaLobby, Warning,
			TEXT("[LobbyGM] HandleRejoinDeclined ignored | PlayerId=%s | PendingPort=%d | StoredPort=%d"),
			*PlayerId, LobbyPC->PendingRejoinPort, Port);

		ContinueLobbyInitAfterRejoinDecision(LobbyPC, PlayerId);
		return;
	}

	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] HandleRejoinDeclined | PlayerId=%s"), *PlayerId);

	SQLiteSubsystem->SetPlayerDeployed(PlayerId, false);
	SQLiteSubsystem->DeletePlayerLoadout(PlayerId);
	SQLiteSubsystem->DeletePlayerEquipment(PlayerId);

	ContinueLobbyInitAfterRejoinDecision(LobbyPC, PlayerId);
	return;
	}

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

	if (!LobbyPC || PlayerId.IsEmpty())
	{
		return;
	}

	LobbyPC->PendingRejoinPort = 0;
	LobbyPC->SetDeployInProgress(false);

	// Step 2: Stash 로드
	LoadStashToComponent(LobbyPC, PlayerId);

	// Step 2.5: Loadout 로드
	LoadLoadoutToComponent(LobbyPC, PlayerId);

	// Step 3: Controller-PlayerId 매핑
	RegisterControllerPlayerId(LobbyPC, PlayerId);

	// Step 4: 가용 캐릭터 정보 전달
	{
		TArray<bool> UsedCharacters = GetLobbyAvailableCharacters(PlayerId);
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

void AHellunaLobbyGameMode::ResetMatchmakingStatusForPlayers(const TArray<FString>& PlayerIds, EMatchmakingStatus Status)
{
	FMatchmakingStatusInfo StatusInfo;
	StatusInfo.Status = Status;

	TSet<FString> UniquePlayerIds;
	for (const FString& PlayerId : PlayerIds)
	{
		if (PlayerId.IsEmpty() || UniquePlayerIds.Contains(PlayerId))
		{
			continue;
		}

		UniquePlayerIds.Add(PlayerId);

		const TWeakObjectPtr<AHellunaLobbyController>* PCPtr = PlayerIdToControllerMap.Find(PlayerId);
		if (PCPtr && PCPtr->IsValid())
		{
			(*PCPtr)->Client_MatchmakingStatusUpdate(StatusInfo);
		}
	}
}

void AHellunaLobbyGameMode::SendMatchmakingErrorToPlayers(const TArray<FString>& PlayerIds, const FString& ErrorMessage)
{
	TSet<FString> UniquePlayerIds;
	for (const FString& PlayerId : PlayerIds)
	{
		if (PlayerId.IsEmpty() || UniquePlayerIds.Contains(PlayerId))
		{
			continue;
		}

		UniquePlayerIds.Add(PlayerId);

		const TWeakObjectPtr<AHellunaLobbyController>* PCPtr = PlayerIdToControllerMap.Find(PlayerId);
		if (PCPtr && PCPtr->IsValid())
		{
			(*PCPtr)->Client_MatchmakingError(ErrorMessage);
		}
	}
}

bool AHellunaLobbyGameMode::IsPlayerOnlineInLobby(const FString& PlayerId) const
{
	const TWeakObjectPtr<AHellunaLobbyController>* PCPtr = PlayerIdToControllerMap.Find(PlayerId);
	return PCPtr && PCPtr->IsValid();
}

bool AHellunaLobbyGameMode::PersistDeployDataForPlayer(
	AHellunaLobbyController* LobbyPC,
	const FString& PlayerId,
	int32 HeroType,
	int32 ServerPort,
	FString& OutError)
{
	if (!LobbyPC)
	{
		OutError = TEXT("유효하지 않은 로비 컨트롤러입니다.");
		return false;
	}

	if (PlayerId.IsEmpty())
	{
		OutError = TEXT("플레이어 식별자가 없습니다.");
		return false;
	}

	if (ServerPort <= 0)
	{
		OutError = TEXT("유효하지 않은 게임 서버 포트입니다.");
		return false;
	}

	if (!SQLiteSubsystem || !SQLiteSubsystem->IsDatabaseReady())
	{
		OutError = TEXT("데이터베이스가 준비되지 않았습니다.");
		return false;
	}

	UInv_InventoryComponent* LoadoutComp = LobbyPC->GetLoadoutComponent();
	if (!LoadoutComp)
	{
		OutError = TEXT("Loadout 컴포넌트를 찾을 수 없습니다.");
		return false;
	}

	const bool bUseTimedDeployPersistence = FPlatformTime::Cycles64() >= 0;
	if (bUseTimedDeployPersistence)
	{
		const double PersistStartSeconds = FPlatformTime::Seconds();
		auto LogPersistStep = [&PlayerId](const TCHAR* StepName, const double StepStartSeconds)
		{
			const double StepMs = (FPlatformTime::Seconds() - StepStartSeconds) * 1000.0;
			if (StepMs >= 100.0)
			{
				UE_LOG(LogHellunaLobby, Warning,
					TEXT("[LobbyGM] PersistDeployDataForPlayer step slow | PlayerId=%s | Step=%s | %.2f ms"),
					*PlayerId, StepName, StepMs);
			}
			else
			{
				UE_LOG(LogHellunaLobby, Verbose,
					TEXT("[LobbyGM] PersistDeployDataForPlayer step | PlayerId=%s | Step=%s | %.2f ms"),
					*PlayerId, StepName, StepMs);
			}
		};

		double StepStartSeconds = FPlatformTime::Seconds();
		TArray<FInv_SavedItemData> TimedLoadoutItems = LoadoutComp->CollectInventoryDataForSave();
		{
			int32 EquippedCount = 0;
			int32 AttachmentCount = 0;
			for (const FInv_SavedItemData& SavedItem : TimedLoadoutItems)
			{
				if (SavedItem.bEquipped)
				{
					++EquippedCount;
				}
				AttachmentCount += SavedItem.Attachments.Num();
			}
			UE_LOG(LogHellunaLobby, Log,
				TEXT("[LobbyGM] PersistDeployDataForPlayer collect | PlayerId=%s | Loadout=%d | Equipped=%d | Grid=%d | Attachments=%d"),
				*PlayerId,
				TimedLoadoutItems.Num(),
				EquippedCount,
				TimedLoadoutItems.Num() - EquippedCount,
				AttachmentCount);
		}
		LogPersistStep(TEXT("CollectLoadout"), StepStartSeconds);
		if (TimedLoadoutItems.Num() > 0)
		{
			StepStartSeconds = FPlatformTime::Seconds();
			if (!SQLiteSubsystem->SavePlayerLoadout(PlayerId, TimedLoadoutItems))
			{
				OutError = TEXT("Loadout ??μ뿉 ?ㅽ뙣?덉뒿?덈떎.");
				return false;
			}
			LogPersistStep(TEXT("SavePlayerLoadout"), StepStartSeconds);

			TArray<FHellunaEquipmentSlotData> TimedEquipSlots;
			StepStartSeconds = FPlatformTime::Seconds();
			BuildEquipmentSlotsFromLoadout(TimedLoadoutItems, TimedEquipSlots);
			LogPersistStep(TEXT("BuildEquipmentSlots"), StepStartSeconds);

			StepStartSeconds = FPlatformTime::Seconds();
			if (!SQLiteSubsystem->SavePlayerEquipment(PlayerId, TimedEquipSlots))
			{
				OutError = TEXT("?λ퉬 ?щ’ ??μ뿉 ?ㅽ뙣?덉뒿?덈떎.");
				return false;
			}
			LogPersistStep(TEXT("SavePlayerEquipment"), StepStartSeconds);

			StepStartSeconds = FPlatformTime::Seconds();
			if (!SQLiteSubsystem->ExportLoadoutToFile(PlayerId, TimedLoadoutItems, HeroType))
			{
				OutError = TEXT("Loadout ?뚯씪 ?대낫?닿린???ㅽ뙣?덉뒿?덈떎.");
				return false;
			}
			LogPersistStep(TEXT("ExportLoadoutToFile"), StepStartSeconds);
		}

		if (UInv_InventoryComponent* TimedStashComp = LobbyPC->GetStashComponent())
		{
			StepStartSeconds = FPlatformTime::Seconds();
			const TArray<FInv_SavedItemData> TimedStashItems = TimedStashComp->CollectInventoryDataForSave();
			UE_LOG(LogHellunaLobby, Log,
				TEXT("[LobbyGM] PersistDeployDataForPlayer collect | PlayerId=%s | Stash=%d"),
				*PlayerId,
				TimedStashItems.Num());
			LogPersistStep(TEXT("CollectStash"), StepStartSeconds);

			StepStartSeconds = FPlatformTime::Seconds();
			if (!SQLiteSubsystem->SavePlayerStash(PlayerId, TimedStashItems))
			{
				OutError = TEXT("Stash ??μ뿉 ?ㅽ뙣?덉뒿?덈떎.");
				return false;
			}
			LogPersistStep(TEXT("SavePlayerStash"), StepStartSeconds);
		}

		StepStartSeconds = FPlatformTime::Seconds();
		if (!SQLiteSubsystem->SetPlayerDeployedWithPort(PlayerId, true, ServerPort, HeroType))
		{
			OutError = TEXT("異쒓꺽 ?곹깭 ??μ뿉 ?ㅽ뙣?덉뒿?덈떎.");
			return false;
		}
		LogPersistStep(TEXT("SetPlayerDeployedWithPort"), StepStartSeconds);

		const double TotalPersistMs = (FPlatformTime::Seconds() - PersistStartSeconds) * 1000.0;
		if (TotalPersistMs >= 500.0)
		{
			UE_LOG(LogHellunaLobby, Warning,
				TEXT("[LobbyGM] PersistDeployDataForPlayer total slow | PlayerId=%s | Port=%d | HeroType=%d | %.2f ms"),
				*PlayerId, ServerPort, HeroType, TotalPersistMs);
		}
		else
		{
			UE_LOG(LogHellunaLobby, Log,
				TEXT("[LobbyGM] PersistDeployDataForPlayer total | PlayerId=%s | Port=%d | HeroType=%d | %.2f ms"),
				*PlayerId, ServerPort, HeroType, TotalPersistMs);
		}

		return true;
	}

	TArray<FInv_SavedItemData> LoadoutItems = LoadoutComp->CollectInventoryDataForSave();
	if (LoadoutItems.Num() > 0)
	{
		if (!SQLiteSubsystem->SavePlayerLoadout(PlayerId, LoadoutItems))
		{
			OutError = TEXT("Loadout 저장에 실패했습니다.");
			return false;
		}

		TArray<FHellunaEquipmentSlotData> EquipSlots;
		BuildEquipmentSlotsFromLoadout(LoadoutItems, EquipSlots);
		if (!SQLiteSubsystem->SavePlayerEquipment(PlayerId, EquipSlots))
		{
			OutError = TEXT("장비 슬롯 저장에 실패했습니다.");
			return false;
		}

		if (!SQLiteSubsystem->ExportLoadoutToFile(PlayerId, LoadoutItems, HeroType))
		{
			OutError = TEXT("Loadout 파일 내보내기에 실패했습니다.");
			return false;
		}
	}

	if (UInv_InventoryComponent* StashComp = LobbyPC->GetStashComponent())
	{
		const TArray<FInv_SavedItemData> StashItems = StashComp->CollectInventoryDataForSave();
		if (!SQLiteSubsystem->SavePlayerStash(PlayerId, StashItems))
		{
			OutError = TEXT("Stash 저장에 실패했습니다.");
			return false;
		}
	}

	if (!SQLiteSubsystem->SetPlayerDeployedWithPort(PlayerId, true, ServerPort, HeroType))
	{
		OutError = TEXT("출격 상태 저장에 실패했습니다.");
		return false;
	}

	return true;
}

void AHellunaLobbyGameMode::RollbackDeployStateForPlayer(const FString& PlayerId, int32 ExpectedPort)
{
	if (PlayerId.IsEmpty() || !SQLiteSubsystem || !SQLiteSubsystem->IsDatabaseReady())
	{
		return;
	}

	if (!SQLiteSubsystem->IsPlayerDeployed(PlayerId))
	{
		return;
	}

	if (ExpectedPort > 0)
	{
		const int32 CurrentPort = SQLiteSubsystem->GetPlayerDeployedPort(PlayerId);
		if (CurrentPort != ExpectedPort)
		{
			return;
		}
	}

	if (!SQLiteSubsystem->SetPlayerDeployed(PlayerId, false))
	{
		UE_LOG(LogHellunaLobby, Warning,
			TEXT("[LobbyGM] RollbackDeployStateForPlayer 실패 | PlayerId=%s | ExpectedPort=%d"),
			*PlayerId, ExpectedPort);
	}
}

void AHellunaLobbyGameMode::RollbackDeployStateForPlayers(const TArray<FString>& PlayerIds, int32 ExpectedPort)
{
	for (const FString& PlayerId : PlayerIds)
	{
		RollbackDeployStateForPlayer(PlayerId, ExpectedPort);
	}
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

// ============================================================================
// [Phase 15] 매치메이킹 시스템
// ============================================================================

void AHellunaLobbyGameMode::EnterMatchmakingQueue(const FString& PlayerId, const FString& MapKey)
{
	const bool bUseSafeQueueEntryFlow = FPlatformTime::Cycles64() >= 0;
	if (bUseSafeQueueEntryFlow)
	{
		UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] EnterMatchmakingQueue | PlayerId=%s | MapKey=%s"), *PlayerId, *MapKey);

		const TWeakObjectPtr<AHellunaLobbyController>* RequesterPtr = PlayerIdToControllerMap.Find(PlayerId);
		if (!RequesterPtr || !RequesterPtr->IsValid())
		{
			return;
		}

		AHellunaLobbyController* RequesterPC = RequesterPtr->Get();
		if (PlayerId.IsEmpty())
		{
			RequesterPC->Client_MatchmakingError(TEXT("플레이어 식별자를 확인할 수 없습니다."));
			return;
		}

		if (PlayerToQueueEntryMap.Contains(PlayerId))
		{
			RequesterPC->Client_MatchmakingError(TEXT("이미 매칭 대기 중입니다."));
			return;
		}

		FString ResolvedMapKey = MapKey;
		if (ResolvedMapKey.IsEmpty())
		{
			ResolvedMapKey = RequesterPC->SelectedMapKey;
		}
		if (ResolvedMapKey.IsEmpty())
		{
			ResolvedMapKey = DefaultMapKey;
		}

		if (!IsValidConfiguredMapKey(ResolvedMapKey))
		{
			RequesterPC->Client_MatchmakingError(TEXT("유효하지 않은 맵 선택입니다."));
			return;
		}

		FString ModeError;
		if (!ValidateRequestedGameModeForPlayer(PlayerId, RequesterPC->SelectedGameMode, ModeError))
		{
			RequesterPC->Client_MatchmakingError(ModeError);
			return;
		}

		FMatchmakingQueueEntry NewEntry;
		NewEntry.SelectedMapKey = ResolvedMapKey;
		NewEntry.GameMode = RequesterPC->SelectedGameMode;

		const int32 NoneHeroType = HeroTypeToIndex(EHellunaHeroType::None);
		const int32* PartyIdPtr = PlayerToPartyMap.Find(PlayerId);
		if (PartyIdPtr && *PartyIdPtr > 0)
		{
			const FHellunaPartyInfo* Info = ActivePartyCache.Find(*PartyIdPtr);
			if (!Info || !Info->IsValid())
			{
				RequesterPC->Client_MatchmakingError(TEXT("파티 정보를 확인할 수 없습니다."));
				return;
			}

			if (Info->LeaderId != PlayerId)
			{
				RequesterPC->Client_MatchmakingError(TEXT("파티 리더만 매칭을 시작할 수 있습니다."));
				return;
			}

			if (Info->Members.Num() > GetModeCapacity(NewEntry.GameMode))
			{
				RequesterPC->Client_MatchmakingError(TEXT("현재 파티 인원과 맞지 않는 게임 모드입니다."));
				return;
			}

			NewEntry.PartyId = *PartyIdPtr;
			for (const FHellunaPartyMemberInfo& Member : Info->Members)
			{
				if (!IsPlayerOnlineInLobby(Member.PlayerId))
				{
					RequesterPC->Client_MatchmakingError(TEXT("로비에 연결되지 않은 파티원이 있어 매칭을 시작할 수 없습니다."));
					return;
				}

				if (!Member.bIsReady)
				{
					RequesterPC->Client_MatchmakingError(TEXT("모든 파티원이 준비 완료 상태여야 합니다."));
					return;
				}

				if (Member.SelectedHeroType == NoneHeroType)
				{
					RequesterPC->Client_MatchmakingError(TEXT("모든 파티원이 영웅을 선택해야 합니다."));
					return;
				}

				if (PlayerToQueueEntryMap.Contains(Member.PlayerId))
				{
					RequesterPC->Client_MatchmakingError(TEXT("파티원 중 이미 매칭 대기 중인 플레이어가 있습니다."));
					return;
				}

				NewEntry.PlayerIds.Add(Member.PlayerId);
				NewEntry.HeroTypes.Add(Member.SelectedHeroType);
			}
		}
		else
		{
			if (RequesterPC->GetSelectedHeroType() == EHellunaHeroType::None)
			{
				RequesterPC->Client_MatchmakingError(TEXT("영웅을 먼저 선택해주세요."));
				return;
			}

			NewEntry.PartyId = 0;
			NewEntry.PlayerIds.Add(PlayerId);
			NewEntry.HeroTypes.Add(static_cast<int32>(RequesterPC->GetSelectedHeroType()));
		}

		EnqueueMatchmakingEntry(MoveTemp(NewEntry));
		return;
	}

	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] [Phase15] EnterMatchmakingQueue | PlayerId=%s | MapKey=%s"), *PlayerId, *MapKey);

	// 이미 큐에 있으면 무시
	if (PlayerToQueueEntryMap.Contains(PlayerId))
	{
		UE_LOG(LogHellunaLobby, Warning, TEXT("[LobbyGM] [Phase15] 이미 큐에 있음 | PlayerId=%s"), *PlayerId);
		auto* PC = PlayerIdToControllerMap.Find(PlayerId);
		if (PC && PC->IsValid())
		{
			(*PC)->Client_MatchmakingError(TEXT("이미 매칭 대기 중입니다"));
		}
		return;
	}

	// 캐릭터 미선택 체크
	auto* PCPtr = PlayerIdToControllerMap.Find(PlayerId);
	if (PCPtr && PCPtr->IsValid())
	{
		AHellunaLobbyController* LobbyPC = PCPtr->Get();
		if (LobbyPC->GetSelectedHeroType() == EHellunaHeroType::None)
		{
			LobbyPC->Client_MatchmakingError(TEXT("캐릭터를 먼저 선택해주세요"));
			return;
		}
	}

	FMatchmakingQueueEntry NewEntry;

	// [Phase 16] 맵 키 설정
	{
		FString ResolvedMapKey = MapKey;
		if (ResolvedMapKey.IsEmpty())
		{
			// 컨트롤러에서 가져오기
			auto* CtrlPtr = PlayerIdToControllerMap.Find(PlayerId);
			if (CtrlPtr && CtrlPtr->IsValid())
			{
				ResolvedMapKey = CtrlPtr->Get()->SelectedMapKey;
			}
		}
		NewEntry.SelectedMapKey = ResolvedMapKey.IsEmpty() ? DefaultMapKey : ResolvedMapKey;
	}

	// 파티 소속이면 파티 전원을 1개 엔트리로 등록
	const int32* PartyIdPtr = PlayerToPartyMap.Find(PlayerId);
	if (PartyIdPtr && *PartyIdPtr > 0)
	{
		NewEntry.PartyId = *PartyIdPtr;
		const FHellunaPartyInfo* Info = ActivePartyCache.Find(*PartyIdPtr);
		if (Info)
		{
			for (const FHellunaPartyMemberInfo& Member : Info->Members)
			{
				// 이미 큐에 있는 멤버가 있으면 중복 방지
				if (PlayerToQueueEntryMap.Contains(Member.PlayerId))
				{
					UE_LOG(LogHellunaLobby, Warning, TEXT("[LobbyGM] [Phase15] 파티 멤버가 이미 큐에 있음 | PlayerId=%s"), *Member.PlayerId);
					auto* MPC = PlayerIdToControllerMap.Find(PlayerId);
					if (MPC && MPC->IsValid())
					{
						(*MPC)->Client_MatchmakingError(TEXT("파티 멤버가 이미 매칭 대기 중입니다"));
					}
					return;
				}
				NewEntry.PlayerIds.Add(Member.PlayerId);
				NewEntry.HeroTypes.Add(Member.SelectedHeroType);
			}
		}
	}
	else
	{
		// 솔로 → 1인 엔트리
		NewEntry.PartyId = 0;
		NewEntry.PlayerIds.Add(PlayerId);

		if (PCPtr && PCPtr->IsValid())
		{
			NewEntry.HeroTypes.Add(static_cast<int32>(PCPtr->Get()->GetSelectedHeroType()));
		}
		else
		{
			NewEntry.HeroTypes.Add(3); // None
		}
	}

	// [Phase 18] 게임 모드 저장
	if (PCPtr && PCPtr->IsValid())
	{
		NewEntry.GameMode = PCPtr->Get()->SelectedGameMode;
	}

	EnqueueMatchmakingEntry(MoveTemp(NewEntry));
}

void AHellunaLobbyGameMode::LeaveMatchmakingQueue(const FString& PlayerId)
{
	const bool bUseSafeQueueLeaveFlow = FPlatformTime::Cycles64() >= 0;
	if (bUseSafeQueueLeaveFlow)
	{
		const int32* EntryIdPtr = PlayerToQueueEntryMap.Find(PlayerId);
		if (!EntryIdPtr)
		{
			return;
		}

		const int32 EntryId = *EntryIdPtr;
		TArray<FString> RemovedPlayerIds;
		for (const FMatchmakingQueueEntry& Entry : MatchmakingQueue)
		{
			if (Entry.EntryId == EntryId)
			{
				RemovedPlayerIds = Entry.PlayerIds;
				break;
			}
		}

		UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] LeaveMatchmakingQueue | PlayerId=%s | EntryId=%d"), *PlayerId, EntryId);

		RemoveQueueEntry(EntryId);
		ResetMatchmakingStatusForPlayers(RemovedPlayerIds);
		BroadcastMatchmakingStatus();

		if (MatchmakingQueue.Num() == 0)
		{
			if (UWorld* World = GetWorld())
			{
				World->GetTimerManager().ClearTimer(MatchmakingTickTimer);
			}
		}
		return;
	}

	const int32* EntryIdPtr = PlayerToQueueEntryMap.Find(PlayerId);
	if (!EntryIdPtr)
	{
		return; // 큐에 없음
	}

	const int32 EntryId = *EntryIdPtr;
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] [Phase15] LeaveMatchmakingQueue | PlayerId=%s | EntryId=%d"), *PlayerId, EntryId);

	RemoveQueueEntry(EntryId);
	BroadcastMatchmakingStatus();

	// 큐가 비었으면 틱 타이머 중지
	if (MatchmakingQueue.Num() == 0)
	{
		UWorld* World = GetWorld();
		if (World)
		{
			World->GetTimerManager().ClearTimer(MatchmakingTickTimer);
			UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] [Phase15] 매칭 틱 타이머 중지 (큐 비어있음)"));
		}
	}
}

bool AHellunaLobbyGameMode::IsPlayerInQueue(const FString& PlayerId) const
{
	return PlayerToQueueEntryMap.Contains(PlayerId);
}

void AHellunaLobbyGameMode::TickMatchmaking()
{
	if (MatchmakingQueue.Num() == 0)
	{
		// 큐 비어있으면 타이머 중지
		UWorld* World = GetWorld();
		if (World)
		{
			World->GetTimerManager().ClearTimer(MatchmakingTickTimer);
		}
		return;
	}

	TryFormMatch();
	BroadcastMatchmakingStatus();
}

void AHellunaLobbyGameMode::BroadcastMatchmakingStatus()
{
	const double Now = FPlatformTime::Seconds();

	// 같은 MapKey별 전체 대기 인원 합산
	TMap<FString, int32> MapKeyPlayerCounts;
	for (const FMatchmakingQueueEntry& Entry : MatchmakingQueue)
	{
		MapKeyPlayerCounts.FindOrAdd(Entry.SelectedMapKey) += Entry.GetPlayerCount();
	}

	for (const FMatchmakingQueueEntry& Entry : MatchmakingQueue)
	{
		FMatchmakingStatusInfo StatusInfo;
		StatusInfo.Status = EMatchmakingStatus::Searching;
		StatusInfo.ElapsedTime = static_cast<float>(Now - Entry.QueueEnterTime);
		StatusInfo.CurrentPlayerCount = MapKeyPlayerCounts.FindRef(Entry.SelectedMapKey);
		StatusInfo.TargetPlayerCount = GetModeCapacity(Entry.GameMode);

		for (const FString& PId : Entry.PlayerIds)
		{
			auto* PCPtr = PlayerIdToControllerMap.Find(PId);
			if (PCPtr && PCPtr->IsValid())
			{
				(*PCPtr)->Client_MatchmakingStatusUpdate(StatusInfo);
			}
		}
	}
}

bool AHellunaLobbyGameMode::TryFormMatch()
{
	// Pass 1: 단일 엔트리가 모드 정원 이상 → 즉시 매칭 (SOLO=1, DUO=2, SQUAD=3)
	for (int32 i = MatchmakingQueue.Num() - 1; i >= 0; --i)
	{
		const int32 Capacity = GetModeCapacity(MatchmakingQueue[i].GameMode);
		if (MatchmakingQueue[i].GetPlayerCount() >= Capacity)
		{
			TArray<FMatchmakingQueueEntry> Matched;
			Matched.Add(MatchmakingQueue[i]);
			auto ReassignedMap = ResolveHeroDuplication(Matched);
			StartMatchCountdown(MoveTemp(Matched), MoveTemp(ReassignedMap));
			return true;
		}
	}

	// Pass 2: SQUAD 2인 + 1인 조합 — 같은 MapKey + 같은 GameMode
	for (int32 a = 0; a < MatchmakingQueue.Num(); ++a)
	{
		if (MatchmakingQueue[a].GameMode != ELobbyGameMode::Squad) continue;
		if (MatchmakingQueue[a].GetPlayerCount() != 2) continue;

		for (int32 b = 0; b < MatchmakingQueue.Num(); ++b)
		{
			if (b == a) continue;
			if (MatchmakingQueue[b].GameMode != ELobbyGameMode::Squad) continue;
			if (MatchmakingQueue[b].GetPlayerCount() != 1) continue;
			if (MatchmakingQueue[a].SelectedMapKey != MatchmakingQueue[b].SelectedMapKey) continue;

			TArray<FMatchmakingQueueEntry> Matched;
			Matched.Add(MatchmakingQueue[a]);
			Matched.Add(MatchmakingQueue[b]);
			auto ReassignedMap = ResolveHeroDuplication(Matched);
			StartMatchCountdown(MoveTemp(Matched), MoveTemp(ReassignedMap));
			return true;
		}
	}

	// Pass 3: DUO 1인 + 1인 조합 — 같은 MapKey + 같은 GameMode
	for (int32 a = 0; a < MatchmakingQueue.Num(); ++a)
	{
		if (MatchmakingQueue[a].GameMode != ELobbyGameMode::Duo) continue;
		if (MatchmakingQueue[a].GetPlayerCount() != 1) continue;

		for (int32 b = a + 1; b < MatchmakingQueue.Num(); ++b)
		{
			if (MatchmakingQueue[b].GameMode != ELobbyGameMode::Duo) continue;
			if (MatchmakingQueue[b].GetPlayerCount() != 1) continue;
			if (MatchmakingQueue[a].SelectedMapKey != MatchmakingQueue[b].SelectedMapKey) continue;

			TArray<FMatchmakingQueueEntry> Matched;
			Matched.Add(MatchmakingQueue[a]);
			Matched.Add(MatchmakingQueue[b]);
			auto ReassignedMap = ResolveHeroDuplication(Matched);
			StartMatchCountdown(MoveTemp(Matched), MoveTemp(ReassignedMap));
			return true;
		}
	}

	// Pass 4: SQUAD 1인 + 1인 + 1인 조합 — 같은 MapKey + 같은 GameMode
	TArray<int32> SquadSoloIndices;
	for (int32 i = 0; i < MatchmakingQueue.Num(); ++i)
	{
		if (MatchmakingQueue[i].GameMode == ELobbyGameMode::Squad && MatchmakingQueue[i].GetPlayerCount() == 1)
		{
			SquadSoloIndices.Add(i);
		}
	}

	if (SquadSoloIndices.Num() >= 3)
	{
		for (int32 a = 0; a < SquadSoloIndices.Num() - 2; ++a)
		{
			for (int32 b = a + 1; b < SquadSoloIndices.Num() - 1; ++b)
			{
				if (MatchmakingQueue[SquadSoloIndices[a]].SelectedMapKey != MatchmakingQueue[SquadSoloIndices[b]].SelectedMapKey)
				{
					continue;
				}
				for (int32 c = b + 1; c < SquadSoloIndices.Num(); ++c)
				{
					if (MatchmakingQueue[SquadSoloIndices[a]].SelectedMapKey != MatchmakingQueue[SquadSoloIndices[c]].SelectedMapKey)
					{
						continue;
					}
					TArray<FMatchmakingQueueEntry> Matched;
					Matched.Add(MatchmakingQueue[SquadSoloIndices[a]]);
					Matched.Add(MatchmakingQueue[SquadSoloIndices[b]]);
					Matched.Add(MatchmakingQueue[SquadSoloIndices[c]]);
					auto ReassignedMap = ResolveHeroDuplication(Matched);
					StartMatchCountdown(MoveTemp(Matched), MoveTemp(ReassignedMap));
					return true;
				}
			}
		}
	}

	return false;
}

void AHellunaLobbyGameMode::ExecuteMatchedDeploy(const TArray<FMatchmakingQueueEntry>& Matched, bool bRequeueOnFailure)
{
	const bool bUseSafeMatchedDeployFlow = FPlatformTime::Cycles64() >= 0;
	if (bUseSafeMatchedDeployFlow)
	{
		UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] ExecuteMatchedDeploy | Entries=%d | RequeueOnFailure=%s"),
			Matched.Num(), bRequeueOnFailure ? TEXT("true") : TEXT("false"));

		TArray<FString> MatchedPlayerIds;
		for (const FMatchmakingQueueEntry& Entry : Matched)
		{
			MatchedPlayerIds.Append(Entry.PlayerIds);
		}

		auto FailMatchedDeploy = [this, &Matched, &MatchedPlayerIds, bRequeueOnFailure](const FString& ErrorMessage)
		{
			SendMatchmakingErrorToPlayers(MatchedPlayerIds, ErrorMessage);
			if (bRequeueOnFailure)
			{
				RequeueMatchEntries(Matched, FString(), nullptr, false);
			}
		};

		if (Matched.Num() == 0)
		{
			return;
		}

		const FString MapKey = Matched[0].SelectedMapKey.IsEmpty() ? DefaultMapKey : Matched[0].SelectedMapKey;
		if (!IsValidConfiguredMapKey(MapKey))
		{
			FailMatchedDeploy(TEXT("유효하지 않은 맵 설정입니다."));
			return;
		}

		FGameChannelInfo EmptyChannel;
		if (!FindEmptyChannelForMap(MapKey, EmptyChannel))
		{
			if (!GameServerManager)
			{
				FailMatchedDeploy(TEXT("게임 서버 관리자가 준비되지 않았습니다."));
				return;
			}

			const FString MapPath = GetMapPathByKey(MapKey);
			if (MapPath.IsEmpty())
			{
				FailMatchedDeploy(TEXT("맵 경로를 확인할 수 없습니다."));
				return;
			}

			FGameChannelInfo AnyEmptyChannel;
			if (FindEmptyChannel(AnyEmptyChannel))
			{
				const int32 RespawnedPort = GameServerManager->RespawnGameServer(AnyEmptyChannel.Port, MapPath);
				if (RespawnedPort > 0)
				{
					MarkChannelAsPendingDeploy(RespawnedPort);
					WaitAndDeploy(RespawnedPort, Matched, MapKey, bRequeueOnFailure);
					return;
				}
			}

			const int32 SpawnedPort = GameServerManager->SpawnGameServer(MapPath);
			if (SpawnedPort < 0)
			{
				FailMatchedDeploy(TEXT("게임 서버 스폰에 실패했습니다."));
				return;
			}

			MarkChannelAsPendingDeploy(SpawnedPort);
			WaitAndDeploy(SpawnedPort, Matched, MapKey, bRequeueOnFailure);
			return;
		}

		MarkChannelAsPendingDeploy(EmptyChannel.Port);

		TArray<FString> PreparedPlayerIds;
		for (const FMatchmakingQueueEntry& Entry : Matched)
		{
			for (int32 Idx = 0; Idx < Entry.PlayerIds.Num(); ++Idx)
			{
				const FString& PlayerId = Entry.PlayerIds[Idx];
				const int32 HeroType = Entry.HeroTypes.IsValidIndex(Idx) ? Entry.HeroTypes[Idx] : HeroTypeToIndex(EHellunaHeroType::None);

				const TWeakObjectPtr<AHellunaLobbyController>* PCPtr = PlayerIdToControllerMap.Find(PlayerId);
				if (!PCPtr || !PCPtr->IsValid())
				{
					RollbackDeployStateForPlayers(PreparedPlayerIds, EmptyChannel.Port);
					FailMatchedDeploy(TEXT("로비 컨트롤러가 유효하지 않아 매칭 출격을 중단했습니다."));
					return;
				}

				FString PersistError;
				if (!PersistDeployDataForPlayer(PCPtr->Get(), PlayerId, HeroType, EmptyChannel.Port, PersistError))
				{
					RollbackDeployStateForPlayer(PlayerId, EmptyChannel.Port);
					RollbackDeployStateForPlayers(PreparedPlayerIds, EmptyChannel.Port);
					FailMatchedDeploy(PersistError);
					return;
				}

				PreparedPlayerIds.Add(PlayerId);
			}
		}

		for (const FMatchmakingQueueEntry& Entry : Matched)
		{
			for (const FString& PlayerId : Entry.PlayerIds)
			{
				const TWeakObjectPtr<AHellunaLobbyController>* PCPtr = PlayerIdToControllerMap.Find(PlayerId);
				if (!PCPtr || !PCPtr->IsValid())
				{
					continue;
				}

				AHellunaLobbyController* MemberPC = PCPtr->Get();
				MemberPC->SetDeployInProgress(true);
				MemberPC->Client_ExecutePartyDeploy(EmptyChannel.Port);
			}
		}

		for (const FMatchmakingQueueEntry& Entry : Matched)
		{
			RemoveQueueEntry(Entry.EntryId);
		}

		return;
	}

	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] [Phase15] ExecuteMatchedDeploy | Entries=%d"), Matched.Num());

	// [Phase 16] 맵 키 추출 (첫 엔트리 기준)
	const FString MapKey = Matched.Num() > 0 ? Matched[0].SelectedMapKey : DefaultMapKey;

	// Step 1: 해당 맵의 빈 채널 검색
	FGameChannelInfo EmptyChannel;
	if (FindEmptyChannelForMap(MapKey, EmptyChannel))
	{
		// 이미 실행 중인 빈 서버 발견 → 즉시 Deploy
		MarkChannelAsPendingDeploy(EmptyChannel.Port);
		UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] [Phase16] 기존 빈 채널 배정 | Port=%d | Map=%s"), EmptyChannel.Port, *MapKey);
	}
	else
	{
		// [Phase 16/19] 빈 채널 없음 → 맵 전환 or 동적 서버 스폰
		if (!GameServerManager)
		{
			UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyGM] [Phase16] GameServerManager 없음!"));
			for (const FMatchmakingQueueEntry& Entry : Matched)
			{
				for (const FString& PId : Entry.PlayerIds)
				{
					auto* PCPtr = PlayerIdToControllerMap.Find(PId);
					if (PCPtr && PCPtr->IsValid())
					{
						(*PCPtr)->Client_MatchmakingError(TEXT("서버 관리자 초기화 오류"));
					}
				}
			}
			return;
		}

		const FString MapPath = GetMapPathByKey(MapKey);
		if (MapPath.IsEmpty())
		{
			UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyGM] [Phase16] 맵 경로를 찾을 수 없음 | MapKey=%s"), *MapKey);
			for (const FMatchmakingQueueEntry& Entry : Matched)
			{
				for (const FString& PId : Entry.PlayerIds)
				{
					auto* PCPtr = PlayerIdToControllerMap.Find(PId);
					if (PCPtr && PCPtr->IsValid())
					{
						(*PCPtr)->Client_MatchmakingError(TEXT("맵 설정 오류"));
					}
				}
			}
			return;
		}

		// [Phase 19] 아무 빈 서버 찾기 → 종료 후 같은 포트에 새 맵으로 재스폰
		// (ServerTravel은 UE 5.7 World Partition 크래시 유발 → 프로세스 재시작 방식)
		FGameChannelInfo AnyEmptyChannel;
		if (FindEmptyChannel(AnyEmptyChannel))
		{
			const int32 RespawnedPort = GameServerManager->RespawnGameServer(AnyEmptyChannel.Port, MapPath);
			if (RespawnedPort > 0)
			{
				MarkChannelAsPendingDeploy(RespawnedPort);
				UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] [Phase19] 빈 서버 재스폰 | Port=%d | MapPath=%s"),
					RespawnedPort, *MapPath);

				for (const FMatchmakingQueueEntry& Entry : Matched)
				{
					RemoveQueueEntry(Entry.EntryId);
				}

				TArray<FMatchmakingQueueEntry> MatchedCopy = Matched;
				WaitAndDeploy(RespawnedPort, MoveTemp(MatchedCopy), MapKey);
				return;
			}
			// RespawnGameServer 실패 시 아래로 fallthrough → 새 포트에 스폰
			UE_LOG(LogHellunaLobby, Warning, TEXT("[LobbyGM] [Phase19] RespawnGameServer 실패 → 새 프로세스 스폰 시도"));
		}

		// [Phase 16] 빈 서버 없거나 재스폰 실패 → 새 프로세스 스폰
		const int32 SpawnedPort = GameServerManager->SpawnGameServer(MapPath);
		if (SpawnedPort < 0)
		{
			UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyGM] [Phase16] 서버 스폰 실패 | MapKey=%s"), *MapKey);
			for (const FMatchmakingQueueEntry& Entry : Matched)
			{
				for (const FString& PId : Entry.PlayerIds)
				{
					auto* PCPtr = PlayerIdToControllerMap.Find(PId);
					if (PCPtr && PCPtr->IsValid())
					{
						(*PCPtr)->Client_MatchmakingError(TEXT("서버 용량 초과 또는 스폰 실패"));
					}
				}
			}
			return;
		}

		MarkChannelAsPendingDeploy(SpawnedPort);
		UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] [Phase16] 서버 스폰 시작 → WaitAndDeploy | Port=%d"), SpawnedPort);

		// 큐에서 제거 (대기 중 재매칭 방지)
		for (const FMatchmakingQueueEntry& Entry : Matched)
		{
			RemoveQueueEntry(Entry.EntryId);
		}

		// 비동기 대기 시작
		TArray<FMatchmakingQueueEntry> MatchedCopy = Matched;
		WaitAndDeploy(SpawnedPort, MoveTemp(MatchedCopy), MapKey);
		return;
	}

	// 매칭된 전원의 EntryId 수집 (큐 제거용)
	TArray<int32> EntryIdsToRemove;

	// Step 2: 전원 Save + Deploy
	for (const FMatchmakingQueueEntry& Entry : Matched)
	{
		EntryIdsToRemove.Add(Entry.EntryId);

		for (int32 Idx = 0; Idx < Entry.PlayerIds.Num(); ++Idx)
		{
			const FString& PId = Entry.PlayerIds[Idx];
			const int32 HeroType = Entry.HeroTypes.IsValidIndex(Idx) ? Entry.HeroTypes[Idx] : 3;

			auto* PCPtr = PlayerIdToControllerMap.Find(PId);
			if (!PCPtr || !PCPtr->IsValid())
			{
				UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyGM] [Phase15] Controller 없음 | PlayerId=%s"), *PId);
				continue;
			}

			AHellunaLobbyController* MemberPC = PCPtr->Get();
			UInv_InventoryComponent* LoadoutComp = MemberPC->GetLoadoutComponent();
			UInv_InventoryComponent* StashComp = MemberPC->GetStashComponent();

			if (SQLiteSubsystem && SQLiteSubsystem->IsDatabaseReady())
			{
				// Loadout → ExportToFile → Stash (Fix29-C 순서)
				if (LoadoutComp)
				{
					TArray<FInv_SavedItemData> LoadoutItems = LoadoutComp->CollectInventoryDataForSave();
					if (LoadoutItems.Num() > 0)
					{
						SQLiteSubsystem->SavePlayerLoadout(PId, LoadoutItems);
						SQLiteSubsystem->ExportLoadoutToFile(PId, LoadoutItems, HeroType);
					}
				}
				if (StashComp)
				{
					TArray<FInv_SavedItemData> StashItems = StashComp->CollectInventoryDataForSave();
					SQLiteSubsystem->SavePlayerStash(PId, StashItems);
				}

				// 출격 상태 설정 (크래시 복구 + 재참가용)
				SQLiteSubsystem->SetPlayerDeployedWithPort(PId, true, EmptyChannel.Port, HeroType);
			}
		}
	}

	// Step 3: 전원 Client Deploy RPC
	for (const FMatchmakingQueueEntry& Entry : Matched)
	{
		for (const FString& PId : Entry.PlayerIds)
		{
			auto* PCPtr = PlayerIdToControllerMap.Find(PId);
			if (!PCPtr || !PCPtr->IsValid())
			{
				continue;
			}

			AHellunaLobbyController* MemberPC = PCPtr->Get();
			MemberPC->SetDeployInProgress(true);
			MemberPC->Client_ExecutePartyDeploy(EmptyChannel.Port);

			UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] [Phase15] Client_ExecutePartyDeploy 전송 | PlayerId=%s | Port=%d"),
				*PId, EmptyChannel.Port);
		}
	}

	// Step 4: 큐에서 제거
	for (int32 EId : EntryIdsToRemove)
	{
		RemoveQueueEntry(EId);
	}

	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] [Phase15] ExecuteMatchedDeploy 완료 | Port=%d | QueueRemaining=%d"),
		EmptyChannel.Port, MatchmakingQueue.Num());
}

// ════════════════════════════════════════════════════════════════════════════════
// [Phase 16] WaitAndDeploy — 서버 스폰 후 준비 대기
// ════════════════════════════════════════════════════════════════════════════════

void AHellunaLobbyGameMode::WaitAndDeploy(int32 Port, TArray<FMatchmakingQueueEntry> Matched, const FString& MapKey, bool bRequeueOnFailure)
{
	const bool bUseCrashFixedWaitAndDeployFlow = FPlatformTime::Cycles64() >= 0;
	if (bUseCrashFixedWaitAndDeployFlow)
	{
		UWorld* World = GetWorld();
		if (!World)
		{
			return;
		}

		const double StartTime = FPlatformTime::Seconds();
		constexpr double TimeoutSeconds = 30.0;
		constexpr float PollInterval = 0.5f;
		const FString ResolvedMapPath = GetMapPathByKey(MapKey);
		const FString ExpectedMapIdentifier = NormalizeLobbyMapIdentifier(ResolvedMapPath.IsEmpty() ? MapKey : ResolvedMapPath);

		FTimerHandle& TimerHandle = WaitAndDeployTimers.FindOrAdd(Port);
		TWeakObjectPtr<AHellunaLobbyGameMode> WeakThis(this);

		World->GetTimerManager().SetTimer(
			TimerHandle,
			[WeakThis, Port, Matched = MoveTemp(Matched), StartTime, TimeoutSeconds, MapKey, ExpectedMapIdentifier, bRequeueOnFailure]() mutable
			{
				AHellunaLobbyGameMode* Self = WeakThis.Get();
				if (!Self)
				{
					return;
				}

				UWorld* InnerWorld = Self->GetWorld();
				if (!InnerWorld)
				{
					Self->WaitAndDeployTimers.Remove(Port);
					Self->PendingDeployTimers.Remove(Port);
					Self->PendingDeployChannels.Remove(Port);
					return;
				}

				auto ScheduleWaitCleanup = [WeakThis, Port]()
				{
					AHellunaLobbyGameMode* CleanupSelf = WeakThis.Get();
					if (!CleanupSelf)
					{
						return;
					}

					UWorld* CleanupWorld = CleanupSelf->GetWorld();
					if (!CleanupWorld)
					{
						CleanupSelf->WaitAndDeployTimers.Remove(Port);
						CleanupSelf->PendingDeployTimers.Remove(Port);
						CleanupSelf->PendingDeployChannels.Remove(Port);
						return;
					}

					// Clear the looping timer after the current callback returns.
					CleanupWorld->GetTimerManager().SetTimerForNextTick(
						[WeakThis, Port]()
						{
							AHellunaLobbyGameMode* NextTickSelf = WeakThis.Get();
							if (!NextTickSelf)
							{
								return;
							}

							if (UWorld* NextTickWorld = NextTickSelf->GetWorld())
							{
								if (FTimerHandle* WaitHandle = NextTickSelf->WaitAndDeployTimers.Find(Port))
								{
									NextTickWorld->GetTimerManager().ClearTimer(*WaitHandle);
								}

								if (FTimerHandle* PendingHandle = NextTickSelf->PendingDeployTimers.Find(Port))
								{
									NextTickWorld->GetTimerManager().ClearTimer(*PendingHandle);
								}
							}

							NextTickSelf->WaitAndDeployTimers.Remove(Port);
							NextTickSelf->PendingDeployTimers.Remove(Port);
							NextTickSelf->PendingDeployChannels.Remove(Port);
						});
				};

				auto HandleFailure = [Self, Port, bRequeueOnFailure](const TArray<FMatchmakingQueueEntry>& FailedMatched, const FString& ErrorMessage)
				{
					TArray<FString> PlayerIds;
					for (const FMatchmakingQueueEntry& Entry : FailedMatched)
					{
						PlayerIds.Append(Entry.PlayerIds);
					}

					const bool bIsSoloDeployFailure =
						!bRequeueOnFailure &&
						FailedMatched.Num() == 1 &&
						FailedMatched[0].EntryId == 0 &&
						PlayerIds.Num() == 1;

					if (bIsSoloDeployFailure)
					{
						const TWeakObjectPtr<AHellunaLobbyController>* PCPtr = Self->PlayerIdToControllerMap.Find(PlayerIds[0]);
						if (PCPtr && PCPtr->IsValid())
						{
							(*PCPtr)->Client_DeployFailed(ErrorMessage);
						}
					}
					else
					{
						Self->SendMatchmakingErrorToPlayers(PlayerIds, ErrorMessage);
					}

					if (bRequeueOnFailure)
					{
						Self->RequeueMatchEntries(FailedMatched, FString(), nullptr, false);
					}
					else
					{
						Self->RollbackDeployStateForPlayers(PlayerIds, Port);
					}
				};

				const double Elapsed = FPlatformTime::Seconds() - StartTime;
				if (Elapsed > TimeoutSeconds)
				{
					TArray<FMatchmakingQueueEntry> FailedMatched = MoveTemp(Matched);
					ScheduleWaitCleanup();
					HandleFailure(FailedMatched, TEXT("게임 서버 준비 시간이 초과되었습니다."));
					return;
				}

				if (!Self->GameServerManager)
				{
					return;
				}

				const bool bReady = MapKey.IsEmpty()
					? Self->GameServerManager->IsServerReady(Port)
					: Self->GameServerManager->IsServerReadyForMap(
						Port,
						ExpectedMapIdentifier.IsEmpty() ? MapKey : ExpectedMapIdentifier);
				if (!bReady)
				{
					return;
				}

				TArray<FMatchmakingQueueEntry> DeployMatched = MoveTemp(Matched);
				ScheduleWaitCleanup();

				InnerWorld->GetTimerManager().SetTimerForNextTick(
					[WeakThis, DeployPort = Port, DeployMatched = MoveTemp(DeployMatched), bRequeueOnFailure]() mutable
					{
						AHellunaLobbyGameMode* GM = WeakThis.Get();
						if (!GM)
						{
							return;
						}

						TArray<FString> MatchedPlayerIds;
						for (const FMatchmakingQueueEntry& Entry : DeployMatched)
						{
							MatchedPlayerIds.Append(Entry.PlayerIds);
						}

						TArray<FString> PreparedPlayerIds;
						for (const FMatchmakingQueueEntry& Entry : DeployMatched)
						{
							for (int32 Idx = 0; Idx < Entry.PlayerIds.Num(); ++Idx)
							{
								const FString& PlayerId = Entry.PlayerIds[Idx];
								const int32 HeroType = Entry.HeroTypes.IsValidIndex(Idx)
									? Entry.HeroTypes[Idx]
									: HeroTypeToIndex(EHellunaHeroType::None);

								const TWeakObjectPtr<AHellunaLobbyController>* PCPtr = GM->PlayerIdToControllerMap.Find(PlayerId);
								if (!PCPtr || !PCPtr->IsValid())
								{
									GM->RollbackDeployStateForPlayers(PreparedPlayerIds, DeployPort);
									GM->SendMatchmakingErrorToPlayers(MatchedPlayerIds, TEXT("로비 컨트롤러가 유효하지 않아 출격을 중단했습니다."));
									if (bRequeueOnFailure)
									{
										GM->RequeueMatchEntries(DeployMatched, FString(), nullptr, false);
									}
									return;
								}

								FString PersistError;
								if (!GM->PersistDeployDataForPlayer(PCPtr->Get(), PlayerId, HeroType, DeployPort, PersistError))
								{
									GM->RollbackDeployStateForPlayer(PlayerId, DeployPort);
									GM->RollbackDeployStateForPlayers(PreparedPlayerIds, DeployPort);
									GM->SendMatchmakingErrorToPlayers(MatchedPlayerIds, PersistError);
									if (bRequeueOnFailure)
									{
										GM->RequeueMatchEntries(DeployMatched, FString(), nullptr, false);
									}
									return;
								}

								PreparedPlayerIds.Add(PlayerId);
							}
						}

						for (const FMatchmakingQueueEntry& Entry : DeployMatched)
						{
							for (const FString& PlayerId : Entry.PlayerIds)
							{
								const TWeakObjectPtr<AHellunaLobbyController>* PCPtr = GM->PlayerIdToControllerMap.Find(PlayerId);
								if (!PCPtr || !PCPtr->IsValid())
								{
									continue;
								}

								AHellunaLobbyController* MemberPC = PCPtr->Get();
								MemberPC->SetDeployInProgress(true);
								MemberPC->Client_ExecutePartyDeploy(DeployPort);
							}
						}
					});
			},
			PollInterval,
			true);
		return;
	}

	const bool bUseSafeWaitAndDeployFlow = FPlatformTime::Cycles64() >= 0;
	if (bUseSafeWaitAndDeployFlow)
	{
		UWorld* World = GetWorld();
		if (!World)
		{
			return;
		}

		const double StartTime = FPlatformTime::Seconds();
		constexpr double TimeoutSeconds = 30.0;
		constexpr float PollInterval = 0.5f;

		FTimerHandle& TimerHandle = WaitAndDeployTimers.FindOrAdd(Port);
		TWeakObjectPtr<AHellunaLobbyGameMode> WeakThis(this);

		World->GetTimerManager().SetTimer(TimerHandle,
			[WeakThis, Port, Matched = MoveTemp(Matched), StartTime, TimeoutSeconds, MapKey, bRequeueOnFailure]() mutable
			{
				if (!WeakThis.IsValid())
				{
					return;
				}

				AHellunaLobbyGameMode* Self = WeakThis.Get();
				const double Elapsed = FPlatformTime::Seconds() - StartTime;

				auto ClearPendingState = [Self, Port]()
				{
					if (UWorld* InnerWorld = Self->GetWorld())
					{
						if (FTimerHandle* TH = Self->WaitAndDeployTimers.Find(Port))
						{
							InnerWorld->GetTimerManager().ClearTimer(*TH);
						}
					}

					Self->WaitAndDeployTimers.Remove(Port);
					Self->PendingDeployChannels.Remove(Port);
				};

				auto HandleFailure = [Self, &Matched, Port, bRequeueOnFailure](const FString& ErrorMessage)
				{
					TArray<FString> PlayerIds;
					for (const FMatchmakingQueueEntry& Entry : Matched)
					{
						PlayerIds.Append(Entry.PlayerIds);
					}

					const bool bIsSoloDeployFailure =
						!bRequeueOnFailure &&
						Matched.Num() == 1 &&
						Matched[0].EntryId == 0 &&
						PlayerIds.Num() == 1;

					if (bIsSoloDeployFailure)
					{
						const TWeakObjectPtr<AHellunaLobbyController>* PCPtr = Self->PlayerIdToControllerMap.Find(PlayerIds[0]);
						if (PCPtr && PCPtr->IsValid())
						{
							(*PCPtr)->Client_DeployFailed(ErrorMessage);
						}
					}
					else
					{
						Self->SendMatchmakingErrorToPlayers(PlayerIds, ErrorMessage);
					}

					if (bRequeueOnFailure)
					{
						Self->RequeueMatchEntries(Matched, FString(), nullptr, false);
					}
					else
					{
						Self->RollbackDeployStateForPlayers(PlayerIds, Port);
					}
				};

				if (Elapsed > TimeoutSeconds)
				{
					ClearPendingState();
					HandleFailure(TEXT("게임 서버 준비 시간이 초과되었습니다."));
					return;
				}

				if (!Self->GameServerManager)
				{
					return;
				}

				const bool bReady = MapKey.IsEmpty()
					? Self->GameServerManager->IsServerReady(Port)
					: Self->GameServerManager->IsServerReadyForMap(Port, MapKey);
				if (!bReady)
				{
					return;
				}

				ClearPendingState();

				TWeakObjectPtr<AHellunaLobbyGameMode> DeployWeakSelf = WeakThis;
				const int32 DeployPort = Port;
				if (UWorld* InnerWorld = Self->GetWorld())
				{
					InnerWorld->GetTimerManager().SetTimerForNextTick(
						[DeployWeakSelf, DeployPort, DeployMatched = MoveTemp(Matched), bRequeueOnFailure]() mutable
						{
							if (!DeployWeakSelf.IsValid())
							{
								return;
							}

							AHellunaLobbyGameMode* GM = DeployWeakSelf.Get();
							TArray<FString> MatchedPlayerIds;
							for (const FMatchmakingQueueEntry& Entry : DeployMatched)
							{
								MatchedPlayerIds.Append(Entry.PlayerIds);
							}

							TArray<FString> PreparedPlayerIds;
							for (const FMatchmakingQueueEntry& Entry : DeployMatched)
							{
								for (int32 Idx = 0; Idx < Entry.PlayerIds.Num(); ++Idx)
								{
									const FString& PlayerId = Entry.PlayerIds[Idx];
									const int32 HeroType = Entry.HeroTypes.IsValidIndex(Idx) ? Entry.HeroTypes[Idx] : HeroTypeToIndex(EHellunaHeroType::None);

									const TWeakObjectPtr<AHellunaLobbyController>* PCPtr = GM->PlayerIdToControllerMap.Find(PlayerId);
									if (!PCPtr || !PCPtr->IsValid())
									{
										GM->RollbackDeployStateForPlayers(PreparedPlayerIds, DeployPort);
										GM->SendMatchmakingErrorToPlayers(MatchedPlayerIds, TEXT("로비 컨트롤러가 유효하지 않아 출격을 중단했습니다."));
										if (bRequeueOnFailure)
										{
											GM->RequeueMatchEntries(DeployMatched, FString(), nullptr, false);
										}
										return;
									}

									FString PersistError;
									if (!GM->PersistDeployDataForPlayer(PCPtr->Get(), PlayerId, HeroType, DeployPort, PersistError))
									{
										GM->RollbackDeployStateForPlayer(PlayerId, DeployPort);
										GM->RollbackDeployStateForPlayers(PreparedPlayerIds, DeployPort);
										GM->SendMatchmakingErrorToPlayers(MatchedPlayerIds, PersistError);
										if (bRequeueOnFailure)
										{
											GM->RequeueMatchEntries(DeployMatched, FString(), nullptr, false);
										}
										return;
									}

									PreparedPlayerIds.Add(PlayerId);
								}
							}

							for (const FMatchmakingQueueEntry& Entry : DeployMatched)
							{
								for (const FString& PlayerId : Entry.PlayerIds)
								{
									const TWeakObjectPtr<AHellunaLobbyController>* PCPtr = GM->PlayerIdToControllerMap.Find(PlayerId);
									if (!PCPtr || !PCPtr->IsValid())
									{
										continue;
									}

									AHellunaLobbyController* MemberPC = PCPtr->Get();
									MemberPC->SetDeployInProgress(true);
									MemberPC->Client_ExecutePartyDeploy(DeployPort);
								}
							}
						});
				}
			},
			PollInterval, true);
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const double StartTime = FPlatformTime::Seconds();
	constexpr double TimeoutSeconds = 30.0;
	constexpr float PollInterval = 0.5f;

	FTimerHandle& TimerHandle = WaitAndDeployTimers.FindOrAdd(Port);
	TWeakObjectPtr<AHellunaLobbyGameMode> WeakThis(this);

	// [Phase 19] MapKey 캡처 — 비어있으면 맵 무관 체크 (기존 동작), 있으면 맵 일치 체크
	World->GetTimerManager().SetTimer(TimerHandle,
		[WeakThis, Port, Matched = MoveTemp(Matched), StartTime, TimeoutSeconds, MapKey]() mutable
		{
			if (!WeakThis.IsValid())
			{
				return;
			}

			AHellunaLobbyGameMode* Self = WeakThis.Get();

			// 타임아웃 체크
			const double Elapsed = FPlatformTime::Seconds() - StartTime;
			if (Elapsed > TimeoutSeconds)
			{
				UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyGM] [Phase16] WaitAndDeploy 타임아웃 | Port=%d | %.1f초"),
					Port, Elapsed);

				// [Fix51-B] ClearTimer 전에 데이터를 먼저 꺼냄 (ClearTimer가 람다 캡처를 즉시 파괴할 수 있음)
				TArray<FMatchmakingQueueEntry> TimeoutMatched = MoveTemp(Matched);

				// 타이머 해제 (Matched는 이미 이동됨 → 빈 배열 → 소멸 안전)
				if (UWorld* W = Self->GetWorld())
				{
					FTimerHandle* TH = Self->WaitAndDeployTimers.Find(Port);
					if (TH)
					{
						W->GetTimerManager().ClearTimer(*TH);
					}
				}
				Self->WaitAndDeployTimers.Remove(Port);
				Self->PendingDeployChannels.Remove(Port);

				// 에러 전송 + 큐 복구 (안전한 로컬 복사본 사용)
				for (const FMatchmakingQueueEntry& Entry : TimeoutMatched)
				{
					for (const FString& PId : Entry.PlayerIds)
					{
						auto* PCPtr = Self->PlayerIdToControllerMap.Find(PId);
						if (PCPtr && PCPtr->IsValid())
						{
							(*PCPtr)->Client_MatchmakingError(TEXT("서버 시작 시간 초과. 다시 시도해주세요."));
						}
					}
				}

				Self->RequeueMatchEntries(TimeoutMatched);
				return;
			}

			// [Phase 19] 서버 준비 확인 — MapKey가 있으면 맵 일치까지 체크
			if (!Self->GameServerManager)
			{
				return;
			}
			const bool bReady = MapKey.IsEmpty()
				? Self->GameServerManager->IsServerReady(Port)
				: Self->GameServerManager->IsServerReadyForMap(Port, MapKey);
			if (!bReady)
			{
				return; // 아직 미준비 → 다음 폴링 대기
			}

			UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] [Phase16] 서버 준비 완료 | Port=%d | %.1f초 대기"), Port, Elapsed);

			// [Fix51-B] ClearTimer 전에 데이터를 먼저 꺼냄 (ClearTimer가 람다 캡처를 즉시 파괴할 수 있음)
			TArray<FMatchmakingQueueEntry> DeployMatched = MoveTemp(Matched);
			TWeakObjectPtr<AHellunaLobbyGameMode> DeployWeakSelf = WeakThis;
			const int32 DeployPort = Port;

			// 타이머 해제 (Matched는 이미 이동됨 → 빈 배열 → 소멸 안전)
			if (UWorld* W = Self->GetWorld())
			{
				FTimerHandle* TH = Self->WaitAndDeployTimers.Find(Port);
				if (TH)
				{
					W->GetTimerManager().ClearTimer(*TH);
				}
			}
			Self->WaitAndDeployTimers.Remove(Port);


			if (UWorld* W = Self->GetWorld())
			{
				W->GetTimerManager().SetTimerForNextTick([DeployWeakSelf, DeployPort, DeployMatched = MoveTemp(DeployMatched)]()
				{
					if (!DeployWeakSelf.IsValid()) return;
					AHellunaLobbyGameMode* GM = DeployWeakSelf.Get();

					// 전원 Save + Deploy
					for (const FMatchmakingQueueEntry& Entry : DeployMatched)
					{
						for (int32 Idx = 0; Idx < Entry.PlayerIds.Num(); ++Idx)
						{
							const FString& PId = Entry.PlayerIds[Idx];
							const int32 HeroType = Entry.HeroTypes.IsValidIndex(Idx) ? Entry.HeroTypes[Idx] : 3;

							auto* PCPtr = GM->PlayerIdToControllerMap.Find(PId);
							if (!PCPtr || !PCPtr->IsValid())
							{
								UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyGM] [Phase16] Controller 없음 | PlayerId=%s"), *PId);
								continue;
							}

							AHellunaLobbyController* MemberPC = PCPtr->Get();
							UInv_InventoryComponent* LoadoutComp = MemberPC->GetLoadoutComponent();
							UInv_InventoryComponent* StashComp = MemberPC->GetStashComponent();

							if (GM->SQLiteSubsystem && GM->SQLiteSubsystem->IsDatabaseReady())
							{
								if (LoadoutComp)
								{
									TArray<FInv_SavedItemData> LoadoutItems = LoadoutComp->CollectInventoryDataForSave();
									if (LoadoutItems.Num() > 0)
									{
										GM->SQLiteSubsystem->SavePlayerLoadout(PId, LoadoutItems);
										GM->SQLiteSubsystem->ExportLoadoutToFile(PId, LoadoutItems, HeroType);
									}
								}
								if (StashComp)
								{
									TArray<FInv_SavedItemData> StashItems = StashComp->CollectInventoryDataForSave();
									GM->SQLiteSubsystem->SavePlayerStash(PId, StashItems);
								}

								GM->SQLiteSubsystem->SetPlayerDeployedWithPort(PId, true, DeployPort, HeroType);
							}
						}
					}

					// 전원 Client Deploy RPC
					for (const FMatchmakingQueueEntry& Entry : DeployMatched)
					{
						for (const FString& PId : Entry.PlayerIds)
						{
							auto* PCPtr = GM->PlayerIdToControllerMap.Find(PId);
							if (!PCPtr || !PCPtr->IsValid())
							{
								continue;
							}

							AHellunaLobbyController* MemberPC = PCPtr->Get();
							MemberPC->SetDeployInProgress(true);
							MemberPC->Client_ExecutePartyDeploy(DeployPort);

							UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] [Phase16] Client_ExecutePartyDeploy 전송 | PlayerId=%s | Port=%d"),
								*PId, DeployPort);
						}
					}
				});
			}
		},
		PollInterval, true // 반복
	);
}

// ════════════════════════════════════════════════════════════════════════════════
// [Phase 19] WriteMapSwitchCommand — 빈 서버에 맵 전환 커맨드 파일 작성
// ════════════════════════════════════════════════════════════════════════════════

void AHellunaLobbyGameMode::WriteMapSwitchCommand(int32 Port, const FString& MapPath)
{
	const FString CmdPath = FPaths::Combine(
		GetRegistryDirectoryPath(),
		FString::Printf(TEXT("command_%d.json"), Port));

	const FString Json = FString::Printf(
		TEXT("{\"command\":\"servertravel\",\"mapPath\":\"%s\",\"timestamp\":\"%s\"}"),
		*MapPath, *FDateTime::UtcNow().ToIso8601());

	FFileHelper::SaveStringToFile(Json, *CmdPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);

	UE_LOG(LogHellunaLobby, Log, TEXT("[Phase19] WriteMapSwitchCommand | Port=%d | MapPath=%s | Path=%s"),
		Port, *MapPath, *CmdPath);
}

void AHellunaLobbyGameMode::RemoveQueueEntry(int32 EntryId)
{
	for (int32 i = MatchmakingQueue.Num() - 1; i >= 0; --i)
	{
		if (MatchmakingQueue[i].EntryId == EntryId)
		{
			// PlayerToQueueEntryMap에서 제거
			for (const FString& PId : MatchmakingQueue[i].PlayerIds)
			{
				PlayerToQueueEntryMap.Remove(PId);
			}
			MatchmakingQueue.RemoveAt(i);
			break;
		}
	}
}

void AHellunaLobbyGameMode::EnqueueMatchmakingEntry(FMatchmakingQueueEntry Entry, bool bTryMatchImmediately)
{
	Entry.EntryId = NextQueueEntryId++;
	Entry.QueueEnterTime = FPlatformTime::Seconds();

	MatchmakingQueue.Add(Entry);
	for (const FString& PId : Entry.PlayerIds)
	{
		PlayerToQueueEntryMap.Add(PId, Entry.EntryId);
	}

	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] [Phase15] 큐 엔트리 추가 | EntryId=%d | PlayerCount=%d | QueueSize=%d"),
		Entry.EntryId, Entry.GetPlayerCount(), MatchmakingQueue.Num());

	if (bTryMatchImmediately)
	{
		TryFormMatch();
	}

	BroadcastMatchmakingStatus();

	if (UWorld* World = GetWorld())
	{
		if (!World->GetTimerManager().IsTimerActive(MatchmakingTickTimer))
		{
			World->GetTimerManager().SetTimer(MatchmakingTickTimer, this, &AHellunaLobbyGameMode::TickMatchmaking, 1.0f, true);
			UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] [Phase15] 매칭 틱 타이머 시작"));
		}
	}
}

void AHellunaLobbyGameMode::RequeueMatchEntries(
	const TArray<FMatchmakingQueueEntry>& Entries,
	const FString& ExcludedPlayerId,
	const TMap<FString, TPair<int32, int32>>* ReassignedHeroes,
	bool bTryMatchImmediately)
{
	for (const FMatchmakingQueueEntry& Entry : Entries)
	{
		FMatchmakingQueueEntry RequeueEntry = Entry;
		RequeueEntry.PlayerIds.Reset();
		RequeueEntry.HeroTypes.Reset();

		for (int32 Idx = 0; Idx < Entry.PlayerIds.Num(); ++Idx)
		{
			const FString& PId = Entry.PlayerIds[Idx];
			if (PId == ExcludedPlayerId)
			{
				continue;
			}
			const TWeakObjectPtr<AHellunaLobbyController>* PCPtr = PlayerIdToControllerMap.Find(PId);
			if (!PCPtr || !PCPtr->IsValid())
			{
				UE_LOG(LogHellunaLobby, Warning, TEXT("[LobbyGM] [Phase17] 재큐잉 스킵 | PlayerId=%s | Controller 없음"), *PId);
				continue;
			}

			int32 HeroType = Entry.HeroTypes.IsValidIndex(Idx) ? Entry.HeroTypes[Idx] : 3;
			if (ReassignedHeroes)
			{
				if (const TPair<int32, int32>* Pair = ReassignedHeroes->Find(PId))
				{
					HeroType = Pair->Key;
				}
			}

			RequeueEntry.PlayerIds.Add(PId);
			RequeueEntry.HeroTypes.Add(HeroType);
		}

		if (RequeueEntry.PlayerIds.Num() == 0)
		{
			continue;
		}

		if (RequeueEntry.PlayerIds.Num() <= 1)
		{
			RequeueEntry.PartyId = 0;
		}

		EnqueueMatchmakingEntry(MoveTemp(RequeueEntry), bTryMatchImmediately);
	}
}

bool AHellunaLobbyGameMode::ValidateMatchHeroDuplication(const TArray<FMatchmakingQueueEntry>& Entries) const
{
	TSet<int32> UsedHeroes;
	for (const FMatchmakingQueueEntry& Entry : Entries)
	{
		for (int32 HeroType : Entry.HeroTypes)
		{
			if (HeroType == 3) // None — 중복 체크 대상 아님
			{
				continue;
			}
			if (UsedHeroes.Contains(HeroType))
			{
				return false; // 중복 발견
			}
			UsedHeroes.Add(HeroType);
		}
	}
	return true; // 중복 없음
}

void AHellunaLobbyGameMode::UpdateQueueEntryHeroType(const FString& PlayerId, int32 NewHeroType)
{
	const int32* EntryIdPtr = PlayerToQueueEntryMap.Find(PlayerId);
	if (!EntryIdPtr)
	{
		return;
	}

	for (FMatchmakingQueueEntry& Entry : MatchmakingQueue)
	{
		if (Entry.EntryId == *EntryIdPtr)
		{
			for (int32 i = 0; i < Entry.PlayerIds.Num(); ++i)
			{
				if (Entry.PlayerIds[i] == PlayerId && Entry.HeroTypes.IsValidIndex(i))
				{
					Entry.HeroTypes[i] = NewHeroType;
					UE_LOG(LogHellunaLobby, Verbose, TEXT("[LobbyGM] [Phase15] 큐 영웅 갱신 | PlayerId=%s | HeroType=%d"), *PlayerId, NewHeroType);
					break;
				}
			}
			break;
		}
	}
}

FString AHellunaLobbyGameMode::CreateDebugLobbyPlayerId(APlayerController* LobbyPC) const
{
	if (const FString* ExistingId = ControllerToPlayerIdMap.Find(LobbyPC))
	{
		if (!ExistingId->IsEmpty())
		{
			return *ExistingId;
		}
	}

	return FString::Printf(TEXT("DEBUG_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
}

// ════════════════════════════════════════════════════════════════════════════════
// [Phase 17] 영웅 자동 재배정 + 매칭 카운트다운
// ════════════════════════════════════════════════════════════════════════════════

TMap<FString, TPair<int32, int32>> AHellunaLobbyGameMode::ResolveHeroDuplication(
	TArray<FMatchmakingQueueEntry>& Matched)
{
	TMap<FString, TPair<int32, int32>> ReassignedMap;
	TSet<int32> UsedHeroes;

	// 1차원 펼침: 모든 (PlayerId, HeroType, 엔트리인덱스, 플레이어인덱스)
	struct FPlayerRef
	{
		FString PlayerId;
		int32 HeroType;
		int32 EntryIndex;
		int32 PlayerIndex;
	};
	TArray<FPlayerRef> AllPlayers;
	TArray<FPlayerRef> PendingPlayers;

	for (int32 e = 0; e < Matched.Num(); ++e)
	{
		for (int32 p = 0; p < Matched[e].PlayerIds.Num(); ++p)
		{
			FPlayerRef Ref;
			Ref.PlayerId = Matched[e].PlayerIds[p];
			Ref.HeroType = Matched[e].HeroTypes.IsValidIndex(p) ? Matched[e].HeroTypes[p] : 3;
			Ref.EntryIndex = e;
			Ref.PlayerIndex = p;
			AllPlayers.Add(Ref);
		}
	}

	// Pass 1: 유효한 영웅 선착순 확정
	for (FPlayerRef& Ref : AllPlayers)
	{
		if (Ref.HeroType != 3 && !UsedHeroes.Contains(Ref.HeroType))
		{
			UsedHeroes.Add(Ref.HeroType);
		}
		else
		{
			PendingPlayers.Add(Ref);
		}
	}

	// Pass 2: 남은 영웅 풀 구성 + 셔플
	TArray<int32> RemainingHeroes;
	for (int32 h = 0; h < 3; ++h) // Lui=0, Luna=1, Liam=2
	{
		if (!UsedHeroes.Contains(h))
		{
			RemainingHeroes.Add(h);
		}
	}

	// Fisher-Yates 셔플
	for (int32 i = RemainingHeroes.Num() - 1; i > 0; --i)
	{
		const int32 j = FMath::RandRange(0, i);
		RemainingHeroes.Swap(i, j);
	}

	// Pass 3: 대기자에게 남은 영웅 배정
	for (int32 i = 0; i < PendingPlayers.Num() && i < RemainingHeroes.Num(); ++i)
	{
		const FPlayerRef& Ref = PendingPlayers[i];
		const int32 OldHero = Ref.HeroType;
		const int32 NewHero = RemainingHeroes[i];

		// Matched 배열 직접 수정
		if (Matched[Ref.EntryIndex].HeroTypes.IsValidIndex(Ref.PlayerIndex))
		{
			Matched[Ref.EntryIndex].HeroTypes[Ref.PlayerIndex] = NewHero;
		}

		ReassignedMap.Add(Ref.PlayerId, TPair<int32, int32>(OldHero, NewHero));

		UE_LOG(LogHellunaLobby, Log,
			TEXT("[LobbyGM] [Phase17] 영웅 재배정 | PlayerId=%s | %d -> %d"),
			*Ref.PlayerId, OldHero, NewHero);
	}

	return ReassignedMap;
}

void AHellunaLobbyGameMode::StartMatchCountdown(
	TArray<FMatchmakingQueueEntry> Matched,
	TMap<FString, TPair<int32, int32>> ReassignedHeroes)
{
	const int32 GroupId = NextMatchGroupId++;

	// 큐에서 제거
	for (const FMatchmakingQueueEntry& Entry : Matched)
	{
		RemoveQueueEntry(Entry.EntryId);
	}

	// PendingCountdownMatch 생성
	FPendingCountdownMatch PendingMatch;
	PendingMatch.MatchedEntries = MoveTemp(Matched);
	PendingMatch.ReassignedHeroes = MoveTemp(ReassignedHeroes);
	PendingMatch.RemainingSeconds = 5;
	PendingMatch.MatchGroupId = GroupId;

	// 전원에게 Client_MatchmakingFound RPC
	// MatchedMembers 구성 (FHellunaPartyMemberInfo 배열)
	TArray<FHellunaPartyMemberInfo> MatchedMembers;
	for (const FMatchmakingQueueEntry& Entry : PendingMatch.MatchedEntries)
	{
		for (int32 Idx = 0; Idx < Entry.PlayerIds.Num(); ++Idx)
		{
			FHellunaPartyMemberInfo MemberInfo;
			MemberInfo.PlayerId = Entry.PlayerIds[Idx];
			MemberInfo.DisplayName = Entry.PlayerIds[Idx];
			MemberInfo.SelectedHeroType = Entry.HeroTypes.IsValidIndex(Idx) ? Entry.HeroTypes[Idx] : 3;
			MemberInfo.Role = (MatchedMembers.Num() == 0) ? EHellunaPartyRole::Leader : EHellunaPartyRole::Member;
			MemberInfo.bIsReady = true;
			MatchedMembers.Add(MemberInfo);
		}
	}

	// 각 플레이어별 개인화된 FoundInfo 전송
	for (const FMatchmakingQueueEntry& Entry : PendingMatch.MatchedEntries)
	{
		for (const FString& PId : Entry.PlayerIds)
		{
			auto* PCPtr = PlayerIdToControllerMap.Find(PId);
			if (!PCPtr || !PCPtr->IsValid()) continue;

			FMatchmakingFoundInfo FoundInfo;
			FoundInfo.MatchedMembers = MatchedMembers;
			FoundInfo.CountdownSeconds = 5;

			// 재배정 정보 (수신자별 다름)
			if (auto* Pair = PendingMatch.ReassignedHeroes.Find(PId))
			{
				FoundInfo.bHeroWasReassigned = true;
				FoundInfo.OriginalHeroType = Pair->Key;
				FoundInfo.AssignedHeroType = Pair->Value;
			}

			(*PCPtr)->Client_MatchmakingFound(FoundInfo);
		}
	}

	// 1초 반복 카운트다운 타이머
	UWorld* World = GetWorld();
	if (World)
	{
		TWeakObjectPtr<AHellunaLobbyGameMode> WeakThis(this);
		World->GetTimerManager().SetTimer(PendingMatch.CountdownTimerHandle,
			[WeakThis, GroupId]()
			{
				if (WeakThis.IsValid())
				{
					WeakThis->TickMatchCountdown(GroupId);
				}
			},
			1.0f, true // 1초 반복
		);
	}

	PendingCountdownMatches.Add(MoveTemp(PendingMatch));

	UE_LOG(LogHellunaLobby, Log,
		TEXT("[LobbyGM] [Phase17] 카운트다운 시작 | GroupId=%d | Players=%d"),
		GroupId, MatchedMembers.Num());
}

void AHellunaLobbyGameMode::TickMatchCountdown(int32 MatchGroupId)
{
	FPendingCountdownMatch* PendingMatch = nullptr;
	for (FPendingCountdownMatch& PM : PendingCountdownMatches)
	{
		if (PM.MatchGroupId == MatchGroupId)
		{
			PendingMatch = &PM;
			break;
		}
	}
	if (!PendingMatch) return;

	PendingMatch->RemainingSeconds--;

	// 전원에게 카운트다운 RPC
	for (const FMatchmakingQueueEntry& Entry : PendingMatch->MatchedEntries)
	{
		for (const FString& PId : Entry.PlayerIds)
		{
			auto* PCPtr = PlayerIdToControllerMap.Find(PId);
			if (PCPtr && PCPtr->IsValid())
			{
				(*PCPtr)->Client_MatchmakingCountdown(PendingMatch->RemainingSeconds);
			}
		}
	}

	if (PendingMatch->RemainingSeconds <= 0)
	{
		OnCountdownFinished(MatchGroupId);
	}
}

void AHellunaLobbyGameMode::OnCountdownFinished(int32 MatchGroupId)
{
	int32 FoundIndex = INDEX_NONE;
	for (int32 i = 0; i < PendingCountdownMatches.Num(); ++i)
	{
		if (PendingCountdownMatches[i].MatchGroupId == MatchGroupId)
		{
			FoundIndex = i;
			break;
		}
	}
	if (FoundIndex == INDEX_NONE) return;

	FPendingCountdownMatch PendingMatch = MoveTemp(PendingCountdownMatches[FoundIndex]);
	PendingCountdownMatches.RemoveAt(FoundIndex);

	// 타이머 해제
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PendingMatch.CountdownTimerHandle);
	}

	// 기존 ExecuteMatchedDeploy 호출
	ExecuteMatchedDeploy(PendingMatch.MatchedEntries, true);

	UE_LOG(LogHellunaLobby, Log,
		TEXT("[LobbyGM] [Phase17] 카운트다운 완료 -> Deploy | GroupId=%d"), MatchGroupId);
}

void AHellunaLobbyGameMode::HandleCountdownPlayerDisconnect(const FString& PlayerId)
{
	int32 FoundIndex = INDEX_NONE;
	for (int32 i = 0; i < PendingCountdownMatches.Num(); ++i)
	{
		for (const FMatchmakingQueueEntry& Entry : PendingCountdownMatches[i].MatchedEntries)
		{
			if (Entry.PlayerIds.Contains(PlayerId))
			{
				FoundIndex = i;
				break;
			}
		}
		if (FoundIndex != INDEX_NONE) break;
	}
	if (FoundIndex == INDEX_NONE) return;

	FPendingCountdownMatch PendingMatch = MoveTemp(PendingCountdownMatches[FoundIndex]);
	PendingCountdownMatches.RemoveAt(FoundIndex);

	// 타이머 해제
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PendingMatch.CountdownTimerHandle);
	}

	// 나머지 인원에게 취소 알림
	for (const FMatchmakingQueueEntry& Entry : PendingMatch.MatchedEntries)
	{
		for (int32 Idx = 0; Idx < Entry.PlayerIds.Num(); ++Idx)
		{
			const FString& PId = Entry.PlayerIds[Idx];
			if (PId == PlayerId) continue; // 이탈자 제외

			auto* PCPtr = PlayerIdToControllerMap.Find(PId);
			if (PCPtr && PCPtr->IsValid())
			{
				(*PCPtr)->Client_MatchmakingCancelled(TEXT("플레이어가 이탈하여 매칭이 취소되었습니다."));
			}
		}
	}

	RequeueMatchEntries(PendingMatch.MatchedEntries, PlayerId, &PendingMatch.ReassignedHeroes);

	UE_LOG(LogHellunaLobby, Log,
		TEXT("[LobbyGM] [Phase17] 카운트다운 취소 (이탈) | GroupId=%d | DisconnectedPlayer=%s"),
		PendingMatch.MatchGroupId, *PlayerId);
}