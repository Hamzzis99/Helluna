// File: Source/Helluna/Private/Lobby/Database/HellunaSQLiteSubsystem.cpp
// ════════════════════════════════════════════════════════════════════════════════
//
// UHellunaSQLiteSubsystem — SQLite 인벤토리 DB 구현
//
// ════════════════════════════════════════════════════════════════════════════════
//
// [이 파일의 역할]
//   IInventoryDatabase 인터페이스를 SQLite로 구현한 메인 파일.
//   Stash(창고) CRUD + Loadout(출격) CRUD + 크래시 복구 + 디버그 콘솔 명령어
//
// [함수 호출 흐름 정리]
//
//   📌 서브시스템 수명:
//     GameInstance 생성 → ShouldCreateSubsystem(true) → Initialize()
//       → OpenDatabase() → InitializeSchema() → DB 준비 완료
//     GameInstance 소멸 → Deinitialize() → CloseDatabase()
//
//   📌 저장 흐름 (게임 중 → SQLite):
//     OnAutoSaveTimer / OnInventoryControllerEndPlay / OnPlayerInventoryLogout
//       → SaveCollectedItems(virtual) [HellunaBaseGameMode 오버라이드]
//       → DB->SavePlayerStash(PlayerId, Items)
//       → BEGIN TRANSACTION → DELETE old → INSERT new → COMMIT
//
//   📌 로드 흐름 (접속 시):
//     PostLogin → LoadAndSendInventoryToClient(virtual) [HellunaBaseGameMode 오버라이드]
//       → DB->LoadPlayerStash(PlayerId)
//       → SELECT → ParseRowToSavedItem → TArray<FInv_SavedItemData>
//
//   📌 출격 흐름 (로비 → 게임서버):
//     출격 버튼 → DB->SavePlayerLoadout(PlayerId, Items)
//       → BEGIN TRANSACTION → Loadout INSERT + Stash DELETE → COMMIT
//     게임서버 PostLogin → DB->LoadPlayerLoadout(PlayerId) → 인벤토리 복원 → DB->DeletePlayerLoadout(PlayerId)
//
//   📌 게임 결과 반영:
//     게임 종료 → DB->MergeGameResultToStash(PlayerId, ResultItems)
//       → BEGIN TRANSACTION → Stash INSERT (기존 유지) → COMMIT
//
//   📌 크래시 복구:
//     로비 PostLogin → CheckAndRecoverFromCrash
//       → DB->HasPendingLoadout(PlayerId)  — Loadout 잔존 확인 (COUNT > 0)
//       → DB->RecoverFromCrash(PlayerId)   — Loadout → Stash 복귀 + Loadout DELETE
//
// [디버그 콘솔 명령어] (PIE 콘솔에서 실행)
//   Helluna.SQLite.DebugSave    [PlayerId]  — 더미 아이템 2개 Stash 저장
//   Helluna.SQLite.DebugLoad    [PlayerId]  — Stash 로드 후 로그 출력
//   Helluna.SQLite.DebugWipe    [PlayerId]  — Stash + Loadout 전체 삭제
//   Helluna.SQLite.DebugLoadout [PlayerId]  — 출격→크래시복구 전체 시나리오 테스트
//
// 작성자: Gihyeon (Claude Code 보조)
// ════════════════════════════════════════════════════════════════════════════════

#include "Lobby/Database/HellunaSQLiteSubsystem.h"
#include "SQLiteDatabase.h"              // FSQLiteDatabase — 엔진 내장 SQLiteCore 모듈
#include "SQLitePreparedStatement.h"     // FSQLitePreparedStatement — Prepared Statement 실행
#include "Misc/Paths.h"                  // FPaths — 프로젝트 경로 유틸리티
#include "HAL/FileManager.h"             // IFileManager — 디렉토리 생성
#include "Misc/Base64.h"                 // FBase64 — BLOB(부착물 매니페스트) ↔ Base64 변환
#include "Serialization/JsonReader.h"    // TJsonReader — JSON 역직렬화
#include "Serialization/JsonSerializer.h"// FJsonSerializer — JSON 직렬화/역직렬화
#include "Dom/JsonObject.h"              // FJsonObject — JSON 오브젝트
#include "Dom/JsonValue.h"               // FJsonValue — JSON 값
#include "Helluna.h"                     // LogHelluna 로그 카테고리


// ════════════════════════════════════════════════════════════════════════════════
// USubsystem 오버라이드
// ════════════════════════════════════════════════════════════════════════════════

// ──────────────────────────────────────────────────────────────
// ShouldCreateSubsystem
// ──────────────────────────────────────────────────────────────
// 언제 호출됨: GameInstance 생성 시, 엔진이 등록된 모든 GameInstanceSubsystem에 대해 호출
// 역할: true 반환 → 서브시스템 인스턴스 생성, false → 생성 안 함
// 우리는 데디서버/클라이언트 모두에서 SQLite가 필요하므로 항상 true
// ──────────────────────────────────────────────────────────────
bool UHellunaSQLiteSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	UE_LOG(LogHelluna, Log, TEXT("[SQLite] ShouldCreateSubsystem 호출 | Outer=%s"), *GetNameSafe(Outer));
	return true;
}

// ──────────────────────────────────────────────────────────────
// Initialize — 서브시스템 초기화
// ──────────────────────────────────────────────────────────────
// 언제 호출됨: ShouldCreateSubsystem이 true를 반환한 직후
// 역할:
//   1. DB 파일 경로 설정
//   2. DB 디렉토리 생성 (최초 실행 시)
//   3. OpenDatabase() 호출 → DB 연결 + 스키마 초기화
// ──────────────────────────────────────────────────────────────
void UHellunaSQLiteSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	UE_LOG(LogHelluna, Log, TEXT("[SQLite] ▶ Initialize 시작"));

	// 1. DB 파일 경로 설정: {ProjectSavedDir}/Database/Helluna.db
	//    예: D:/UnrealProject/Capston_Project/Helluna/Saved/Database/Helluna.db
	CachedDatabasePath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Database"), TEXT("Helluna.db"));
	UE_LOG(LogHelluna, Log, TEXT("[SQLite]   DB 경로: %s"), *CachedDatabasePath);

	// 2. DB 디렉토리가 없으면 생성 (최초 실행 시 필요)
	const FString DatabaseDir = FPaths::GetPath(CachedDatabasePath);
	const bool bDirCreated = IFileManager::Get().MakeDirectory(*DatabaseDir, true);
	UE_LOG(LogHelluna, Log, TEXT("[SQLite]   디렉토리 생성: %s (결과=%s)"), *DatabaseDir, bDirCreated ? TEXT("성공/이미존재") : TEXT("실패"));

	// 3. DB 열기 + 스키마 초기화
	if (OpenDatabase())
	{
		UE_LOG(LogHelluna, Log, TEXT("[SQLite] ✓ 서브시스템 초기화 완료"));
	}
	else
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ 서브시스템 초기화 실패 — DB 열기 실패 | 경로: %s"), *CachedDatabasePath);
	}
}

// ──────────────────────────────────────────────────────────────
// Deinitialize — 서브시스템 종료
// ──────────────────────────────────────────────────────────────
// 언제 호출됨: GameInstance 소멸 직전
// 역할: DB 연결 닫기 + 메모리 해제
// ──────────────────────────────────────────────────────────────
void UHellunaSQLiteSubsystem::Deinitialize()
{
	UE_LOG(LogHelluna, Log, TEXT("[SQLite] ▶ Deinitialize — DB 닫기 시작"));
	CloseDatabase();
	Super::Deinitialize();
	UE_LOG(LogHelluna, Log, TEXT("[SQLite] ✓ Deinitialize 완료"));
}


// ════════════════════════════════════════════════════════════════════════════════
// DB 관리
// ════════════════════════════════════════════════════════════════════════════════

// ──────────────────────────────────────────────────────────────
// OpenDatabase — DB 열기 + 스키마 초기화
// ──────────────────────────────────────────────────────────────
// 역할:
//   1. FSQLiteDatabase 인스턴스 생성 (new)
//   2. Database->Open() 호출 (ReadWriteCreate 모드 — 없으면 자동 생성)
//   3. InitializeSchema() 호출 (PRAGMA + 테이블 생성)
//
// 반환: true=성공, false=실패 (Database는 nullptr로 정리됨)
//
// 주의: FSQLiteDatabase는 UObject가 아님 → GC 관리 안 됨 → 반드시 수동 delete
// ──────────────────────────────────────────────────────────────
bool UHellunaSQLiteSubsystem::OpenDatabase()
{
	UE_LOG(LogHelluna, Log, TEXT("[SQLite] ▶ OpenDatabase 시작 | 경로: %s"), *CachedDatabasePath);

	// 이미 열려있으면 경고 후 닫기 (보통 발생하면 안 됨)
	if (Database != nullptr)
	{
		UE_LOG(LogHelluna, Warning, TEXT("[SQLite]   ⚠ 기존 DB가 이미 열려있음 — 닫고 재오픈"));
		CloseDatabase();
	}

	// 1. FSQLiteDatabase 인스턴스 생성
	Database = new FSQLiteDatabase();
	if (!ensureMsgf(Database != nullptr, TEXT("[SQLite] FSQLiteDatabase 메모리 할당 실패")))
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ FSQLiteDatabase new 실패 — 메모리 부족?"));
		bDatabaseOpen = false;
		return false;
	}

	// 2. DB 파일 열기 (ReadWriteCreate: 읽기/쓰기/없으면 생성)
	if (Database->Open(*CachedDatabasePath, ESQLiteDatabaseOpenMode::ReadWriteCreate))
	{
		bDatabaseOpen = true;
		UE_LOG(LogHelluna, Log, TEXT("[SQLite]   DB 열림 성공 | IsValid=%s"), Database->IsValid() ? TEXT("true") : TEXT("false"));

		// 3. 스키마 초기화 (PRAGMA 설정 + 테이블 생성)
		if (!InitializeSchema())
		{
			UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ 스키마 초기화 실패 — DB를 닫습니다."));
			CloseDatabase();
			return false;
		}

		UE_LOG(LogHelluna, Log, TEXT("[SQLite] ✓ OpenDatabase 성공"));
		return true;
	}
	else
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ DB 열기 실패 | 경로: %s | 에러: %s"),
			*CachedDatabasePath, *Database->GetLastError());
		delete Database;
		Database = nullptr;
		bDatabaseOpen = false;
		return false;
	}
}

// ──────────────────────────────────────────────────────────────
// CloseDatabase — DB 닫기 + 메모리 해제
// ──────────────────────────────────────────────────────────────
void UHellunaSQLiteSubsystem::CloseDatabase()
{
	if (Database == nullptr)
	{
		UE_LOG(LogHelluna, Log, TEXT("[SQLite] CloseDatabase — 이미 닫혀있음 (Database==nullptr)"));
		return;
	}

	Database->Close();
	delete Database;
	Database = nullptr;
	bDatabaseOpen = false;
	UE_LOG(LogHelluna, Log, TEXT("[SQLite] ✓ DB 닫힘 + 메모리 해제"));
}


// ════════════════════════════════════════════════════════════════════════════════
// 스키마 초기화
// ════════════════════════════════════════════════════════════════════════════════
//
// PRAGMA 설정 3개 + 테이블 3개 + 인덱스 2개 생성
//
// 테이블 스키마:
//
// ┌─ player_stash ──────────────────────────────────────────┐
// │ id (PK AUTOINCREMENT)                                   │
// │ player_id (TEXT, NOT NULL)          ← 인덱스 있음       │
// │ item_type (TEXT, NOT NULL)          ← FGameplayTag 문자열│
// │ stack_count (INTEGER, NOT NULL)                          │
// │ grid_position_x (INTEGER)                                │
// │ grid_position_y (INTEGER)                                │
// │ grid_category (INTEGER)             ← 0=장비,1=소모,2=재료│
// │ is_equipped (INTEGER)               ← 0/1 (bool)        │
// │ weapon_slot (INTEGER)               ← -1=미장착          │
// │ serialized_manifest (BLOB)          ← 매니페스트 바이너리│
// │ attachments_json (TEXT)             ← 부착물 JSON        │
// │ updated_at (DATETIME)                                    │
// └─────────────────────────────────────────────────────────┘
//
// ┌─ player_loadout ────────────────────────────────────────┐
// │ (player_stash와 동일, 단 is_equipped 컬럼 없음)         │
// │ created_at (DATETIME) — updated_at 대신                  │
// └─────────────────────────────────────────────────────────┘
//
// ┌─ schema_version ────────────────────────────────────────┐
// │ version (INTEGER)                                        │
// │ applied_at (DATETIME)                                    │
// └─────────────────────────────────────────────────────────┘
//
// ════════════════════════════════════════════════════════════════════════════════
bool UHellunaSQLiteSubsystem::InitializeSchema()
{
	check(Database != nullptr);

	UE_LOG(LogHelluna, Log, TEXT("[SQLite] ▶ InitializeSchema 시작"));

	// ── PRAGMA 설정 ──

	// WAL (Write-Ahead Logging):
	//   로비서버와 게임서버가 동시에 같은 DB에 접근할 때
	//   읽기-쓰기 동시성을 향상시킴 (기본 DELETE 모드보다 빠름)
	if (!Database->Execute(TEXT("PRAGMA journal_mode=WAL;")))
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ PRAGMA journal_mode=WAL 실패 | 에러: %s"), *Database->GetLastError());
		return false;
	}
	UE_LOG(LogHelluna, Log, TEXT("[SQLite]   PRAGMA journal_mode=WAL ✓"));

	// busy_timeout=3000:
	//   다른 프로세스(게임서버)가 DB를 잠그고 있을 때
	//   즉시 실패하지 않고 최대 3초까지 재시도
	//   (SQLite 기본값은 0 = 즉시 SQLITE_BUSY 반환)
	if (!Database->Execute(TEXT("PRAGMA busy_timeout=3000;")))
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ PRAGMA busy_timeout 실패 | 에러: %s"), *Database->GetLastError());
		return false;
	}
	UE_LOG(LogHelluna, Log, TEXT("[SQLite]   PRAGMA busy_timeout=3000 ✓"));

	// foreign_keys=OFF:
	//   테이블 간 FK 관계 없으므로 불필요한 검사 비활성화
	if (!Database->Execute(TEXT("PRAGMA foreign_keys=OFF;")))
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ PRAGMA foreign_keys 실패 | 에러: %s"), *Database->GetLastError());
		return false;
	}
	UE_LOG(LogHelluna, Log, TEXT("[SQLite]   PRAGMA foreign_keys=OFF ✓"));

	// ── player_stash 테이블 생성 ──
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
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ CREATE TABLE player_stash 실패 | 에러: %s"), *Database->GetLastError());
		return false;
	}
	UE_LOG(LogHelluna, Log, TEXT("[SQLite]   CREATE TABLE player_stash ✓"));

	// ── player_stash 인덱스 (player_id로 빠른 검색) ──
	if (!Database->Execute(TEXT("CREATE INDEX IF NOT EXISTS idx_stash_player_id ON player_stash(player_id);")))
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ INDEX idx_stash_player_id 실패 | 에러: %s"), *Database->GetLastError());
		return false;
	}

	// ── player_loadout 테이블 생성 ──
	// player_stash와 거의 동일하지만:
	//   - is_equipped 컬럼 없음 (출격장비에는 장착 개념 없음)
	//   - updated_at 대신 created_at (한 번 생성되면 수정되지 않음)
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
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ CREATE TABLE player_loadout 실패 | 에러: %s"), *Database->GetLastError());
		return false;
	}
	UE_LOG(LogHelluna, Log, TEXT("[SQLite]   CREATE TABLE player_loadout ✓"));

	// ── player_loadout 인덱스 ──
	if (!Database->Execute(TEXT("CREATE INDEX IF NOT EXISTS idx_loadout_player_id ON player_loadout(player_id);")))
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ INDEX idx_loadout_player_id 실패 | 에러: %s"), *Database->GetLastError());
		return false;
	}

	// ── schema_version 테이블 (DB 마이그레이션용) ──
	if (!Database->Execute(TEXT(
		"CREATE TABLE IF NOT EXISTS schema_version ("
		"    version     INTEGER NOT NULL,"
		"    applied_at  DATETIME DEFAULT CURRENT_TIMESTAMP"
		");"
	)))
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ CREATE TABLE schema_version 실패 | 에러: %s"), *Database->GetLastError());
		return false;
	}

	// 스키마 버전 초기값 (없을 때만 INSERT)
	if (!Database->Execute(TEXT("INSERT OR IGNORE INTO schema_version (rowid, version) VALUES (1, 1);")))
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ INSERT schema_version 실패 | 에러: %s"), *Database->GetLastError());
		return false;
	}

	UE_LOG(LogHelluna, Log, TEXT("[SQLite] ✓ InitializeSchema 완료 (테이블 3개, 인덱스 2개)"));
	return true;
}


// ════════════════════════════════════════════════════════════════════════════════
// DB 상태 확인
// ════════════════════════════════════════════════════════════════════════════════

// ──────────────────────────────────────────────────────────────
// IsDatabaseReady — DB 사용 가능 여부
// ──────────────────────────────────────────────────────────────
// 모든 CRUD 함수 진입부에서 반드시 호출!
// 세 가지 조건 모두 만족해야 true:
//   1. bDatabaseOpen = true (OpenDatabase 성공)
//   2. Database != nullptr (메모리 할당됨)
//   3. Database->IsValid() (SQLite 내부 핸들 유효)
// ──────────────────────────────────────────────────────────────
bool UHellunaSQLiteSubsystem::IsDatabaseReady() const
{
	const bool bReady = bDatabaseOpen && Database != nullptr && Database->IsValid();

	// 문제 진단용: false일 때 어떤 조건이 실패했는지 로그
	if (!bReady)
	{
		UE_LOG(LogHelluna, Warning, TEXT("[SQLite] IsDatabaseReady=false | bDatabaseOpen=%s | Database=%s | IsValid=%s"),
			bDatabaseOpen ? TEXT("true") : TEXT("false"),
			Database != nullptr ? TEXT("존재") : TEXT("nullptr"),
			(Database != nullptr && Database->IsValid()) ? TEXT("true") : TEXT("false"));
	}

	return bReady;
}

FString UHellunaSQLiteSubsystem::GetDatabasePath() const
{
	return CachedDatabasePath;
}


// ════════════════════════════════════════════════════════════════════════════════
// FInv_SavedItemData ↔ DB 변환 헬퍼
// ════════════════════════════════════════════════════════════════════════════════
//
// DB의 각 행(row)과 게임 데이터 구조체 사이의 변환을 담당.
// 이 헬퍼들은 static → 인스턴스 없이 호출 가능.
//
// ════════════════════════════════════════════════════════════════════════════════

// ──────────────────────────────────────────────────────────────
// SerializeAttachmentsToJson
// ──────────────────────────────────────────────────────────────
// TArray<FInv_SavedAttachmentData> → JSON 문자열
//
// JSON 형식 예시:
//   [
//     {"t":"Weapon.Attachment.Scope","s":0,"at":"Attachment.Scope","m":"Base64..."},
//     {"t":"Weapon.Attachment.Grip","s":1,"at":"Attachment.Grip"}
//   ]
//
// 키 약어 (DB 저장 공간 절약):
//   t  = AttachmentItemType (FGameplayTag)
//   s  = SlotIndex (int)
//   at = AttachmentType (FGameplayTag)
//   m  = SerializedManifest (Base64, 있을 때만)
// ──────────────────────────────────────────────────────────────
FString UHellunaSQLiteSubsystem::SerializeAttachmentsToJson(const TArray<FInv_SavedAttachmentData>& Attachments)
{
	if (Attachments.Num() == 0)
	{
		return FString();  // 빈 문자열 → DB에 빈 TEXT로 저장됨
	}

	TArray<TSharedPtr<FJsonValue>> JsonArray;
	for (const FInv_SavedAttachmentData& Att : Attachments)
	{
		TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("t"), Att.AttachmentItemType.ToString());   // 부착물 아이템 타입
		Obj->SetNumberField(TEXT("s"), Att.SlotIndex);                       // 슬롯 번호
		Obj->SetStringField(TEXT("at"), Att.AttachmentType.ToString());      // 부착물 종류

		// 매니페스트가 있을 때만 Base64로 인코딩하여 저장 (없으면 키 자체를 생략)
		if (Att.SerializedManifest.Num() > 0)
		{
			Obj->SetStringField(TEXT("m"), FBase64::Encode(Att.SerializedManifest));
		}

		JsonArray.Add(MakeShared<FJsonValueObject>(Obj));
	}

	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(JsonArray, Writer);

	UE_LOG(LogHelluna, Verbose, TEXT("[SQLite] SerializeAttachments: %d개 → JSON %d자"), Attachments.Num(), OutputString.Len());
	return OutputString;
}

// ──────────────────────────────────────────────────────────────
// DeserializeAttachmentsFromJson
// ──────────────────────────────────────────────────────────────
// JSON 문자열 → TArray<FInv_SavedAttachmentData> (위의 역변환)
// ──────────────────────────────────────────────────────────────
TArray<FInv_SavedAttachmentData> UHellunaSQLiteSubsystem::DeserializeAttachmentsFromJson(const FString& JsonString)
{
	TArray<FInv_SavedAttachmentData> Result;

	if (JsonString.IsEmpty())
	{
		return Result;  // 빈 문자열 → 부착물 없음
	}

	// JSON 파싱
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
	TArray<TSharedPtr<FJsonValue>> JsonArray;
	if (!FJsonSerializer::Deserialize(Reader, JsonArray))
	{
		UE_LOG(LogHelluna, Warning, TEXT("[SQLite] DeserializeAttachments: JSON 파싱 실패 | JSON=%s"), *JsonString);
		return Result;
	}

	// 각 JSON 오브젝트 → FInv_SavedAttachmentData 변환
	for (const TSharedPtr<FJsonValue>& Value : JsonArray)
	{
		const TSharedPtr<FJsonObject> Obj = Value->AsObject();
		if (!Obj.IsValid())
		{
			UE_LOG(LogHelluna, Warning, TEXT("[SQLite] DeserializeAttachments: JSON 배열 원소가 오브젝트가 아님 — 스킵"));
			continue;
		}

		FInv_SavedAttachmentData Att;

		// "t" → AttachmentItemType (FGameplayTag)
		// RequestGameplayTag의 두 번째 인자 false = 태그가 없어도 크래시하지 않음
		Att.AttachmentItemType = FGameplayTag::RequestGameplayTag(FName(*Obj->GetStringField(TEXT("t"))), false);

		// "s" → SlotIndex
		Att.SlotIndex = static_cast<int32>(Obj->GetNumberField(TEXT("s")));

		// "at" → AttachmentType
		Att.AttachmentType = FGameplayTag::RequestGameplayTag(FName(*Obj->GetStringField(TEXT("at"))), false);

		// "m" → SerializedManifest (Base64 디코딩, 있을 때만)
		FString ManifestB64;
		if (Obj->TryGetStringField(TEXT("m"), ManifestB64) && !ManifestB64.IsEmpty())
		{
			FBase64::Decode(ManifestB64, Att.SerializedManifest);
		}

		Result.Add(MoveTemp(Att));
	}

	UE_LOG(LogHelluna, Verbose, TEXT("[SQLite] DeserializeAttachments: JSON → %d개 파싱 완료"), Result.Num());
	return Result;
}

// ──────────────────────────────────────────────────────────────
// ParseRowToSavedItem
// ──────────────────────────────────────────────────────────────
// SELECT 결과의 한 행(row)을 FInv_SavedItemData로 변환
//
// 컬럼 이름으로 값을 읽음 (GetColumnValueByName)
// → 컬럼이 없으면 조용히 실패하고 기본값 유지
//   (player_loadout에는 is_equipped 컬럼이 없지만 안전하게 동작)
//
// ─── 컬럼 매핑 ───
// item_type           → Item.ItemType (FGameplayTag)
// stack_count         → Item.StackCount (int32)
// grid_position_x     → Item.GridPosition.X (int32)
// grid_position_y     → Item.GridPosition.Y (int32)
// grid_category       → Item.GridCategory (uint8)
// is_equipped         → Item.bEquipped (bool) — Loadout에는 없음(기본 false)
// weapon_slot         → Item.WeaponSlotIndex (int32)
// serialized_manifest → Item.SerializedManifest (TArray<uint8>)
// attachments_json    → Item.Attachments (TArray<FInv_SavedAttachmentData>)
// ──────────────────────────────────────────────────────────────
FInv_SavedItemData UHellunaSQLiteSubsystem::ParseRowToSavedItem(const FSQLitePreparedStatement& Statement)
{
	FInv_SavedItemData Item;

	// ── item_type → FGameplayTag ──
	FString ItemTypeStr;
	Statement.GetColumnValueByName(TEXT("item_type"), ItemTypeStr);
	Item.ItemType = FGameplayTag::RequestGameplayTag(FName(*ItemTypeStr), false);

	// ── stack_count ──
	Statement.GetColumnValueByName(TEXT("stack_count"), Item.StackCount);

	// ── grid_position (X, Y) ──
	int32 PosX = -1, PosY = -1;
	Statement.GetColumnValueByName(TEXT("grid_position_x"), PosX);
	Statement.GetColumnValueByName(TEXT("grid_position_y"), PosY);
	Item.GridPosition = FIntPoint(PosX, PosY);

	// ── grid_category (0=장비, 1=소모품, 2=재료) ──
	int32 GridCat = 0;
	Statement.GetColumnValueByName(TEXT("grid_category"), GridCat);
	Item.GridCategory = static_cast<uint8>(GridCat);

	// ── is_equipped (player_loadout에는 이 컬럼이 없음 → 기본값 0 유지) ──
	int32 Equipped = 0;
	Statement.GetColumnValueByName(TEXT("is_equipped"), Equipped);
	Item.bEquipped = (Equipped != 0);

	// ── weapon_slot (-1 = 미장착) ──
	Statement.GetColumnValueByName(TEXT("weapon_slot"), Item.WeaponSlotIndex);

	// ── serialized_manifest (BLOB — 아이템 매니페스트 바이너리 데이터) ──
	Statement.GetColumnValueByName(TEXT("serialized_manifest"), Item.SerializedManifest);

	// ── attachments_json → TArray<FInv_SavedAttachmentData> ──
	FString AttJson;
	Statement.GetColumnValueByName(TEXT("attachments_json"), AttJson);
	Item.Attachments = DeserializeAttachmentsFromJson(AttJson);

	UE_LOG(LogHelluna, Verbose, TEXT("[SQLite] ParseRow: ItemType=%s | Stack=%d | Grid=(%d,%d) | Cat=%d | Equipped=%d | Slot=%d | Att=%d개"),
		*ItemTypeStr, Item.StackCount, PosX, PosY, GridCat, Equipped, Item.WeaponSlotIndex, Item.Attachments.Num());

	return Item;
}


// ════════════════════════════════════════════════════════════════════════════════
// IInventoryDatabase — Stash(창고) CRUD 구현
// ════════════════════════════════════════════════════════════════════════════════

// ──────────────────────────────────────────────────────────────
// LoadPlayerStash — 창고 아이템 전체 로드
// ──────────────────────────────────────────────────────────────
// SQL: SELECT * FROM player_stash WHERE player_id = ?
// → 각 행을 ParseRowToSavedItem으로 변환
// → TArray<FInv_SavedItemData> 반환
//
// 호출 시점:
//   - HellunaBaseGameMode::LoadAndSendInventoryToClient()
//   - 디버그 콘솔: Helluna.SQLite.DebugLoad
// ──────────────────────────────────────────────────────────────
TArray<FInv_SavedItemData> UHellunaSQLiteSubsystem::LoadPlayerStash(const FString& PlayerId)
{
	UE_LOG(LogHelluna, Log, TEXT("[SQLite] ▶ LoadPlayerStash | PlayerId=%s"), *PlayerId);

	if (!IsDatabaseReady())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ LoadPlayerStash: DB가 준비되지 않음"));
		return TArray<FInv_SavedItemData>();
	}

	// SELECT 쿼리 준비 (?1 = player_id 파라미터 바인딩)
	const TCHAR* SelectSQL = TEXT(
		"SELECT item_type, stack_count, grid_position_x, grid_position_y, "
		"grid_category, is_equipped, weapon_slot, serialized_manifest, attachments_json "
		"FROM player_stash WHERE player_id = ?1;"
	);

	FSQLitePreparedStatement SelectStmt = Database->PrepareStatement(SelectSQL);
	if (!SelectStmt.IsValid())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ LoadPlayerStash: PrepareStatement 실패 | 에러: %s"), *Database->GetLastError());
		return TArray<FInv_SavedItemData>();
	}

	// ?1에 PlayerId 바인딩
	SelectStmt.SetBindingValueByIndex(1, PlayerId);

	// 쿼리 실행 — 각 행마다 콜백 호출
	TArray<FInv_SavedItemData> Result;
	SelectStmt.Execute([&Result](const FSQLitePreparedStatement& Stmt) -> ESQLitePreparedStatementExecuteRowResult
	{
		FInv_SavedItemData Item = ParseRowToSavedItem(Stmt);
		if (Item.IsValid())
		{
			Result.Add(MoveTemp(Item));
		}
		return ESQLitePreparedStatementExecuteRowResult::Continue;  // 다음 행 계속
	});

	UE_LOG(LogHelluna, Log, TEXT("[SQLite] ✓ LoadPlayerStash 완료 | PlayerId=%s | 아이템 %d개"), *PlayerId, Result.Num());
	return Result;
}

// ──────────────────────────────────────────────────────────────
// SavePlayerStash — 창고 전체 저장 (전부 교체 방식)
// ──────────────────────────────────────────────────────────────
// 내부 처리 (하나의 트랜잭션):
//   1. BEGIN TRANSACTION
//   2. DELETE FROM player_stash WHERE player_id = ?  (기존 전부 삭제)
//   3. INSERT INTO player_stash ... (Items 각각 INSERT)
//   4. COMMIT (또는 실패 시 ROLLBACK)
//
// Items가 빈 배열이면 DELETE만 수행됨 = 창고 비우기
//
// 호출 시점:
//   - HellunaBaseGameMode::SaveCollectedItems()
//   - 디버그 콘솔: Helluna.SQLite.DebugSave
//
// Persistent Statement:
//   반복 INSERT에 ESQLitePreparedStatementFlags::Persistent 사용
//   → SQLite가 쿼리 계획을 캐시하여 반복 실행 시 성능 향상
// ──────────────────────────────────────────────────────────────
bool UHellunaSQLiteSubsystem::SavePlayerStash(const FString& PlayerId, const TArray<FInv_SavedItemData>& Items)
{
	UE_LOG(LogHelluna, Log, TEXT("[SQLite] ▶ SavePlayerStash | PlayerId=%s | 아이템 %d개"), *PlayerId, Items.Num());

	if (!IsDatabaseReady())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ SavePlayerStash: DB가 준비되지 않음"));
		return false;
	}

	// ── 트랜잭션 시작 ──
	// 여러 SQL을 하나의 원자적 단위로 묶음
	// → 중간에 실패하면 ROLLBACK으로 전부 취소 (데이터 정합성 보장)
	if (!Database->Execute(TEXT("BEGIN TRANSACTION;")))
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ SavePlayerStash: BEGIN TRANSACTION 실패 | 에러: %s"), *Database->GetLastError());
		return false;
	}
	UE_LOG(LogHelluna, Verbose, TEXT("[SQLite]   BEGIN TRANSACTION ✓"));

	// (1) 기존 Stash 전부 삭제
	{
		FSQLitePreparedStatement DeleteStmt = Database->PrepareStatement(
			TEXT("DELETE FROM player_stash WHERE player_id = ?1;"));
		if (!DeleteStmt.IsValid())
		{
			UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ SavePlayerStash: DELETE Prepare 실패 | 에러: %s"), *Database->GetLastError());
			Database->Execute(TEXT("ROLLBACK;"));
			return false;
		}
		DeleteStmt.SetBindingValueByIndex(1, PlayerId);
		if (!DeleteStmt.Execute())
		{
			UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ SavePlayerStash: DELETE 실패 — ROLLBACK | 에러: %s"), *Database->GetLastError());
			Database->Execute(TEXT("ROLLBACK;"));
			return false;
		}
		UE_LOG(LogHelluna, Verbose, TEXT("[SQLite]   DELETE old stash ✓"));
	}

	// (2) 새 아이템 INSERT (배치)
	if (Items.Num() > 0)
	{
		const TCHAR* InsertSQL = TEXT(
			"INSERT INTO player_stash "
			"(player_id, item_type, stack_count, grid_position_x, grid_position_y, "
			"grid_category, is_equipped, weapon_slot, serialized_manifest, attachments_json) "
			"VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10);"
		);

		// Persistent: 반복 INSERT 시 쿼리 계획 캐시
		FSQLitePreparedStatement InsertStmt = Database->PrepareStatement(InsertSQL, ESQLitePreparedStatementFlags::Persistent);
		if (!InsertStmt.IsValid())
		{
			UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ SavePlayerStash: INSERT Prepare 실패 — ROLLBACK | 에러: %s"), *Database->GetLastError());
			Database->Execute(TEXT("ROLLBACK;"));
			return false;
		}

		for (int32 i = 0; i < Items.Num(); ++i)
		{
			const FInv_SavedItemData& Item = Items[i];

			// 10개 파라미터 바인딩 (?1 ~ ?10)
			InsertStmt.SetBindingValueByIndex(1, PlayerId);                                   // ?1: player_id
			InsertStmt.SetBindingValueByIndex(2, Item.ItemType.ToString());                   // ?2: item_type
			InsertStmt.SetBindingValueByIndex(3, Item.StackCount);                            // ?3: stack_count
			InsertStmt.SetBindingValueByIndex(4, Item.GridPosition.X);                        // ?4: grid_position_x
			InsertStmt.SetBindingValueByIndex(5, Item.GridPosition.Y);                        // ?5: grid_position_y
			InsertStmt.SetBindingValueByIndex(6, static_cast<int32>(Item.GridCategory));       // ?6: grid_category
			InsertStmt.SetBindingValueByIndex(7, Item.bEquipped ? 1 : 0);                     // ?7: is_equipped
			InsertStmt.SetBindingValueByIndex(8, Item.WeaponSlotIndex);                       // ?8: weapon_slot

			// ?9: serialized_manifest (BLOB — 있을 때만, 없으면 NULL)
			if (Item.SerializedManifest.Num() > 0)
			{
				InsertStmt.SetBindingValueByIndex(9, TArrayView<const uint8>(Item.SerializedManifest), true);
			}
			else
			{
				InsertStmt.SetBindingValueByIndex(9); // NULL 바인딩
			}

			// ?10: attachments_json (JSON 문자열 — 부착물 목록)
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
				UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ SavePlayerStash: INSERT[%d] 실패 — ROLLBACK | 아이템=%s | 에러: %s"),
					i, *Item.ItemType.ToString(), *Database->GetLastError());
				Database->Execute(TEXT("ROLLBACK;"));
				return false;
			}

			// Reset + ClearBindings: 다음 행 INSERT 준비
			// (Persistent Statement는 재사용해야 하므로 Reset 필수)
			InsertStmt.Reset();
			InsertStmt.ClearBindings();
		}
		UE_LOG(LogHelluna, Verbose, TEXT("[SQLite]   INSERT %d개 ✓"), Items.Num());
	}

	// (3) 커밋
	if (!Database->Execute(TEXT("COMMIT;")))
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ SavePlayerStash: COMMIT 실패 — ROLLBACK | 에러: %s"), *Database->GetLastError());
		Database->Execute(TEXT("ROLLBACK;"));
		return false;
	}

	UE_LOG(LogHelluna, Log, TEXT("[SQLite] ✓ SavePlayerStash 완료 | PlayerId=%s | 아이템 %d개"), *PlayerId, Items.Num());
	return true;
}

// ──────────────────────────────────────────────────────────────
// IsPlayerExists — 해당 플레이어의 Stash 데이터 존재 여부
// ──────────────────────────────────────────────────────────────
// SQL: SELECT COUNT(*) FROM player_stash WHERE player_id = ?
// → COUNT > 0 이면 true
// ──────────────────────────────────────────────────────────────
bool UHellunaSQLiteSubsystem::IsPlayerExists(const FString& PlayerId)
{
	UE_LOG(LogHelluna, Verbose, TEXT("[SQLite] IsPlayerExists | PlayerId=%s"), *PlayerId);

	if (!IsDatabaseReady())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ IsPlayerExists: DB가 준비되지 않음 | PlayerId=%s"), *PlayerId);
		return false;
	}

	const TCHAR* CountSQL = TEXT("SELECT COUNT(*) FROM player_stash WHERE player_id = ?1;");
	FSQLitePreparedStatement CountStmt = Database->PrepareStatement(CountSQL);
	if (!CountStmt.IsValid())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ IsPlayerExists: PrepareStatement 실패 | 에러: %s"), *Database->GetLastError());
		return false;
	}

	CountStmt.SetBindingValueByIndex(1, PlayerId);

	int64 Count = 0;
	CountStmt.Execute([&Count](const FSQLitePreparedStatement& Stmt) -> ESQLitePreparedStatementExecuteRowResult
	{
		Stmt.GetColumnValueByIndex(0, Count);
		return ESQLitePreparedStatementExecuteRowResult::Stop;  // 1행만 읽으면 됨
	});

	UE_LOG(LogHelluna, Verbose, TEXT("[SQLite] IsPlayerExists: PlayerId=%s | COUNT=%lld | 존재=%s"),
		*PlayerId, Count, Count > 0 ? TEXT("true") : TEXT("false"));
	return Count > 0;
}


// ════════════════════════════════════════════════════════════════════════════════
// IInventoryDatabase — Loadout(출격) CRUD 구현
// ════════════════════════════════════════════════════════════════════════════════

// ──────────────────────────────────────────────────────────────
// LoadPlayerLoadout — 출격장비 로드
// ──────────────────────────────────────────────────────────────
// SQL: SELECT * FROM player_loadout WHERE player_id = ?
//
// 주의: player_loadout에는 is_equipped 컬럼이 없음!
//   → SELECT 목록에서 is_equipped 제외
//   → ParseRowToSavedItem에서 GetColumnValueByName("is_equipped")는
//     조용히 실패하고 기본값 0(false) 유지 → 문제없음
//
// 호출 시점:
//   - 게임서버 PostLogin에서 LoadPlayerLoadout → InvComp에 복원
// ──────────────────────────────────────────────────────────────
TArray<FInv_SavedItemData> UHellunaSQLiteSubsystem::LoadPlayerLoadout(const FString& PlayerId)
{
	UE_LOG(LogHelluna, Log, TEXT("[SQLite] ▶ LoadPlayerLoadout | PlayerId=%s"), *PlayerId);

	if (!IsDatabaseReady())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ LoadPlayerLoadout: DB가 준비되지 않음"));
		return TArray<FInv_SavedItemData>();
	}

	// is_equipped 컬럼 없음 → SELECT에서 제외
	const TCHAR* SelectSQL = TEXT(
		"SELECT item_type, stack_count, grid_position_x, grid_position_y, "
		"grid_category, weapon_slot, serialized_manifest, attachments_json "
		"FROM player_loadout WHERE player_id = ?1;"
	);

	FSQLitePreparedStatement SelectStmt = Database->PrepareStatement(SelectSQL);
	if (!SelectStmt.IsValid())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ LoadPlayerLoadout: PrepareStatement 실패 | 에러: %s"), *Database->GetLastError());
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

	UE_LOG(LogHelluna, Log, TEXT("[SQLite] ✓ LoadPlayerLoadout 완료 | PlayerId=%s | 아이템 %d개"), *PlayerId, Result.Num());
	return Result;
}

// ──────────────────────────────────────────────────────────────
// SavePlayerLoadout — 출격 원자적 트랜잭션
// ──────────────────────────────────────────────────────────────
// "비행기표 패턴":
//   출격 = Loadout에 아이템 옮기기 + Stash에서 빼기
//   반드시 동시에 처리! (하나만 되면 아이템 복사/손실 버그)
//
// 내부 처리 (하나의 트랜잭션):
//   1. BEGIN TRANSACTION
//   2. Loadout INSERT (9개 바인딩 — is_equipped 없음)
//   3. Stash DELETE (해당 플레이어 전체)
//   4. COMMIT (또는 ROLLBACK)
//
// TODO: [SQL전환] 부분 차감이 필요하면 (3)의 전체 DELETE를 개별 아이템 DELETE로 교체
//
// 호출 시점:
//   - 로비에서 출격 버튼 클릭 시
// ──────────────────────────────────────────────────────────────
bool UHellunaSQLiteSubsystem::SavePlayerLoadout(const FString& PlayerId, const TArray<FInv_SavedItemData>& Items)
{
	UE_LOG(LogHelluna, Log, TEXT("[SQLite] ▶ SavePlayerLoadout | PlayerId=%s | 출격 아이템 %d개"), *PlayerId, Items.Num());

	if (!IsDatabaseReady())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ SavePlayerLoadout: DB가 준비되지 않음"));
		return false;
	}

	if (Items.Num() == 0)
	{
		UE_LOG(LogHelluna, Warning, TEXT("[SQLite] ⚠ SavePlayerLoadout: 출격 아이템 없음 — 스킵 | PlayerId=%s"), *PlayerId);
		return false;
	}

	// ── 트랜잭션 시작 ──
	if (!Database->Execute(TEXT("BEGIN TRANSACTION;")))
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ SavePlayerLoadout: BEGIN TRANSACTION 실패 | 에러: %s"), *Database->GetLastError());
		return false;
	}
	UE_LOG(LogHelluna, Verbose, TEXT("[SQLite]   BEGIN TRANSACTION ✓"));

	// (a) player_loadout에 Items INSERT
	//     is_equipped 컬럼 없음 → 9개 바인딩 (?1~?9)
	const TCHAR* InsertSQL = TEXT(
		"INSERT INTO player_loadout "
		"(player_id, item_type, stack_count, grid_position_x, grid_position_y, "
		"grid_category, weapon_slot, serialized_manifest, attachments_json) "
		"VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9);"
	);

	FSQLitePreparedStatement InsertStmt = Database->PrepareStatement(InsertSQL, ESQLitePreparedStatementFlags::Persistent);
	if (!InsertStmt.IsValid())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ SavePlayerLoadout: INSERT Prepare 실패 — ROLLBACK | 에러: %s"), *Database->GetLastError());
		Database->Execute(TEXT("ROLLBACK;"));
		return false;
	}

	for (int32 i = 0; i < Items.Num(); ++i)
	{
		const FInv_SavedItemData& Item = Items[i];

		InsertStmt.SetBindingValueByIndex(1, PlayerId);                                   // ?1: player_id
		InsertStmt.SetBindingValueByIndex(2, Item.ItemType.ToString());                   // ?2: item_type
		InsertStmt.SetBindingValueByIndex(3, Item.StackCount);                            // ?3: stack_count
		InsertStmt.SetBindingValueByIndex(4, Item.GridPosition.X);                        // ?4: grid_position_x
		InsertStmt.SetBindingValueByIndex(5, Item.GridPosition.Y);                        // ?5: grid_position_y
		InsertStmt.SetBindingValueByIndex(6, static_cast<int32>(Item.GridCategory));       // ?6: grid_category
		InsertStmt.SetBindingValueByIndex(7, Item.WeaponSlotIndex);                       // ?7: weapon_slot

		// ?8: serialized_manifest (BLOB)
		if (Item.SerializedManifest.Num() > 0)
		{
			InsertStmt.SetBindingValueByIndex(8, TArrayView<const uint8>(Item.SerializedManifest), true);
		}
		else
		{
			InsertStmt.SetBindingValueByIndex(8); // NULL
		}

		// ?9: attachments_json
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
			UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ SavePlayerLoadout: Loadout INSERT[%d] 실패 — ROLLBACK | 아이템=%s | 에러: %s"),
				i, *Item.ItemType.ToString(), *Database->GetLastError());
			Database->Execute(TEXT("ROLLBACK;"));
			return false;
		}

		InsertStmt.Reset();
		InsertStmt.ClearBindings();
	}
	UE_LOG(LogHelluna, Verbose, TEXT("[SQLite]   Loadout INSERT %d개 ✓"), Items.Num());

	// (b) player_stash에서 해당 플레이어 전체 DELETE
	//     비행기표 패턴: 출격하면 창고가 비워짐
	// TODO: [SQL전환] 추후 부분 차감이 필요하면 이 DELETE를 개별 아이템 차감으로 교체
	{
		FSQLitePreparedStatement DeleteStmt = Database->PrepareStatement(
			TEXT("DELETE FROM player_stash WHERE player_id = ?1;"));
		if (!DeleteStmt.IsValid())
		{
			UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ SavePlayerLoadout: Stash DELETE Prepare 실패 — ROLLBACK | 에러: %s"), *Database->GetLastError());
			Database->Execute(TEXT("ROLLBACK;"));
			return false;
		}
		DeleteStmt.SetBindingValueByIndex(1, PlayerId);
		if (!DeleteStmt.Execute())
		{
			UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ SavePlayerLoadout: Stash DELETE 실패 — ROLLBACK | 에러: %s"), *Database->GetLastError());
			Database->Execute(TEXT("ROLLBACK;"));
			return false;
		}
		UE_LOG(LogHelluna, Verbose, TEXT("[SQLite]   Stash DELETE ✓"));
	}

	// ── 트랜잭션 커밋 ──
	if (!Database->Execute(TEXT("COMMIT;")))
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ SavePlayerLoadout: COMMIT 실패 — ROLLBACK | 에러: %s"), *Database->GetLastError());
		Database->Execute(TEXT("ROLLBACK;"));
		return false;
	}

	UE_LOG(LogHelluna, Log, TEXT("[SQLite] ✓ SavePlayerLoadout 완료 | PlayerId=%s | Loadout %d개 INSERT + Stash DELETE"), *PlayerId, Items.Num());
	return true;
}

// ──────────────────────────────────────────────────────────────
// DeletePlayerLoadout — Loadout 삭제
// ──────────────────────────────────────────────────────────────
// SQL: DELETE FROM player_loadout WHERE player_id = ?
//
// 호출 시점:
//   - 게임서버 PostLogin에서 Loadout을 InvComp에 복원한 후 호출
//   - 정상적으로 삭제되면 이후 HasPendingLoadout = false
// ──────────────────────────────────────────────────────────────
bool UHellunaSQLiteSubsystem::DeletePlayerLoadout(const FString& PlayerId)
{
	UE_LOG(LogHelluna, Log, TEXT("[SQLite] ▶ DeletePlayerLoadout | PlayerId=%s"), *PlayerId);

	if (!IsDatabaseReady())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ DeletePlayerLoadout: DB가 준비되지 않음"));
		return false;
	}

	FSQLitePreparedStatement DeleteStmt = Database->PrepareStatement(
		TEXT("DELETE FROM player_loadout WHERE player_id = ?1;"));
	if (!DeleteStmt.IsValid())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ DeletePlayerLoadout: PrepareStatement 실패 | 에러: %s"), *Database->GetLastError());
		return false;
	}

	DeleteStmt.SetBindingValueByIndex(1, PlayerId);
	if (!DeleteStmt.Execute())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ DeletePlayerLoadout: DELETE 실패 | 에러: %s"), *Database->GetLastError());
		return false;
	}

	UE_LOG(LogHelluna, Log, TEXT("[SQLite] ✓ DeletePlayerLoadout 완료 | PlayerId=%s"), *PlayerId);
	return true;
}


// ════════════════════════════════════════════════════════════════════════════════
// IInventoryDatabase — 게임 결과 반영
// ════════════════════════════════════════════════════════════════════════════════

// ──────────────────────────────────────────────────────────────
// MergeGameResultToStash — 게임 결과 아이템을 Stash에 병합
// ──────────────────────────────────────────────────────────────
// 방식 B(MERGE): 기존 Stash 유지 + 결과 아이템 INSERT
//   → DELETE 없이 INSERT만! (기존 창고 아이템 보존)
//
// 호출 시점:
//   - 게임 종료 시 (탈출 성공, 방어 성공 등)
//   - 사망 시에는 ResultItems가 빈 배열 → 스킵
// ──────────────────────────────────────────────────────────────
bool UHellunaSQLiteSubsystem::MergeGameResultToStash(const FString& PlayerId, const TArray<FInv_SavedItemData>& ResultItems)
{
	UE_LOG(LogHelluna, Log, TEXT("[SQLite] ▶ MergeGameResultToStash | PlayerId=%s | 결과 아이템 %d개"), *PlayerId, ResultItems.Num());

	if (!IsDatabaseReady())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ MergeGameResultToStash: DB가 준비되지 않음"));
		return false;
	}

	if (ResultItems.Num() == 0)
	{
		UE_LOG(LogHelluna, Log, TEXT("[SQLite] ✓ MergeGameResultToStash: 결과 아이템 없음 — 스킵 (사망?)"));
		return true;  // 성공으로 처리 (할 일이 없을 뿐)
	}

	// ── 트랜잭션 시작 ──
	if (!Database->Execute(TEXT("BEGIN TRANSACTION;")))
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ MergeGameResultToStash: BEGIN TRANSACTION 실패 | 에러: %s"), *Database->GetLastError());
		return false;
	}

	// Stash INSERT (기존 DELETE 없음 → 합산!)
	const TCHAR* InsertSQL = TEXT(
		"INSERT INTO player_stash "
		"(player_id, item_type, stack_count, grid_position_x, grid_position_y, "
		"grid_category, is_equipped, weapon_slot, serialized_manifest, attachments_json) "
		"VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10);"
	);

	FSQLitePreparedStatement InsertStmt = Database->PrepareStatement(InsertSQL, ESQLitePreparedStatementFlags::Persistent);
	if (!InsertStmt.IsValid())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ MergeGameResultToStash: Prepare 실패 — ROLLBACK | 에러: %s"), *Database->GetLastError());
		Database->Execute(TEXT("ROLLBACK;"));
		return false;
	}

	for (int32 i = 0; i < ResultItems.Num(); ++i)
	{
		const FInv_SavedItemData& Item = ResultItems[i];

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
			UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ MergeGameResultToStash: INSERT[%d] 실패 — ROLLBACK | 아이템=%s | 에러: %s"),
				i, *Item.ItemType.ToString(), *Database->GetLastError());
			Database->Execute(TEXT("ROLLBACK;"));
			return false;
		}

		InsertStmt.Reset();
		InsertStmt.ClearBindings();
	}

	if (!Database->Execute(TEXT("COMMIT;")))
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ MergeGameResultToStash: COMMIT 실패 — ROLLBACK | 에러: %s"), *Database->GetLastError());
		Database->Execute(TEXT("ROLLBACK;"));
		return false;
	}

	UE_LOG(LogHelluna, Log, TEXT("[SQLite] ✓ MergeGameResultToStash 완료 | PlayerId=%s | 결과 아이템 %d개 병합"), *PlayerId, ResultItems.Num());
	return true;
}


// ════════════════════════════════════════════════════════════════════════════════
// IInventoryDatabase — 크래시 복구
// ════════════════════════════════════════════════════════════════════════════════
//
// [크래시 복구 원리]
//   정상 흐름: 출격 → Loadout 생성 → 게임 → Loadout 삭제 + Stash MERGE
//   비정상 종료: 출격 → Loadout 생성 → (크래시!) → Loadout이 남아있음
//
//   로비 재접속 시:
//     1. HasPendingLoadout() → Loadout이 남아있는지 확인 (COUNT > 0)
//     2. RecoverFromCrash() → Loadout 아이템을 Stash로 복귀 + Loadout DELETE
//
// ════════════════════════════════════════════════════════════════════════════════

// ──────────────────────────────────────────────────────────────
// HasPendingLoadout — 미처리 Loadout 잔존 확인
// ──────────────────────────────────────────────────────────────
// SQL: SELECT COUNT(*) FROM player_loadout WHERE player_id = ?
// → COUNT > 0 이면 비정상 종료 의심
//
// 호출 시점:
//   - HellunaBaseGameMode::CheckAndRecoverFromCrash() (PostLogin에서)
// ──────────────────────────────────────────────────────────────
bool UHellunaSQLiteSubsystem::HasPendingLoadout(const FString& PlayerId)
{
	UE_LOG(LogHelluna, Log, TEXT("[SQLite] ▶ HasPendingLoadout | PlayerId=%s"), *PlayerId);

	if (!IsDatabaseReady())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ HasPendingLoadout: DB가 준비되지 않음"));
		return false;
	}

	const TCHAR* CountSQL = TEXT("SELECT COUNT(*) FROM player_loadout WHERE player_id = ?1;");
	FSQLitePreparedStatement CountStmt = Database->PrepareStatement(CountSQL);
	if (!CountStmt.IsValid())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ HasPendingLoadout: PrepareStatement 실패 | 에러: %s"), *Database->GetLastError());
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
		UE_LOG(LogHelluna, Warning, TEXT("[SQLite] ⚠ HasPendingLoadout: Loadout 잔존 감지! (비정상 종료 의심) | PlayerId=%s | 잔존 %lld개"), *PlayerId, Count);
	}
	else
	{
		UE_LOG(LogHelluna, Log, TEXT("[SQLite] ✓ HasPendingLoadout: 잔존 없음 (정상) | PlayerId=%s"), *PlayerId);
	}

	return Count > 0;
}

// ──────────────────────────────────────────────────────────────
// RecoverFromCrash — Loadout → Stash 복구
// ──────────────────────────────────────────────────────────────
// 내부 처리 (하나의 트랜잭션):
//   1. BEGIN TRANSACTION
//   2. SELECT: player_loadout에서 잔존 아이템 읽기
//   3. INSERT: player_stash에 복구 (Stash로 돌려보냄)
//   4. DELETE: player_loadout 정리
//   5. COMMIT (또는 ROLLBACK)
//
// 호출 시점:
//   - HellunaBaseGameMode::CheckAndRecoverFromCrash()
//   - 디버그 콘솔: Helluna.SQLite.DebugLoadout (테스트 4단계)
// ──────────────────────────────────────────────────────────────
bool UHellunaSQLiteSubsystem::RecoverFromCrash(const FString& PlayerId)
{
	UE_LOG(LogHelluna, Log, TEXT("[SQLite] ▶ RecoverFromCrash | PlayerId=%s"), *PlayerId);

	if (!IsDatabaseReady())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ RecoverFromCrash: DB가 준비되지 않음"));
		return false;
	}

	// ── 트랜잭션 시작 ──
	if (!Database->Execute(TEXT("BEGIN TRANSACTION;")))
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ RecoverFromCrash: BEGIN TRANSACTION 실패 | 에러: %s"), *Database->GetLastError());
		return false;
	}

	// (a) player_loadout에서 잔존 아이템 SELECT
	const TCHAR* SelectSQL = TEXT(
		"SELECT item_type, stack_count, grid_position_x, grid_position_y, "
		"grid_category, weapon_slot, serialized_manifest, attachments_json "
		"FROM player_loadout WHERE player_id = ?1;"
	);

	FSQLitePreparedStatement SelectStmt = Database->PrepareStatement(SelectSQL);
	if (!SelectStmt.IsValid())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ RecoverFromCrash: SELECT Prepare 실패 — ROLLBACK | 에러: %s"), *Database->GetLastError());
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

	UE_LOG(LogHelluna, Log, TEXT("[SQLite]   SELECT 완료 | 잔존 아이템 %d개"), LoadoutItems.Num());

	if (LoadoutItems.Num() == 0)
	{
		// Loadout이 비어있으면 복구할 것이 없음 → 정상 처리
		Database->Execute(TEXT("ROLLBACK;"));
		UE_LOG(LogHelluna, Log, TEXT("[SQLite] ✓ RecoverFromCrash: Loadout이 비어있음 — 복구 불필요"));
		return true;
	}

	// (b) player_stash에 복구 INSERT (Stash로 돌려보냄)
	const TCHAR* InsertSQL = TEXT(
		"INSERT INTO player_stash "
		"(player_id, item_type, stack_count, grid_position_x, grid_position_y, "
		"grid_category, is_equipped, weapon_slot, serialized_manifest, attachments_json) "
		"VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10);"
	);

	FSQLitePreparedStatement InsertStmt = Database->PrepareStatement(InsertSQL, ESQLitePreparedStatementFlags::Persistent);
	if (!InsertStmt.IsValid())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ RecoverFromCrash: INSERT Prepare 실패 — ROLLBACK | 에러: %s"), *Database->GetLastError());
		Database->Execute(TEXT("ROLLBACK;"));
		return false;
	}

	for (int32 i = 0; i < LoadoutItems.Num(); ++i)
	{
		const FInv_SavedItemData& Item = LoadoutItems[i];

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
			UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ RecoverFromCrash: Stash INSERT[%d] 실패 — ROLLBACK | 아이템=%s | 에러: %s"),
				i, *Item.ItemType.ToString(), *Database->GetLastError());
			Database->Execute(TEXT("ROLLBACK;"));
			return false;
		}

		InsertStmt.Reset();
		InsertStmt.ClearBindings();
	}
	UE_LOG(LogHelluna, Log, TEXT("[SQLite]   Stash INSERT %d개 ✓ (복구)"), LoadoutItems.Num());

	// (c) player_loadout에서 DELETE (정리)
	{
		FSQLitePreparedStatement DeleteStmt = Database->PrepareStatement(
			TEXT("DELETE FROM player_loadout WHERE player_id = ?1;"));
		if (!DeleteStmt.IsValid())
		{
			UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ RecoverFromCrash: DELETE Prepare 실패 — ROLLBACK | 에러: %s"), *Database->GetLastError());
			Database->Execute(TEXT("ROLLBACK;"));
			return false;
		}
		DeleteStmt.SetBindingValueByIndex(1, PlayerId);
		if (!DeleteStmt.Execute())
		{
			UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ RecoverFromCrash: Loadout DELETE 실패 — ROLLBACK | 에러: %s"), *Database->GetLastError());
			Database->Execute(TEXT("ROLLBACK;"));
			return false;
		}
		UE_LOG(LogHelluna, Log, TEXT("[SQLite]   Loadout DELETE ✓"));
	}

	// ── 트랜잭션 커밋 ──
	if (!Database->Execute(TEXT("COMMIT;")))
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ RecoverFromCrash: COMMIT 실패 — ROLLBACK | 에러: %s"), *Database->GetLastError());
		Database->Execute(TEXT("ROLLBACK;"));
		return false;
	}

	UE_LOG(LogHelluna, Log, TEXT("[SQLite] ✓ RecoverFromCrash 완료 | PlayerId=%s | 복구 아이템 %d개"), *PlayerId, LoadoutItems.Num());
	return true;
}


// ════════════════════════════════════════════════════════════════════════════════
// 디버그 콘솔 명령어 (Phase 2 Step 2-6) — 비출시 빌드 전용
// ════════════════════════════════════════════════════════════════════════════════
//
// 사용법 (PIE 실행 중 콘솔 ~ 키로 열기):
//   Helluna.SQLite.DebugSave    [PlayerId]   — 더미 아이템 2개를 Stash에 저장
//   Helluna.SQLite.DebugLoad    [PlayerId]   — Stash 로드 후 로그 출력
//   Helluna.SQLite.DebugWipe    [PlayerId]   — Stash + Loadout 전체 삭제
//   Helluna.SQLite.DebugLoadout [PlayerId]   — 출격→크래시복구 전체 시나리오 테스트
//   PlayerId 생략 시 "DebugPlayer" 사용
//
// 주의: PIE(Play In Editor) 실행 중에만 동작!
//       에디터만 켠 상태에서는 WorldContext에 GameInstance가 없어서 실패함
//
// ════════════════════════════════════════════════════════════════════════════════
#if !UE_BUILD_SHIPPING

namespace
{
	// ──────────────────────────────────────────────────────────
	// FindSQLiteSubsystem — 현재 PIE/서버 World에서 서브시스템 찾기
	// ──────────────────────────────────────────────────────────
	// 콘솔 명령어는 특정 World Context에 바인딩되지 않으므로
	// GEngine의 모든 WorldContext를 순회하여 서브시스템을 찾아야 함
	//
	// 반환: 찾은 서브시스템 (없으면 nullptr + 진단 로그)
	// ──────────────────────────────────────────────────────────
	UHellunaSQLiteSubsystem* FindSQLiteSubsystem()
	{
		if (!GEngine)
		{
			UE_LOG(LogHelluna, Error, TEXT("[FindSQLiteSubsystem] GEngine이 nullptr — 엔진 초기화 전?"));
			return nullptr;
		}

		// GetWorldContexts()는 TIndirectArray<FWorldContext>를 반환 (TArray 아님!)
		const TIndirectArray<FWorldContext>& Contexts = GEngine->GetWorldContexts();
		UE_LOG(LogHelluna, Log, TEXT("[FindSQLiteSubsystem] WorldContext 수: %d"), Contexts.Num());

		for (const FWorldContext& Ctx : Contexts)
		{
			UWorld* W = Ctx.World();
			if (!W)
			{
				UE_LOG(LogHelluna, Verbose, TEXT("[FindSQLiteSubsystem]   Context WorldType=%d | World=nullptr — 스킵"), static_cast<int32>(Ctx.WorldType));
				continue;
			}

			UGameInstance* GI = W->GetGameInstance();
			if (!GI)
			{
				UE_LOG(LogHelluna, Warning, TEXT("[FindSQLiteSubsystem]   World=%s (Type=%d) | GameInstance=nullptr — PIE 미실행?"),
					*W->GetName(), static_cast<int32>(Ctx.WorldType));
				continue;
			}

			UHellunaSQLiteSubsystem* Sub = GI->GetSubsystem<UHellunaSQLiteSubsystem>();
			if (!Sub)
			{
				UE_LOG(LogHelluna, Warning, TEXT("[FindSQLiteSubsystem]   World=%s | GI=%s | Subsystem=nullptr — ShouldCreateSubsystem이 false 반환?"),
					*W->GetName(), *GI->GetClass()->GetName());
				continue;
			}

			if (!Sub->IsDatabaseReady())
			{
				UE_LOG(LogHelluna, Warning, TEXT("[FindSQLiteSubsystem]   World=%s | Subsystem 존재하나 DB 미준비 (IsDatabaseReady=false) — DB 열기 실패?"),
					*W->GetName());
				continue;
			}

			UE_LOG(LogHelluna, Log, TEXT("[FindSQLiteSubsystem] ✓ 서브시스템 발견! World=%s | DB=%s"),
				*W->GetName(), *Sub->GetDatabasePath());
			return Sub;
		}

		UE_LOG(LogHelluna, Error, TEXT("[FindSQLiteSubsystem] ✗ 서브시스템을 찾을 수 없음 — PIE가 실행 중인지 확인"));
		return nullptr;
	}
} // anonymous namespace


// ════════════════════════════════════════════════════════════════
// DebugSave — 더미 아이템 2개를 Stash에 저장
// ════════════════════════════════════════════════════════════════
// 목적: SavePlayerStash가 정상 동작하는지 검증
// 사용법: Helluna.SQLite.DebugSave [PlayerId]
// 예상 로그: "[DebugSQLiteSave] PlayerId=DebugPlayer | 결과=성공 | 저장 2개"
// ════════════════════════════════════════════════════════════════
static FAutoConsoleCommand CmdDebugSQLiteSave(
	TEXT("Helluna.SQLite.DebugSave"),
	TEXT("Usage: Helluna.SQLite.DebugSave [PlayerId]\n더미 아이템 2개를 player_stash에 저장하여 SavePlayerStash를 검증합니다."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		const FString PlayerId = (Args.Num() > 0) ? Args[0] : TEXT("DebugPlayer");

		UE_LOG(LogHelluna, Log, TEXT("[DebugSQLiteSave] ===== 시작 | PlayerId=%s ====="), *PlayerId);

		UHellunaSQLiteSubsystem* Sub = FindSQLiteSubsystem();
		if (!Sub)
		{
			UE_LOG(LogHelluna, Error, TEXT("[DebugSQLiteSave] ✗ Subsystem을 찾을 수 없음 — PIE 실행 중인지 확인"));
			return;
		}

		// 더미 아이템 2개 생성 (실제 GameplayTag 사용 — IsValid() 통과 필수!)
		TArray<FInv_SavedItemData> Items;

		FInv_SavedItemData Item1;
		Item1.ItemType        = FGameplayTag::RequestGameplayTag(FName(TEXT("GameItems.Equipment.Weapons.Axe")), false);
		Item1.StackCount      = 5;               // 5개 스택
		Item1.GridPosition    = FIntPoint(0, 0);  // 그리드 (0,0) 위치
		Item1.GridCategory    = 0;                // 장비 카테고리
		Item1.bEquipped       = false;
		Item1.WeaponSlotIndex = -1;               // 무기 슬롯 미장착
		Items.Add(Item1);

		FInv_SavedItemData Item2;
		Item2.ItemType        = FGameplayTag::RequestGameplayTag(FName(TEXT("GameItems.Consumables.Potions.Blue.Small")), false);
		Item2.StackCount      = 10;
		Item2.GridPosition    = FIntPoint(1, 0);
		Item2.GridCategory    = 1;                // 소모품 카테고리
		Item2.bEquipped       = false;
		Item2.WeaponSlotIndex = -1;
		Items.Add(Item2);

		const bool bOk = Sub->SavePlayerStash(PlayerId, Items);
		UE_LOG(LogHelluna, Log, TEXT("[DebugSQLiteSave] 결과: %s | PlayerId=%s | 저장 %d개"),
			bOk ? TEXT("성공") : TEXT("실패"), *PlayerId, Items.Num());
	})
);


// ════════════════════════════════════════════════════════════════
// DebugLoad — Stash 로드 후 로그 출력
// ════════════════════════════════════════════════════════════════
// 목적: LoadPlayerStash + ParseRowToSavedItem이 정상 동작하는지 검증
// 사용법: Helluna.SQLite.DebugLoad [PlayerId]
// 예상 로그: 각 아이템의 ItemType, StackCount, GridPosition 등 출력
// ════════════════════════════════════════════════════════════════
static FAutoConsoleCommand CmdDebugSQLiteLoad(
	TEXT("Helluna.SQLite.DebugLoad"),
	TEXT("Usage: Helluna.SQLite.DebugLoad [PlayerId]\nplayer_stash에서 아이템을 로드하고 결과를 로그에 출력합니다."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		const FString PlayerId = (Args.Num() > 0) ? Args[0] : TEXT("DebugPlayer");

		UE_LOG(LogHelluna, Log, TEXT("[DebugSQLiteLoad] ===== 시작 | PlayerId=%s ====="), *PlayerId);

		UHellunaSQLiteSubsystem* Sub = FindSQLiteSubsystem();
		if (!Sub)
		{
			UE_LOG(LogHelluna, Error, TEXT("[DebugSQLiteLoad] ✗ Subsystem을 찾을 수 없음"));
			return;
		}

		const TArray<FInv_SavedItemData> Items = Sub->LoadPlayerStash(PlayerId);
		UE_LOG(LogHelluna, Log, TEXT("[DebugSQLiteLoad] 파싱된 아이템 %d개:"), Items.Num());

		// 각 아이템 상세 출력
		for (int32 i = 0; i < Items.Num(); ++i)
		{
			UE_LOG(LogHelluna, Log,
				TEXT("  [%d] ItemType=%s | Stack=%d | Grid=(%d,%d) | Cat=%d | Equipped=%d | WeaponSlot=%d | Attachments=%d개"),
				i,
				*Items[i].ItemType.ToString(),
				Items[i].StackCount,
				Items[i].GridPosition.X, Items[i].GridPosition.Y,
				Items[i].GridCategory,
				Items[i].bEquipped ? 1 : 0,
				Items[i].WeaponSlotIndex,
				Items[i].Attachments.Num());
		}

		UE_LOG(LogHelluna, Log, TEXT("[DebugSQLiteLoad] ===== 완료 ====="));
	})
);


// ════════════════════════════════════════════════════════════════
// DebugWipe — Stash + Loadout 전체 삭제 (초기화)
// ════════════════════════════════════════════════════════════════
// 목적: 테스트 데이터 정리
// 사용법: Helluna.SQLite.DebugWipe [PlayerId]
// ════════════════════════════════════════════════════════════════
static FAutoConsoleCommand CmdDebugSQLiteWipe(
	TEXT("Helluna.SQLite.DebugWipe"),
	TEXT("Usage: Helluna.SQLite.DebugWipe [PlayerId]\n해당 PlayerId의 Stash와 Loadout을 전부 삭제합니다."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		const FString PlayerId = (Args.Num() > 0) ? Args[0] : TEXT("DebugPlayer");

		UE_LOG(LogHelluna, Log, TEXT("[DebugSQLiteWipe] ===== 시작 | PlayerId=%s ====="), *PlayerId);

		UHellunaSQLiteSubsystem* Sub = FindSQLiteSubsystem();
		if (!Sub)
		{
			UE_LOG(LogHelluna, Error, TEXT("[DebugSQLiteWipe] ✗ Subsystem을 찾을 수 없음"));
			return;
		}

		// 빈 배열로 SavePlayerStash 호출 = 기존 Stash DELETE + INSERT 없음 = 전체 삭제
		const bool bStashOk = Sub->SavePlayerStash(PlayerId, TArray<FInv_SavedItemData>());

		// Loadout도 삭제
		const bool bLoadoutOk = Sub->DeletePlayerLoadout(PlayerId);

		UE_LOG(LogHelluna, Log, TEXT("[DebugSQLiteWipe] 결과: Stash=%s | Loadout=%s | PlayerId=%s"),
			bStashOk    ? TEXT("삭제완료") : TEXT("실패"),
			bLoadoutOk  ? TEXT("삭제완료") : TEXT("없음/실패"),
			*PlayerId);
	})
);


// ════════════════════════════════════════════════════════════════
// DebugLoadout — 출격→크래시복구 전체 시나리오 테스트
// ════════════════════════════════════════════════════════════════
// 목적: SavePlayerLoadout + HasPendingLoadout + RecoverFromCrash가
//       올바르게 동작하는지 한 번에 검증
//
// 테스트 순서:
//   1) Stash에 더미 아이템 저장
//   2) SavePlayerLoadout (Loadout INSERT + Stash DELETE)
//   3) HasPendingLoadout → true 여야 정상
//   4) RecoverFromCrash (Loadout → Stash 복귀 + Loadout DELETE)
//   5) HasPendingLoadout → false 여야 정상
//   6) LoadPlayerStash → 복구된 아이템 수 확인
//
// 사용법: Helluna.SQLite.DebugLoadout [PlayerId]
// ════════════════════════════════════════════════════════════════
static FAutoConsoleCommand CmdDebugSQLiteLoadout(
	TEXT("Helluna.SQLite.DebugLoadout"),
	TEXT("Usage: Helluna.SQLite.DebugLoadout [PlayerId]\nSavePlayerLoadout -> HasPendingLoadout -> RecoverFromCrash 순서로 크래시 복구 경로를 검증합니다."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		const FString PlayerId = (Args.Num() > 0) ? Args[0] : TEXT("DebugPlayer");

		UHellunaSQLiteSubsystem* Sub = FindSQLiteSubsystem();
		if (!Sub)
		{
			UE_LOG(LogHelluna, Error, TEXT("[DebugSQLiteLoadout] ✗ Subsystem을 찾을 수 없음"));
			return;
		}

		UE_LOG(LogHelluna, Log, TEXT("[DebugSQLiteLoadout] ===== 테스트 시작 | PlayerId=%s ====="), *PlayerId);

		// 1) Stash에 더미 아이템 저장 (출격 전 창고 상태)
		{
			TArray<FInv_SavedItemData> StashItems;
			FInv_SavedItemData StashItem;
			StashItem.ItemType        = FGameplayTag::RequestGameplayTag(FName(TEXT("GameItems.Equipment.Weapons.Axe")), false);
			StashItem.StackCount      = 3;
			StashItem.GridPosition    = FIntPoint(0, 0);
			StashItem.GridCategory    = 0;
			StashItem.WeaponSlotIndex = -1;
			StashItems.Add(StashItem);

			const bool bOk = Sub->SavePlayerStash(PlayerId, StashItems);
			UE_LOG(LogHelluna, Log, TEXT("[DebugSQLiteLoadout] 1) Stash 더미 저장: %s (%d개)"),
				bOk ? TEXT("성공") : TEXT("실패"), StashItems.Num());
		}

		// 2) SavePlayerLoadout — 출격! (Loadout INSERT + Stash DELETE)
		{
			TArray<FInv_SavedItemData> LoadoutItems;
			FInv_SavedItemData LoadoutItem;
			LoadoutItem.ItemType        = FGameplayTag::RequestGameplayTag(FName(TEXT("GameItems.Consumables.Potions.Red.Small")), false);
			LoadoutItem.StackCount      = 2;
			LoadoutItem.GridPosition    = FIntPoint(0, 0);
			LoadoutItem.GridCategory    = 0;
			LoadoutItem.WeaponSlotIndex = 0;
			LoadoutItems.Add(LoadoutItem);

			const bool bOk = Sub->SavePlayerLoadout(PlayerId, LoadoutItems);
			UE_LOG(LogHelluna, Log, TEXT("[DebugSQLiteLoadout] 2) SavePlayerLoadout: %s"), bOk ? TEXT("성공") : TEXT("실패"));
		}

		// 3) HasPendingLoadout — Loadout이 남아있는지 확인 (true 여야 정상)
		{
			const bool bPending = Sub->HasPendingLoadout(PlayerId);
			UE_LOG(LogHelluna, Log, TEXT("[DebugSQLiteLoadout] 3) HasPendingLoadout: %s  <-- true여야 정상"),
				bPending ? TEXT("true (정상)") : TEXT("false (비정상!)"));
		}

		// 4) RecoverFromCrash — 크래시 복구! (Loadout → Stash 복구 + Loadout DELETE)
		{
			const bool bOk = Sub->RecoverFromCrash(PlayerId);
			UE_LOG(LogHelluna, Log, TEXT("[DebugSQLiteLoadout] 4) RecoverFromCrash: %s"), bOk ? TEXT("성공") : TEXT("실패"));
		}

		// 5) HasPendingLoadout 다시 확인 — Loadout이 비워졌는지 (false 여야 정상)
		{
			const bool bPendingAfter = Sub->HasPendingLoadout(PlayerId);
			UE_LOG(LogHelluna, Log, TEXT("[DebugSQLiteLoadout] 5) HasPendingLoadout(복구 후): %s  <-- false여야 정상"),
				bPendingAfter ? TEXT("true (비정상!)") : TEXT("false (정상)"));
		}

		// 6) Stash 아이템 수 확인 — 복구된 아이템이 있어야 함
		{
			const TArray<FInv_SavedItemData> Restored = Sub->LoadPlayerStash(PlayerId);
			UE_LOG(LogHelluna, Log, TEXT("[DebugSQLiteLoadout] 6) 복구된 Stash 아이템: %d개 (0 이상이면 정상)"), Restored.Num());
		}

		UE_LOG(LogHelluna, Log, TEXT("[DebugSQLiteLoadout] ===== 테스트 완료 ====="));
	})
);

#endif // !UE_BUILD_SHIPPING
