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

	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] ========================================"));
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] 생성자 호출 | DefaultPawnClass=nullptr"));
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

	if (!SQLiteSubsystem || !SQLiteSubsystem->IsDatabaseReady())
	{
		UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyGM] PostLogin: SQLite 서브시스템 없음 또는 DB 미준비!"));
		UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyGM]   → SQLiteSubsystem=%s, IsDatabaseReady=%s"),
			SQLiteSubsystem ? TEXT("O") : TEXT("X"),
			(SQLiteSubsystem && SQLiteSubsystem->IsDatabaseReady()) ? TEXT("true") : TEXT("false"));
		return;
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

	// ── 1) 크래시 복구 ──
	// 이전 세션에서 비정상 종료된 경우 player_loadout에 데이터가 남아있을 수 있음
	// → Stash로 복구하여 아이템 유실 방지
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] [1/3] 크래시 복구 체크 | PlayerId=%s"), *PlayerId);
	CheckAndRecoverFromCrash(PlayerId);

	// ── 2) Stash 로드 → StashComp에 복원 ──
	// SQLite player_stash → TArray<FInv_SavedItemData> → FInv_PlayerSaveData
	// → RestoreFromSaveData(SaveData, Resolver) 호출
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] [2/3] Stash 로드 → StashComp | PlayerId=%s"), *PlayerId);
	LoadStashToComponent(LobbyPC, PlayerId);

	// ── 3) Controller-PlayerId 매핑 등록 ──
	// Logout 시 Controller에서 PlayerId를 역추적하기 위한 TMap 등록
	// (Logout 시점에는 PlayerState가 이미 정리됐을 수 있으므로 미리 저장)
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] [3/3] Controller-PlayerId 매핑 등록 | PlayerId=%s"), *PlayerId);
	RegisterControllerPlayerId(LobbyPC, PlayerId);

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
		const FString PlayerId = GetPlayerSaveId(Cast<APlayerController>(Exiting));

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

	Super::Logout(Exiting);
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

	// ── 로드된 아이템 상세 로그 (디버깅용) ──
	for (int32 i = 0; i < StashItems.Num(); ++i)
	{
		const FInv_SavedItemData& ItemData = StashItems[i];
		UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM]   [%d] ItemType=%s | Stack=%d | GridPos=(%d,%d)"),
			i, *ItemData.ItemType.ToString(), ItemData.StackCount,
			ItemData.GridPosition.X, ItemData.GridPosition.Y);
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

	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] StashComp 복원 완료 | PlayerId=%s | 요청 아이템 %d개"),
		*PlayerId, SaveData.Items.Num());
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

	// ── 1) Stash 저장 ──
	// CollectInventoryDataForSave(): 서버의 FastArray에서 직접 수집 (RPC 불필요)
	UInv_InventoryComponent* StashComp = LobbyPC->GetStashComponent();
	if (StashComp)
	{
		TArray<FInv_SavedItemData> StashItems = StashComp->CollectInventoryDataForSave();
		UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] Stash 아이템 수집 완료 | %d개"), StashItems.Num());

		const bool bStashOk = SQLiteSubsystem->SavePlayerStash(PlayerId, StashItems);
		UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] Stash SQLite 저장 %s | PlayerId=%s | 아이템 %d개"),
			bStashOk ? TEXT("성공") : TEXT("실패"), *PlayerId, StashItems.Num());
	}
	else
	{
		UE_LOG(LogHellunaLobby, Warning, TEXT("[LobbyGM] SaveComponents: StashComp가 nullptr! | PlayerId=%s"), *PlayerId);
	}

	// ── 2) Loadout 잔존 아이템 처리 ──
	// 출격 없이 로비를 나간 경우 → Loadout 아이템을 Stash에 병합 저장
	// 출격으로 나간 경우 → LoadoutComp은 비어있으므로 아무 일도 안 함
	UInv_InventoryComponent* LoadoutComp = LobbyPC->GetLoadoutComponent();
	if (LoadoutComp)
	{
		TArray<FInv_SavedItemData> LoadoutItems = LoadoutComp->CollectInventoryDataForSave();
		UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] Loadout 잔존 아이템 확인 | %d개"), LoadoutItems.Num());

		if (LoadoutItems.Num() > 0)
		{
			// ⚠ Loadout에 아이템이 있다 = 출격 안 하고 나감
			// → 이 아이템들을 Stash에 합산해서 재저장 (유실 방지)
			UE_LOG(LogHellunaLobby, Warning, TEXT("[LobbyGM] ⚠ Logout 시 Loadout에 %d개 아이템 잔존! → Stash에 병합 저장"),
				LoadoutItems.Num());

			if (StashComp)
			{
				// Stash를 다시 수집 + Loadout 아이템 합산
				TArray<FInv_SavedItemData> MergedStash = StashComp->CollectInventoryDataForSave();
				const int32 StashCount = MergedStash.Num();
				MergedStash.Append(LoadoutItems);

				UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] 병합: Stash(%d) + Loadout(%d) = 총 %d개"),
					StashCount, LoadoutItems.Num(), MergedStash.Num());

				const bool bMergeOk = SQLiteSubsystem->SavePlayerStash(PlayerId, MergedStash);
				UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] Stash+Loadout 병합 저장 %s | 총 %d개"),
					bMergeOk ? TEXT("성공") : TEXT("실패"), MergedStash.Num());
			}
		}
		else
		{
			UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] Loadout이 비어있음 → 병합 불필요 (정상 출격 또는 이동 없음)"));
		}
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
	UE_LOG(LogHellunaLobby, Verbose, TEXT("[LobbyGM] ResolveItemTemplate: %s"), *ItemType.ToString());

	TSubclassOf<AActor> ActorClass = ResolveItemClass(ItemType);
	if (!ActorClass)
	{
		UE_LOG(LogHellunaLobby, Warning, TEXT("[LobbyGM] ResolveItemTemplate: ResolveItemClass 실패! | ItemType=%s"),
			*ItemType.ToString());
		UE_LOG(LogHellunaLobby, Warning, TEXT("[LobbyGM]   → HellunaBaseGameMode의 ItemClassMap에 이 태그가 등록되었는지 확인하세요"));
		return nullptr;
	}

	UInv_ItemComponent* Template = FindItemComponentTemplate(ActorClass);
	if (!Template)
	{
		UE_LOG(LogHellunaLobby, Warning, TEXT("[LobbyGM] ResolveItemTemplate: FindItemComponentTemplate 실패! | ItemType=%s, ActorClass=%s"),
			*ItemType.ToString(), *ActorClass->GetName());
	}

	return Template;
}
