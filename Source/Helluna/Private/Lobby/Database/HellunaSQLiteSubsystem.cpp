// File: Source/Helluna/Private/Lobby/Database/HellunaSQLiteSubsystem.cpp

#include "Lobby/Database/HellunaSQLiteSubsystem.h"
#include "SQLiteDatabase.h"              // FSQLiteDatabase (엔진 SQLiteCore 모듈)
#include "SQLitePreparedStatement.h"     // FSQLitePreparedStatement (명시적)
#include "Misc/Paths.h"                  // FPaths
#include "HAL/FileManager.h"             // IFileManager (디렉토리 생성)
#include "Misc/Base64.h"                 // FBase64 (부착물 BLOB → Base64 변환)
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Helluna.h"                     // LogHelluna

// ============================================================
// USubsystem 오버라이드
// ============================================================

bool UHellunaSQLiteSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	// 데디서버/클라이언트 모두 생성 (로비서버 + 게임서버 양쪽에서 필요)
	return true;
}

void UHellunaSQLiteSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// DB 파일 경로 설정: {ProjectSavedDir}/Database/Helluna.db
	CachedDatabasePath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Database"), TEXT("Helluna.db"));

	// DB 디렉토리가 없으면 생성
	const FString DatabaseDir = FPaths::GetPath(CachedDatabasePath);
	IFileManager::Get().MakeDirectory(*DatabaseDir, true);

	// DB 열기
	if (OpenDatabase())
	{
		UE_LOG(LogHelluna, Log, TEXT("[SQLite] 서브시스템 초기화 완료 | DB 경로: %s"), *CachedDatabasePath);
	}
	else
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] 서브시스템 초기화 실패 — DB 열기 실패 | 경로: %s"), *CachedDatabasePath);
	}
}

void UHellunaSQLiteSubsystem::Deinitialize()
{
	CloseDatabase();
	Super::Deinitialize();
}

// ============================================================
// DB 관리
// ============================================================

bool UHellunaSQLiteSubsystem::OpenDatabase()
{
	// 이미 열려있으면 경고 후 닫기
	if (Database != nullptr)
	{
		UE_LOG(LogHelluna, Warning, TEXT("[SQLite] OpenDatabase 호출 시 기존 DB가 열려있음 — 기존 DB를 닫고 재오픈합니다."));
		CloseDatabase();
	}

	Database = new FSQLiteDatabase();
	if (!ensureMsgf(Database != nullptr, TEXT("[SQLite] FSQLiteDatabase 메모리 할당 실패")))
	{
		bDatabaseOpen = false;
		return false;
	}

	if (Database->Open(*CachedDatabasePath, ESQLiteDatabaseOpenMode::ReadWriteCreate))
	{
		bDatabaseOpen = true;
		UE_LOG(LogHelluna, Log, TEXT("[SQLite] 데이터베이스 열림: %s"), *CachedDatabasePath);

		// 스키마 초기화 (PRAGMA + 테이블 생성)
		if (!InitializeSchema())
		{
			UE_LOG(LogHelluna, Error, TEXT("[SQLite] 스키마 초기화 실패 — DB를 닫습니다."));
			CloseDatabase();
			return false;
		}

		return true;
	}
	else
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] 데이터베이스 열기 실패: %s | 에러: %s"),
			*CachedDatabasePath, *Database->GetLastError());
		delete Database;
		Database = nullptr;
		bDatabaseOpen = false;
		return false;
	}
}

void UHellunaSQLiteSubsystem::CloseDatabase()
{
	if (Database == nullptr)
	{
		return;
	}

	Database->Close();
	delete Database;
	Database = nullptr;
	bDatabaseOpen = false;
	UE_LOG(LogHelluna, Log, TEXT("[SQLite] 데이터베이스 닫힘"));
}

// ============================================================
// 스키마 초기화
// ============================================================

bool UHellunaSQLiteSubsystem::InitializeSchema()
{
	check(Database != nullptr);

	// ── PRAGMA 설정 ──
	// WAL: 로비서버+게임서버가 동시에 같은 DB 접근 시 읽기-쓰기 동시성 향상
	if (!Database->Execute(TEXT("PRAGMA journal_mode=WAL;")))
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] InitializeSchema 실패: PRAGMA journal_mode=WAL | 에러: %s"), *Database->GetLastError());
		return false;
	}

	// busy_timeout=3000: 다른 프로세스 잠금 시 3초까지 재시도 (SQLite 기본은 0=즉시 실패)
	if (!Database->Execute(TEXT("PRAGMA busy_timeout=3000;")))
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] InitializeSchema 실패: PRAGMA busy_timeout | 에러: %s"), *Database->GetLastError());
		return false;
	}

	// foreign_keys=OFF: 두 테이블 간 FK 관계 없음, 불필요한 검사 비활성화
	if (!Database->Execute(TEXT("PRAGMA foreign_keys=OFF;")))
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] InitializeSchema 실패: PRAGMA foreign_keys | 에러: %s"), *Database->GetLastError());
		return false;
	}

	// ── player_stash 테이블 ──
	if (!Database->Execute(TEXT(
		"CREATE TABLE IF NOT EXISTS player_stash ("
		"    id                  INTEGER PRIMARY KEY AUTOINCREMENT,"
		"    player_id           TEXT NOT NULL,"
		"    item_type           TEXT NOT NULL,"
		"    stack_count         INTEGER NOT NULL DEFAULT 1,"
		"    grid_position_x     INTEGER DEFAULT -1,"
		"    grid_position_y     INTEGER DEFAULT -1,"
		"    grid_category       INTEGER DEFAULT 0,"
		"    is_equipped         INTEGER DEFAULT 0,"
		"    weapon_slot         INTEGER DEFAULT -1,"
		"    serialized_manifest BLOB,"
		"    attachments_json    TEXT,"
		"    updated_at          DATETIME DEFAULT CURRENT_TIMESTAMP"
		");"
	)))
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] InitializeSchema 실패: CREATE TABLE player_stash | 에러: %s"), *Database->GetLastError());
		return false;
	}

	// ── player_stash 인덱스 ──
	if (!Database->Execute(TEXT("CREATE INDEX IF NOT EXISTS idx_stash_player_id ON player_stash(player_id);")))
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] InitializeSchema 실패: INDEX idx_stash_player_id | 에러: %s"), *Database->GetLastError());
		return false;
	}

	// ── player_loadout 테이블 ──
	if (!Database->Execute(TEXT(
		"CREATE TABLE IF NOT EXISTS player_loadout ("
		"    id                  INTEGER PRIMARY KEY AUTOINCREMENT,"
		"    player_id           TEXT NOT NULL,"
		"    item_type           TEXT NOT NULL,"
		"    stack_count         INTEGER NOT NULL DEFAULT 1,"
		"    grid_position_x     INTEGER DEFAULT -1,"
		"    grid_position_y     INTEGER DEFAULT -1,"
		"    grid_category       INTEGER DEFAULT 0,"
		"    weapon_slot         INTEGER DEFAULT -1,"
		"    serialized_manifest BLOB,"
		"    attachments_json    TEXT,"
		"    created_at          DATETIME DEFAULT CURRENT_TIMESTAMP"
		");"
	)))
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] InitializeSchema 실패: CREATE TABLE player_loadout | 에러: %s"), *Database->GetLastError());
		return false;
	}

	// ── player_loadout 인덱스 ──
	if (!Database->Execute(TEXT("CREATE INDEX IF NOT EXISTS idx_loadout_player_id ON player_loadout(player_id);")))
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] InitializeSchema 실패: INDEX idx_loadout_player_id | 에러: %s"), *Database->GetLastError());
		return false;
	}

	// ── schema_version 테이블 ──
	if (!Database->Execute(TEXT(
		"CREATE TABLE IF NOT EXISTS schema_version ("
		"    version     INTEGER NOT NULL,"
		"    applied_at  DATETIME DEFAULT CURRENT_TIMESTAMP"
		");"
	)))
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] InitializeSchema 실패: CREATE TABLE schema_version | 에러: %s"), *Database->GetLastError());
		return false;
	}

	// ── schema_version 초기값 (없을 때만) ──
	if (!Database->Execute(TEXT("INSERT OR IGNORE INTO schema_version (rowid, version) VALUES (1, 1);")))
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] InitializeSchema 실패: INSERT schema_version | 에러: %s"), *Database->GetLastError());
		return false;
	}

	UE_LOG(LogHelluna, Log, TEXT("[SQLite] 스키마 초기화 완료 (테이블 3개, 인덱스 2개)"));
	return true;
}

// ============================================================
// DB 상태 확인
// ============================================================

bool UHellunaSQLiteSubsystem::IsDatabaseReady() const
{
	return bDatabaseOpen && Database != nullptr && Database->IsValid();
}

FString UHellunaSQLiteSubsystem::GetDatabasePath() const
{
	return CachedDatabasePath;
}

// ============================================================
// FInv_SavedItemData ↔ DB 변환 헬퍼
// ============================================================

FString UHellunaSQLiteSubsystem::SerializeAttachmentsToJson(const TArray<FInv_SavedAttachmentData>& Attachments)
{
	if (Attachments.Num() == 0)
	{
		return FString();
	}

	TArray<TSharedPtr<FJsonValue>> JsonArray;
	for (const FInv_SavedAttachmentData& Att : Attachments)
	{
		TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("t"), Att.AttachmentItemType.ToString());
		Obj->SetNumberField(TEXT("s"), Att.SlotIndex);
		Obj->SetStringField(TEXT("at"), Att.AttachmentType.ToString());

		if (Att.SerializedManifest.Num() > 0)
		{
			Obj->SetStringField(TEXT("m"), FBase64::Encode(Att.SerializedManifest));
		}

		JsonArray.Add(MakeShared<FJsonValueObject>(Obj));
	}

	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(JsonArray, Writer);
	return OutputString;
}

TArray<FInv_SavedAttachmentData> UHellunaSQLiteSubsystem::DeserializeAttachmentsFromJson(const FString& JsonString)
{
	TArray<FInv_SavedAttachmentData> Result;

	if (JsonString.IsEmpty())
	{
		return Result;
	}

	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
	TArray<TSharedPtr<FJsonValue>> JsonArray;
	if (!FJsonSerializer::Deserialize(Reader, JsonArray))
	{
		UE_LOG(LogHelluna, Warning, TEXT("[SQLite] DeserializeAttachmentsFromJson: JSON 파싱 실패 | JSON=%s"), *JsonString);
		return Result;
	}

	for (const TSharedPtr<FJsonValue>& Value : JsonArray)
	{
		const TSharedPtr<FJsonObject> Obj = Value->AsObject();
		if (!Obj.IsValid())
		{
			continue;
		}

		FInv_SavedAttachmentData Att;
		Att.AttachmentItemType = FGameplayTag::RequestGameplayTag(FName(*Obj->GetStringField(TEXT("t"))), false);
		Att.SlotIndex = static_cast<int32>(Obj->GetNumberField(TEXT("s")));
		Att.AttachmentType = FGameplayTag::RequestGameplayTag(FName(*Obj->GetStringField(TEXT("at"))), false);

		FString ManifestB64;
		if (Obj->TryGetStringField(TEXT("m"), ManifestB64) && !ManifestB64.IsEmpty())
		{
			FBase64::Decode(ManifestB64, Att.SerializedManifest);
		}

		Result.Add(MoveTemp(Att));
	}

	return Result;
}

FInv_SavedItemData UHellunaSQLiteSubsystem::ParseRowToSavedItem(const FSQLitePreparedStatement& Statement)
{
	FInv_SavedItemData Item;

	// item_type → FGameplayTag
	FString ItemTypeStr;
	Statement.GetColumnValueByName(TEXT("item_type"), ItemTypeStr);
	Item.ItemType = FGameplayTag::RequestGameplayTag(FName(*ItemTypeStr), false);

	// stack_count
	Statement.GetColumnValueByName(TEXT("stack_count"), Item.StackCount);

	// grid_position
	int32 PosX = -1, PosY = -1;
	Statement.GetColumnValueByName(TEXT("grid_position_x"), PosX);
	Statement.GetColumnValueByName(TEXT("grid_position_y"), PosY);
	Item.GridPosition = FIntPoint(PosX, PosY);

	// grid_category
	int32 GridCat = 0;
	Statement.GetColumnValueByName(TEXT("grid_category"), GridCat);
	Item.GridCategory = static_cast<uint8>(GridCat);

	// is_equipped
	int32 Equipped = 0;
	Statement.GetColumnValueByName(TEXT("is_equipped"), Equipped);
	Item.bEquipped = (Equipped != 0);

	// weapon_slot
	Statement.GetColumnValueByName(TEXT("weapon_slot"), Item.WeaponSlotIndex);

	// serialized_manifest (BLOB)
	Statement.GetColumnValueByName(TEXT("serialized_manifest"), Item.SerializedManifest);

	// attachments_json → TArray<FInv_SavedAttachmentData>
	FString AttJson;
	Statement.GetColumnValueByName(TEXT("attachments_json"), AttJson);
	Item.Attachments = DeserializeAttachmentsFromJson(AttJson);

	return Item;
}

// ============================================================
// IInventoryDatabase — Stash CRUD 구현
// ============================================================

TArray<FInv_SavedItemData> UHellunaSQLiteSubsystem::LoadPlayerStash(const FString& PlayerId)
{
	if (!IsDatabaseReady())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] LoadPlayerStash: DB가 준비되지 않음 | PlayerId=%s"), *PlayerId);
		return TArray<FInv_SavedItemData>();
	}

	const TCHAR* SelectSQL = TEXT(
		"SELECT item_type, stack_count, grid_position_x, grid_position_y, "
		"grid_category, is_equipped, weapon_slot, serialized_manifest, attachments_json "
		"FROM player_stash WHERE player_id = ?1;"
	);

	FSQLitePreparedStatement SelectStmt = Database->PrepareStatement(SelectSQL);
	if (!SelectStmt.IsValid())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] LoadPlayerStash: PrepareStatement 실패 | 에러: %s"), *Database->GetLastError());
		return TArray<FInv_SavedItemData>();
	}

	SelectStmt.SetBindingValueByIndex(1, PlayerId);

	TArray<FInv_SavedItemData> Result;
	SelectStmt.Execute([&Result](const FSQLitePreparedStatement& Stmt) -> ESQLitePreparedStatementExecuteRowResult
	{
		FInv_SavedItemData Item = ParseRowToSavedItem(Stmt);
		if (Item.IsValid())
		{
			Result.Add(MoveTemp(Item));
		}
		return ESQLitePreparedStatementExecuteRowResult::Continue;
	});

	UE_LOG(LogHelluna, Log, TEXT("[SQLite] LoadPlayerStash 완료 | PlayerId=%s | 아이템 %d개"), *PlayerId, Result.Num());
	return Result;
}

bool UHellunaSQLiteSubsystem::SavePlayerStash(const FString& PlayerId, const TArray<FInv_SavedItemData>& Items)
{
	if (!IsDatabaseReady())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] SavePlayerStash: DB가 준비되지 않음 | PlayerId=%s"), *PlayerId);
		return false;
	}

	// 트랜잭션 시작
	if (!Database->Execute(TEXT("BEGIN TRANSACTION;")))
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] SavePlayerStash: BEGIN TRANSACTION 실패 | 에러: %s"), *Database->GetLastError());
		return false;
	}

	// 기존 Stash 전부 삭제
	{
		FSQLitePreparedStatement DeleteStmt = Database->PrepareStatement(
			TEXT("DELETE FROM player_stash WHERE player_id = ?1;"));
		if (!DeleteStmt.IsValid())
		{
			UE_LOG(LogHelluna, Error, TEXT("[SQLite] SavePlayerStash: DELETE PrepareStatement 실패 | 에러: %s"), *Database->GetLastError());
			Database->Execute(TEXT("ROLLBACK;"));
			return false;
		}
		DeleteStmt.SetBindingValueByIndex(1, PlayerId);
		if (!DeleteStmt.Execute())
		{
			UE_LOG(LogHelluna, Error, TEXT("[SQLite] SavePlayerStash: DELETE 실패 — ROLLBACK | 에러: %s"), *Database->GetLastError());
			Database->Execute(TEXT("ROLLBACK;"));
			return false;
		}
	}

	// INSERT 배치
	const TCHAR* InsertSQL = TEXT(
		"INSERT INTO player_stash "
		"(player_id, item_type, stack_count, grid_position_x, grid_position_y, "
		"grid_category, is_equipped, weapon_slot, serialized_manifest, attachments_json) "
		"VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10);"
	);

	FSQLitePreparedStatement InsertStmt = Database->PrepareStatement(InsertSQL, ESQLitePreparedStatementFlags::Persistent);
	if (!InsertStmt.IsValid())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] SavePlayerStash: INSERT PrepareStatement 실패 — ROLLBACK | 에러: %s"), *Database->GetLastError());
		Database->Execute(TEXT("ROLLBACK;"));
		return false;
	}

	for (const FInv_SavedItemData& Item : Items)
	{
		InsertStmt.SetBindingValueByIndex(1, PlayerId);
		InsertStmt.SetBindingValueByIndex(2, Item.ItemType.ToString());
		InsertStmt.SetBindingValueByIndex(3, Item.StackCount);
		InsertStmt.SetBindingValueByIndex(4, Item.GridPosition.X);
		InsertStmt.SetBindingValueByIndex(5, Item.GridPosition.Y);
		InsertStmt.SetBindingValueByIndex(6, static_cast<int32>(Item.GridCategory));
		InsertStmt.SetBindingValueByIndex(7, Item.bEquipped ? 1 : 0);
		InsertStmt.SetBindingValueByIndex(8, Item.WeaponSlotIndex);

		// BLOB (SerializedManifest)
		if (Item.SerializedManifest.Num() > 0)
		{
			InsertStmt.SetBindingValueByIndex(9, TArrayView<const uint8>(Item.SerializedManifest), true);
		}
		else
		{
			InsertStmt.SetBindingValueByIndex(9); // NULL
		}

		// JSON (Attachments)
		const FString AttJson = SerializeAttachmentsToJson(Item.Attachments);
		if (AttJson.IsEmpty())
		{
			InsertStmt.SetBindingValueByIndex(10, TEXT(""));
		}
		else
		{
			InsertStmt.SetBindingValueByIndex(10, AttJson);
		}

		if (!InsertStmt.Execute())
		{
			UE_LOG(LogHelluna, Error, TEXT("[SQLite] SavePlayerStash: INSERT 실패 — ROLLBACK | 아이템=%s | 에러: %s"),
				*Item.ItemType.ToString(), *Database->GetLastError());
			Database->Execute(TEXT("ROLLBACK;"));
			return false;
		}

		InsertStmt.Reset();
		InsertStmt.ClearBindings();
	}

	// 커밋
	if (!Database->Execute(TEXT("COMMIT;")))
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] SavePlayerStash: COMMIT 실패 — ROLLBACK | 에러: %s"), *Database->GetLastError());
		Database->Execute(TEXT("ROLLBACK;"));
		return false;
	}

	UE_LOG(LogHelluna, Log, TEXT("[SQLite] SavePlayerStash 완료 | PlayerId=%s | 아이템 %d개"), *PlayerId, Items.Num());
	return true;
}

bool UHellunaSQLiteSubsystem::IsPlayerExists(const FString& PlayerId)
{
	if (!IsDatabaseReady())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] IsPlayerExists: DB가 준비되지 않음 | PlayerId=%s"), *PlayerId);
		return false;
	}

	const TCHAR* CountSQL = TEXT("SELECT COUNT(*) FROM player_stash WHERE player_id = ?1;");
	FSQLitePreparedStatement CountStmt = Database->PrepareStatement(CountSQL);
	if (!CountStmt.IsValid())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] IsPlayerExists: PrepareStatement 실패 | 에러: %s"), *Database->GetLastError());
		return false;
	}

	CountStmt.SetBindingValueByIndex(1, PlayerId);

	int64 Count = 0;
	CountStmt.Execute([&Count](const FSQLitePreparedStatement& Stmt) -> ESQLitePreparedStatementExecuteRowResult
	{
		Stmt.GetColumnValueByIndex(0, Count);
		return ESQLitePreparedStatementExecuteRowResult::Stop;
	});

	return Count > 0;
}

// ============================================================
// IInventoryDatabase — Loadout CRUD 구현 (Phase 2 Step 2-3)
// ============================================================

TArray<FInv_SavedItemData> UHellunaSQLiteSubsystem::LoadPlayerLoadout(const FString& PlayerId)
{
	if (!IsDatabaseReady())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] LoadPlayerLoadout: DB가 준비되지 않음 | PlayerId=%s"), *PlayerId);
		return TArray<FInv_SavedItemData>();
	}

	// player_loadout에는 is_equipped 컬럼이 없음 — SELECT에서 제외
	// ParseRowToSavedItem에서 GetColumnValueByName("is_equipped") 실패 시 기본값 0 유지 → 안전
	const TCHAR* SelectSQL = TEXT(
		"SELECT item_type, stack_count, grid_position_x, grid_position_y, "
		"grid_category, weapon_slot, serialized_manifest, attachments_json "
		"FROM player_loadout WHERE player_id = ?1;"
	);

	FSQLitePreparedStatement SelectStmt = Database->PrepareStatement(SelectSQL);
	if (!SelectStmt.IsValid())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] LoadPlayerLoadout: PrepareStatement 실패 | 에러: %s"), *Database->GetLastError());
		return TArray<FInv_SavedItemData>();
	}

	SelectStmt.SetBindingValueByIndex(1, PlayerId);

	TArray<FInv_SavedItemData> Result;
	SelectStmt.Execute([&Result](const FSQLitePreparedStatement& Stmt) -> ESQLitePreparedStatementExecuteRowResult
	{
		FInv_SavedItemData Item = ParseRowToSavedItem(Stmt);
		if (Item.IsValid())
		{
			Result.Add(MoveTemp(Item));
		}
		return ESQLitePreparedStatementExecuteRowResult::Continue;
	});

	UE_LOG(LogHelluna, Log, TEXT("[SQLite] LoadPlayerLoadout 완료 | PlayerId=%s | 아이템 %d개"), *PlayerId, Result.Num());
	return Result;
}

// Phase 2 Step 2-4: 출격 원자적 트랜잭션 — Loadout INSERT + Stash DELETE
bool UHellunaSQLiteSubsystem::SavePlayerLoadout(const FString& PlayerId, const TArray<FInv_SavedItemData>& Items)
{
	if (!IsDatabaseReady())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] SavePlayerLoadout: DB가 준비되지 않음 | PlayerId=%s"), *PlayerId);
		return false;
	}

	if (Items.Num() == 0)
	{
		UE_LOG(LogHelluna, Warning, TEXT("[SQLite] SavePlayerLoadout: 출격 아이템 없음 — 스킵 | PlayerId=%s"), *PlayerId);
		return false;
	}

	// ── 트랜잭션 시작 (Loadout INSERT + Stash DELETE 원자적 처리) ──
	if (!Database->Execute(TEXT("BEGIN TRANSACTION;")))
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] SavePlayerLoadout: BEGIN TRANSACTION 실패 | 에러: %s"), *Database->GetLastError());
		return false;
	}

	// (a) player_loadout에 Items INSERT — is_equipped 컬럼 없음 (9개 바인딩)
	const TCHAR* InsertSQL = TEXT(
		"INSERT INTO player_loadout "
		"(player_id, item_type, stack_count, grid_position_x, grid_position_y, "
		"grid_category, weapon_slot, serialized_manifest, attachments_json) "
		"VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9);"
	);

	FSQLitePreparedStatement InsertStmt = Database->PrepareStatement(InsertSQL, ESQLitePreparedStatementFlags::Persistent);
	if (!InsertStmt.IsValid())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] SavePlayerLoadout: INSERT PrepareStatement 실패 — ROLLBACK | 에러: %s"), *Database->GetLastError());
		Database->Execute(TEXT("ROLLBACK;"));
		return false;
	}

	for (const FInv_SavedItemData& Item : Items)
	{
		InsertStmt.SetBindingValueByIndex(1, PlayerId);
		InsertStmt.SetBindingValueByIndex(2, Item.ItemType.ToString());
		InsertStmt.SetBindingValueByIndex(3, Item.StackCount);
		InsertStmt.SetBindingValueByIndex(4, Item.GridPosition.X);
		InsertStmt.SetBindingValueByIndex(5, Item.GridPosition.Y);
		InsertStmt.SetBindingValueByIndex(6, static_cast<int32>(Item.GridCategory));
		InsertStmt.SetBindingValueByIndex(7, Item.WeaponSlotIndex);

		// BLOB (SerializedManifest)
		if (Item.SerializedManifest.Num() > 0)
		{
			InsertStmt.SetBindingValueByIndex(8, TArrayView<const uint8>(Item.SerializedManifest), true);
		}
		else
		{
			InsertStmt.SetBindingValueByIndex(8); // NULL
		}

		// JSON (Attachments)
		const FString AttJson = SerializeAttachmentsToJson(Item.Attachments);
		if (AttJson.IsEmpty())
		{
			InsertStmt.SetBindingValueByIndex(9, TEXT(""));
		}
		else
		{
			InsertStmt.SetBindingValueByIndex(9, AttJson);
		}

		if (!InsertStmt.Execute())
		{
			UE_LOG(LogHelluna, Error, TEXT("[SQLite] SavePlayerLoadout: Loadout INSERT 실패 — ROLLBACK | 아이템=%s | 에러: %s"),
				*Item.ItemType.ToString(), *Database->GetLastError());
			Database->Execute(TEXT("ROLLBACK;"));
			return false;
		}

		InsertStmt.Reset();
		InsertStmt.ClearBindings();
	}

	// (b) player_stash에서 해당 플레이어 전체 DELETE (비행기표 패턴: 출격 시 창고 비움)
	// TODO: [SQL전환] 추후 부분 차감이 필요하면 이 DELETE를 개별 아이템 차감으로 교체
	{
		FSQLitePreparedStatement DeleteStmt = Database->PrepareStatement(
			TEXT("DELETE FROM player_stash WHERE player_id = ?1;"));
		if (!DeleteStmt.IsValid())
		{
			UE_LOG(LogHelluna, Error, TEXT("[SQLite] SavePlayerLoadout: Stash DELETE PrepareStatement 실패 — ROLLBACK | 에러: %s"), *Database->GetLastError());
			Database->Execute(TEXT("ROLLBACK;"));
			return false;
		}
		DeleteStmt.SetBindingValueByIndex(1, PlayerId);
		if (!DeleteStmt.Execute())
		{
			UE_LOG(LogHelluna, Error, TEXT("[SQLite] SavePlayerLoadout: Stash DELETE 실패 — ROLLBACK | 에러: %s"), *Database->GetLastError());
			Database->Execute(TEXT("ROLLBACK;"));
			return false;
		}
	}

	// ── 트랜잭션 커밋 ──
	if (!Database->Execute(TEXT("COMMIT;")))
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] SavePlayerLoadout: COMMIT 실패 — ROLLBACK | 에러: %s"), *Database->GetLastError());
		Database->Execute(TEXT("ROLLBACK;"));
		return false;
	}

	UE_LOG(LogHelluna, Log, TEXT("[SQLite] SavePlayerLoadout 완료 | PlayerId=%s | Loadout %d개 INSERT + Stash DELETE"), *PlayerId, Items.Num());
	return true;
}

bool UHellunaSQLiteSubsystem::DeletePlayerLoadout(const FString& PlayerId)
{
	if (!IsDatabaseReady())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] DeletePlayerLoadout: DB가 준비되지 않음 | PlayerId=%s"), *PlayerId);
		return false;
	}

	FSQLitePreparedStatement DeleteStmt = Database->PrepareStatement(
		TEXT("DELETE FROM player_loadout WHERE player_id = ?1;"));
	if (!DeleteStmt.IsValid())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] DeletePlayerLoadout: PrepareStatement 실패 | 에러: %s"), *Database->GetLastError());
		return false;
	}

	DeleteStmt.SetBindingValueByIndex(1, PlayerId);
	if (!DeleteStmt.Execute())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] DeletePlayerLoadout: DELETE 실패 | 에러: %s"), *Database->GetLastError());
		return false;
	}

	UE_LOG(LogHelluna, Log, TEXT("[SQLite] DeletePlayerLoadout 완료 | PlayerId=%s"), *PlayerId);
	return true;
}

bool UHellunaSQLiteSubsystem::MergeGameResultToStash(const FString& PlayerId, const TArray<FInv_SavedItemData>& ResultItems)
{
	if (!IsDatabaseReady())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] MergeGameResultToStash: DB가 준비되지 않음 | PlayerId=%s"), *PlayerId);
		return false;
	}

	if (ResultItems.Num() == 0)
	{
		UE_LOG(LogHelluna, Log, TEXT("[SQLite] MergeGameResultToStash: 결과 아이템 없음 — 스킵 | PlayerId=%s"), *PlayerId);
		return true;
	}

	// 트랜잭션 시작 (기존 Stash 유지, 결과만 INSERT — DELETE 없음)
	if (!Database->Execute(TEXT("BEGIN TRANSACTION;")))
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] MergeGameResultToStash: BEGIN TRANSACTION 실패 | 에러: %s"), *Database->GetLastError());
		return false;
	}

	const TCHAR* InsertSQL = TEXT(
		"INSERT INTO player_stash "
		"(player_id, item_type, stack_count, grid_position_x, grid_position_y, "
		"grid_category, is_equipped, weapon_slot, serialized_manifest, attachments_json) "
		"VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10);"
	);

	FSQLitePreparedStatement InsertStmt = Database->PrepareStatement(InsertSQL, ESQLitePreparedStatementFlags::Persistent);
	if (!InsertStmt.IsValid())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] MergeGameResultToStash: PrepareStatement 실패 — ROLLBACK | 에러: %s"), *Database->GetLastError());
		Database->Execute(TEXT("ROLLBACK;"));
		return false;
	}

	for (const FInv_SavedItemData& Item : ResultItems)
	{
		InsertStmt.SetBindingValueByIndex(1, PlayerId);
		InsertStmt.SetBindingValueByIndex(2, Item.ItemType.ToString());
		InsertStmt.SetBindingValueByIndex(3, Item.StackCount);
		InsertStmt.SetBindingValueByIndex(4, Item.GridPosition.X);
		InsertStmt.SetBindingValueByIndex(5, Item.GridPosition.Y);
		InsertStmt.SetBindingValueByIndex(6, static_cast<int32>(Item.GridCategory));
		InsertStmt.SetBindingValueByIndex(7, Item.bEquipped ? 1 : 0);
		InsertStmt.SetBindingValueByIndex(8, Item.WeaponSlotIndex);

		if (Item.SerializedManifest.Num() > 0)
		{
			InsertStmt.SetBindingValueByIndex(9, TArrayView<const uint8>(Item.SerializedManifest), true);
		}
		else
		{
			InsertStmt.SetBindingValueByIndex(9); // NULL
		}

		const FString AttJson = SerializeAttachmentsToJson(Item.Attachments);
		if (AttJson.IsEmpty())
		{
			InsertStmt.SetBindingValueByIndex(10, TEXT(""));
		}
		else
		{
			InsertStmt.SetBindingValueByIndex(10, AttJson);
		}

		if (!InsertStmt.Execute())
		{
			UE_LOG(LogHelluna, Error, TEXT("[SQLite] MergeGameResultToStash: INSERT 실패 — ROLLBACK | 아이템=%s | 에러: %s"),
				*Item.ItemType.ToString(), *Database->GetLastError());
			Database->Execute(TEXT("ROLLBACK;"));
			return false;
		}

		InsertStmt.Reset();
		InsertStmt.ClearBindings();
	}

	if (!Database->Execute(TEXT("COMMIT;")))
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] MergeGameResultToStash: COMMIT 실패 — ROLLBACK | 에러: %s"), *Database->GetLastError());
		Database->Execute(TEXT("ROLLBACK;"));
		return false;
	}

	UE_LOG(LogHelluna, Log, TEXT("[SQLite] MergeGameResultToStash 완료 | PlayerId=%s | 결과 아이템 %d개 병합"), *PlayerId, ResultItems.Num());
	return true;
}

// ============================================================
// IInventoryDatabase — 크래시 복구 (Phase 2 Step 2-5)
// ============================================================

bool UHellunaSQLiteSubsystem::HasPendingLoadout(const FString& PlayerId)
{
	if (!IsDatabaseReady())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] HasPendingLoadout: DB가 준비되지 않음 | PlayerId=%s"), *PlayerId);
		return false;
	}

	const TCHAR* CountSQL = TEXT("SELECT COUNT(*) FROM player_loadout WHERE player_id = ?1;");
	FSQLitePreparedStatement CountStmt = Database->PrepareStatement(CountSQL);
	if (!CountStmt.IsValid())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] HasPendingLoadout: PrepareStatement 실패 | 에러: %s"), *Database->GetLastError());
		return false;
	}

	CountStmt.SetBindingValueByIndex(1, PlayerId);

	int64 Count = 0;
	CountStmt.Execute([&Count](const FSQLitePreparedStatement& Stmt) -> ESQLitePreparedStatementExecuteRowResult
	{
		Stmt.GetColumnValueByIndex(0, Count);
		return ESQLitePreparedStatementExecuteRowResult::Stop;
	});

	if (Count > 0)
	{
		UE_LOG(LogHelluna, Warning, TEXT("[SQLite] HasPendingLoadout: Loadout 잔존 감지 (비정상 종료 의심) | PlayerId=%s | 잔존 %lld개"), *PlayerId, Count);
	}

	return Count > 0;
}

bool UHellunaSQLiteSubsystem::RecoverFromCrash(const FString& PlayerId)
{
	if (!IsDatabaseReady())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] RecoverFromCrash: DB가 준비되지 않음 | PlayerId=%s"), *PlayerId);
		return false;
	}

	// ── 트랜잭션 시작 (Loadout SELECT → Stash INSERT → Loadout DELETE) ──
	if (!Database->Execute(TEXT("BEGIN TRANSACTION;")))
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] RecoverFromCrash: BEGIN TRANSACTION 실패 | 에러: %s"), *Database->GetLastError());
		return false;
	}

	// (a) player_loadout에서 해당 플레이어 데이터 SELECT
	const TCHAR* SelectSQL = TEXT(
		"SELECT item_type, stack_count, grid_position_x, grid_position_y, "
		"grid_category, weapon_slot, serialized_manifest, attachments_json "
		"FROM player_loadout WHERE player_id = ?1;"
	);

	FSQLitePreparedStatement SelectStmt = Database->PrepareStatement(SelectSQL);
	if (!SelectStmt.IsValid())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] RecoverFromCrash: SELECT PrepareStatement 실패 — ROLLBACK | 에러: %s"), *Database->GetLastError());
		Database->Execute(TEXT("ROLLBACK;"));
		return false;
	}

	SelectStmt.SetBindingValueByIndex(1, PlayerId);

	TArray<FInv_SavedItemData> LoadoutItems;
	SelectStmt.Execute([&LoadoutItems](const FSQLitePreparedStatement& Stmt) -> ESQLitePreparedStatementExecuteRowResult
	{
		FInv_SavedItemData Item = ParseRowToSavedItem(Stmt);
		if (Item.IsValid())
		{
			LoadoutItems.Add(MoveTemp(Item));
		}
		return ESQLitePreparedStatementExecuteRowResult::Continue;
	});

	if (LoadoutItems.Num() == 0)
	{
		// Loadout이 비어있으면 복구할 것이 없음
		Database->Execute(TEXT("ROLLBACK;"));
		UE_LOG(LogHelluna, Log, TEXT("[SQLite] RecoverFromCrash: Loadout이 비어있음 — 복구 불필요 | PlayerId=%s"), *PlayerId);
		return true;
	}

	// (b) 해당 아이템을 player_stash에 INSERT (MergeGameResultToStash와 동일 패턴)
	const TCHAR* InsertSQL = TEXT(
		"INSERT INTO player_stash "
		"(player_id, item_type, stack_count, grid_position_x, grid_position_y, "
		"grid_category, is_equipped, weapon_slot, serialized_manifest, attachments_json) "
		"VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10);"
	);

	FSQLitePreparedStatement InsertStmt = Database->PrepareStatement(InsertSQL, ESQLitePreparedStatementFlags::Persistent);
	if (!InsertStmt.IsValid())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] RecoverFromCrash: INSERT PrepareStatement 실패 — ROLLBACK | 에러: %s"), *Database->GetLastError());
		Database->Execute(TEXT("ROLLBACK;"));
		return false;
	}

	for (const FInv_SavedItemData& Item : LoadoutItems)
	{
		InsertStmt.SetBindingValueByIndex(1, PlayerId);
		InsertStmt.SetBindingValueByIndex(2, Item.ItemType.ToString());
		InsertStmt.SetBindingValueByIndex(3, Item.StackCount);
		InsertStmt.SetBindingValueByIndex(4, Item.GridPosition.X);
		InsertStmt.SetBindingValueByIndex(5, Item.GridPosition.Y);
		InsertStmt.SetBindingValueByIndex(6, static_cast<int32>(Item.GridCategory));
		InsertStmt.SetBindingValueByIndex(7, Item.bEquipped ? 1 : 0);
		InsertStmt.SetBindingValueByIndex(8, Item.WeaponSlotIndex);

		// BLOB (SerializedManifest)
		if (Item.SerializedManifest.Num() > 0)
		{
			InsertStmt.SetBindingValueByIndex(9, TArrayView<const uint8>(Item.SerializedManifest), true);
		}
		else
		{
			InsertStmt.SetBindingValueByIndex(9); // NULL
		}

		// JSON (Attachments)
		const FString AttJson = SerializeAttachmentsToJson(Item.Attachments);
		if (AttJson.IsEmpty())
		{
			InsertStmt.SetBindingValueByIndex(10, TEXT(""));
		}
		else
		{
			InsertStmt.SetBindingValueByIndex(10, AttJson);
		}

		if (!InsertStmt.Execute())
		{
			UE_LOG(LogHelluna, Error, TEXT("[SQLite] RecoverFromCrash: Stash INSERT 실패 — ROLLBACK | 아이템=%s | 에러: %s"),
				*Item.ItemType.ToString(), *Database->GetLastError());
			Database->Execute(TEXT("ROLLBACK;"));
			return false;
		}

		InsertStmt.Reset();
		InsertStmt.ClearBindings();
	}

	// (c) player_loadout에서 DELETE
	{
		FSQLitePreparedStatement DeleteStmt = Database->PrepareStatement(
			TEXT("DELETE FROM player_loadout WHERE player_id = ?1;"));
		if (!DeleteStmt.IsValid())
		{
			UE_LOG(LogHelluna, Error, TEXT("[SQLite] RecoverFromCrash: DELETE PrepareStatement 실패 — ROLLBACK | 에러: %s"), *Database->GetLastError());
			Database->Execute(TEXT("ROLLBACK;"));
			return false;
		}
		DeleteStmt.SetBindingValueByIndex(1, PlayerId);
		if (!DeleteStmt.Execute())
		{
			UE_LOG(LogHelluna, Error, TEXT("[SQLite] RecoverFromCrash: Loadout DELETE 실패 — ROLLBACK | 에러: %s"), *Database->GetLastError());
			Database->Execute(TEXT("ROLLBACK;"));
			return false;
		}
	}

	// ── 트랜잭션 커밋 ──
	if (!Database->Execute(TEXT("COMMIT;")))
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] RecoverFromCrash: COMMIT 실패 — ROLLBACK | 에러: %s"), *Database->GetLastError());
		Database->Execute(TEXT("ROLLBACK;"));
		return false;
	}

	UE_LOG(LogHelluna, Log, TEXT("[SQLite] RecoverFromCrash 완료 | PlayerId=%s | 복구 아이템 %d개"), *PlayerId, LoadoutItems.Num());
	return true;
}
