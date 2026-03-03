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
	// GameInstanceSubsystem이므로 게임 인스턴스 생존 기간 동안 유지됨
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

	// ── PlayerId 획득 ──
	// 📌 디버그 모드(bDebugSkipLogin=true)일 때는 고정 ID 사용
	//    이유: PIE에서 GetPlayerSaveId()는 매 세션마다 다른 랜덤 DEBUG_xxx를 반환
	//    → DebugSave로 저장한 데이터(PlayerId="DebugPlayer")와 절대 일치하지 않음
	//    → 테스트를 위해 고정 ID "DebugPlayer"를 강제 사용
	//
	// 📌 테스트 순서:
	//    1) PIE 실행 → 콘솔: Helluna.SQLite.DebugSave (아이템 2개 저장)
	//    2) PIE 종료 → 재실행 → PostLogin에서 "DebugPlayer"로 Stash 로드 → Grid에 표시!
	FString PlayerId;
	if (bDebugSkipLogin)
	{
		PlayerId = TEXT("DebugPlayer");
		UE_LOG(LogHellunaLobby, Warning, TEXT("[LobbyGM] ⚠ 디버그 모드 → 고정 ID '%s' 사용 (DebugSave와 일치)"), *PlayerId);
	}
	else
	{
		PlayerId = GetPlayerSaveId(NewPlayer);
	}

	if (PlayerId.IsEmpty())
	{
		UE_LOG(LogHellunaLobby, Warning, TEXT("[LobbyGM] PostLogin: PlayerId가 비어있음! Stash 로드 스킵"));
		return;
	}

	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] PostLogin → PlayerId=%s"), *PlayerId);

	// ── 0) 게임 결과 파일 처리 ──
	// 게임서버가 ExportGameResultToFile로 내보낸 결과를 로비에서 import
	// → 결과 파일 존재 = 정상 게임 종료: Loadout에 복원(생존) 또는 삭제(사망)
	// → 결과 파일 없음 + Loadout 존재 = 크래시: 아래 크래시 복구에서 처리
	bool bGameResultProcessed = false;  // [Fix23] 크래시 복구 조건 제어용
	if (SQLiteSubsystem->HasPendingGameResultFile(PlayerId))
	{
		UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] [0] 게임 결과 파일 발견 → import 시작 | PlayerId=%s"), *PlayerId);

		bool bSurvived = false;
		bool bImportSuccess = false;
		TArray<FInv_SavedItemData> ResultItems = SQLiteSubsystem->ImportGameResultFromFile(PlayerId, bSurvived, bImportSuccess);

		if (bImportSuccess)
		{
			bGameResultProcessed = true;  // [Fix23] 크래시 복구 스킵 플래그

			// 정상 파싱 성공
			UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] [0] 게임 결과: survived=%s | 아이템=%d개 | PlayerId=%s"),
				bSurvived ? TEXT("Y") : TEXT("N"), ResultItems.Num(), *PlayerId);

			// [Fix23] 생존: 결과 아이템을 Loadout에 복원 (기존: Stash에 병합 → Loadout 유실)
			// Stash는 Deploy 시점에 이미 SavePlayerStash로 저장됨 → 건드리지 않음
			if (bSurvived && ResultItems.Num() > 0)
			{
				// 게임/로비 Grid 사이즈가 다르므로 위치 리셋 → 자동 배치
				for (FInv_SavedItemData& Item : ResultItems)
				{
					Item.GridPosition = FIntPoint(-1, -1);
				}

				if (SQLiteSubsystem->SavePlayerLoadout(PlayerId, ResultItems))
				{
					UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] [0] [Fix23] SavePlayerLoadout 성공 — 게임 결과를 Loadout에 복원 | PlayerId=%s | %d개"),
						*PlayerId, ResultItems.Num());
				}
				else
				{
					UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyGM] [0] [Fix23] SavePlayerLoadout 실패! Stash 폴백 | PlayerId=%s"), *PlayerId);
					// 폴백: Loadout 저장 실패 시 Stash에 병합 (데이터 유실 방지)
					SQLiteSubsystem->MergeGameResultToStash(PlayerId, ResultItems);
					SQLiteSubsystem->DeletePlayerLoadout(PlayerId);
				}
			}
			else
			{
				// 사망 또는 아이템 없음: Loadout 삭제 (아이템 전부 손실)
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
			// [Case 8] GameResult JSON 파일 손상 → Loadout 보존하여 크래시 복구로 전환
			// 손상된 파일은 ImportGameResultFromFile 내부에서 이미 삭제됨
			// → Step 1 (CheckAndRecoverFromCrash)에서 Loadout→Stash 복구 처리
			UE_LOG(LogHellunaLobby, Error,
				TEXT("[LobbyGM] [0] GameResult 파일 손상! Loadout 보존 → 크래시 복구로 전환 | PlayerId=%s"), *PlayerId);
		}
	}

	// ── 1) 크래시 복구 ──
	// 결과 파일이 없는데 Loadout이 남아있는 경우 = 게임서버 크래시
	// → Loadout 아이템을 Stash로 복구하여 유실 방지
	// [Fix23] 게임 결과를 정상 처리한 경우에는 스킵 (SavePlayerLoadout으로 Loadout에 저장했으므로)
	if (!bGameResultProcessed)
	{
		UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] [1/4] 크래시 복구 체크 | PlayerId=%s"), *PlayerId);
		CheckAndRecoverFromCrash(PlayerId);
	}
	else
	{
		UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] [1/4] 크래시 복구 스킵 — 게임 결과 정상 처리됨 | PlayerId=%s"), *PlayerId);
	}

	// ── 2) Stash 로드 → StashComp에 복원 ──
	// SQLite player_stash → TArray<FInv_SavedItemData> → FInv_PlayerSaveData
	// → RestoreFromSaveData(SaveData, Resolver) 호출
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] [2/4] Stash 로드 → StashComp | PlayerId=%s"), *PlayerId);
	LoadStashToComponent(LobbyPC, PlayerId);

	// ── 2.5) [Fix23] Loadout 로드 → LoadoutComp에 복원 ──
	// 게임 생존 후 복귀: player_loadout에 저장된 결과 아이템을 LoadoutComp에 복원
	// → 로드 후 player_loadout 삭제 (다음 로그인 시 중복 방지)
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] [2.5/4] [Fix23] Loadout 로드 → LoadoutComp | PlayerId=%s"), *PlayerId);
	LoadLoadoutToComponent(LobbyPC, PlayerId);

	// ── 3) Controller-PlayerId 매핑 등록 ──
	// Logout 시 Controller에서 PlayerId를 역추적하기 위한 TMap 등록
	// (Logout 시점에는 PlayerState가 이미 정리됐을 수 있으므로 미리 저장)
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] [3/4] Controller-PlayerId 매핑 등록 | PlayerId=%s"), *PlayerId);
	RegisterControllerPlayerId(LobbyPC, PlayerId);

	// ── 4) 가용 캐릭터 정보 전달 ──
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] [4/4] 가용 캐릭터 정보 전달"));
	{
		TArray<bool> UsedCharacters = GetLobbyAvailableCharacters();
		LobbyPC->Client_ShowLobbyCharacterSelectUI(UsedCharacters);
	}

	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] ══════════════════════════════════════"));
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] PostLogin 완료 → PlayerId=%s"), *PlayerId);
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
		UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] Stash가 비어있음 → 빈 인벤토리로 시작 | PlayerId=%s"), *PlayerId);
		UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM]   → DebugSave 콘솔 명령으로 테스트 데이터를 넣을 수 있습니다"));
		return;
	}

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
		UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] [Fix23] Loadout이 비어있음 → 스킵 (최초 로그인/사망 후 복귀)"));
		return;
	}

	// ── 원본 아이템 수 기록 (파괴적 캐스케이드 방지용) ──
	const int32 LoadedLoadoutItemCount = LoadoutItems.Num();

	// ── 장착 상태 해제 (Loadout 패널에 무기 슬롯 없음) ──
	// DB에는 장착 정보가 보존되어 있으므로, 향후 무기 장착 슬롯 추가 시 활용 가능
	for (FInv_SavedItemData& ItemData : LoadoutItems)
	{
		if (ItemData.bEquipped)
		{
			UE_LOG(LogHellunaLobby, Log, TEXT("[Fix23] Loadout 아이템 장착 해제: %s (WeaponSlot=%d)"),
				*ItemData.ItemType.ToString(), ItemData.WeaponSlotIndex);
			ItemData.bEquipped = false;
			ItemData.WeaponSlotIndex = -1;
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
	// → player_loadout을 보존하여 다음 로그인 시 크래시 복구(Loadout→Stash)로 안전하게 복원
	if (RestoredCount < LoadedLoadoutItemCount)
	{
		UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyGM] [Fix23] ◆◆ Loadout 아이템 유실 감지! DB=%d → 복원=%d → %d개 유실"),
			LoadedLoadoutItemCount, RestoredCount, LoadedLoadoutItemCount - RestoredCount);
		UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyGM] [Fix23] ◆◆ player_loadout 보존 → 다음 로그인 시 크래시 복구로 Stash 이동"));

		// [Fix29-A] LoadoutComp를 비워서 Logout 시 SaveComponentsToDatabase에서 부분 아이템이 Stash에 병합되지 않도록 방지
		// → player_loadout이 DB에 보존되므로, 다음 로그인 시 크래시 복구가 전체 아이템을 Stash로 안전하게 이동
		// → LoadoutComp에 부분 아이템이 남아있으면: Logout 병합(부분) + 크래시 복구(전체) = 아이템 복제!
		FInv_PlayerSaveData EmptySaveData;
		EmptySaveData.LastSaveTime = FDateTime::Now();
		LoadoutComp->RestoreFromSaveData(EmptySaveData, Resolver);
		UE_LOG(LogHellunaLobby, Warning, TEXT("[LobbyGM] [Fix29-A] LoadoutComp 비움 — 부분 복원 아이템 복제 방지"));
		return;
	}

	// [Fix35] player_loadout을 DB에 유지 — 타르코프 방식 per-interaction save
	// 로비에 있는 동안 Stash+Loadout 모두 DB에 반영되어야 크래시 시 유실 없음
	// 정리 시점: Logout → SaveComponentsToDatabase → DeletePlayerLoadout (Stash에 병합 후)
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] [Fix35] player_loadout DB 유지 (per-interaction save 대비) | PlayerId=%s"), *PlayerId);
}

// ════════════════════════════════════════════════════════════════════════════════
// SaveComponentsToDatabase — StashComp + LoadoutComp 상태를 SQLite에 저장
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 호출 시점: Logout (정상 종료 / 접속 해제)
//
// 📌 저장 전략:
//   1) StashComp 데이터 → SQLite player_stash (현재 창고 상태)
//   2) LoadoutComp에 아이템이 남아있으면:
//      - 출격하지 않고 나간 것이므로 Loadout 아이템도 Stash에 병합 저장
//      - 이렇게 해야 다음 로그인 시 아이템 유실이 없음
//
// 📌 주의:
//   - Deploy(출격)으로 나간 경우 LoadoutComp은 이미 Server_Deploy에서 저장됨
//   - 이 경우 LoadoutComp은 비어있으므로 병합 저장이 발생하지 않음
//
// ════════════════════════════════════════════════════════════════════════════════
void AHellunaLobbyGameMode::SaveComponentsToDatabase(AHellunaLobbyController* LobbyPC, const FString& PlayerId)
{
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] SaveComponentsToDatabase 시작 | PlayerId=%s"), *PlayerId);

	if (!LobbyPC || !SQLiteSubsystem || !SQLiteSubsystem->IsDatabaseReady())
	{
		UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyGM] SaveComponents: 조건 미충족! | PC=%s, DB=%s"),
			*GetNameSafe(LobbyPC),
			(SQLiteSubsystem && SQLiteSubsystem->IsDatabaseReady()) ? TEXT("Ready") : TEXT("Not Ready"));
		return;
	}

	// ── 1) Stash 아이템 수집 ──
	UInv_InventoryComponent* StashComp = LobbyPC->GetStashComponent();
	TArray<FInv_SavedItemData> FinalStashItems;
	if (StashComp)
	{
		FinalStashItems = StashComp->CollectInventoryDataForSave();
		UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] Stash 아이템 수집 완료 | %d개"), FinalStashItems.Num());
	}
	else
	{
		UE_LOG(LogHellunaLobby, Warning, TEXT("[LobbyGM] SaveComponents: StashComp가 nullptr! | PlayerId=%s"), *PlayerId);
	}

	// ── 2) Loadout에 잔여 아이템이 있으면 Stash에 병합 (출격 안 하고 로그아웃한 경우) ──
	UInv_InventoryComponent* LoadoutComp = LobbyPC->GetLoadoutComponent();
	if (LoadoutComp)
	{
		TArray<FInv_SavedItemData> LoadoutItems = LoadoutComp->CollectInventoryDataForSave();
		if (LoadoutItems.Num() > 0)
		{
			UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] Logout [Save]: Loadout에 잔여 아이템 %d개 → Stash에 병합"), LoadoutItems.Num());
			FinalStashItems.Append(LoadoutItems);
		}
		else
		{
			UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] Loadout이 비어있음 → 병합 불필요 (정상 출격 또는 이동 없음)"));
		}
	}

	// ── 2.5) 파괴적 캐스케이드 방지 ──
	// DB에서 로드한 원본 아이템 수보다 현재 보유 아이템이 적으면
	// 복원 실패(ResolveItemTemplate 실패)로 아이템이 유실된 것
	// → 이 상태로 저장하면 DB에서도 영구 삭제되므로 저장을 거부
	const int32 OriginalLoadedCount = LobbyPC->GetLoadedStashItemCount();

	// [Fix29-D] 미로드 상태(-1) = PostLogin이 완료되기 전에 Logout 발생
	// → 빈 Stash를 DB에 저장하면 기존 데이터 덮어쓰기 → 저장 차단
	if (OriginalLoadedCount < 0)
	{
		UE_LOG(LogHellunaLobby, Warning, TEXT("[LobbyGM] [Fix29-D] Stash 미로드 상태(PostLogin 미완료)에서 Logout → 저장 차단 | PlayerId=%s"), *PlayerId);
		return;
	}

	if (OriginalLoadedCount > 0 && FinalStashItems.Num() < OriginalLoadedCount)
	{
		UE_LOG(LogHellunaLobby, Error, TEXT(""));
		UE_LOG(LogHellunaLobby, Error, TEXT("╔══════════════════════════════════════════════════════════════╗"));
		UE_LOG(LogHellunaLobby, Error, TEXT("║  ◆◆ 파괴적 캐스케이드 방지: 저장 거부!                      ║"));
		UE_LOG(LogHellunaLobby, Error, TEXT("║  DB 원본=%d개 → 현재=%d개 → %d개 유실 감지              ║"),
			OriginalLoadedCount, FinalStashItems.Num(), OriginalLoadedCount - FinalStashItems.Num());
		UE_LOG(LogHellunaLobby, Error, TEXT("║  → ResolveItemTemplate 실패로 복원되지 않은 아이템 있음 ║"));
		UE_LOG(LogHellunaLobby, Error, TEXT("║  → DB 데이터를 보호하기 위해 저장을 건너뜁니다         ║"));
		UE_LOG(LogHellunaLobby, Error, TEXT("║  → ItemTypeMappingDataTable 설정을 확인하세요!          ║"));
		UE_LOG(LogHellunaLobby, Error, TEXT("╚══════════════════════════════════════════════════════════════╝"));
		UE_LOG(LogHellunaLobby, Error, TEXT(""));
		return;
	}

	// ── 3) 한 번만 저장 ──
	const bool bStashOk = SQLiteSubsystem->SavePlayerStash(PlayerId, FinalStashItems);
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] Stash SQLite 저장 %s | PlayerId=%s | 아이템 %d개"),
		bStashOk ? TEXT("성공") : TEXT("실패"), *PlayerId, FinalStashItems.Num());

	// [Fix35] Per-interaction save가 player_loadout에 남긴 데이터 정리
	// Logout 시 Loadout 아이템은 이미 위에서 Stash에 병합 저장됨
	// 삭제 안 하면 다음 PostLogin에서 false crash recovery 발생 (아이템 중복)
	if (bStashOk)
	{
		SQLiteSubsystem->DeletePlayerLoadout(PlayerId);
		UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] [Fix35] DeletePlayerLoadout 완료 (Logout 병합 후 정리) | PlayerId=%s"), *PlayerId);
	}

	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] SaveComponentsToDatabase 완료 | PlayerId=%s"), *PlayerId);
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
