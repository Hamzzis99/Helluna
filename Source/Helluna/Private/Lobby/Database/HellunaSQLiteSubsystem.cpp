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
#include "Misc/FileHelper.h"             // FFileHelper — JSON 파일 읽기/쓰기
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

	// 1. DB 파일 경로 설정
	//    우선순위: 커맨드라인 -DatabasePath=<절대경로> > 기본 {ProjectSavedDir}/Database/Helluna.db
	//    → -DatabasePath는 로비서버와 게임서버가 같은 DB를 공유하기 위한 경로
	//    → WAL 모드 + busy_timeout으로 동시 접근 처리됨
	FString CommandLinePath;
	if (FParse::Value(FCommandLine::Get(), TEXT("-DatabasePath="), CommandLinePath))
	{
		// 커맨드라인에서 경로를 직접 지정 → 그대로 사용
		// (로비서버/게임서버 모두 같은 DB 파일을 공유해야 함)
		CachedDatabasePath = CommandLinePath;
		UE_LOG(LogHelluna, Warning, TEXT("[SQLite]   DB 경로 (커맨드라인 지정): %s"), *CachedDatabasePath);
	}
	else
	{
		// 기본: {ProjectSavedDir}/Database/Helluna.db (절대 경로로 변환)
		CachedDatabasePath = FPaths::ConvertRelativePathToFull(
			FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Database"), TEXT("Helluna.db")));
		UE_LOG(LogHelluna, Log, TEXT("[SQLite]   DB 경로 (기본): %s"), *CachedDatabasePath);
	}

	// 2. DB 디렉토리가 없으면 생성 (최초 실행 시 필요)
	const FString DatabaseDir = FPaths::GetPath(CachedDatabasePath);
	const bool bDirCreated = IFileManager::Get().MakeDirectory(*DatabaseDir, true);
	UE_LOG(LogHelluna, Log, TEXT("[SQLite]   디렉토리 생성: %s (결과=%s)"), *DatabaseDir, bDirCreated ? TEXT("성공/이미존재") : TEXT("실패"));

	// 3. DB 열기 + 스키마 초기화
	//    데디서버(HellunaServer.exe)에서 게임서버(L_Lobby가 아닌 맵)는 DB를 열지 않음
	//    → 게임서버는 파일 전송(Export/Import) 함수만 사용하므로 DB 불필요
	//    → DB 잠금을 피해 로비서버만 DB를 독점 사용
	bool bSkipDatabaseOpen = false;
	if (IsRunningDedicatedServer())
	{
		const FString CmdLine = FCommandLine::Get();
		if (!CmdLine.Contains(TEXT("L_Lobby")))
		{
			bSkipDatabaseOpen = true;
			UE_LOG(LogHelluna, Log, TEXT("[SQLite] 게임서버 감지 (L_Lobby 없음) → DB 열기 생략, 파일 전송 전용 모드"));
		}
	}

	// 명시적 커맨드라인 플래그로도 DB 열기 생략 가능
	if (FParse::Param(FCommandLine::Get(), TEXT("NoDatabaseOpen")))
	{
		bSkipDatabaseOpen = true;
		UE_LOG(LogHelluna, Log, TEXT("[SQLite] -NoDatabaseOpen 플래그 감지 → DB 열기 생략"));
	}

	if (bSkipDatabaseOpen)
	{
		bFileTransferOnly = true;
		UE_LOG(LogHelluna, Log, TEXT("[SQLite] ✓ 서브시스템 초기화 완료 (파일 전송 전용 — DB 미사용)"));
	}
	else if (OpenDatabase())
	{
		UE_LOG(LogHelluna, Log, TEXT("[SQLite] ✓ 서브시스템 초기화 완료 (DB 열기 성공)"));
	}
	else
	{
		// DB 열기 실패
		// PIE에서 멀티플레이어 테스트 시, 서버 GameInstance가 DB를 선점하면
		// 클라이언트 GameInstance는 "disk I/O error"로 실패함 (정상 동작)
		// 클라이언트는 서버 RPC로 데이터를 받으므로 파일 전송 전용 모드로 전환
#if WITH_EDITOR
		bFileTransferOnly = true;
		UE_LOG(LogHelluna, Warning, TEXT("[SQLite] DB 열기 실패 (에디터) → 파일 전송 전용 모드 전환 (PIE 클라이언트 추정) | 경로: %s"), *CachedDatabasePath);
#else
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ 서브시스템 초기화 실패 — DB 열기 실패 | 경로: %s"), *CachedDatabasePath);
#endif
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


// ──────────────────────────────────────────────────────────────
// TryReopenDatabase — DB가 닫혀있으면 재오픈 시도
// ──────────────────────────────────────────────────────────────
// 사용 시점: Initialize에서 실패한 경우, 파일 잠금이 풀린 후 호출
// 예: 로비 PIE가 DB를 잠근 상태에서 데디서버가 시작 → 로비 PIE 종료 후 재시도
// ──────────────────────────────────────────────────────────────
bool UHellunaSQLiteSubsystem::TryReopenDatabase()
{
	// 파일 전송 전용 모드에서는 DB를 절대 열지 않음
	if (bFileTransferOnly)
	{
		UE_LOG(LogHelluna, Log, TEXT("[SQLite] TryReopenDatabase: 파일 전송 전용 모드 → DB 열기 차단"));
		return false;
	}

	// 이미 열려있으면 true 반환
	if (IsDatabaseReady())
	{
		return true;
	}

	UE_LOG(LogHelluna, Log, TEXT("[SQLite] TryReopenDatabase: DB 재오픈 시도 | 경로: %s"), *CachedDatabasePath);

	if (OpenDatabase())
	{
		UE_LOG(LogHelluna, Warning, TEXT("[SQLite] TryReopenDatabase: ✓ DB 재오픈 성공!"));
		return true;
	}

	UE_LOG(LogHelluna, Warning, TEXT("[SQLite] TryReopenDatabase: ✗ DB 여전히 열 수 없음 (다른 프로세스가 잠금 중?)"));
	return false;
}

// ──────────────────────────────────────────────────────────────
// ReleaseDatabaseConnection — DB 연결 명시적 해제 (파일 잠금 풀기)
// ──────────────────────────────────────────────────────────────
// 사용 시점: 로비에서 마지막 플레이어 로그아웃 후 호출
// → 게임서버(데디서버)가 같은 DB를 열 수 있도록 잠금 해제
// ──────────────────────────────────────────────────────────────
void UHellunaSQLiteSubsystem::ReleaseDatabaseConnection()
{
	if (!bDatabaseOpen && Database == nullptr)
	{
		UE_LOG(LogHelluna, Log, TEXT("[SQLite] ReleaseDatabaseConnection: 이미 닫혀있음"));
		return;
	}

	UE_LOG(LogHelluna, Warning, TEXT("[SQLite] ReleaseDatabaseConnection: DB 연결 해제 (파일 잠금 풀기)"));
	CloseDatabase();
}


// ════════════════════════════════════════════════════════════════════════════════
// 파일 기반 Loadout 전송
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 배경: PIE(로비)와 데디서버(게임)가 같은 SQLite DB를 동시에 열 수 없음
//   → Loadout을 JSON 파일로 내보내고, 게임서버에서 읽어서 인벤토리 복원
//
// 📌 파일 경로: {DB 디렉토리}/Transfer/Loadout_{PlayerId}.json
//   CachedDatabasePath에서 디렉토리를 추출하므로 -DatabasePath= 커맨드라인과 무관하게
//   두 프로세스가 같은 경로를 참조
//
// 📌 흐름:
//   로비: SavePlayerLoadout → ExportLoadoutToFile (JSON 생성)
//   게임: HasPendingLoadoutFile → ImportLoadoutFromFile (JSON 읽기 + 삭제)
// ════════════════════════════════════════════════════════════════════════════════

// ──────────────────────────────────────────────────────────────
// GetLoadoutTransferFilePath — 전송 파일 경로 생성
// ──────────────────────────────────────────────────────────────
FString UHellunaSQLiteSubsystem::GetLoadoutTransferFilePath(const FString& PlayerId) const
{
	// PlayerId 경로 탈출 방지 (path traversal 차단)
	FString SafePlayerId = PlayerId;
	SafePlayerId.ReplaceInline(TEXT("/"), TEXT("_"));
	SafePlayerId.ReplaceInline(TEXT("\\"), TEXT("_"));
	SafePlayerId.ReplaceInline(TEXT(".."), TEXT("_"));

	const FString DBDir = FPaths::GetPath(CachedDatabasePath);
	return FPaths::Combine(DBDir, TEXT("Transfer"), FString::Printf(TEXT("Loadout_%s.json"), *SafePlayerId));
}

// ──────────────────────────────────────────────────────────────
// HasPendingLoadoutFile — 전송 파일 존재 여부 확인
// ──────────────────────────────────────────────────────────────
bool UHellunaSQLiteSubsystem::HasPendingLoadoutFile(const FString& PlayerId) const
{
	const FString FilePath = GetLoadoutTransferFilePath(PlayerId);
	const bool bExists = FPaths::FileExists(FilePath);
	UE_LOG(LogHelluna, Log, TEXT("[SQLite] HasPendingLoadoutFile: PlayerId=%s | 경로=%s | 존재=%s"),
		*PlayerId, *FilePath, bExists ? TEXT("Y") : TEXT("N"));
	return bExists;
}

// ──────────────────────────────────────────────────────────────
// ExportLoadoutToFile — Loadout 아이템을 JSON 파일로 내보내기
// ──────────────────────────────────────────────────────────────
//
// JSON 구조:
// {
//   "player_id": "DebugPlayer",
//   "hero_type": 0,
//   "export_time": "2026-02-26T12:34:56",
//   "items": [
//     {
//       "item_type": "Item.Weapon.Rifle",
//       "stack_count": 1,
//       "grid_x": 0, "grid_y": 0,
//       "grid_category": 0,
//       "is_equipped": false,
//       "weapon_slot": -1,
//       "manifest": "Base64...",
//       "attachments": [{"t":"...","s":0,"at":"...","m":"Base64..."}]
//     }
//   ]
// }
// ──────────────────────────────────────────────────────────────
bool UHellunaSQLiteSubsystem::ExportLoadoutToFile(const FString& PlayerId, const TArray<FInv_SavedItemData>& Items, int32 HeroType)
{
	const FString FilePath = GetLoadoutTransferFilePath(PlayerId);
	UE_LOG(LogHelluna, Log, TEXT("[SQLite] ExportLoadoutToFile: 시작 | PlayerId=%s | %d개 아이템 | 경로=%s"),
		*PlayerId, Items.Num(), *FilePath);

	// Transfer 디렉토리 생성
	const FString TransferDir = FPaths::GetPath(FilePath);
	if (!IFileManager::Get().MakeDirectory(*TransferDir, true))
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ExportLoadoutToFile: Transfer 디렉토리 생성 실패 | %s"), *TransferDir);
		return false;
	}

	// JSON 루트 오브젝트 생성
	TSharedRef<FJsonObject> RootObj = MakeShared<FJsonObject>();
	RootObj->SetStringField(TEXT("player_id"), PlayerId);
	RootObj->SetNumberField(TEXT("hero_type"), HeroType);
	RootObj->SetStringField(TEXT("export_time"), FDateTime::Now().ToString());

	// 아이템 배열
	TArray<TSharedPtr<FJsonValue>> ItemsArray;
	for (const FInv_SavedItemData& Item : Items)
	{
		TSharedRef<FJsonObject> ItemObj = MakeShared<FJsonObject>();

		ItemObj->SetStringField(TEXT("item_type"), Item.ItemType.ToString());
		ItemObj->SetNumberField(TEXT("stack_count"), Item.StackCount);
		ItemObj->SetNumberField(TEXT("grid_x"), Item.GridPosition.X);
		ItemObj->SetNumberField(TEXT("grid_y"), Item.GridPosition.Y);
		ItemObj->SetNumberField(TEXT("grid_category"), static_cast<int32>(Item.GridCategory));
		ItemObj->SetBoolField(TEXT("is_equipped"), Item.bEquipped);
		ItemObj->SetNumberField(TEXT("weapon_slot"), Item.WeaponSlotIndex);
		ItemObj->SetBoolField(TEXT("is_rotated"), Item.bRotated);

		// SerializedManifest → Base64
		if (Item.SerializedManifest.Num() > 0)
		{
			ItemObj->SetStringField(TEXT("manifest"), FBase64::Encode(Item.SerializedManifest));
		}

		// Attachments → 기존 JSON 직렬화 재사용
		if (Item.Attachments.Num() > 0)
		{
			const FString AttJson = SerializeAttachmentsToJson(Item.Attachments);
			// JSON 문자열을 파싱하여 JSON 배열로 내장 (문자열이 아닌 구조체)
			TSharedRef<TJsonReader<>> AttReader = TJsonReaderFactory<>::Create(AttJson);
			TArray<TSharedPtr<FJsonValue>> AttArray;
			if (FJsonSerializer::Deserialize(AttReader, AttArray))
			{
				ItemObj->SetArrayField(TEXT("attachments"), AttArray);
			}
		}

		ItemsArray.Add(MakeShared<FJsonValueObject>(ItemObj));
	}
	RootObj->SetArrayField(TEXT("items"), ItemsArray);

	// JSON → 문자열 → 파일 쓰기
	FString OutputString;
	TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&OutputString);
	FJsonSerializer::Serialize(RootObj, Writer);
	Writer->Close();

	if (FFileHelper::SaveStringToFile(OutputString, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		UE_LOG(LogHelluna, Warning, TEXT("[SQLite] ExportLoadoutToFile: ✓ JSON 파일 저장 성공 | %d자 | %s"),
			OutputString.Len(), *FilePath);
		return true;
	}

	UE_LOG(LogHelluna, Error, TEXT("[SQLite] ExportLoadoutToFile: ✗ 파일 쓰기 실패 | %s"), *FilePath);
	return false;
}

// ──────────────────────────────────────────────────────────────
// ImportLoadoutFromFile — JSON 파일에서 Loadout 읽기 + 파일 삭제
// ──────────────────────────────────────────────────────────────
TArray<FInv_SavedItemData> UHellunaSQLiteSubsystem::ImportLoadoutFromFile(const FString& PlayerId, int32& OutHeroType)
{
	TArray<FInv_SavedItemData> Result;
	OutHeroType = 0;

	const FString FilePath = GetLoadoutTransferFilePath(PlayerId);
	UE_LOG(LogHelluna, Log, TEXT("[SQLite] ImportLoadoutFromFile: 시작 | PlayerId=%s | 경로=%s"),
		*PlayerId, *FilePath);

	// 파일 읽기
	FString JsonString;
	if (!FFileHelper::LoadFileToString(JsonString, *FilePath))
	{
		UE_LOG(LogHelluna, Log, TEXT("[SQLite] ImportLoadoutFromFile: 파일 없음 또는 읽기 실패"));
		return Result;
	}

	// JSON 파싱
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
	TSharedPtr<FJsonObject> RootObj;
	if (!FJsonSerializer::Deserialize(Reader, RootObj) || !RootObj.IsValid())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ImportLoadoutFromFile: JSON 파싱 실패"));
		// 손상된 파일 삭제
		IFileManager::Get().Delete(*FilePath);
		return Result;
	}

	// 헤더 필드
	OutHeroType = static_cast<int32>(RootObj->GetNumberField(TEXT("hero_type")));

	// 아이템 배열 파싱
	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (!RootObj->TryGetArrayField(TEXT("items"), ItemsArray) || !ItemsArray)
	{
		UE_LOG(LogHelluna, Warning, TEXT("[SQLite] ImportLoadoutFromFile: items 배열 없음"));
		IFileManager::Get().Delete(*FilePath);
		return Result;
	}

	for (const TSharedPtr<FJsonValue>& ItemValue : *ItemsArray)
	{
		const TSharedPtr<FJsonObject> ItemObj = ItemValue->AsObject();
		if (!ItemObj.IsValid())
		{
			continue;
		}

		FInv_SavedItemData Item;

		// item_type → FGameplayTag
		FString ItemTypeStr;
		if (ItemObj->TryGetStringField(TEXT("item_type"), ItemTypeStr))
		{
			Item.ItemType = FGameplayTag::RequestGameplayTag(FName(*ItemTypeStr), false);
		}

		Item.StackCount = static_cast<int32>(ItemObj->GetNumberField(TEXT("stack_count")));
		Item.GridPosition.X = static_cast<int32>(ItemObj->GetNumberField(TEXT("grid_x")));
		Item.GridPosition.Y = static_cast<int32>(ItemObj->GetNumberField(TEXT("grid_y")));
		Item.GridCategory = static_cast<uint8>(ItemObj->GetNumberField(TEXT("grid_category")));
		Item.bEquipped = ItemObj->GetBoolField(TEXT("is_equipped"));
		Item.WeaponSlotIndex = static_cast<int32>(ItemObj->GetNumberField(TEXT("weapon_slot")));
		Item.bRotated = ItemObj->GetBoolField(TEXT("is_rotated"));

		// manifest → Base64 디코딩
		FString ManifestB64;
		if (ItemObj->TryGetStringField(TEXT("manifest"), ManifestB64) && !ManifestB64.IsEmpty())
		{
			FBase64::Decode(ManifestB64, Item.SerializedManifest);
		}

		// attachments → 기존 역직렬화 재사용
		const TArray<TSharedPtr<FJsonValue>>* AttArray = nullptr;
		if (ItemObj->TryGetArrayField(TEXT("attachments"), AttArray) && AttArray && AttArray->Num() > 0)
		{
			// JSON 배열 → 문자열 → DeserializeAttachmentsFromJson
			FString AttJsonStr;
			TSharedRef<TJsonWriter<>> AttWriter = TJsonWriterFactory<>::Create(&AttJsonStr);
			FJsonSerializer::Serialize(*AttArray, AttWriter);
			Item.Attachments = DeserializeAttachmentsFromJson(AttJsonStr);
		}

		Result.Add(MoveTemp(Item));
	}

	UE_LOG(LogHelluna, Warning, TEXT("[SQLite] ImportLoadoutFromFile: ✓ %d개 아이템 로드 | HeroType=%d"),
		Result.Num(), OutHeroType);

	// 파일 삭제 (비행기표 소멸)
	if (IFileManager::Get().Delete(*FilePath))
	{
		UE_LOG(LogHelluna, Log, TEXT("[SQLite] ImportLoadoutFromFile: 전송 파일 삭제 완료"));
	}
	else
	{
		UE_LOG(LogHelluna, Warning, TEXT("[SQLite] ImportLoadoutFromFile: 파일 삭제 실패 (다음 접속 시 재시도) | %s"), *FilePath);
	}

	return Result;
}

// ──────────────────────────────────────────────────────────────
// GetGameResultTransferFilePath — 게임 결과 전송 파일 경로 생성
// ──────────────────────────────────────────────────────────────
FString UHellunaSQLiteSubsystem::GetGameResultTransferFilePath(const FString& PlayerId) const
{
	FString SafePlayerId = PlayerId;
	SafePlayerId.ReplaceInline(TEXT("/"), TEXT("_"));
	SafePlayerId.ReplaceInline(TEXT("\\"), TEXT("_"));
	SafePlayerId.ReplaceInline(TEXT(".."), TEXT("_"));

	const FString DBDir = FPaths::GetPath(CachedDatabasePath);
	return FPaths::Combine(DBDir, TEXT("Transfer"), FString::Printf(TEXT("GameResult_%s.json"), *SafePlayerId));
}

// ──────────────────────────────────────────────────────────────
// HasPendingGameResultFile — 게임 결과 전송 파일 존재 여부 확인
// ──────────────────────────────────────────────────────────────
bool UHellunaSQLiteSubsystem::HasPendingGameResultFile(const FString& PlayerId) const
{
	const FString FilePath = GetGameResultTransferFilePath(PlayerId);
	const bool bExists = FPaths::FileExists(FilePath);
	UE_LOG(LogHelluna, Log, TEXT("[SQLite] HasPendingGameResultFile: PlayerId=%s | 경로=%s | 존재=%s"),
		*PlayerId, *FilePath, bExists ? TEXT("Y") : TEXT("N"));
	return bExists;
}

// ──────────────────────────────────────────────────────────────
// ExportGameResultToFile — 게임 결과를 JSON 파일로 내보내기
// ──────────────────────────────────────────────────────────────
//
// JSON 구조:
// {
//   "player_id": "DebugPlayer",
//   "survived": true,
//   "export_time": "2026-02-27T12:34:56",
//   "items": [
//     {
//       "item_type": "Item.Weapon.Rifle",
//       "stack_count": 1,
//       "grid_x": 0, "grid_y": 0,
//       "grid_category": 0,
//       "is_equipped": false,
//       "weapon_slot": -1,
//       "manifest": "Base64...",
//       "attachments": [{"t":"...","s":0,"at":"...","m":"Base64..."}]
//     }
//   ]
// }
// ──────────────────────────────────────────────────────────────
bool UHellunaSQLiteSubsystem::ExportGameResultToFile(const FString& PlayerId, const TArray<FInv_SavedItemData>& Items, bool bSurvived)
{
	const FString FilePath = GetGameResultTransferFilePath(PlayerId);
	UE_LOG(LogHelluna, Log, TEXT("[SQLite] ExportGameResultToFile: 시작 | PlayerId=%s | %d개 아이템 | survived=%s | 경로=%s"),
		*PlayerId, Items.Num(), bSurvived ? TEXT("Y") : TEXT("N"), *FilePath);

	// Transfer 디렉토리 생성
	const FString TransferDir = FPaths::GetPath(FilePath);
	if (!IFileManager::Get().MakeDirectory(*TransferDir, true))
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ExportGameResultToFile: Transfer 디렉토리 생성 실패 | %s"), *TransferDir);
		return false;
	}

	// JSON 루트 오브젝트 생성
	TSharedRef<FJsonObject> RootObj = MakeShared<FJsonObject>();
	RootObj->SetStringField(TEXT("player_id"), PlayerId);
	RootObj->SetBoolField(TEXT("survived"), bSurvived);
	RootObj->SetStringField(TEXT("export_time"), FDateTime::Now().ToString());

	// 장착 슬롯 정보 추출 → equipment 섹션
	TArray<TSharedPtr<FJsonValue>> EquipArray;
	for (const FInv_SavedItemData& Item : Items)
	{
		if (Item.bEquipped && Item.WeaponSlotIndex >= 0)
		{
			TSharedRef<FJsonObject> SlotObj = MakeShared<FJsonObject>();
			SlotObj->SetStringField(TEXT("slot_id"), FString::Printf(TEXT("weapon_%d"), Item.WeaponSlotIndex));
			SlotObj->SetStringField(TEXT("item_type"), Item.ItemType.ToString());
			EquipArray.Add(MakeShared<FJsonValueObject>(SlotObj));
		}
	}
	RootObj->SetArrayField(TEXT("equipment"), EquipArray);

	// 아이템 배열
	TArray<TSharedPtr<FJsonValue>> ItemsArray;
	int32 ExportIdx = 0;
	for (const FInv_SavedItemData& Item : Items)
	{
		UE_LOG(LogHelluna, Log, TEXT("[SQLite] ExportGameResultToFile: [%d] %s | Stack=%d | Equipped=%s"),
			ExportIdx, *Item.ItemType.ToString(), Item.StackCount,
			Item.bEquipped ? TEXT("Y") : TEXT("N"));
		++ExportIdx;

		TSharedRef<FJsonObject> ItemObj = MakeShared<FJsonObject>();

		ItemObj->SetStringField(TEXT("item_type"), Item.ItemType.ToString());
		ItemObj->SetNumberField(TEXT("stack_count"), Item.StackCount);
		ItemObj->SetNumberField(TEXT("grid_x"), Item.GridPosition.X);
		ItemObj->SetNumberField(TEXT("grid_y"), Item.GridPosition.Y);
		ItemObj->SetNumberField(TEXT("grid_category"), static_cast<int32>(Item.GridCategory));
		ItemObj->SetBoolField(TEXT("is_equipped"), Item.bEquipped);
		ItemObj->SetNumberField(TEXT("weapon_slot"), Item.WeaponSlotIndex);
		ItemObj->SetBoolField(TEXT("is_rotated"), Item.bRotated);

		// SerializedManifest → Base64
		if (Item.SerializedManifest.Num() > 0)
		{
			ItemObj->SetStringField(TEXT("manifest"), FBase64::Encode(Item.SerializedManifest));
		}

		// Attachments → 기존 JSON 직렬화 재사용
		if (Item.Attachments.Num() > 0)
		{
			const FString AttJson = SerializeAttachmentsToJson(Item.Attachments);
			TSharedRef<TJsonReader<>> AttReader = TJsonReaderFactory<>::Create(AttJson);
			TArray<TSharedPtr<FJsonValue>> AttArray;
			if (FJsonSerializer::Deserialize(AttReader, AttArray))
			{
				ItemObj->SetArrayField(TEXT("attachments"), AttArray);
			}
		}

		ItemsArray.Add(MakeShared<FJsonValueObject>(ItemObj));
	}
	RootObj->SetArrayField(TEXT("items"), ItemsArray);

	// JSON → 문자열 → 파일 쓰기
	FString OutputString;
	TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&OutputString);
	FJsonSerializer::Serialize(RootObj, Writer);
	Writer->Close();

	if (FFileHelper::SaveStringToFile(OutputString, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		UE_LOG(LogHelluna, Warning, TEXT("[SQLite] ExportGameResultToFile: ✓ JSON 파일 저장 성공 | %d자 | %s"),
			OutputString.Len(), *FilePath);
		return true;
	}

	UE_LOG(LogHelluna, Error, TEXT("[SQLite] ExportGameResultToFile: ✗ 파일 쓰기 실패 | %s"), *FilePath);
	return false;
}

// ──────────────────────────────────────────────────────────────
// ImportGameResultFromFile — JSON 파일에서 게임 결과 읽기 + 파일 삭제
// ──────────────────────────────────────────────────────────────
TArray<FInv_SavedItemData> UHellunaSQLiteSubsystem::ImportGameResultFromFile(const FString& PlayerId, bool& OutSurvived, bool& bOutSuccess,
	TArray<FHellunaEquipmentSlotData>* OutEquipment)
{
	TArray<FInv_SavedItemData> Result;
	OutSurvived = false;
	bOutSuccess = false;

	const FString FilePath = GetGameResultTransferFilePath(PlayerId);
	UE_LOG(LogHelluna, Log, TEXT("[SQLite] ImportGameResultFromFile: 시작 | PlayerId=%s | 경로=%s"),
		*PlayerId, *FilePath);

	// 파일 읽기
	FString JsonString;
	if (!FFileHelper::LoadFileToString(JsonString, *FilePath))
	{
		UE_LOG(LogHelluna, Log, TEXT("[SQLite] ImportGameResultFromFile: 파일 없음 또는 읽기 실패"));
		return Result;
	}

	// JSON 파싱
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
	TSharedPtr<FJsonObject> RootObj;
	if (!FJsonSerializer::Deserialize(Reader, RootObj) || !RootObj.IsValid())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ImportGameResultFromFile: JSON 파싱 실패 → 손상된 파일 삭제, 크래시 복구로 전환"));
		IFileManager::Get().Delete(*FilePath);
		return Result;  // bOutSuccess = false → 호출자가 Loadout 보존
	}

	// 헤더 필드
	OutSurvived = RootObj->GetBoolField(TEXT("survived"));

	// 장착 슬롯 파싱 (equipment 섹션)
	if (OutEquipment)
	{
		const TArray<TSharedPtr<FJsonValue>>* EquipArray = nullptr;
		if (RootObj->TryGetArrayField(TEXT("equipment"), EquipArray) && EquipArray)
		{
			for (const TSharedPtr<FJsonValue>& SlotValue : *EquipArray)
			{
				const TSharedPtr<FJsonObject> SlotObj = SlotValue->AsObject();
				if (!SlotObj.IsValid()) continue;

				FHellunaEquipmentSlotData Slot;
				SlotObj->TryGetStringField(TEXT("slot_id"), Slot.SlotId);
				FString ItemTypeStr;
				if (SlotObj->TryGetStringField(TEXT("item_type"), ItemTypeStr))
				{
					Slot.ItemType = FGameplayTag::RequestGameplayTag(FName(*ItemTypeStr), false);
				}
				OutEquipment->Add(MoveTemp(Slot));
			}
			UE_LOG(LogHelluna, Log, TEXT("[SQLite] ImportGameResultFromFile: equipment %d개 슬롯 파싱"),
				OutEquipment->Num());
		}
	}

	// 아이템 배열 파싱
	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (!RootObj->TryGetArrayField(TEXT("items"), ItemsArray) || !ItemsArray)
	{
		UE_LOG(LogHelluna, Warning, TEXT("[SQLite] ImportGameResultFromFile: items 배열 없음 → 손상된 파일 삭제, 크래시 복구로 전환"));
		IFileManager::Get().Delete(*FilePath);
		return Result;  // bOutSuccess = false → 호출자가 Loadout 보존
	}

	for (const TSharedPtr<FJsonValue>& ItemValue : *ItemsArray)
	{
		const TSharedPtr<FJsonObject> ItemObj = ItemValue->AsObject();
		if (!ItemObj.IsValid())
		{
			continue;
		}

		FInv_SavedItemData Item;

		// item_type → FGameplayTag
		FString ItemTypeStr;
		if (ItemObj->TryGetStringField(TEXT("item_type"), ItemTypeStr))
		{
			Item.ItemType = FGameplayTag::RequestGameplayTag(FName(*ItemTypeStr), false);
		}

		Item.StackCount = static_cast<int32>(ItemObj->GetNumberField(TEXT("stack_count")));
		Item.GridPosition.X = static_cast<int32>(ItemObj->GetNumberField(TEXT("grid_x")));
		Item.GridPosition.Y = static_cast<int32>(ItemObj->GetNumberField(TEXT("grid_y")));
		Item.GridCategory = static_cast<uint8>(ItemObj->GetNumberField(TEXT("grid_category")));
		Item.bEquipped = ItemObj->GetBoolField(TEXT("is_equipped"));
		Item.WeaponSlotIndex = static_cast<int32>(ItemObj->GetNumberField(TEXT("weapon_slot")));
		Item.bRotated = ItemObj->GetBoolField(TEXT("is_rotated"));

		// manifest → Base64 디코딩
		FString ManifestB64;
		if (ItemObj->TryGetStringField(TEXT("manifest"), ManifestB64) && !ManifestB64.IsEmpty())
		{
			FBase64::Decode(ManifestB64, Item.SerializedManifest);
		}

		// attachments → 기존 역직렬화 재사용
		const TArray<TSharedPtr<FJsonValue>>* AttArray = nullptr;
		if (ItemObj->TryGetArrayField(TEXT("attachments"), AttArray) && AttArray && AttArray->Num() > 0)
		{
			FString AttJsonStr;
			TSharedRef<TJsonWriter<>> AttWriter = TJsonWriterFactory<>::Create(&AttJsonStr);
			FJsonSerializer::Serialize(*AttArray, AttWriter);
			Item.Attachments = DeserializeAttachmentsFromJson(AttJsonStr);
		}

		// ⭐ [진단] 각 아이템의 StackCount 확인 — Stack=0 원인 추적
		UE_LOG(LogHelluna, Log, TEXT("[SQLite] ImportGameResultFromFile: [%d] %s | Stack=%d | Equipped=%s"),
			Result.Num(), *Item.ItemType.ToString(), Item.StackCount,
			Item.bEquipped ? TEXT("Y") : TEXT("N"));

		Result.Add(MoveTemp(Item));
	}

	UE_LOG(LogHelluna, Warning, TEXT("[SQLite] ImportGameResultFromFile: ✓ %d개 아이템 로드 | survived=%s"),
		Result.Num(), OutSurvived ? TEXT("Y") : TEXT("N"));

	// JSON 파싱 성공 (아이템 0개라도 성공 — 사망 시 빈 배열이 정상)
	bOutSuccess = true;

	// 파일 삭제 (처리 완료)
	if (IFileManager::Get().Delete(*FilePath))
	{
		UE_LOG(LogHelluna, Log, TEXT("[SQLite] ImportGameResultFromFile: 전송 파일 삭제 완료"));
	}
	else
	{
		UE_LOG(LogHelluna, Warning, TEXT("[SQLite] ImportGameResultFromFile: 파일 삭제 실패 (다음 접속 시 재시도) | %s"), *FilePath);
	}

	return Result;
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
	// [Fix26] check() → safe return (데디서버 프로세스 종료 방지)
	if (!Database)
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] InitializeSchema: Database is nullptr!"));
		return false;
	}

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
	// player_stash와 거의 동일 (updated_at 대신 created_at)
	// [Fix14] is_equipped 컬럼 추가 — 장착 상태 보존
	if (!Database->Execute(TEXT(
		"CREATE TABLE IF NOT EXISTS player_loadout ("
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

	// ── [Fix14] 마이그레이션: 기존 player_loadout에 is_equipped 컬럼 추가 ──
	// CREATE TABLE IF NOT EXISTS는 이미 있는 테이블을 수정하지 않으므로,
	// 기존 DB에 is_equipped 컬럼이 없을 수 있음 → ALTER TABLE로 추가 (이미 있으면 에러 무시)
	Database->Execute(TEXT("ALTER TABLE player_loadout ADD COLUMN is_equipped INTEGER DEFAULT 0;"));

	// ── active_game_characters 테이블 생성 (캐릭터 중복 방지) ──
	// TODO: [크래시 복구] 서버 비정상 종료 시 레코드가 남아있을 수 있음 → heartbeat/TTL 기반 자동 정리 필요
	// TODO: [Race Condition] UNIQUE INDEX로 동시 등록은 방지되지만, UI 갱신에 지연 있음
	// TODO: [실시간 알림] 다른 플레이어의 선택을 실시간으로 알리려면 Multicast RPC 또는 폴링 필요
	if (!Database->Execute(TEXT(
		"CREATE TABLE IF NOT EXISTS active_game_characters ("
		"    id          INTEGER PRIMARY KEY AUTOINCREMENT,"
		"    hero_type   INTEGER NOT NULL,"
		"    player_id   TEXT NOT NULL,"
		"    server_id   TEXT NOT NULL,"
		"    created_at  DATETIME DEFAULT CURRENT_TIMESTAMP"
		");"
	)))
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ CREATE TABLE active_game_characters 실패 | 에러: %s"), *Database->GetLastError());
		return false;
	}
	UE_LOG(LogHelluna, Log, TEXT("[SQLite]   CREATE TABLE active_game_characters ✓"));

	// hero_type + server_id 복합 유니크 인덱스 (같은 서버에서 같은 캐릭터 중복 등록 방지)
	if (!Database->Execute(TEXT("CREATE UNIQUE INDEX IF NOT EXISTS idx_agc_hero_server ON active_game_characters(hero_type, server_id);")))
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ INDEX idx_agc_hero_server 실패 | 에러: %s"), *Database->GetLastError());
		return false;
	}

	// player_id 인덱스 (플레이어별 빠른 검색)
	if (!Database->Execute(TEXT("CREATE INDEX IF NOT EXISTS idx_agc_player ON active_game_characters(player_id);")))
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ INDEX idx_agc_player 실패 | 에러: %s"), *Database->GetLastError());
		return false;
	}

	// ── [Fix36] player_deploy_state 테이블 생성 (출격 상태 추적) ──
	// Loadout 존재 ≠ 크래시. 출격 상태를 별도 추적하여 크래시 감지
	if (!Database->Execute(TEXT(
		"CREATE TABLE IF NOT EXISTS player_deploy_state ("
		"    player_id   TEXT PRIMARY KEY,"
		"    is_deployed INTEGER NOT NULL DEFAULT 0,"
		"    deployed_at DATETIME DEFAULT CURRENT_TIMESTAMP"
		");"
	)))
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ CREATE TABLE player_deploy_state 실패 | 에러: %s"), *Database->GetLastError());
		return false;
	}
	UE_LOG(LogHelluna, Log, TEXT("[SQLite]   CREATE TABLE player_deploy_state ✓"));

	// ── [Phase 12a] party_groups 테이블 생성 (파티 그룹) ──
	if (!Database->Execute(TEXT(
		"CREATE TABLE IF NOT EXISTS party_groups ("
		"    id          INTEGER PRIMARY KEY AUTOINCREMENT,"
		"    party_code  TEXT NOT NULL UNIQUE,"
		"    leader_id   TEXT NOT NULL,"
		"    created_at  DATETIME DEFAULT CURRENT_TIMESTAMP"
		");"
	)))
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ CREATE TABLE party_groups 실패 | 에러: %s"), *Database->GetLastError());
		return false;
	}
	UE_LOG(LogHelluna, Log, TEXT("[SQLite]   CREATE TABLE party_groups ✓"));

	// ── party_groups 인덱스 (party_code UNIQUE는 CREATE TABLE에서 이미 보장) ──
	if (!Database->Execute(TEXT("CREATE INDEX IF NOT EXISTS idx_party_groups_leader ON party_groups(leader_id);")))
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ INDEX idx_party_groups_leader 실패 | 에러: %s"), *Database->GetLastError());
		return false;
	}

	// ── [Phase 12a] party_members 테이블 생성 (파티 멤버) ──
	// player_id UNIQUE: 1인 1파티 보장
	if (!Database->Execute(TEXT(
		"CREATE TABLE IF NOT EXISTS party_members ("
		"    id           INTEGER PRIMARY KEY AUTOINCREMENT,"
		"    party_id     INTEGER NOT NULL,"
		"    player_id    TEXT NOT NULL UNIQUE,"
		"    display_name TEXT NOT NULL DEFAULT '',"
		"    role         INTEGER NOT NULL DEFAULT 1,"
		"    is_ready     INTEGER NOT NULL DEFAULT 0,"
		"    hero_type    INTEGER NOT NULL DEFAULT 3,"
		"    joined_at    DATETIME DEFAULT CURRENT_TIMESTAMP"
		");"
	)))
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ CREATE TABLE party_members 실패 | 에러: %s"), *Database->GetLastError());
		return false;
	}
	UE_LOG(LogHelluna, Log, TEXT("[SQLite]   CREATE TABLE party_members ✓"));

	// ── party_members 인덱스 ──
	if (!Database->Execute(TEXT("CREATE INDEX IF NOT EXISTS idx_party_members_party_id ON party_members(party_id);")))
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ INDEX idx_party_members_party_id 실패 | 에러: %s"), *Database->GetLastError());
		return false;
	}
	if (!Database->Execute(TEXT("CREATE INDEX IF NOT EXISTS idx_party_members_player_id ON party_members(player_id);")))
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ INDEX idx_party_members_player_id 실패 | 에러: %s"), *Database->GetLastError());
		return false;
	}

	// ── player_equipment 테이블 생성 (장착 상태 보존) ──
	// 게임→로비 복귀 시 장착 슬롯 정보를 별도 관리
	if (!Database->Execute(TEXT(
		"CREATE TABLE IF NOT EXISTS player_equipment ("
		"    player_id   TEXT NOT NULL,"
		"    slot_id     TEXT NOT NULL,"
		"    item_type   TEXT NOT NULL,"
		"    updated_at  DATETIME DEFAULT CURRENT_TIMESTAMP,"
		"    PRIMARY KEY (player_id, slot_id)"
		");"
	)))
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ CREATE TABLE player_equipment 실패 | 에러: %s"), *Database->GetLastError());
		return false;
	}
	UE_LOG(LogHelluna, Log, TEXT("[SQLite]   CREATE TABLE player_equipment ✓"));

	// ── [Phase 14a] player_deploy_state 마이그레이션: deployed_port, deployed_hero_type 추가 ──
	// 기존 테이블에 컬럼이 없을 수 있으므로 ALTER TABLE (이미 있으면 에러 무시)
	Database->Execute(TEXT("ALTER TABLE player_deploy_state ADD COLUMN deployed_port INTEGER NOT NULL DEFAULT 0;"));
	Database->Execute(TEXT("ALTER TABLE player_deploy_state ADD COLUMN deployed_hero_type INTEGER NOT NULL DEFAULT 3;"));

	UE_LOG(LogHelluna, Log, TEXT("[SQLite] ✓ InitializeSchema 완료 (테이블 8개, 인덱스 7개)"));
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
		UE_LOG(LogHelluna, Verbose, TEXT("[SQLite] IsDatabaseReady=false | bDatabaseOpen=%s | Database=%s | IsValid=%s"),
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
	Writer->Close();

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
//
// ─── 컬럼 매핑 ───
// item_type           → Item.ItemType (FGameplayTag)
// stack_count         → Item.StackCount (int32)
// grid_position_x     → Item.GridPosition.X (int32)
// grid_position_y     → Item.GridPosition.Y (int32)
// grid_category       → Item.GridCategory (uint8)
// is_equipped         → Item.bEquipped (bool) — [Fix14] Stash/Loadout 모두 사용
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


	if (PlayerId.IsEmpty())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ LoadPlayerStash: PlayerId가 비어있음 — 중단"));
		return TArray<FInv_SavedItemData>();
	}

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
	int32 TotalRows = 0;
	int32 InvalidRows = 0;
	SelectStmt.Execute([&Result, &TotalRows, &InvalidRows](const FSQLitePreparedStatement& Stmt) -> ESQLitePreparedStatementExecuteRowResult
	{
		FInv_SavedItemData Item = ParseRowToSavedItem(Stmt);
		TotalRows++;
		if (Item.IsValid())
		{
			Result.Add(MoveTemp(Item));
		}
		else
		{
			InvalidRows++;
			UE_LOG(LogHelluna, Warning, TEXT("[SQLite] LoadPlayerStash: IsValid() 실패한 행 발견! | ItemType=%s | Stack=%d | (행 %d)"),
				*Item.ItemType.ToString(), Item.StackCount, TotalRows);
		}
		return ESQLitePreparedStatementExecuteRowResult::Continue;  // 다음 행 계속
	});

	if (InvalidRows > 0)
	{
		UE_LOG(LogHelluna, Warning, TEXT("[SQLite] LoadPlayerStash: %d/%d 행이 IsValid() 실패 → 무시됨"), InvalidRows, TotalRows);
	}

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

	if (PlayerId.IsEmpty())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ SavePlayerStash: PlayerId가 비어있음 — 중단"));
		return false;
	}

	if (!IsDatabaseReady())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ SavePlayerStash: DB가 준비되지 않음"));
		return false;
	}

	// ── 트랜잭션 시작 ──
	// 여러 SQL을 하나의 원자적 단위로 묶음
	// → 중간에 실패하면 ROLLBACK으로 전부 취소 (데이터 정합성 보장)
	if (!Database->Execute(TEXT("BEGIN IMMEDIATE TRANSACTION;")))
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ SavePlayerStash: BEGIN IMMEDIATE TRANSACTION 실패 | 에러: %s"), *Database->GetLastError());
		return false;
	}
	UE_LOG(LogHelluna, Verbose, TEXT("[SQLite]   BEGIN IMMEDIATE TRANSACTION ✓"));

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

	if (PlayerId.IsEmpty())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ IsPlayerExists: PlayerId가 비어있음 — 중단"));
		return false;
	}

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
// [Fix14] is_equipped 컬럼 추가 — 장착 상태 보존하여 게임서버에 전달
//   → ParseRowToSavedItem에서 is_equipped + weapon_slot 읽어 bEquipped + WeaponSlotIndex 설정
//
// 호출 시점:
//   - 게임서버 PostLogin에서 LoadPlayerLoadout → InvComp에 복원
// ──────────────────────────────────────────────────────────────
TArray<FInv_SavedItemData> UHellunaSQLiteSubsystem::LoadPlayerLoadout(const FString& PlayerId)
{
	UE_LOG(LogHelluna, Log, TEXT("[SQLite] ▶ LoadPlayerLoadout | PlayerId=%s"), *PlayerId);

	if (PlayerId.IsEmpty())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ LoadPlayerLoadout: PlayerId가 비어있음 — 중단"));
		return TArray<FInv_SavedItemData>();
	}

	if (!IsDatabaseReady())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ LoadPlayerLoadout: DB가 준비되지 않음"));
		return TArray<FInv_SavedItemData>();
	}

	// [Fix14] is_equipped 컬럼 포함하여 장착 상태 보존
	const TCHAR* SelectSQL = TEXT(
		"SELECT item_type, stack_count, grid_position_x, grid_position_y, "
		"grid_category, is_equipped, weapon_slot, serialized_manifest, attachments_json "
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
//   2. Loadout INSERT (10개 바인딩 — [Fix14] is_equipped 포함)
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
	int32 EquippedCount = 0;
	int32 GridCount = 0;
	int32 AttachmentCount = 0;
	for (const FInv_SavedItemData& Item : Items)
	{
		if (Item.bEquipped)
		{
			++EquippedCount;
		}
		else
		{
			++GridCount;
		}
		AttachmentCount += Item.Attachments.Num();
	}
	UE_LOG(LogHelluna, Log, TEXT("[SQLite] ▶ SavePlayerLoadout | PlayerId=%s | 출격 아이템 %d개"), *PlayerId, Items.Num());

	if (PlayerId.IsEmpty())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ SavePlayerLoadout: PlayerId가 비어있음 — 중단"));
		return false;
	}

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
	if (!Database->Execute(TEXT("BEGIN IMMEDIATE TRANSACTION;")))
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ SavePlayerLoadout: BEGIN IMMEDIATE TRANSACTION 실패 | 에러: %s"), *Database->GetLastError());
		return false;
	}
	UE_LOG(LogHelluna, Verbose, TEXT("[SQLite]   BEGIN IMMEDIATE TRANSACTION ✓"));

	// (0) 기존 Loadout 삭제 (중복 방지 — 더블 클릭 등)
	{
		FSQLitePreparedStatement DeleteOldLoadout = Database->PrepareStatement(
			TEXT("DELETE FROM player_loadout WHERE player_id = ?1;"));
		if (DeleteOldLoadout.IsValid())
		{
			DeleteOldLoadout.SetBindingValueByIndex(1, PlayerId);
			DeleteOldLoadout.Execute();
		}
		UE_LOG(LogHelluna, Verbose, TEXT("[SQLite]   DELETE old loadout ✓"));
	}

	// (a) player_loadout에 Items INSERT
	//     [Fix14] is_equipped 컬럼 추가 → 10개 바인딩 (?1~?10)
	UE_LOG(LogHelluna, Log, TEXT("[SQLite] SavePlayerLoadout detail | PlayerId=%s | Grid=%d | Equipped=%d | Attachments=%d"),
		*PlayerId, GridCount, EquippedCount, AttachmentCount);

	const TCHAR* InsertSQL = TEXT(
		"INSERT INTO player_loadout "
		"(player_id, item_type, stack_count, grid_position_x, grid_position_y, "
		"grid_category, is_equipped, weapon_slot, serialized_manifest, attachments_json) "
		"VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10);"
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
		InsertStmt.SetBindingValueByIndex(7, Item.bEquipped ? 1 : 0);                    // ?7: is_equipped [Fix14]
		InsertStmt.SetBindingValueByIndex(8, Item.WeaponSlotIndex);                       // ?8: weapon_slot

		// ?9: serialized_manifest (BLOB)
		if (Item.SerializedManifest.Num() > 0)
		{
			InsertStmt.SetBindingValueByIndex(9, TArrayView<const uint8>(Item.SerializedManifest), true);
		}
		else
		{
			InsertStmt.SetBindingValueByIndex(9); // NULL
		}

		// ?10: attachments_json
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
			UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ SavePlayerLoadout: Loadout INSERT[%d] 실패 — ROLLBACK | 아이템=%s | 에러: %s"),
				i, *Item.ItemType.ToString(), *Database->GetLastError());
			Database->Execute(TEXT("ROLLBACK;"));
			return false;
		}

		InsertStmt.Reset();
		InsertStmt.ClearBindings();
	}
	UE_LOG(LogHelluna, Verbose, TEXT("[SQLite]   Loadout INSERT %d개 ✓"), Items.Num());

	// ⭐ [Fix] Stash DELETE 제거 — 잔여 Stash 보존
	// 이전: SavePlayerLoadout 안에서 player_stash 전부 DELETE (비행기표 패턴)
	// 문제: Deploy 흐름에서 [3a] SavePlayerStash(잔여) → [3b] SavePlayerLoadout(DELETE stash)
	//       → [3a]에서 저장한 잔여 아이템이 [3b]에서 다시 삭제됨 (데이터 소실)
	// 수정: Stash 관리는 Deploy 흐름의 SavePlayerStash에 위임
	//       SavePlayerLoadout은 Loadout 데이터만 관리

	// ── 트랜잭션 커밋 ──
	if (!Database->Execute(TEXT("COMMIT;")))
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ SavePlayerLoadout: COMMIT 실패 — ROLLBACK | 에러: %s"), *Database->GetLastError());
		Database->Execute(TEXT("ROLLBACK;"));
		return false;
	}

	UE_LOG(LogHelluna, Log, TEXT("[SQLite] ✓ SavePlayerLoadout 완료 | PlayerId=%s | Loadout %d개 INSERT (Stash 보존)"), *PlayerId, Items.Num());
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

	if (PlayerId.IsEmpty())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ DeletePlayerLoadout: PlayerId가 비어있음 — 중단"));
		return false;
	}

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

	if (PlayerId.IsEmpty())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ MergeGameResultToStash: PlayerId가 비어있음 — 중단"));
		return false;
	}

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
	if (!Database->Execute(TEXT("BEGIN IMMEDIATE TRANSACTION;")))
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ MergeGameResultToStash: BEGIN IMMEDIATE TRANSACTION 실패 | 에러: %s"), *Database->GetLastError());
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

		// ⭐ [진단] 각 아이템의 StackCount 확인 — Stack=0 원인 추적
		UE_LOG(LogHelluna, Log, TEXT("[SQLite] MergeGameResultToStash: INSERT[%d] %s | Stack=%d | Equipped=%s | Grid=(%d,%d)"),
			i, *Item.ItemType.ToString(), Item.StackCount,
			Item.bEquipped ? TEXT("Y") : TEXT("N"),
			Item.GridPosition.X, Item.GridPosition.Y);

		InsertStmt.SetBindingValueByIndex(1, PlayerId);
		InsertStmt.SetBindingValueByIndex(2, Item.ItemType.ToString());
		InsertStmt.SetBindingValueByIndex(3, Item.StackCount);
		// ⭐ 게임 그리드 위치를 (-1,-1)로 리셋 — 게임/로비 그리드 사이즈가 다르므로
		// 로비에서 HasRoomForItem → 2D 순회로 자동 배치되게 함
		InsertStmt.SetBindingValueByIndex(4, -1);  // grid_position_x = -1 (미배치)
		InsertStmt.SetBindingValueByIndex(5, -1);  // grid_position_y = -1 (미배치)
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

	if (PlayerId.IsEmpty())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ HasPendingLoadout: PlayerId가 비어있음 — 중단"));
		return false;
	}

	if (!IsDatabaseReady())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ HasPendingLoadout: DB가 준비되지 않음"));
		return false;
	}

	// [Fix46-M5] SELECT 1 LIMIT 1 — 존재 여부만 판별 (COUNT(*) 불필요)
	const TCHAR* ExistsSQL = TEXT("SELECT 1 FROM player_loadout WHERE player_id = ?1 LIMIT 1;");
	FSQLitePreparedStatement ExistsStmt = Database->PrepareStatement(ExistsSQL);
	if (!ExistsStmt.IsValid())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ HasPendingLoadout: PrepareStatement 실패 | 에러: %s"), *Database->GetLastError());
		return false;
	}

	ExistsStmt.SetBindingValueByIndex(1, PlayerId);

	bool bFound = false;
	ExistsStmt.Execute([&bFound](const FSQLitePreparedStatement& Stmt) -> ESQLitePreparedStatementExecuteRowResult
	{
		bFound = true;
		return ESQLitePreparedStatementExecuteRowResult::Stop;
	});

	if (bFound)
	{
		UE_LOG(LogHelluna, Warning, TEXT("[SQLite] ⚠ HasPendingLoadout: Loadout 잔존 감지! (비정상 종료 의심) | PlayerId=%s"), *PlayerId);
	}
	else
	{
		UE_LOG(LogHelluna, Log, TEXT("[SQLite] ✓ HasPendingLoadout: 잔존 없음 (정상) | PlayerId=%s"), *PlayerId);
	}

	return bFound;
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

	if (PlayerId.IsEmpty())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ RecoverFromCrash: PlayerId가 비어있음 — 중단"));
		return false;
	}

	if (!IsDatabaseReady())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ RecoverFromCrash: DB가 준비되지 않음"));
		return false;
	}

	// ── 트랜잭션 시작 ──
	if (!Database->Execute(TEXT("BEGIN IMMEDIATE TRANSACTION;")))
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ RecoverFromCrash: BEGIN IMMEDIATE TRANSACTION 실패 | 에러: %s"), *Database->GetLastError());
		return false;
	}

	// (a) player_loadout에서 잔존 아이템 SELECT
	// [Fix26] is_equipped 컬럼 추가 (누락 시 장비 장착 상태 복원 불가)
	const TCHAR* SelectSQL = TEXT(
		"SELECT item_type, stack_count, grid_position_x, grid_position_y, "
		"grid_category, is_equipped, weapon_slot, serialized_manifest, attachments_json "
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
		// [Fix29-J] Loadout Grid 좌표는 Stash Grid와 크기가 다를 수 있음 → (-1,-1)로 리셋하여 클라에서 자동 배치
		InsertStmt.SetBindingValueByIndex(4, -1);
		InsertStmt.SetBindingValueByIndex(5, -1);
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
// [Fix36] 출격 상태 추적 (독립 Loadout 영속성)
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 목적:
//   Loadout 존재 여부가 아닌 별도 deploy_state 테이블로 크래시 감지
//   → Loadout이 DB에 남아있어도 정상 (독립 영속성)
//   → is_deployed=true + 게임 결과 없음 = 크래시
//
// ════════════════════════════════════════════════════════════════════════════════

// ──────────────────────────────────────────────────────────────
// SetPlayerDeployed — 출격 상태 설정/해제
// ──────────────────────────────────────────────────────────────
bool UHellunaSQLiteSubsystem::SetPlayerDeployed(const FString& PlayerId, bool bDeployed)
{
	UE_LOG(LogHelluna, Log, TEXT("[SQLite] [Fix36] ▶ SetPlayerDeployed | PlayerId=%s | bDeployed=%s"),
		*PlayerId, bDeployed ? TEXT("true") : TEXT("false"));

	if (PlayerId.IsEmpty())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] [Fix36] ✗ SetPlayerDeployed: PlayerId가 비어있음"));
		return false;
	}

	if (!IsDatabaseReady())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] [Fix36] ✗ SetPlayerDeployed: DB가 준비되지 않음"));
		return false;
	}

	// INSERT OR REPLACE (UPSERT): player_id가 PRIMARY KEY이므로 존재하면 UPDATE, 없으면 INSERT
	FSQLitePreparedStatement Statement;
	Statement.Create(*Database,
		TEXT("INSERT OR REPLACE INTO player_deploy_state (player_id, is_deployed, deployed_at) VALUES (?, ?, CURRENT_TIMESTAMP);"),
		ESQLitePreparedStatementFlags::Persistent);

	if (!Statement.IsValid())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] [Fix36] ✗ SetPlayerDeployed: PrepareStatement 실패 | 에러: %s"), *Database->GetLastError());
		return false;
	}

	Statement.SetBindingValueByIndex(1, PlayerId);
	Statement.SetBindingValueByIndex(2, static_cast<int64>(bDeployed ? 1 : 0));

	if (!Statement.Execute())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] [Fix36] ✗ SetPlayerDeployed: Execute 실패 | 에러: %s"), *Database->GetLastError());
		return false;
	}

	UE_LOG(LogHelluna, Log, TEXT("[SQLite] [Fix36] ✓ SetPlayerDeployed 완료 | PlayerId=%s | is_deployed=%d"),
		*PlayerId, bDeployed ? 1 : 0);
	return true;
}

// ──────────────────────────────────────────────────────────────
// IsPlayerDeployed — 출격 중인지 확인
// ──────────────────────────────────────────────────────────────
bool UHellunaSQLiteSubsystem::IsPlayerDeployed(const FString& PlayerId)
{
	UE_LOG(LogHelluna, Log, TEXT("[SQLite] [Fix36] ▶ IsPlayerDeployed | PlayerId=%s"), *PlayerId);

	if (PlayerId.IsEmpty())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] [Fix36] ✗ IsPlayerDeployed: PlayerId가 비어있음"));
		return false;
	}

	if (!IsDatabaseReady())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] [Fix36] ✗ IsPlayerDeployed: DB가 준비되지 않음"));
		return false;
	}

	FSQLitePreparedStatement Statement;
	Statement.Create(*Database,
		TEXT("SELECT is_deployed FROM player_deploy_state WHERE player_id = ?;"),
		ESQLitePreparedStatementFlags::Persistent);

	if (!Statement.IsValid())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] [Fix36] ✗ IsPlayerDeployed: PrepareStatement 실패 | 에러: %s"), *Database->GetLastError());
		return false;
	}

	Statement.SetBindingValueByIndex(1, PlayerId);

	if (Statement.Step() == ESQLitePreparedStatementStepResult::Row)
	{
		int64 IsDeployed = 0;
		Statement.GetColumnValueByIndex(0, IsDeployed);
		const bool bResult = (IsDeployed != 0);
		UE_LOG(LogHelluna, Log, TEXT("[SQLite] [Fix36] ✓ IsPlayerDeployed: %s | PlayerId=%s"),
			bResult ? TEXT("출격 중") : TEXT("로비"), *PlayerId);
		return bResult;
	}

	// 행 없음 = 한 번도 출격한 적 없음 → false
	UE_LOG(LogHelluna, Log, TEXT("[SQLite] [Fix36] ✓ IsPlayerDeployed: 기록 없음 (미출격) | PlayerId=%s"), *PlayerId);
	return false;
}

// ──────────────────────────────────────────────────────────────
// [Phase 14a] SetPlayerDeployedWithPort — 출격 상태 + 포트/영웅타입 저장
// ──────────────────────────────────────────────────────────────
bool UHellunaSQLiteSubsystem::SetPlayerDeployedWithPort(const FString& PlayerId, bool bDeployed, int32 ServerPort, int32 HeroTypeIndex)
{
	UE_LOG(LogHelluna, Log, TEXT("[SQLite] [Phase14] ▶ SetPlayerDeployedWithPort | PlayerId=%s | bDeployed=%s | Port=%d | HeroType=%d"),
		*PlayerId, bDeployed ? TEXT("true") : TEXT("false"), ServerPort, HeroTypeIndex);

	if (PlayerId.IsEmpty())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] [Phase14] ✗ SetPlayerDeployedWithPort: PlayerId가 비어있음"));
		return false;
	}

	if (!IsDatabaseReady())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] [Phase14] ✗ SetPlayerDeployedWithPort: DB가 준비되지 않음"));
		return false;
	}

	FSQLitePreparedStatement Statement;
	Statement.Create(*Database,
		TEXT("INSERT OR REPLACE INTO player_deploy_state (player_id, is_deployed, deployed_port, deployed_hero_type, deployed_at) VALUES (?, ?, ?, ?, CURRENT_TIMESTAMP);"),
		ESQLitePreparedStatementFlags::Persistent);

	if (!Statement.IsValid())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] [Phase14] ✗ SetPlayerDeployedWithPort: PrepareStatement 실패 | 에러: %s"), *Database->GetLastError());
		return false;
	}

	Statement.SetBindingValueByIndex(1, PlayerId);
	Statement.SetBindingValueByIndex(2, static_cast<int64>(bDeployed ? 1 : 0));
	Statement.SetBindingValueByIndex(3, static_cast<int64>(ServerPort));
	Statement.SetBindingValueByIndex(4, static_cast<int64>(HeroTypeIndex));

	if (!Statement.Execute())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] [Phase14] ✗ SetPlayerDeployedWithPort: Execute 실패 | 에러: %s"), *Database->GetLastError());
		return false;
	}

	UE_LOG(LogHelluna, Log, TEXT("[SQLite] [Phase14] ✓ SetPlayerDeployedWithPort 완료 | PlayerId=%s | Port=%d | HeroType=%d"),
		*PlayerId, ServerPort, HeroTypeIndex);
	return true;
}

// ──────────────────────────────────────────────────────────────
// [Phase 14a] GetPlayerDeployedPort — 출격 플레이어의 게임서버 포트 조회
// ──────────────────────────────────────────────────────────────
int32 UHellunaSQLiteSubsystem::GetPlayerDeployedPort(const FString& PlayerId)
{
	if (PlayerId.IsEmpty() || !IsDatabaseReady()) return 0;

	FSQLitePreparedStatement Statement;
	Statement.Create(*Database,
		TEXT("SELECT deployed_port FROM player_deploy_state WHERE player_id = ? AND is_deployed = 1;"),
		ESQLitePreparedStatementFlags::Persistent);

	if (!Statement.IsValid()) return 0;

	Statement.SetBindingValueByIndex(1, PlayerId);

	if (Statement.Step() == ESQLitePreparedStatementStepResult::Row)
	{
		int64 Port = 0;
		Statement.GetColumnValueByIndex(0, Port);
		return static_cast<int32>(Port);
	}

	return 0;
}

// ──────────────────────────────────────────────────────────────
// [Phase 14a] GetPlayerDeployedHeroType — 출격 플레이어의 영웅타입 인덱스 조회
// ──────────────────────────────────────────────────────────────
int32 UHellunaSQLiteSubsystem::GetPlayerDeployedHeroType(const FString& PlayerId)
{
	if (PlayerId.IsEmpty() || !IsDatabaseReady()) return 3; // 3 = None

	FSQLitePreparedStatement Statement;
	Statement.Create(*Database,
		TEXT("SELECT deployed_hero_type FROM player_deploy_state WHERE player_id = ? AND is_deployed = 1;"),
		ESQLitePreparedStatementFlags::Persistent);

	if (!Statement.IsValid()) return 3;

	Statement.SetBindingValueByIndex(1, PlayerId);

	if (Statement.Step() == ESQLitePreparedStatementStepResult::Row)
	{
		int64 HeroType = 3;
		Statement.GetColumnValueByIndex(0, HeroType);
		return static_cast<int32>(HeroType);
	}

	return 3;
}


// ════════════════════════════════════════════════════════════════════════════════
// 장착 상태 관리 (player_equipment)
// ════════════════════════════════════════════════════════════════════════════════

// ──────────────────────────────────────────────────────────────
// SavePlayerEquipment — 장착 스냅샷 저장 (DELETE + INSERT)
// ──────────────────────────────────────────────────────────────
bool UHellunaSQLiteSubsystem::SavePlayerEquipment(const FString& PlayerId, const TArray<FHellunaEquipmentSlotData>& Equipment)
{
	UE_LOG(LogHelluna, Log, TEXT("[SQLite] ▶ SavePlayerEquipment | PlayerId=%s | %d개 슬롯"), *PlayerId, Equipment.Num());

	if (PlayerId.IsEmpty())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ SavePlayerEquipment: PlayerId가 비어있음"));
		return false;
	}
	if (!IsDatabaseReady())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ SavePlayerEquipment: DB가 준비되지 않음"));
		return false;
	}

	// 트랜잭션
	if (!Database->Execute(TEXT("BEGIN TRANSACTION;")))
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ SavePlayerEquipment: BEGIN 실패 | 에러: %s"), *Database->GetLastError());
		return false;
	}

	// (1) 기존 삭제
	{
		FSQLitePreparedStatement DeleteStmt = Database->PrepareStatement(
			TEXT("DELETE FROM player_equipment WHERE player_id = ?1;"));
		if (!DeleteStmt.IsValid())
		{
			Database->Execute(TEXT("ROLLBACK;"));
			return false;
		}
		DeleteStmt.SetBindingValueByIndex(1, PlayerId);
		if (!DeleteStmt.Execute())
		{
			UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ SavePlayerEquipment: DELETE 실패 | 에러: %s"), *Database->GetLastError());
			Database->Execute(TEXT("ROLLBACK;"));
			return false;
		}
	}

	// (2) 신규 INSERT
	if (Equipment.Num() > 0)
	{
		FSQLitePreparedStatement InsertStmt = Database->PrepareStatement(
			TEXT("INSERT INTO player_equipment (player_id, slot_id, item_type) VALUES (?1, ?2, ?3);"),
			ESQLitePreparedStatementFlags::Persistent);
		if (!InsertStmt.IsValid())
		{
			UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ SavePlayerEquipment: INSERT Prepare 실패 | 에러: %s"), *Database->GetLastError());
			Database->Execute(TEXT("ROLLBACK;"));
			return false;
		}

		for (const FHellunaEquipmentSlotData& Slot : Equipment)
		{
			InsertStmt.SetBindingValueByIndex(1, PlayerId);
			InsertStmt.SetBindingValueByIndex(2, Slot.SlotId);
			InsertStmt.SetBindingValueByIndex(3, Slot.ItemType.ToString());

			if (!InsertStmt.Execute())
			{
				UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ SavePlayerEquipment: INSERT 실패 | slot=%s | 에러: %s"),
					*Slot.SlotId, *Database->GetLastError());
				Database->Execute(TEXT("ROLLBACK;"));
				return false;
			}
			InsertStmt.Reset();
			InsertStmt.ClearBindings();
		}
	}

	if (!Database->Execute(TEXT("COMMIT;")))
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ SavePlayerEquipment: COMMIT 실패 | 에러: %s"), *Database->GetLastError());
		Database->Execute(TEXT("ROLLBACK;"));
		return false;
	}

	UE_LOG(LogHelluna, Log, TEXT("[SQLite] ✓ SavePlayerEquipment 완료 | PlayerId=%s | %d개 슬롯"), *PlayerId, Equipment.Num());
	return true;
}

// ──────────────────────────────────────────────────────────────
// LoadPlayerEquipment — 장착 스냅샷 로드
// ──────────────────────────────────────────────────────────────
TArray<FHellunaEquipmentSlotData> UHellunaSQLiteSubsystem::LoadPlayerEquipment(const FString& PlayerId)
{
	TArray<FHellunaEquipmentSlotData> Result;

	if (PlayerId.IsEmpty() || !IsDatabaseReady())
	{
		return Result;
	}

	FSQLitePreparedStatement Stmt = Database->PrepareStatement(
		TEXT("SELECT slot_id, item_type FROM player_equipment WHERE player_id = ?1;"));
	if (!Stmt.IsValid())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ LoadPlayerEquipment: PrepareStatement 실패"));
		return Result;
	}

	Stmt.SetBindingValueByIndex(1, PlayerId);

	while (Stmt.Step() == ESQLitePreparedStatementStepResult::Row)
	{
		FHellunaEquipmentSlotData Slot;
		Stmt.GetColumnValueByName(TEXT("slot_id"), Slot.SlotId);

		FString ItemTypeStr;
		Stmt.GetColumnValueByName(TEXT("item_type"), ItemTypeStr);
		Slot.ItemType = FGameplayTag::RequestGameplayTag(FName(*ItemTypeStr), false);

		Result.Add(MoveTemp(Slot));
	}

	UE_LOG(LogHelluna, Log, TEXT("[SQLite] ✓ LoadPlayerEquipment | PlayerId=%s | %d개 슬롯 로드"), *PlayerId, Result.Num());
	return Result;
}

// ──────────────────────────────────────────────────────────────
// DeletePlayerEquipment — 장착 정보 삭제
// ──────────────────────────────────────────────────────────────
bool UHellunaSQLiteSubsystem::DeletePlayerEquipment(const FString& PlayerId)
{
	if (PlayerId.IsEmpty() || !IsDatabaseReady())
	{
		return false;
	}

	FSQLitePreparedStatement Stmt = Database->PrepareStatement(
		TEXT("DELETE FROM player_equipment WHERE player_id = ?1;"));
	if (!Stmt.IsValid())
	{
		return false;
	}

	Stmt.SetBindingValueByIndex(1, PlayerId);
	if (!Stmt.Execute())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ DeletePlayerEquipment 실패 | 에러: %s"), *Database->GetLastError());
		return false;
	}

	UE_LOG(LogHelluna, Log, TEXT("[SQLite] ✓ DeletePlayerEquipment 완료 | PlayerId=%s"), *PlayerId);
	return true;
}


// ════════════════════════════════════════════════════════════════════════════════
// 게임 캐릭터 중복 방지 (active_game_characters)
// ════════════════════════════════════════════════════════════════════════════════

// ──────────────────────────────────────────────────────────────
// GetActiveGameCharacters — 현재 사용 중인 캐릭터 목록
// ──────────────────────────────────────────────────────────────
TArray<bool> UHellunaSQLiteSubsystem::GetActiveGameCharacters()
{
	// 3개 캐릭터: [0]=Lui, [1]=Luna, [2]=Liam, 기본값 false(미사용)
	TArray<bool> Result;
	Result.SetNum(3);
	Result[0] = false;
	Result[1] = false;
	Result[2] = false;

	if (!IsDatabaseReady())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] GetActiveGameCharacters: DB 미준비"));
		return Result;
	}

	const TCHAR* SelectSQL = TEXT("SELECT DISTINCT hero_type FROM active_game_characters;");
	FSQLitePreparedStatement SelectStmt = Database->PrepareStatement(SelectSQL);

	if (!SelectStmt.IsValid())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] GetActiveGameCharacters: PrepareStatement 실패 | 에러: %s"), *Database->GetLastError());
		return Result;
	}

	SelectStmt.Execute([&Result](const FSQLitePreparedStatement& Stmt) -> ESQLitePreparedStatementExecuteRowResult
	{
		int64 HeroType = 0;
		Stmt.GetColumnValueByIndex(0, HeroType);
		if (HeroType >= 0 && HeroType < 3)
		{
			Result[static_cast<int32>(HeroType)] = true;
		}
		return ESQLitePreparedStatementExecuteRowResult::Continue;
	});

	UE_LOG(LogHelluna, Log, TEXT("[SQLite] GetActiveGameCharacters: Lui=%s Luna=%s Liam=%s"),
		Result[0] ? TEXT("사용중") : TEXT("가능"),
		Result[1] ? TEXT("사용중") : TEXT("가능"),
		Result[2] ? TEXT("사용중") : TEXT("가능"));

	return Result;
}

// ──────────────────────────────────────────────────────────────
// RegisterActiveGameCharacter — 캐릭터 사용 등록
// ──────────────────────────────────────────────────────────────
bool UHellunaSQLiteSubsystem::RegisterActiveGameCharacter(int32 HeroType, const FString& PlayerId, const FString& ServerId)
{
	UE_LOG(LogHelluna, Log, TEXT("[SQLite] RegisterActiveGameCharacter | HeroType=%d | PlayerId=%s | ServerId=%s"),
		HeroType, *PlayerId, *ServerId);

	if (!IsDatabaseReady())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] RegisterActiveGameCharacter: DB 미준비"));
		return false;
	}

	if (HeroType < 0 || HeroType > 2)
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] RegisterActiveGameCharacter: 잘못된 HeroType=%d"), HeroType);
		return false;
	}

	// 트랜잭션으로 원자적 처리 (DELETE 후 INSERT 실패 시 등록이 사라지는 것 방지)
	if (!Database->Execute(TEXT("BEGIN IMMEDIATE TRANSACTION;")))
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] RegisterActiveGameCharacter: BEGIN TRANSACTION 실패 | 에러: %s"), *Database->GetLastError());
		return false;
	}

	// 기존 플레이어 등록 제거 (재선택 허용)
	{
		const TCHAR* DeleteSQL = TEXT("DELETE FROM active_game_characters WHERE player_id = ?1;");
		FSQLitePreparedStatement DeleteStmt = Database->PrepareStatement(DeleteSQL);
		if (DeleteStmt.IsValid())
		{
			DeleteStmt.SetBindingValueByIndex(1, PlayerId);
			DeleteStmt.Execute();
		}
	}

	// INSERT (UNIQUE INDEX가 중복 방지)
	const TCHAR* InsertSQL = TEXT("INSERT INTO active_game_characters (hero_type, player_id, server_id) VALUES (?1, ?2, ?3);");
	FSQLitePreparedStatement InsertStmt = Database->PrepareStatement(InsertSQL);

	if (!InsertStmt.IsValid())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] RegisterActiveGameCharacter: PrepareStatement 실패 | 에러: %s"), *Database->GetLastError());
		Database->Execute(TEXT("ROLLBACK;"));
		return false;
	}

	InsertStmt.SetBindingValueByIndex(1, static_cast<int64>(HeroType));
	InsertStmt.SetBindingValueByIndex(2, PlayerId);
	InsertStmt.SetBindingValueByIndex(3, ServerId);

	if (InsertStmt.Execute())
	{
		Database->Execute(TEXT("COMMIT;"));
		UE_LOG(LogHelluna, Log, TEXT("[SQLite] ✓ RegisterActiveGameCharacter 성공 | HeroType=%d | PlayerId=%s"), HeroType, *PlayerId);
		return true;
	}
	else
	{
		Database->Execute(TEXT("ROLLBACK;"));
		UE_LOG(LogHelluna, Warning, TEXT("[SQLite] ✗ RegisterActiveGameCharacter 실패 (중복?) | HeroType=%d | 에러: %s"),
			HeroType, *Database->GetLastError());
		return false;
	}
}

// ──────────────────────────────────────────────────────────────
// UnregisterActiveGameCharacter — 플레이어의 캐릭터 등록 해제
// ──────────────────────────────────────────────────────────────
bool UHellunaSQLiteSubsystem::UnregisterActiveGameCharacter(const FString& PlayerId)
{
	if (!IsDatabaseReady())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] UnregisterActiveGameCharacter: DB 미준비"));
		return false;
	}

	const TCHAR* DeleteSQL = TEXT("DELETE FROM active_game_characters WHERE player_id = ?1;");
	FSQLitePreparedStatement DeleteStmt = Database->PrepareStatement(DeleteSQL);

	if (!DeleteStmt.IsValid())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] UnregisterActiveGameCharacter: PrepareStatement 실패 | 에러: %s"), *Database->GetLastError());
		return false;
	}

	DeleteStmt.SetBindingValueByIndex(1, PlayerId);

	if (DeleteStmt.Execute())
	{
		UE_LOG(LogHelluna, Log, TEXT("[SQLite] ✓ UnregisterActiveGameCharacter | PlayerId=%s"), *PlayerId);
		return true;
	}
	else
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ UnregisterActiveGameCharacter 실패 | PlayerId=%s | 에러: %s"),
			*PlayerId, *Database->GetLastError());
		return false;
	}
}

// ──────────────────────────────────────────────────────────────
// UnregisterAllActiveGameCharactersForServer — 서버의 모든 캐릭터 해제
// ──────────────────────────────────────────────────────────────
bool UHellunaSQLiteSubsystem::UnregisterAllActiveGameCharactersForServer(const FString& ServerId)
{
	if (!IsDatabaseReady())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] UnregisterAllForServer: DB 미준비"));
		return false;
	}

	const TCHAR* DeleteSQL = TEXT("DELETE FROM active_game_characters WHERE server_id = ?1;");
	FSQLitePreparedStatement DeleteStmt = Database->PrepareStatement(DeleteSQL);

	if (!DeleteStmt.IsValid())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] UnregisterAllForServer: PrepareStatement 실패 | 에러: %s"), *Database->GetLastError());
		return false;
	}

	DeleteStmt.SetBindingValueByIndex(1, ServerId);

	if (DeleteStmt.Execute())
	{
		UE_LOG(LogHelluna, Log, TEXT("[SQLite] ✓ UnregisterAllForServer | ServerId=%s"), *ServerId);
		return true;
	}
	else
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ UnregisterAllForServer 실패 | ServerId=%s | 에러: %s"),
			*ServerId, *Database->GetLastError());
		return false;
	}
}

// ──────────────────────────────────────────────────────────────
// ClearAllActiveGameCharacters — 전체 캐릭터 등록 해제 (서버 시작 시 stale 정리)
// ──────────────────────────────────────────────────────────────
bool UHellunaSQLiteSubsystem::ClearAllActiveGameCharacters()
{
	if (!IsDatabaseReady())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ClearAllActiveGameCharacters: DB 미준비"));
		return false;
	}

	if (Database->Execute(TEXT("DELETE FROM active_game_characters;")))
	{
		UE_LOG(LogHelluna, Log, TEXT("[SQLite] ✓ ClearAllActiveGameCharacters 완료"));
		return true;
	}
	else
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ ClearAllActiveGameCharacters 실패 | 에러: %s"), *Database->GetLastError());
		return false;
	}
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

		// 더미 아이템 5개 생성 (실제 GameplayTag 사용 — IsValid() 통과 필수!)
		// 무기류(Axe)는 SerializedManifest가 없으면 복원 실패 → 소모품/재료로 구성
		TArray<FInv_SavedItemData> Items;

		FInv_SavedItemData Item1;
		Item1.ItemType        = FGameplayTag::RequestGameplayTag(FName(TEXT("GameItems.Consumables.Potions.Blue.Small")), false);
		Item1.StackCount      = 10;
		Item1.GridPosition    = FIntPoint(0, 0);
		Item1.GridCategory    = 1;                // 소모품 카테고리
		Item1.bEquipped       = false;
		Item1.WeaponSlotIndex = -1;
		Items.Add(Item1);

		FInv_SavedItemData Item2;
		Item2.ItemType        = FGameplayTag::RequestGameplayTag(FName(TEXT("GameItems.Consumables.Potions.Red.Small")), false);
		Item2.StackCount      = 10;
		Item2.GridPosition    = FIntPoint(1, 0);
		Item2.GridCategory    = 1;                // 소모품 카테고리
		Item2.bEquipped       = false;
		Item2.WeaponSlotIndex = -1;
		Items.Add(Item2);

		FInv_SavedItemData Item3;
		Item3.ItemType        = FGameplayTag::RequestGameplayTag(FName(TEXT("GameItems.Craftables.FireFernFruit")), false);
		Item3.StackCount      = 5;
		Item3.GridPosition    = FIntPoint(0, 0);
		Item3.GridCategory    = 2;                // 재료 카테고리
		Item3.bEquipped       = false;
		Item3.WeaponSlotIndex = -1;
		Items.Add(Item3);

		FInv_SavedItemData Item4;
		Item4.ItemType        = FGameplayTag::RequestGameplayTag(FName(TEXT("GameItems.Craftables.LuminDaisy")), false);
		Item4.StackCount      = 5;
		Item4.GridPosition    = FIntPoint(1, 0);
		Item4.GridCategory    = 2;                // 재료 카테고리
		Item4.bEquipped       = false;
		Item4.WeaponSlotIndex = -1;
		Items.Add(Item4);

		FInv_SavedItemData Item5;
		Item5.ItemType        = FGameplayTag::RequestGameplayTag(FName(TEXT("GameItems.Equipment.Attachments.Muzzle")), false);
		Item5.StackCount      = 1;
		Item5.GridPosition    = FIntPoint(0, 0);
		Item5.GridCategory    = 0;                // 장비 카테고리
		Item5.bEquipped       = false;
		Item5.WeaponSlotIndex = -1;
		Items.Add(Item5);

		FInv_SavedItemData Item6;
		Item6.ItemType        = FGameplayTag::RequestGameplayTag(FName(TEXT("GameItems.Equipment.Weapons.Axe")), false);
		Item6.StackCount      = 1;
		Item6.GridPosition    = FIntPoint(2, 0);
		Item6.GridCategory    = 0;                // 장비 카테고리
		Item6.bEquipped       = false;
		Item6.WeaponSlotIndex = -1;
		Items.Add(Item6);

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


// ════════════════════════════════════════════════════════════════════════════════
// [Phase 12a] 파티 시스템 CRUD
// ════════════════════════════════════════════════════════════════════════════════

// ──────────────────────────────────────────────────────────────
// CreateParty — 파티 생성 (party_groups + party_members Leader 동시 INSERT)
// ──────────────────────────────────────────────────────────────
int32 UHellunaSQLiteSubsystem::CreateParty(const FString& LeaderId, const FString& DisplayName, const FString& PartyCode)
{
	UE_LOG(LogHelluna, Log, TEXT("[SQLite] ▶ CreateParty | Leader=%s | Code=%s"), *LeaderId, *PartyCode);

	if (LeaderId.IsEmpty() || PartyCode.IsEmpty())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ CreateParty: LeaderId 또는 PartyCode가 비어있음"));
		return 0;
	}

	if (!IsDatabaseReady())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ CreateParty: DB가 준비되지 않음"));
		return 0;
	}

	// 트랜잭션 시작
	if (!Database->Execute(TEXT("BEGIN IMMEDIATE TRANSACTION;")))
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ CreateParty: BEGIN 실패 | 에러: %s"), *Database->GetLastError());
		return 0;
	}

	// (1) party_groups INSERT
	{
		FSQLitePreparedStatement InsertGroup = Database->PrepareStatement(
			TEXT("INSERT INTO party_groups (party_code, leader_id) VALUES (?1, ?2);"));
		if (!InsertGroup.IsValid())
		{
			UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ CreateParty: INSERT party_groups Prepare 실패 | 에러: %s"), *Database->GetLastError());
			Database->Execute(TEXT("ROLLBACK;"));
			return 0;
		}
		InsertGroup.SetBindingValueByIndex(1, PartyCode);
		InsertGroup.SetBindingValueByIndex(2, LeaderId);
		if (!InsertGroup.Execute())
		{
			UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ CreateParty: INSERT party_groups 실패 — ROLLBACK | 에러: %s"), *Database->GetLastError());
			Database->Execute(TEXT("ROLLBACK;"));
			return 0;
		}
	}

	// (2) 생성된 PartyId 가져오기
	int32 PartyId = 0;
	{
		FSQLitePreparedStatement LastId = Database->PrepareStatement(TEXT("SELECT last_insert_rowid();"));
		if (LastId.IsValid())
		{
			LastId.Execute([&PartyId](const FSQLitePreparedStatement& Stmt) -> ESQLitePreparedStatementExecuteRowResult
			{
				int64 Id = 0;
				Stmt.GetColumnValueByIndex(0, Id);
				PartyId = static_cast<int32>(Id);
				return ESQLitePreparedStatementExecuteRowResult::Stop;
			});
		}
	}

	if (PartyId <= 0)
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ CreateParty: last_insert_rowid 실패 — ROLLBACK"));
		Database->Execute(TEXT("ROLLBACK;"));
		return 0;
	}

	// (3) party_members INSERT (Leader)
	{
		FSQLitePreparedStatement InsertMember = Database->PrepareStatement(
			TEXT("INSERT INTO party_members (party_id, player_id, display_name, role, is_ready, hero_type) VALUES (?1, ?2, ?3, ?4, ?5, ?6);"));
		if (!InsertMember.IsValid())
		{
			UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ CreateParty: INSERT party_members Prepare 실패 — ROLLBACK | 에러: %s"), *Database->GetLastError());
			Database->Execute(TEXT("ROLLBACK;"));
			return 0;
		}
		InsertMember.SetBindingValueByIndex(1, PartyId);
		InsertMember.SetBindingValueByIndex(2, LeaderId);
		InsertMember.SetBindingValueByIndex(3, DisplayName);
		InsertMember.SetBindingValueByIndex(4, static_cast<int32>(EHellunaPartyRole::Leader));
		InsertMember.SetBindingValueByIndex(5, 0); // not ready
		InsertMember.SetBindingValueByIndex(6, 3); // None
		if (!InsertMember.Execute())
		{
			UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ CreateParty: INSERT party_members 실패 — ROLLBACK | 에러: %s"), *Database->GetLastError());
			Database->Execute(TEXT("ROLLBACK;"));
			return 0;
		}
	}

	// 커밋
	if (!Database->Execute(TEXT("COMMIT;")))
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ CreateParty: COMMIT 실패 — ROLLBACK | 에러: %s"), *Database->GetLastError());
		Database->Execute(TEXT("ROLLBACK;"));
		return 0;
	}

	UE_LOG(LogHelluna, Log, TEXT("[SQLite] ✓ CreateParty 완료 | PartyId=%d | Code=%s | Leader=%s"), PartyId, *PartyCode, *LeaderId);
	return PartyId;
}

// ──────────────────────────────────────────────────────────────
// JoinParty — 파티 참가
// ──────────────────────────────────────────────────────────────
bool UHellunaSQLiteSubsystem::JoinParty(int32 PartyId, const FString& PlayerId, const FString& DisplayName)
{
	UE_LOG(LogHelluna, Log, TEXT("[SQLite] ▶ JoinParty | PartyId=%d | PlayerId=%s"), PartyId, *PlayerId);

	if (PartyId <= 0 || PlayerId.IsEmpty())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ JoinParty: 잘못된 인자 | PartyId=%d | PlayerId=%s"), PartyId, *PlayerId);
		return false;
	}

	if (!IsDatabaseReady())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ JoinParty: DB가 준비되지 않음"));
		return false;
	}

	if (!Database->Execute(TEXT("BEGIN IMMEDIATE TRANSACTION;")))
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ JoinParty: BEGIN 실패 | 에러: %s"), *Database->GetLastError());
		return false;
	}

	FSQLitePreparedStatement InsertStmt = Database->PrepareStatement(
		TEXT("INSERT INTO party_members (party_id, player_id, display_name, role, is_ready, hero_type) VALUES (?1, ?2, ?3, ?4, ?5, ?6);"));
	if (!InsertStmt.IsValid())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ JoinParty: Prepare 실패 — ROLLBACK | 에러: %s"), *Database->GetLastError());
		Database->Execute(TEXT("ROLLBACK;"));
		return false;
	}
	InsertStmt.SetBindingValueByIndex(1, PartyId);
	InsertStmt.SetBindingValueByIndex(2, PlayerId);
	InsertStmt.SetBindingValueByIndex(3, DisplayName);
	InsertStmt.SetBindingValueByIndex(4, static_cast<int32>(EHellunaPartyRole::Member));
	InsertStmt.SetBindingValueByIndex(5, 0); // not ready
	InsertStmt.SetBindingValueByIndex(6, 3); // None
	if (!InsertStmt.Execute())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ JoinParty: INSERT 실패 — ROLLBACK | 에러: %s"), *Database->GetLastError());
		Database->Execute(TEXT("ROLLBACK;"));
		return false;
	}

	if (!Database->Execute(TEXT("COMMIT;")))
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ JoinParty: COMMIT 실패 — ROLLBACK | 에러: %s"), *Database->GetLastError());
		Database->Execute(TEXT("ROLLBACK;"));
		return false;
	}

	UE_LOG(LogHelluna, Log, TEXT("[SQLite] ✓ JoinParty 완료 | PartyId=%d | PlayerId=%s"), PartyId, *PlayerId);
	return true;
}

// ──────────────────────────────────────────────────────────────
// LeaveParty — 파티 탈퇴 (마지막 멤버면 파티 자체 삭제)
// ──────────────────────────────────────────────────────────────
bool UHellunaSQLiteSubsystem::LeaveParty(const FString& PlayerId)
{
	UE_LOG(LogHelluna, Log, TEXT("[SQLite] ▶ LeaveParty | PlayerId=%s"), *PlayerId);

	if (PlayerId.IsEmpty())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ LeaveParty: PlayerId가 비어있음"));
		return false;
	}

	if (!IsDatabaseReady())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ LeaveParty: DB가 준비되지 않음"));
		return false;
	}

	// 먼저 파티 ID 확인
	const int32 PartyId = GetPlayerPartyId(PlayerId);
	if (PartyId <= 0)
	{
		UE_LOG(LogHelluna, Warning, TEXT("[SQLite] LeaveParty: 플레이어가 파티에 속하지 않음 | PlayerId=%s"), *PlayerId);
		return true; // 이미 탈퇴 상태 → 성공으로 처리
	}

	if (!Database->Execute(TEXT("BEGIN IMMEDIATE TRANSACTION;")))
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ LeaveParty: BEGIN 실패 | 에러: %s"), *Database->GetLastError());
		return false;
	}

	// (1) party_members에서 해당 플레이어 DELETE
	{
		FSQLitePreparedStatement DeleteStmt = Database->PrepareStatement(
			TEXT("DELETE FROM party_members WHERE player_id = ?1;"));
		if (!DeleteStmt.IsValid())
		{
			UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ LeaveParty: DELETE Prepare 실패 — ROLLBACK | 에러: %s"), *Database->GetLastError());
			Database->Execute(TEXT("ROLLBACK;"));
			return false;
		}
		DeleteStmt.SetBindingValueByIndex(1, PlayerId);
		if (!DeleteStmt.Execute())
		{
			UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ LeaveParty: DELETE 실패 — ROLLBACK | 에러: %s"), *Database->GetLastError());
			Database->Execute(TEXT("ROLLBACK;"));
			return false;
		}
	}

	// (2) 남은 멤버 수 확인 — 0이면 파티 삭제
	int64 RemainingCount = 0;
	{
		FSQLitePreparedStatement CountStmt = Database->PrepareStatement(
			TEXT("SELECT COUNT(*) FROM party_members WHERE party_id = ?1;"));
		if (CountStmt.IsValid())
		{
			CountStmt.SetBindingValueByIndex(1, PartyId);
			CountStmt.Execute([&RemainingCount](const FSQLitePreparedStatement& Stmt) -> ESQLitePreparedStatementExecuteRowResult
			{
				Stmt.GetColumnValueByIndex(0, RemainingCount);
				return ESQLitePreparedStatementExecuteRowResult::Stop;
			});
		}
	}

	if (RemainingCount <= 0)
	{
		// 파티 삭제
		FSQLitePreparedStatement DeleteGroup = Database->PrepareStatement(
			TEXT("DELETE FROM party_groups WHERE id = ?1;"));
		if (DeleteGroup.IsValid())
		{
			DeleteGroup.SetBindingValueByIndex(1, PartyId);
			DeleteGroup.Execute();
		}
		UE_LOG(LogHelluna, Log, TEXT("[SQLite]   마지막 멤버 탈퇴 → party_groups 삭제 | PartyId=%d"), PartyId);
	}
	else
	{
		// [Phase 12 Fix] 리더가 탈퇴하면 가장 오래된 멤버에게 리더 자동 이전
		// party_groups.leader_id가 탈퇴한 플레이어인지 확인
		FString CurrentLeaderId;
		{
			FSQLitePreparedStatement LeaderStmt = Database->PrepareStatement(
				TEXT("SELECT leader_id FROM party_groups WHERE id = ?1;"));
			if (LeaderStmt.IsValid())
			{
				LeaderStmt.SetBindingValueByIndex(1, PartyId);
				LeaderStmt.Execute([&CurrentLeaderId](const FSQLitePreparedStatement& S) -> ESQLitePreparedStatementExecuteRowResult
				{
					S.GetColumnValueByIndex(0, CurrentLeaderId);
					return ESQLitePreparedStatementExecuteRowResult::Stop;
				});
			}
		}

		if (CurrentLeaderId == PlayerId)
		{
			// 가장 먼저 가입한 멤버를 새 리더로 승격
			FString NewLeaderId;
			{
				FSQLitePreparedStatement NextStmt = Database->PrepareStatement(
					TEXT("SELECT player_id FROM party_members WHERE party_id = ?1 ORDER BY joined_at ASC LIMIT 1;"));
				if (NextStmt.IsValid())
				{
					NextStmt.SetBindingValueByIndex(1, PartyId);
					NextStmt.Execute([&NewLeaderId](const FSQLitePreparedStatement& S) -> ESQLitePreparedStatementExecuteRowResult
					{
						S.GetColumnValueByIndex(0, NewLeaderId);
						return ESQLitePreparedStatementExecuteRowResult::Stop;
					});
				}
			}

			if (!NewLeaderId.IsEmpty())
			{
				// party_members.role 업데이트 + party_groups.leader_id 업데이트
				{
					FSQLitePreparedStatement PromoteStmt = Database->PrepareStatement(
						TEXT("UPDATE party_members SET role = ?1 WHERE party_id = ?2 AND player_id = ?3;"));
					if (PromoteStmt.IsValid())
					{
						PromoteStmt.SetBindingValueByIndex(1, static_cast<int32>(EHellunaPartyRole::Leader));
						PromoteStmt.SetBindingValueByIndex(2, PartyId);
						PromoteStmt.SetBindingValueByIndex(3, NewLeaderId);
						PromoteStmt.Execute();
					}
				}
				{
					FSQLitePreparedStatement UpdateGroupStmt = Database->PrepareStatement(
						TEXT("UPDATE party_groups SET leader_id = ?1 WHERE id = ?2;"));
					if (UpdateGroupStmt.IsValid())
					{
						UpdateGroupStmt.SetBindingValueByIndex(1, NewLeaderId);
						UpdateGroupStmt.SetBindingValueByIndex(2, PartyId);
						UpdateGroupStmt.Execute();
					}
				}
				UE_LOG(LogHelluna, Log, TEXT("[SQLite]   리더 탈퇴 → 리더 자동 이전 | NewLeader=%s"), *NewLeaderId);
			}
		}
	}

	if (!Database->Execute(TEXT("COMMIT;")))
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ LeaveParty: COMMIT 실패 — ROLLBACK | 에러: %s"), *Database->GetLastError());
		Database->Execute(TEXT("ROLLBACK;"));
		return false;
	}

	UE_LOG(LogHelluna, Log, TEXT("[SQLite] ✓ LeaveParty 완료 | PlayerId=%s | PartyId=%d | 남은멤버=%lld"), *PlayerId, PartyId, RemainingCount);
	return true;
}

// ──────────────────────────────────────────────────────────────
// DisbandParty — 파티 강제 해산
// ──────────────────────────────────────────────────────────────
bool UHellunaSQLiteSubsystem::DisbandParty(int32 PartyId)
{
	UE_LOG(LogHelluna, Log, TEXT("[SQLite] ▶ DisbandParty | PartyId=%d"), PartyId);

	if (PartyId <= 0)
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ DisbandParty: 잘못된 PartyId=%d"), PartyId);
		return false;
	}

	if (!IsDatabaseReady())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ DisbandParty: DB가 준비되지 않음"));
		return false;
	}

	if (!Database->Execute(TEXT("BEGIN IMMEDIATE TRANSACTION;")))
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ DisbandParty: BEGIN 실패 | 에러: %s"), *Database->GetLastError());
		return false;
	}

	// (1) party_members 전부 삭제
	{
		FSQLitePreparedStatement DeleteMembers = Database->PrepareStatement(
			TEXT("DELETE FROM party_members WHERE party_id = ?1;"));
		if (!DeleteMembers.IsValid())
		{
			Database->Execute(TEXT("ROLLBACK;"));
			return false;
		}
		DeleteMembers.SetBindingValueByIndex(1, PartyId);
		if (!DeleteMembers.Execute())
		{
			Database->Execute(TEXT("ROLLBACK;"));
			return false;
		}
	}

	// (2) party_groups 삭제
	{
		FSQLitePreparedStatement DeleteGroup = Database->PrepareStatement(
			TEXT("DELETE FROM party_groups WHERE id = ?1;"));
		if (!DeleteGroup.IsValid())
		{
			Database->Execute(TEXT("ROLLBACK;"));
			return false;
		}
		DeleteGroup.SetBindingValueByIndex(1, PartyId);
		if (!DeleteGroup.Execute())
		{
			Database->Execute(TEXT("ROLLBACK;"));
			return false;
		}
	}

	if (!Database->Execute(TEXT("COMMIT;")))
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ DisbandParty: COMMIT 실패 — ROLLBACK | 에러: %s"), *Database->GetLastError());
		Database->Execute(TEXT("ROLLBACK;"));
		return false;
	}

	UE_LOG(LogHelluna, Log, TEXT("[SQLite] ✓ DisbandParty 완료 | PartyId=%d"), PartyId);
	return true;
}

// ──────────────────────────────────────────────────────────────
// FindPartyByCode — 파티 코드로 ID 조회
// ──────────────────────────────────────────────────────────────
int32 UHellunaSQLiteSubsystem::FindPartyByCode(const FString& PartyCode)
{
	UE_LOG(LogHelluna, Verbose, TEXT("[SQLite] FindPartyByCode | Code=%s"), *PartyCode);

	if (PartyCode.IsEmpty() || !IsDatabaseReady())
	{
		return 0;
	}

	FSQLitePreparedStatement Stmt = Database->PrepareStatement(
		TEXT("SELECT id FROM party_groups WHERE party_code = ?1;"));
	if (!Stmt.IsValid())
	{
		return 0;
	}
	Stmt.SetBindingValueByIndex(1, PartyCode);

	int32 PartyId = 0;
	Stmt.Execute([&PartyId](const FSQLitePreparedStatement& S) -> ESQLitePreparedStatementExecuteRowResult
	{
		int64 Id = 0;
		S.GetColumnValueByIndex(0, Id);
		PartyId = static_cast<int32>(Id);
		return ESQLitePreparedStatementExecuteRowResult::Stop;
	});

	UE_LOG(LogHelluna, Verbose, TEXT("[SQLite] FindPartyByCode: Code=%s → PartyId=%d"), *PartyCode, PartyId);
	return PartyId;
}

// ──────────────────────────────────────────────────────────────
// GetPartyMemberCount — 파티 멤버 수
// ──────────────────────────────────────────────────────────────
int32 UHellunaSQLiteSubsystem::GetPartyMemberCount(int32 PartyId)
{
	if (PartyId <= 0 || !IsDatabaseReady())
	{
		return 0;
	}

	FSQLitePreparedStatement Stmt = Database->PrepareStatement(
		TEXT("SELECT COUNT(*) FROM party_members WHERE party_id = ?1;"));
	if (!Stmt.IsValid())
	{
		return 0;
	}
	Stmt.SetBindingValueByIndex(1, PartyId);

	int64 Count = 0;
	Stmt.Execute([&Count](const FSQLitePreparedStatement& S) -> ESQLitePreparedStatementExecuteRowResult
	{
		S.GetColumnValueByIndex(0, Count);
		return ESQLitePreparedStatementExecuteRowResult::Stop;
	});

	return static_cast<int32>(Count);
}

// ──────────────────────────────────────────────────────────────
// GetPlayerPartyId — 플레이어의 파티 ID
// ──────────────────────────────────────────────────────────────
int32 UHellunaSQLiteSubsystem::GetPlayerPartyId(const FString& PlayerId)
{
	if (PlayerId.IsEmpty() || !IsDatabaseReady())
	{
		return 0;
	}

	FSQLitePreparedStatement Stmt = Database->PrepareStatement(
		TEXT("SELECT party_id FROM party_members WHERE player_id = ?1;"));
	if (!Stmt.IsValid())
	{
		return 0;
	}
	Stmt.SetBindingValueByIndex(1, PlayerId);

	int32 PartyId = 0;
	Stmt.Execute([&PartyId](const FSQLitePreparedStatement& S) -> ESQLitePreparedStatementExecuteRowResult
	{
		int64 Id = 0;
		S.GetColumnValueByIndex(0, Id);
		PartyId = static_cast<int32>(Id);
		return ESQLitePreparedStatementExecuteRowResult::Stop;
	});

	return PartyId;
}

// ──────────────────────────────────────────────────────────────
// LoadPartyInfo — 파티 전체 정보 로드 (그룹 + 멤버 JOIN)
// ──────────────────────────────────────────────────────────────
FHellunaPartyInfo UHellunaSQLiteSubsystem::LoadPartyInfo(int32 PartyId)
{
	FHellunaPartyInfo Info;

	if (PartyId <= 0 || !IsDatabaseReady())
	{
		return Info;
	}

	// (1) party_groups에서 코드, 리더 조회
	{
		FSQLitePreparedStatement GroupStmt = Database->PrepareStatement(
			TEXT("SELECT party_code, leader_id FROM party_groups WHERE id = ?1;"));
		if (!GroupStmt.IsValid())
		{
			return Info;
		}
		GroupStmt.SetBindingValueByIndex(1, PartyId);
		GroupStmt.Execute([&Info, PartyId](const FSQLitePreparedStatement& S) -> ESQLitePreparedStatementExecuteRowResult
		{
			S.GetColumnValueByName(TEXT("party_code"), Info.PartyCode);
			S.GetColumnValueByName(TEXT("leader_id"), Info.LeaderId);
			Info.PartyId = PartyId;
			return ESQLitePreparedStatementExecuteRowResult::Stop;
		});
	}

	if (!Info.IsValid())
	{
		UE_LOG(LogHelluna, Warning, TEXT("[SQLite] LoadPartyInfo: PartyId=%d 존재하지 않음"), PartyId);
		return Info;
	}

	// (2) party_members 조회 (joined_at 순서)
	{
		FSQLitePreparedStatement MemberStmt = Database->PrepareStatement(
			TEXT("SELECT player_id, display_name, role, is_ready, hero_type FROM party_members WHERE party_id = ?1 ORDER BY joined_at ASC;"));
		if (!MemberStmt.IsValid())
		{
			return Info;
		}
		MemberStmt.SetBindingValueByIndex(1, PartyId);
		MemberStmt.Execute([&Info](const FSQLitePreparedStatement& S) -> ESQLitePreparedStatementExecuteRowResult
		{
			FHellunaPartyMemberInfo Member;
			S.GetColumnValueByName(TEXT("player_id"), Member.PlayerId);
			S.GetColumnValueByName(TEXT("display_name"), Member.DisplayName);

			int32 RoleInt = 1;
			S.GetColumnValueByName(TEXT("role"), RoleInt);
			Member.Role = static_cast<EHellunaPartyRole>(FMath::Clamp(RoleInt, 0, 1));

			int32 ReadyInt = 0;
			S.GetColumnValueByName(TEXT("is_ready"), ReadyInt);
			Member.bIsReady = (ReadyInt != 0);

			S.GetColumnValueByName(TEXT("hero_type"), Member.SelectedHeroType);

			Info.Members.Add(MoveTemp(Member));
			return ESQLitePreparedStatementExecuteRowResult::Continue;
		});
	}

	UE_LOG(LogHelluna, Verbose, TEXT("[SQLite] LoadPartyInfo: PartyId=%d | Code=%s | Members=%d"),
		Info.PartyId, *Info.PartyCode, Info.Members.Num());
	return Info;
}

// ──────────────────────────────────────────────────────────────
// UpdateMemberReady — Ready 상태 업데이트
// ──────────────────────────────────────────────────────────────
bool UHellunaSQLiteSubsystem::UpdateMemberReady(const FString& PlayerId, bool bReady)
{
	if (PlayerId.IsEmpty() || !IsDatabaseReady())
	{
		return false;
	}

	FSQLitePreparedStatement Stmt = Database->PrepareStatement(
		TEXT("UPDATE party_members SET is_ready = ?1 WHERE player_id = ?2;"));
	if (!Stmt.IsValid())
	{
		return false;
	}
	Stmt.SetBindingValueByIndex(1, bReady ? 1 : 0);
	Stmt.SetBindingValueByIndex(2, PlayerId);

	if (!Stmt.Execute())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ UpdateMemberReady 실패 | PlayerId=%s | 에러: %s"), *PlayerId, *Database->GetLastError());
		return false;
	}

	UE_LOG(LogHelluna, Verbose, TEXT("[SQLite] UpdateMemberReady: PlayerId=%s → %s"), *PlayerId, bReady ? TEXT("Ready") : TEXT("NotReady"));
	return true;
}

// ──────────────────────────────────────────────────────────────
// UpdateMemberHeroType — 영웅 타입 업데이트
// ──────────────────────────────────────────────────────────────
bool UHellunaSQLiteSubsystem::UpdateMemberHeroType(const FString& PlayerId, int32 HeroType)
{
	if (PlayerId.IsEmpty() || !IsDatabaseReady())
	{
		return false;
	}

	FSQLitePreparedStatement Stmt = Database->PrepareStatement(
		TEXT("UPDATE party_members SET hero_type = ?1 WHERE player_id = ?2;"));
	if (!Stmt.IsValid())
	{
		return false;
	}
	Stmt.SetBindingValueByIndex(1, HeroType);
	Stmt.SetBindingValueByIndex(2, PlayerId);

	if (!Stmt.Execute())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ UpdateMemberHeroType 실패 | PlayerId=%s | 에러: %s"), *PlayerId, *Database->GetLastError());
		return false;
	}

	UE_LOG(LogHelluna, Verbose, TEXT("[SQLite] UpdateMemberHeroType: PlayerId=%s → HeroType=%d"), *PlayerId, HeroType);
	return true;
}

// ──────────────────────────────────────────────────────────────
// TransferLeadership — 리더십 이전
// ──────────────────────────────────────────────────────────────
bool UHellunaSQLiteSubsystem::TransferLeadership(int32 PartyId, const FString& NewLeaderId)
{
	UE_LOG(LogHelluna, Log, TEXT("[SQLite] ▶ TransferLeadership | PartyId=%d | NewLeader=%s"), PartyId, *NewLeaderId);

	if (PartyId <= 0 || NewLeaderId.IsEmpty() || !IsDatabaseReady())
	{
		return false;
	}

	if (!Database->Execute(TEXT("BEGIN IMMEDIATE TRANSACTION;")))
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ TransferLeadership: BEGIN 실패 | 에러: %s"), *Database->GetLastError());
		return false;
	}

	// (1) 기존 리더를 Member로
	{
		FSQLitePreparedStatement DemoteStmt = Database->PrepareStatement(
			TEXT("UPDATE party_members SET role = ?1 WHERE party_id = ?2 AND role = ?3;"));
		if (!DemoteStmt.IsValid())
		{
			Database->Execute(TEXT("ROLLBACK;"));
			return false;
		}
		DemoteStmt.SetBindingValueByIndex(1, static_cast<int32>(EHellunaPartyRole::Member));
		DemoteStmt.SetBindingValueByIndex(2, PartyId);
		DemoteStmt.SetBindingValueByIndex(3, static_cast<int32>(EHellunaPartyRole::Leader));
		if (!DemoteStmt.Execute())
		{
			Database->Execute(TEXT("ROLLBACK;"));
			return false;
		}
	}

	// (2) NewLeaderId를 Leader로
	{
		FSQLitePreparedStatement PromoteStmt = Database->PrepareStatement(
			TEXT("UPDATE party_members SET role = ?1 WHERE player_id = ?2 AND party_id = ?3;"));
		if (!PromoteStmt.IsValid())
		{
			Database->Execute(TEXT("ROLLBACK;"));
			return false;
		}
		PromoteStmt.SetBindingValueByIndex(1, static_cast<int32>(EHellunaPartyRole::Leader));
		PromoteStmt.SetBindingValueByIndex(2, NewLeaderId);
		PromoteStmt.SetBindingValueByIndex(3, PartyId);
		if (!PromoteStmt.Execute())
		{
			Database->Execute(TEXT("ROLLBACK;"));
			return false;
		}
	}

	// (3) party_groups.leader_id 갱신
	{
		FSQLitePreparedStatement UpdateGroup = Database->PrepareStatement(
			TEXT("UPDATE party_groups SET leader_id = ?1 WHERE id = ?2;"));
		if (!UpdateGroup.IsValid())
		{
			Database->Execute(TEXT("ROLLBACK;"));
			return false;
		}
		UpdateGroup.SetBindingValueByIndex(1, NewLeaderId);
		UpdateGroup.SetBindingValueByIndex(2, PartyId);
		if (!UpdateGroup.Execute())
		{
			Database->Execute(TEXT("ROLLBACK;"));
			return false;
		}
	}

	if (!Database->Execute(TEXT("COMMIT;")))
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ TransferLeadership: COMMIT 실패 — ROLLBACK | 에러: %s"), *Database->GetLastError());
		Database->Execute(TEXT("ROLLBACK;"));
		return false;
	}

	UE_LOG(LogHelluna, Log, TEXT("[SQLite] ✓ TransferLeadership 완료 | PartyId=%d | NewLeader=%s"), PartyId, *NewLeaderId);
	return true;
}

// ──────────────────────────────────────────────────────────────
// ResetAllReadyStates — 파티 전원 Ready 리셋
// ──────────────────────────────────────────────────────────────
bool UHellunaSQLiteSubsystem::ResetAllReadyStates(int32 PartyId)
{
	if (PartyId <= 0 || !IsDatabaseReady())
	{
		return false;
	}

	FSQLitePreparedStatement Stmt = Database->PrepareStatement(
		TEXT("UPDATE party_members SET is_ready = 0 WHERE party_id = ?1;"));
	if (!Stmt.IsValid())
	{
		return false;
	}
	Stmt.SetBindingValueByIndex(1, PartyId);

	if (!Stmt.Execute())
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ ResetAllReadyStates 실패 | PartyId=%d | 에러: %s"), PartyId, *Database->GetLastError());
		return false;
	}

	UE_LOG(LogHelluna, Verbose, TEXT("[SQLite] ResetAllReadyStates: PartyId=%d"), PartyId);
	return true;
}

// ──────────────────────────────────────────────────────────────
// IsPartyCodeUnique — 파티 코드 유니크 확인
// ──────────────────────────────────────────────────────────────
bool UHellunaSQLiteSubsystem::IsPartyCodeUnique(const FString& Code)
{
	if (Code.IsEmpty() || !IsDatabaseReady())
	{
		return false;
	}

	FSQLitePreparedStatement Stmt = Database->PrepareStatement(
		TEXT("SELECT COUNT(*) FROM party_groups WHERE party_code = ?1;"));
	if (!Stmt.IsValid())
	{
		return false;
	}
	Stmt.SetBindingValueByIndex(1, Code);

	int64 Count = 0;
	Stmt.Execute([&Count](const FSQLitePreparedStatement& S) -> ESQLitePreparedStatementExecuteRowResult
	{
		S.GetColumnValueByIndex(0, Count);
		return ESQLitePreparedStatementExecuteRowResult::Stop;
	});

	return Count == 0;
}

// ──────────────────────────────────────────────────────────────
// CleanupStaleParties — 오래된 파티 정리
// ──────────────────────────────────────────────────────────────
int32 UHellunaSQLiteSubsystem::CleanupStaleParties(int32 HoursOld)
{
	UE_LOG(LogHelluna, Log, TEXT("[SQLite] ▶ CleanupStaleParties | HoursOld=%d"), HoursOld);

	if (!IsDatabaseReady())
	{
		return 0;
	}

	// [Phase 12 Fix] 유효하지 않은 HoursOld 방어
	if (HoursOld <= 0)
	{
		UE_LOG(LogHelluna, Warning, TEXT("[SQLite] CleanupStaleParties: HoursOld가 0 이하 (%d) → 무시"), HoursOld);
		return 0;
	}

	if (!Database->Execute(TEXT("BEGIN IMMEDIATE TRANSACTION;")))
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ CleanupStaleParties: BEGIN 실패 | 에러: %s"), *Database->GetLastError());
		return 0;
	}

	// 오래된 파티 ID 수집
	TArray<int32> StalePartyIds;
	{
		FSQLitePreparedStatement Stmt = Database->PrepareStatement(
			TEXT("SELECT id FROM party_groups WHERE created_at < datetime('now', ?1);"));
		if (!Stmt.IsValid())
		{
			Database->Execute(TEXT("ROLLBACK;"));
			return 0;
		}
		FString TimeOffset = FString::Printf(TEXT("-%d hours"), HoursOld);
		Stmt.SetBindingValueByIndex(1, TimeOffset);
		Stmt.Execute([&StalePartyIds](const FSQLitePreparedStatement& S) -> ESQLitePreparedStatementExecuteRowResult
		{
			int64 Id = 0;
			S.GetColumnValueByIndex(0, Id);
			StalePartyIds.Add(static_cast<int32>(Id));
			return ESQLitePreparedStatementExecuteRowResult::Continue;
		});
	}

	// 각 파티의 멤버 + 그룹 삭제
	for (int32 PId : StalePartyIds)
	{
		{
			FSQLitePreparedStatement DelMembers = Database->PrepareStatement(
				TEXT("DELETE FROM party_members WHERE party_id = ?1;"));
			if (DelMembers.IsValid())
			{
				DelMembers.SetBindingValueByIndex(1, PId);
				DelMembers.Execute();
			}
		}
		{
			FSQLitePreparedStatement DelGroup = Database->PrepareStatement(
				TEXT("DELETE FROM party_groups WHERE id = ?1;"));
			if (DelGroup.IsValid())
			{
				DelGroup.SetBindingValueByIndex(1, PId);
				DelGroup.Execute();
			}
		}
	}

	if (!Database->Execute(TEXT("COMMIT;")))
	{
		UE_LOG(LogHelluna, Error, TEXT("[SQLite] ✗ CleanupStaleParties: COMMIT 실패 — ROLLBACK | 에러: %s"), *Database->GetLastError());
		Database->Execute(TEXT("ROLLBACK;"));
		return 0;
	}

	UE_LOG(LogHelluna, Log, TEXT("[SQLite] ✓ CleanupStaleParties 완료 | 삭제된 파티: %d개"), StalePartyIds.Num());
	return StalePartyIds.Num();
}
