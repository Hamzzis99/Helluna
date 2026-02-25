// ════════════════════════════════════════════════════════════════════════════════
// HellunaLobbyGameMode.cpp
// ════════════════════════════════════════════════════════════════════════════════
//
// 로비 전용 GameMode — Stash 로드/저장 + 크래시 복구
//
// 📌 핵심 흐름:
//   PostLogin → 크래시 복구 체크 → SQLite Stash 로드 → StashComp에 RestoreFromSaveData
//   Logout → StashComp 데이터 수집 → SQLite SavePlayerStash
//
// 작성자: Gihyeon (Claude Code 보조)
// ════════════════════════════════════════════════════════════════════════════════

#include "Lobby/GameMode/HellunaLobbyGameMode.h"
#include "Lobby/Controller/HellunaLobbyController.h"
#include "Lobby/Database/HellunaSQLiteSubsystem.h"
#include "InventoryManagement/Components/Inv_InventoryComponent.h"
#include "Persistence/Inv_SaveTypes.h"
#include "Items/Components/Inv_ItemComponent.h"

// Helluna 로그 카테고리
DECLARE_LOG_CATEGORY_EXTERN(LogHellunaLobby, Log, All);
DEFINE_LOG_CATEGORY(LogHellunaLobby);

// ════════════════════════════════════════════════════════════════════════════════
// 생성자
// ════════════════════════════════════════════════════════════════════════════════
AHellunaLobbyGameMode::AHellunaLobbyGameMode()
{
	// 로비에서는 캐릭터 스폰 불필요 → DefaultPawnClass = nullptr
	DefaultPawnClass = nullptr;

	// 로비 전용 PlayerController는 BP에서 설정
	// PlayerControllerClass = AHellunaLobbyController::StaticClass();

	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] 생성자 호출"));
}

// ════════════════════════════════════════════════════════════════════════════════
// PostLogin — 크래시 복구 + Stash 로드
// ════════════════════════════════════════════════════════════════════════════════
void AHellunaLobbyGameMode::PostLogin(APlayerController* NewPlayer)
{
	// 부모 호출 (로그인 시스템, EndPlay 바인딩 등)
	Super::PostLogin(NewPlayer);

	if (!NewPlayer)
	{
		return;
	}

	AHellunaLobbyController* LobbyPC = Cast<AHellunaLobbyController>(NewPlayer);
	if (!LobbyPC)
	{
		UE_LOG(LogHellunaLobby, Warning, TEXT("[LobbyGM] PostLogin: Controller가 HellunaLobbyController가 아닙니다! → %s"),
			*GetNameSafe(NewPlayer));
		return;
	}

	// SQLite 서브시스템 캐시
	if (!SQLiteSubsystem)
	{
		UGameInstance* GI = GetGameInstance();
		SQLiteSubsystem = GI ? GI->GetSubsystem<UHellunaSQLiteSubsystem>() : nullptr;
	}

	if (!SQLiteSubsystem || !SQLiteSubsystem->IsDatabaseReady())
	{
		UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyGM] PostLogin: SQLite 서브시스템 없음 또는 DB 미준비!"));
		return;
	}

	// PlayerId 획득
	const FString PlayerId = GetPlayerSaveId(NewPlayer);
	if (PlayerId.IsEmpty())
	{
		UE_LOG(LogHellunaLobby, Warning, TEXT("[LobbyGM] PostLogin: PlayerId가 비어있음! (아직 로그인 안 됨?)"));
		// 로비에서 디버그 모드인 경우 기본 ID 사용
		if (bDebugSkipLogin)
		{
			const FString DebugId = TEXT("debug_lobby_player");
			UE_LOG(LogHellunaLobby, Warning, TEXT("[LobbyGM] 디버그 모드 → 기본 ID '%s' 사용"), *DebugId);

			// 크래시 복구
			CheckAndRecoverFromCrash(DebugId);

			// Stash 로드
			LoadStashToComponent(LobbyPC, DebugId);

			// Controller-PlayerId 매핑 등록
			RegisterControllerPlayerId(LobbyPC, DebugId);
		}
		return;
	}

	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] PostLogin → PlayerId=%s"), *PlayerId);

	// ── 1) 크래시 복구 ──
	CheckAndRecoverFromCrash(PlayerId);

	// ── 2) Stash 로드 → StashComp에 복원 ──
	LoadStashToComponent(LobbyPC, PlayerId);

	// ── 3) Controller-PlayerId 매핑 등록 (Logout에서 사용) ──
	RegisterControllerPlayerId(LobbyPC, PlayerId);

	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] PostLogin 완료 → PlayerId=%s"), *PlayerId);
}

// ════════════════════════════════════════════════════════════════════════════════
// Logout — Stash/Loadout 저장
// ════════════════════════════════════════════════════════════════════════════════
void AHellunaLobbyGameMode::Logout(AController* Exiting)
{
	if (Exiting)
	{
		AHellunaLobbyController* LobbyPC = Cast<AHellunaLobbyController>(Exiting);
		const FString PlayerId = GetPlayerSaveId(Cast<APlayerController>(Exiting));

		if (LobbyPC && !PlayerId.IsEmpty())
		{
			UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] Logout → PlayerId=%s | Stash/Loadout 저장 시작"), *PlayerId);
			SaveComponentsToDatabase(LobbyPC, PlayerId);
		}
		else
		{
			// ControllerToPlayerIdMap 폴백
			if (const FString* CachedId = ControllerToPlayerIdMap.Find(Exiting))
			{
				if (LobbyPC && !CachedId->IsEmpty())
				{
					UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] Logout (캐시 ID) → PlayerId=%s"), **CachedId);
					SaveComponentsToDatabase(LobbyPC, *CachedId);
				}
			}
		}
	}

	Super::Logout(Exiting);
}

// ════════════════════════════════════════════════════════════════════════════════
// LoadStashToComponent — SQLite → StashComp
// ════════════════════════════════════════════════════════════════════════════════
void AHellunaLobbyGameMode::LoadStashToComponent(AHellunaLobbyController* LobbyPC, const FString& PlayerId)
{
	if (!LobbyPC || !SQLiteSubsystem || !SQLiteSubsystem->IsDatabaseReady())
	{
		return;
	}

	UInv_InventoryComponent* StashComp = LobbyPC->GetStashComponent();
	if (!StashComp)
	{
		UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyGM] LoadStash: StashComp가 nullptr! | PlayerId=%s"), *PlayerId);
		return;
	}

	// ── SQLite에서 Stash 로드 ──
	TArray<FInv_SavedItemData> StashItems = SQLiteSubsystem->LoadPlayerStash(PlayerId);
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] SQLite Stash 로드 완료 | PlayerId=%s | 아이템 %d개"), *PlayerId, StashItems.Num());

	if (StashItems.Num() == 0)
	{
		UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] Stash가 비어있음 → 빈 인벤토리로 시작 | PlayerId=%s"), *PlayerId);
		return;
	}

	// ── FInv_PlayerSaveData 구성 ──
	FInv_PlayerSaveData SaveData;
	SaveData.Items = MoveTemp(StashItems);
	SaveData.LastSaveTime = FDateTime::Now();

	// ── 템플릿 리졸버 생성 ──
	FInv_ItemTemplateResolver Resolver;
	Resolver.BindUObject(this, &AHellunaLobbyGameMode::ResolveItemTemplate);

	// ── StashComp에 복원 ──
	StashComp->RestoreFromSaveData(SaveData, Resolver);

	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] StashComp 복원 완료 | PlayerId=%s | 아이템 %d개"),
		*PlayerId, SaveData.Items.Num());
}

// ════════════════════════════════════════════════════════════════════════════════
// SaveComponentsToDatabase — StashComp + LoadoutComp → SQLite
// ════════════════════════════════════════════════════════════════════════════════
void AHellunaLobbyGameMode::SaveComponentsToDatabase(AHellunaLobbyController* LobbyPC, const FString& PlayerId)
{
	if (!LobbyPC || !SQLiteSubsystem || !SQLiteSubsystem->IsDatabaseReady())
	{
		UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyGM] SaveComponents: 조건 미충족! | PC=%s, DB=%s"),
			*GetNameSafe(LobbyPC),
			(SQLiteSubsystem && SQLiteSubsystem->IsDatabaseReady()) ? TEXT("Ready") : TEXT("Not Ready"));
		return;
	}

	// ── Stash 저장 (서버의 FastArray에서 직접 수집) ──
	UInv_InventoryComponent* StashComp = LobbyPC->GetStashComponent();
	if (StashComp)
	{
		TArray<FInv_SavedItemData> StashItems = StashComp->CollectInventoryDataForSave();
		const bool bStashOk = SQLiteSubsystem->SavePlayerStash(PlayerId, StashItems);
		UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] Stash 저장 %s | PlayerId=%s | 아이템 %d개"),
			bStashOk ? TEXT("성공") : TEXT("실패"), *PlayerId, StashItems.Num());
	}

	// ── Loadout은 출격 시에만 저장 (Deploy RPC에서 처리) ──
	// Logout 시에는 Loadout을 별도 저장하지 않음
	// Loadout에 있던 아이템은 Stash에 되돌려서 저장해야 데이터 유실 방지
	UInv_InventoryComponent* LoadoutComp = LobbyPC->GetLoadoutComponent();
	if (LoadoutComp)
	{
		TArray<FInv_SavedItemData> LoadoutItems = LoadoutComp->CollectInventoryDataForSave();
		if (LoadoutItems.Num() > 0)
		{
			UE_LOG(LogHellunaLobby, Warning, TEXT("[LobbyGM] Logout 시 Loadout에 %d개 아이템 잔존! → Stash에 병합 저장"),
				LoadoutItems.Num());

			// Loadout에 남은 아이템을 Stash에 합산 저장
			if (StashComp)
			{
				TArray<FInv_SavedItemData> MergedStash = StashComp->CollectInventoryDataForSave();
				MergedStash.Append(LoadoutItems);
				const bool bMergeOk = SQLiteSubsystem->SavePlayerStash(PlayerId, MergedStash);
				UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyGM] Stash+Loadout 병합 저장 %s | 총 %d개"),
					bMergeOk ? TEXT("성공") : TEXT("실패"), MergedStash.Num());
			}
		}
	}
}

// ════════════════════════════════════════════════════════════════════════════════
// ResolveItemTemplate — ItemType → UInv_ItemComponent* (리졸버)
// ════════════════════════════════════════════════════════════════════════════════
UInv_ItemComponent* AHellunaLobbyGameMode::ResolveItemTemplate(const FGameplayTag& ItemType)
{
	TSubclassOf<AActor> ActorClass = ResolveItemClass(ItemType);
	if (!ActorClass) return nullptr;
	return FindItemComponentTemplate(ActorClass);
}
