// File: Source/Helluna/Private/Lobby/Database/HellunaSQLiteSubsystem.cpp

#include "Lobby/Database/HellunaSQLiteSubsystem.h"
#include "SQLiteDatabase.h"     // FSQLiteDatabase (엔진 SQLiteCore 모듈)
#include "Misc/Paths.h"         // FPaths
#include "HAL/FileManager.h"    // IFileManager (디렉토리 생성)
#include "Helluna.h"            // LogHelluna

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
// IInventoryDatabase 인터페이스 — Stub 구현
// Phase 2에서 각 함수에 실제 SQL 쿼리를 채워넣을 예정
// ============================================================

// TODO: [Phase 2 Step 2-X] 실제 쿼리 구현 예정
TArray<FInv_SavedItemData> UHellunaSQLiteSubsystem::LoadPlayerStash(const FString& PlayerId)
{
	if (!IsDatabaseReady())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] LoadPlayerStash: DB가 준비되지 않음 | PlayerId=%s"), *PlayerId);
		return TArray<FInv_SavedItemData>();
	}

	UE_LOG(LogHelluna, Warning, TEXT("[SQLite] %s: 아직 미구현 (stub) | PlayerId=%s"), *FString(__FUNCTION__), *PlayerId);
	return TArray<FInv_SavedItemData>();
}

// TODO: [Phase 2 Step 2-X] 실제 쿼리 구현 예정
bool UHellunaSQLiteSubsystem::SavePlayerStash(const FString& PlayerId, const TArray<FInv_SavedItemData>& Items)
{
	if (!IsDatabaseReady())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] SavePlayerStash: DB가 준비되지 않음 | PlayerId=%s"), *PlayerId);
		return false;
	}

	UE_LOG(LogHelluna, Warning, TEXT("[SQLite] %s: 아직 미구현 (stub) | PlayerId=%s"), *FString(__FUNCTION__), *PlayerId);
	return false;
}

// TODO: [Phase 2 Step 2-X] 실제 쿼리 구현 예정
bool UHellunaSQLiteSubsystem::IsPlayerExists(const FString& PlayerId)
{
	if (!IsDatabaseReady())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] IsPlayerExists: DB가 준비되지 않음 | PlayerId=%s"), *PlayerId);
		return false;
	}

	UE_LOG(LogHelluna, Warning, TEXT("[SQLite] %s: 아직 미구현 (stub) | PlayerId=%s"), *FString(__FUNCTION__), *PlayerId);
	return false;
}

// TODO: [Phase 2 Step 2-X] 실제 쿼리 구현 예정
TArray<FInv_SavedItemData> UHellunaSQLiteSubsystem::LoadPlayerLoadout(const FString& PlayerId)
{
	if (!IsDatabaseReady())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] LoadPlayerLoadout: DB가 준비되지 않음 | PlayerId=%s"), *PlayerId);
		return TArray<FInv_SavedItemData>();
	}

	UE_LOG(LogHelluna, Warning, TEXT("[SQLite] %s: 아직 미구현 (stub) | PlayerId=%s"), *FString(__FUNCTION__), *PlayerId);
	return TArray<FInv_SavedItemData>();
}

// TODO: [Phase 2 Step 2-X] 실제 쿼리 구현 예정
bool UHellunaSQLiteSubsystem::SavePlayerLoadout(const FString& PlayerId, const TArray<FInv_SavedItemData>& Items)
{
	if (!IsDatabaseReady())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] SavePlayerLoadout: DB가 준비되지 않음 | PlayerId=%s"), *PlayerId);
		return false;
	}

	UE_LOG(LogHelluna, Warning, TEXT("[SQLite] %s: 아직 미구현 (stub) | PlayerId=%s"), *FString(__FUNCTION__), *PlayerId);
	return false;
}

// TODO: [Phase 2 Step 2-X] 실제 쿼리 구현 예정
bool UHellunaSQLiteSubsystem::DeletePlayerLoadout(const FString& PlayerId)
{
	if (!IsDatabaseReady())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] DeletePlayerLoadout: DB가 준비되지 않음 | PlayerId=%s"), *PlayerId);
		return false;
	}

	UE_LOG(LogHelluna, Warning, TEXT("[SQLite] %s: 아직 미구현 (stub) | PlayerId=%s"), *FString(__FUNCTION__), *PlayerId);
	return false;
}

// TODO: [Phase 2 Step 2-X] 실제 쿼리 구현 예정
bool UHellunaSQLiteSubsystem::MergeGameResultToStash(const FString& PlayerId, const TArray<FInv_SavedItemData>& ResultItems)
{
	if (!IsDatabaseReady())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] MergeGameResultToStash: DB가 준비되지 않음 | PlayerId=%s"), *PlayerId);
		return false;
	}

	UE_LOG(LogHelluna, Warning, TEXT("[SQLite] %s: 아직 미구현 (stub) | PlayerId=%s"), *FString(__FUNCTION__), *PlayerId);
	return false;
}

// TODO: [Phase 2 Step 2-X] 실제 쿼리 구현 예정
bool UHellunaSQLiteSubsystem::HasPendingLoadout(const FString& PlayerId)
{
	if (!IsDatabaseReady())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] HasPendingLoadout: DB가 준비되지 않음 | PlayerId=%s"), *PlayerId);
		return false;
	}

	UE_LOG(LogHelluna, Warning, TEXT("[SQLite] %s: 아직 미구현 (stub) | PlayerId=%s"), *FString(__FUNCTION__), *PlayerId);
	return false;
}

// TODO: [Phase 2 Step 2-X] 실제 쿼리 구현 예정
bool UHellunaSQLiteSubsystem::RecoverFromCrash(const FString& PlayerId)
{
	if (!IsDatabaseReady())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] RecoverFromCrash: DB가 준비되지 않음 | PlayerId=%s"), *PlayerId);
		return false;
	}

	UE_LOG(LogHelluna, Warning, TEXT("[SQLite] %s: 아직 미구현 (stub) | PlayerId=%s"), *FString(__FUNCTION__), *PlayerId);
	return false;
}
