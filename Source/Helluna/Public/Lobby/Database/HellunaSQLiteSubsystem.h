// File: Source/Helluna/Public/Lobby/Database/HellunaSQLiteSubsystem.h
// ════════════════════════════════════════════════════════════════════════════════
//
// UHellunaSQLiteSubsystem — SQLite 인벤토리 저장 서브시스템
//
// ════════════════════════════════════════════════════════════════════════════════
//
// [개요]
//   IInventoryDatabase 인터페이스의 SQLite 구현체.
//   UGameInstanceSubsystem이므로 GameInstance가 살아있는 동안 유지됨.
//   → 맵 전환(ClientTravel) 시에도 DB 연결이 살아있다!
//
// [DB 파일 위치]
//   {ProjectSavedDir}/Database/Helluna.db
//   예: D:/UnrealProject/Capston_Project/Helluna/Saved/Database/Helluna.db
//
// [테이블 구조]
//   player_stash   — 로비 창고 (로비에서 보이는 아이템)
//   player_loadout — 출격 비행기표 (게임서버로 가져갈 아이템)
//   schema_version — DB 스키마 버전 관리
//
// [핵심 흐름]
//   로비: Stash UI → 드래그 → Loadout 분리 → SavePlayerLoadout(원자적: Loadout INSERT + Stash DELETE)
//   게임: PostLogin → LoadPlayerLoadout → InvComp 복원 → DeletePlayerLoadout
//   복귀: 게임결과 → MergeGameResultToStash → ClientTravel → 로비
//   크래시: HasPendingLoadout → RecoverFromCrash (Loadout → Stash 복구)
//
// [사용 패턴]
//   UGameInstance* GI = GetGameInstance();
//   UHellunaSQLiteSubsystem* DB = GI ? GI->GetSubsystem<UHellunaSQLiteSubsystem>() : nullptr;
//   if (DB && DB->IsDatabaseReady()) { DB->SavePlayerStash(...); }
//
// [동시성]
//   로비서버 + 게임서버가 같은 PC에서 같은 DB 파일 공유
//   PRAGMA journal_mode=WAL + busy_timeout=3000 으로 동시 접근 처리
//
// [주의사항]
//   - FSQLiteDatabase는 UObject가 아니므로 UPROPERTY 불가 → 수동 delete 필수
//   - 모든 쓰기 함수는 트랜잭션(BEGIN/COMMIT/ROLLBACK)으로 원자성 보장
//   - 모든 함수 진입 시 IsDatabaseReady() 체크 필수
//
// TODO: [SQL전환] REST API/PostgreSQL 전환 시 이 클래스를 새 구현으로 교체
//
// 작성자: Gihyeon (Claude Code 보조)
// ════════════════════════════════════════════════════════════════════════════════

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Lobby/Database/IInventoryDatabase.h"
#include "HellunaSQLiteSubsystem.generated.h"

// 전방선언 — FSQLiteDatabase, FSQLitePreparedStatement는 UObject가 아닌 POD 클래스
// #include "SQLiteDatabase.h"와 "SQLitePreparedStatement.h"는 cpp에서만 수행 (헤더 오염 방지)
class FSQLiteDatabase;
class FSQLitePreparedStatement;

UCLASS()
class HELLUNA_API UHellunaSQLiteSubsystem : public UGameInstanceSubsystem, public IInventoryDatabase
{
	GENERATED_BODY()

public:
	// ════════════════════════════════════════════════════════════════
	// USubsystem 오버라이드
	// ════════════════════════════════════════════════════════════════

	/**
	 * 서브시스템 생성 조건
	 * → true 반환: 데디서버/클라이언트 구분 없이 항상 생성
	 *   (로비서버, 게임서버 양쪽 모두 SQLite 접근이 필요)
	 */
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	/**
	 * 서브시스템 초기화 — GameInstance 생성 직후 호출됨
	 *
	 * 내부 처리:
	 *   1. DB 파일 경로 설정 ({ProjectSavedDir}/Database/Helluna.db)
	 *   2. DB 디렉토리 생성 (없으면)
	 *   3. OpenDatabase() → DB 열기 + 스키마 초기화
	 */
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	/**
	 * 서브시스템 종료 — GameInstance 소멸 직전 호출됨
	 * → DB 연결 닫기 + 메모리 해제
	 */
	virtual void Deinitialize() override;

	// ════════════════════════════════════════════════════════════════
	// DB 상태 확인
	// ════════════════════════════════════════════════════════════════

	/**
	 * DB가 열려있고 유효한지 확인
	 * → 모든 CRUD 함수 호출 전에 반드시 이것부터 체크!
	 *
	 * @return bDatabaseOpen && Database != nullptr && Database->IsValid()
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Helluna|Database")
	bool IsDatabaseReady() const;

	/**
	 * DB 파일의 절대 경로를 반환
	 * 예: "D:/UnrealProject/.../Saved/Database/Helluna.db"
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Helluna|Database")
	FString GetDatabasePath() const;

	// ════════════════════════════════════════════════════════════════
	// IInventoryDatabase 인터페이스 구현 — Stash (로비 창고)
	// ════════════════════════════════════════════════════════════════

	/**
	 * 플레이어의 Stash(창고) 아이템 전체 로드
	 *
	 * SQL: SELECT * FROM player_stash WHERE player_id = ?
	 *
	 * @param PlayerId  플레이어 고유 ID (로그인 ID)
	 * @return 파싱된 아이템 배열 (없으면 빈 배열)
	 */
	virtual TArray<FInv_SavedItemData> LoadPlayerStash(const FString& PlayerId) override;

	/**
	 * 플레이어의 Stash(창고) 전체 저장 (전부 교체)
	 *
	 * 내부 처리 (트랜잭션):
	 *   1. DELETE FROM player_stash WHERE player_id = ?  (기존 전부 삭제)
	 *   2. INSERT INTO player_stash ... (Items 각각 INSERT)
	 *   → Items가 빈 배열이면 DELETE만 수행 = 창고 비우기
	 *
	 * @param PlayerId  플레이어 고유 ID
	 * @param Items     저장할 아이템 배열
	 * @return 성공 여부
	 */
	virtual bool SavePlayerStash(const FString& PlayerId, const TArray<FInv_SavedItemData>& Items) override;

	/**
	 * 해당 플레이어의 Stash 데이터가 존재하는지 확인
	 *
	 * SQL: SELECT COUNT(*) FROM player_stash WHERE player_id = ?
	 */
	virtual bool IsPlayerExists(const FString& PlayerId) override;

	// ════════════════════════════════════════════════════════════════
	// IInventoryDatabase 인터페이스 구현 — Loadout (출격 비행기표)
	// ════════════════════════════════════════════════════════════════

	/**
	 * 플레이어의 Loadout(출격장비) 로드
	 *
	 * SQL: SELECT * FROM player_loadout WHERE player_id = ?
	 * 주의: player_loadout에는 is_equipped 컬럼이 없음 (출격장비는 장착 개념 없음)
	 *
	 * @param PlayerId  플레이어 고유 ID
	 * @return 출격 아이템 배열
	 */
	virtual TArray<FInv_SavedItemData> LoadPlayerLoadout(const FString& PlayerId) override;

	/**
	 * 출격 — Loadout INSERT + Stash DELETE (원자적 트랜잭션)
	 *
	 * 비행기표 패턴:
	 *   1. player_loadout에 Items INSERT (출격할 아이템)
	 *   2. player_stash에서 해당 플레이어 전체 DELETE (창고 비움)
	 *   → 하나의 트랜잭션으로 원자적 처리 (중간에 실패하면 전부 ROLLBACK)
	 *
	 * @param PlayerId  플레이어 고유 ID
	 * @param Items     출격할 아이템 배열
	 * @return 성공 여부
	 */
	virtual bool SavePlayerLoadout(const FString& PlayerId, const TArray<FInv_SavedItemData>& Items) override;

	/**
	 * Loadout 삭제 — 게임서버에서 Loadout을 InvComp에 복원한 후 호출
	 *
	 * SQL: DELETE FROM player_loadout WHERE player_id = ?
	 */
	virtual bool DeletePlayerLoadout(const FString& PlayerId) override;

	// ════════════════════════════════════════════════════════════════
	// IInventoryDatabase 인터페이스 구현 — 게임 결과 반영
	// ════════════════════════════════════════════════════════════════

	/**
	 * 게임 결과를 Stash에 병합 (기존 Stash 유지 + 결과 아이템 추가)
	 *
	 * 내부 처리 (트랜잭션):
	 *   - 기존 player_stash DELETE 없이 ResultItems만 INSERT
	 *   → 기존 창고 + 게임에서 얻은 아이템 합산
	 *
	 * @param PlayerId     플레이어 고유 ID
	 * @param ResultItems  게임에서 얻은 아이템 배열
	 * @return 성공 여부
	 */
	virtual bool MergeGameResultToStash(const FString& PlayerId, const TArray<FInv_SavedItemData>& ResultItems) override;

	// ════════════════════════════════════════════════════════════════
	// IInventoryDatabase 인터페이스 구현 — 크래시 복구
	// ════════════════════════════════════════════════════════════════

	/**
	 * 미처리 Loadout이 남아있는지 확인 (비정상 종료 감지)
	 *
	 * 원리: 정상 종료 시 Loadout은 반드시 삭제됨.
	 *       Loadout이 남아있다 = 게임 도중 크래시 또는 비정상 종료
	 *
	 * SQL: SELECT COUNT(*) FROM player_loadout WHERE player_id = ?
	 *
	 * @param PlayerId  플레이어 고유 ID
	 * @return true = Loadout 잔존 (크래시 의심), false = 정상
	 */
	virtual bool HasPendingLoadout(const FString& PlayerId) override;

	/**
	 * 크래시 복구 — Loadout에 남은 아이템을 Stash로 복귀
	 *
	 * 내부 처리 (트랜잭션):
	 *   1. player_loadout에서 SELECT (잔존 아이템 읽기)
	 *   2. player_stash에 INSERT (Stash로 복귀)
	 *   3. player_loadout에서 DELETE (정리)
	 *   → 하나의 트랜잭션으로 원자적 처리
	 *
	 * 호출 시점: 로비 PostLogin에서 CheckAndRecoverFromCrash()가 호출
	 *
	 * @param PlayerId  플레이어 고유 ID
	 * @return 성공 여부
	 */
	virtual bool RecoverFromCrash(const FString& PlayerId) override;

	// ════════════════════════════════════════════════════════════════
	// IInventoryDatabase 인터페이스 구현 — 게임 캐릭터 중복 방지
	// ════════════════════════════════════════════════════════════════

	/** 현재 모든 서버에서 사용 중인 캐릭터 조회 (3개 bool: Lui/Luna/Liam) */
	virtual TArray<bool> GetActiveGameCharacters() override;

	/** 캐릭터 사용 등록 */
	virtual bool RegisterActiveGameCharacter(int32 HeroType, const FString& PlayerId, const FString& ServerId) override;

	/** 플레이어의 캐릭터 등록 해제 */
	virtual bool UnregisterActiveGameCharacter(const FString& PlayerId) override;

	/** 특정 서버의 모든 캐릭터 등록 해제 */
	virtual bool UnregisterAllActiveGameCharactersForServer(const FString& ServerId) override;

private:
	// ════════════════════════════════════════════════════════════════
	// DB 관리 (private)
	// ════════════════════════════════════════════════════════════════

	/**
	 * DB 열기 — Initialize()에서 호출
	 *
	 * 내부 처리:
	 *   1. FSQLiteDatabase 인스턴스 생성 (new)
	 *   2. Database->Open() — ReadWriteCreate 모드
	 *   3. InitializeSchema() — PRAGMA + 테이블 생성
	 *
	 * @return 성공 여부 (실패 시 Database = nullptr)
	 */
	bool OpenDatabase();

	/**
	 * DB 닫기 — Deinitialize()에서 호출
	 * → Database->Close() + delete Database + nullptr 초기화
	 */
	void CloseDatabase();

	/**
	 * 테이블 스키마 생성 + PRAGMA 설정 — OpenDatabase() 성공 직후 호출
	 *
	 * PRAGMA:
	 *   - journal_mode=WAL — 동시 접근 성능 향상 (로비+게임서버 동시 접근)
	 *   - busy_timeout=3000 — 잠금 시 3초 재시도 (기본값 0=즉시 실패)
	 *   - foreign_keys=OFF — FK 관계 없음
	 *
	 * 테이블 3개:
	 *   - player_stash (인덱스: idx_stash_player_id)
	 *   - player_loadout (인덱스: idx_loadout_player_id)
	 *   - schema_version
	 */
	bool InitializeSchema();

	// ════════════════════════════════════════════════════════════════
	// FInv_SavedItemData ↔ DB 변환 헬퍼 (private static)
	// ════════════════════════════════════════════════════════════════

	/**
	 * SELECT 결과 1행 → FInv_SavedItemData 파싱
	 *
	 * 컬럼 매핑:
	 *   item_type           → FGameplayTag (문자열 → 태그 변환)
	 *   stack_count         → int32
	 *   grid_position_x/y   → FIntPoint
	 *   grid_category       → uint8 (0=장비, 1=소모품, 2=재료)
	 *   is_equipped         → bool (player_loadout에는 없음 → 기본값 false)
	 *   weapon_slot         → int32 (-1 = 미장착)
	 *   serialized_manifest → TArray<uint8> (BLOB)
	 *   attachments_json    → TArray<FInv_SavedAttachmentData> (JSON 파싱)
	 */
	static FInv_SavedItemData ParseRowToSavedItem(const FSQLitePreparedStatement& Statement);

	/**
	 * FInv_SavedAttachmentData 배열 → JSON 문자열
	 *
	 * JSON 형식: [{"t":"태그","s":슬롯인덱스,"at":"부착타입","m":"Base64 매니페스트"}, ...]
	 *   t  = AttachmentItemType (FGameplayTag 문자열)
	 *   s  = SlotIndex (int)
	 *   at = AttachmentType (FGameplayTag 문자열)
	 *   m  = SerializedManifest (Base64 인코딩, 있을 때만)
	 */
	static FString SerializeAttachmentsToJson(const TArray<FInv_SavedAttachmentData>& Attachments);

	/** JSON 문자열 → FInv_SavedAttachmentData 배열 (위의 역변환) */
	static TArray<FInv_SavedAttachmentData> DeserializeAttachmentsFromJson(const FString& JsonString);

	// ════════════════════════════════════════════════════════════════
	// 멤버 변수
	// ════════════════════════════════════════════════════════════════

	/**
	 * SQLite DB 인스턴스 (UObject 아님 → UPROPERTY 불가)
	 *
	 * 수명 관리:
	 *   - OpenDatabase()에서 new FSQLiteDatabase()
	 *   - CloseDatabase()에서 delete + nullptr
	 *   - Deinitialize()에서 CloseDatabase() 호출
	 *
	 * 주의: GC가 관리하지 않으므로 반드시 수동 해제!
	 */
	FSQLiteDatabase* Database = nullptr;

	/** DB 파일 절대 경로 캐시 (Initialize에서 한 번 설정) */
	FString CachedDatabasePath;

	/**
	 * DB 열림 상태 플래그
	 * OpenDatabase() 성공 시 true, CloseDatabase() 시 false
	 * IsDatabaseReady()에서 사용
	 */
	bool bDatabaseOpen = false;
};
