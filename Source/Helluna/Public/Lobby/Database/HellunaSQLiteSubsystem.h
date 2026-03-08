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
#include "Lobby/Party/HellunaPartyTypes.h"
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

	/** 파일 전송 전용 모드 여부 (게임서버에서 true → DB 사용 불가, JSON 전송만 가능) */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Helluna|Database")
	bool IsFileTransferOnly() const { return bFileTransferOnly; }

	/**
	 * DB 파일의 절대 경로를 반환
	 * 예: "D:/UnrealProject/.../Saved/Database/Helluna.db"
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Helluna|Database")
	FString GetDatabasePath() const;

	/**
	 * DB가 닫혀있으면 다시 열기 시도
	 *
	 * 사용 시점:
	 *   Initialize()에서 DB 열기가 실패한 경우 (다른 프로세스가 잠금)
	 *   → 잠금이 풀린 후 이 함수를 호출하면 DB 연결 복구
	 *
	 * @return true = DB 열림 (이미 열려있거나 재오픈 성공), false = 여전히 실패
	 */
	bool TryReopenDatabase();

	/**
	 * DB 연결을 명시적으로 닫기 (파일 잠금 해제)
	 *
	 * 사용 시점:
	 *   로비에서 마지막 플레이어가 나간 후 호출
	 *   → 게임서버(데디서버)가 같은 DB 파일을 열 수 있도록 잠금 해제
	 *
	 * 📌 이후 다시 DB가 필요하면 TryReopenDatabase() 호출
	 */
	void ReleaseDatabaseConnection();

	// ════════════════════════════════════════════════════════════════
	// 파일 기반 Loadout 전송 (DB 잠금 회피)
	// ════════════════════════════════════════════════════════════════
	//
	// 📌 왜 필요한가?
	//   PIE(로비)와 데디서버(게임)가 동시에 같은 SQLite DB를 열 수 없음
	//   (Windows에서 FSQLiteDatabase가 exclusive lock을 걸기 때문)
	//   → 로비에서 출격 시 Loadout을 JSON 파일로 내보내고,
	//     게임서버에서 해당 파일을 읽어 인벤토리를 복원
	//
	// 📌 파일 경로: {DB 디렉토리}/Transfer/Loadout_{PlayerId}.json
	//   DB 디렉토리 기준이므로 -DatabasePath= 커맨드라인에 관계없이
	//   두 프로세스가 같은 경로를 참조

	/**
	 * Loadout 아이템을 JSON 파일로 내보내기
	 *
	 * 호출 시점: 로비 Server_Deploy에서 SavePlayerLoadout 성공 후
	 * 파일 경로: {DB 디렉토리}/Transfer/Loadout_{PlayerId}.json
	 *
	 * @param PlayerId  플레이어 고유 ID
	 * @param Items     출격할 아이템 배열
	 * @param HeroType  선택한 히어로 타입 인덱스
	 * @return 성공 여부
	 */
	bool ExportLoadoutToFile(const FString& PlayerId, const TArray<FInv_SavedItemData>& Items, int32 HeroType = 0);

	/**
	 * JSON 파일에서 Loadout 아이템 읽기 + 파일 삭제 (비행기표 소멸)
	 *
	 * 호출 시점: 게임서버 LoadAndSendInventoryToClient에서 DB 체크 전
	 * → 파일이 있으면 로드 후 삭제, 없으면 빈 배열 반환
	 *
	 * @param PlayerId       플레이어 고유 ID
	 * @param OutHeroType    파일에 저장된 히어로 타입 인덱스 (out)
	 * @return 로드된 아이템 배열 (파일 없으면 빈 배열)
	 */
	TArray<FInv_SavedItemData> ImportLoadoutFromFile(const FString& PlayerId, int32& OutHeroType);

	/**
	 * Loadout 전송 파일이 존재하는지 확인
	 *
	 * @param PlayerId  플레이어 고유 ID
	 * @return true = 파일 존재 (출격 대기 중)
	 */
	bool HasPendingLoadoutFile(const FString& PlayerId) const;

	// ════════════════════════════════════════════════════════════════
	// 파일 기반 Game Result 전송 (DB 잠금 회피)
	// ════════════════════════════════════════════════════════════════
	//
	// 📌 왜 필요한가?
	//   게임서버에서 DB를 열지 않고도 게임 결과를 로비로 전달
	//   → 게임 종료 시 결과를 JSON 파일로 내보내고,
	//     로비 PostLogin에서 파일을 읽어 Stash에 병합
	//
	// 📌 파일 경로: {DB 디렉토리}/Transfer/GameResult_{PlayerId}.json

	/**
	 * 게임 결과 아이템을 JSON 파일로 내보내기
	 *
	 * 호출 시점: 게임서버 ProcessPlayerGameResult에서
	 * 파일 경로: {DB 디렉토리}/Transfer/GameResult_{PlayerId}.json
	 *
	 * @param PlayerId   플레이어 고유 ID
	 * @param Items      결과 아이템 배열 (사망자는 빈 배열)
	 * @param bSurvived  생존 여부
	 * @return 성공 여부
	 */
	bool ExportGameResultToFile(const FString& PlayerId, const TArray<FInv_SavedItemData>& Items, bool bSurvived);

	/**
	 * JSON 파일에서 게임 결과 읽기 + 파일 삭제
	 *
	 * 호출 시점: 로비 PostLogin에서 크래시 복구 전
	 * → 파일이 있으면 로드 후 삭제, 없으면 빈 배열 반환
	 *
	 * @param PlayerId       플레이어 고유 ID
	 * @param OutSurvived    파일에 저장된 생존 여부 (out)
	 * @param bOutSuccess    JSON 파싱 성공 여부 (out) — false면 파일 손상 (호출자가 Loadout 삭제 스킵해야 함)
	 * @param OutEquipment   게임 시점 장착 슬롯 정보 (out, 선택적)
	 * @return 로드된 아이템 배열 (파일 없거나 파싱 실패 시 빈 배열)
	 */
	TArray<FInv_SavedItemData> ImportGameResultFromFile(const FString& PlayerId, bool& OutSurvived, bool& bOutSuccess,
		TArray<FHellunaEquipmentSlotData>* OutEquipment = nullptr);

	/**
	 * 게임 결과 전송 파일이 존재하는지 확인
	 *
	 * @param PlayerId  플레이어 고유 ID
	 * @return true = 파일 존재 (게임 결과 대기 중)
	 */
	bool HasPendingGameResultFile(const FString& PlayerId) const;

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
	 * @deprecated [Fix36] IsPlayerDeployed로 대체
	 *
	 * SQL: SELECT COUNT(*) FROM player_loadout WHERE player_id = ?
	 *
	 * @param PlayerId  플레이어 고유 ID
	 * @return true = Loadout 잔존 (크래시 의심), false = 정상
	 */
	virtual bool HasPendingLoadout(const FString& PlayerId) override;

	/**
	 * 크래시 복구 — Loadout에 남은 아이템을 Stash로 복귀
	 * @deprecated [Fix36] IsPlayerDeployed/SetPlayerDeployed로 대체. Loadout→Stash 이동 없음.
	 *
	 * @param PlayerId  플레이어 고유 ID
	 * @return 성공 여부
	 */
	virtual bool RecoverFromCrash(const FString& PlayerId) override;

	// ════════════════════════════════════════════════════════════════
	// [Fix36] 출격 상태 추적 (독립 Loadout 영속성)
	// ════════════════════════════════════════════════════════════════

	/** [Fix36] 출격 상태 설정 (Deploy=true, GameResult 처리=false) */
	virtual bool SetPlayerDeployed(const FString& PlayerId, bool bDeployed) override;

	/** [Fix36] 플레이어가 출격 중인지 확인 (크래시 감지용) */
	virtual bool IsPlayerDeployed(const FString& PlayerId) override;

	/** [Phase 14a] 출격 상태 설정 + 포트/영웅타입 저장 (재참가용) */
	bool SetPlayerDeployedWithPort(const FString& PlayerId, bool bDeployed, int32 ServerPort, int32 HeroTypeIndex);

	/** [Phase 14a] 출격 플레이어의 게임서버 포트 조회 (0=미등록) */
	int32 GetPlayerDeployedPort(const FString& PlayerId);

	/** [Phase 14a] 출격 플레이어의 영웅타입 인덱스 조회 (3=None) */
	int32 GetPlayerDeployedHeroType(const FString& PlayerId);

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

	/** 전체 캐릭터 등록 해제 (서버 시작 시 stale 데이터 정리) */
	bool ClearAllActiveGameCharacters();

	// ════════════════════════════════════════════════════════════════
	// IInventoryDatabase — 장착 상태 관리 (player_equipment)
	// ════════════════════════════════════════════════════════════════

	/** 장착 스냅샷 저장 (DELETE + INSERT) */
	virtual bool SavePlayerEquipment(const FString& PlayerId, const TArray<FHellunaEquipmentSlotData>& Equipment) override;

	/** 장착 스냅샷 로드 */
	virtual TArray<FHellunaEquipmentSlotData> LoadPlayerEquipment(const FString& PlayerId) override;

	/** 장착 정보 삭제 (사망/리셋 시) */
	virtual bool DeletePlayerEquipment(const FString& PlayerId) override;

	// ════════════════════════════════════════════════════════════════
	// 파티 시스템 CRUD (Phase 12a)
	// ════════════════════════════════════════════════════════════════

	/**
	 * 파티 생성 — party_groups INSERT + party_members INSERT(Leader)
	 *
	 * @param LeaderId    리더 플레이어 ID
	 * @param DisplayName 리더 표시 이름
	 * @param PartyCode   6자리 파티 코드 (호출자가 생성)
	 * @return 생성된 PartyId (실패 시 0)
	 */
	int32 CreateParty(const FString& LeaderId, const FString& DisplayName, const FString& PartyCode);

	/**
	 * 파티 참가 — party_members INSERT
	 *
	 * @param PartyId     참가할 파티 ID
	 * @param PlayerId    참가 플레이어 ID
	 * @param DisplayName 표시 이름
	 * @return 성공 여부
	 */
	bool JoinParty(int32 PartyId, const FString& PlayerId, const FString& DisplayName);

	/**
	 * 파티 탈퇴 — party_members DELETE, 마지막 멤버면 party_groups도 DELETE
	 *
	 * @param PlayerId  탈퇴할 플레이어 ID
	 * @return 성공 여부
	 */
	bool LeaveParty(const FString& PlayerId);

	/**
	 * 파티 해산 — party_groups + party_members 전체 DELETE
	 *
	 * @param PartyId  해산할 파티 ID
	 * @return 성공 여부
	 */
	bool DisbandParty(int32 PartyId);

	/**
	 * 파티 코드로 파티 ID 조회
	 *
	 * @param PartyCode  6자리 파티 코드
	 * @return PartyId (없으면 0)
	 */
	int32 FindPartyByCode(const FString& PartyCode);

	/**
	 * 파티 멤버 수 조회
	 *
	 * @param PartyId  파티 ID
	 * @return 멤버 수 (파티 없으면 0)
	 */
	int32 GetPartyMemberCount(int32 PartyId);

	/**
	 * 플레이어가 속한 파티 ID 조회
	 *
	 * @param PlayerId  플레이어 ID
	 * @return PartyId (미가입 시 0)
	 */
	int32 GetPlayerPartyId(const FString& PlayerId);

	/**
	 * 파티 전체 정보 로드 (party_groups + party_members JOIN)
	 *
	 * @param PartyId  파티 ID
	 * @return 파티 정보 (없으면 PartyId=0인 무효 구조체)
	 */
	FHellunaPartyInfo LoadPartyInfo(int32 PartyId);

	/**
	 * 멤버 Ready 상태 업데이트
	 *
	 * @param PlayerId  플레이어 ID
	 * @param bReady    준비 완료 여부
	 * @return 성공 여부
	 */
	bool UpdateMemberReady(const FString& PlayerId, bool bReady);

	/**
	 * 멤버 영웅 타입 업데이트
	 *
	 * @param PlayerId  플레이어 ID
	 * @param HeroType  영웅 타입 인덱스 (EHellunaHeroType 기반)
	 * @return 성공 여부
	 */
	bool UpdateMemberHeroType(const FString& PlayerId, int32 HeroType);

	/**
	 * 리더십 이전 — 기존 리더를 Member로, NewLeaderId를 Leader로
	 *
	 * @param PartyId      파티 ID
	 * @param NewLeaderId  새 리더 플레이어 ID
	 * @return 성공 여부
	 */
	bool TransferLeadership(int32 PartyId, const FString& NewLeaderId);

	/**
	 * 파티 전원 Ready 상태 리셋
	 *
	 * @param PartyId  파티 ID
	 * @return 성공 여부
	 */
	bool ResetAllReadyStates(int32 PartyId);

	/**
	 * 파티 코드 유니크 여부 확인
	 *
	 * @param Code  6자리 코드
	 * @return true = 사용 가능 (중복 없음)
	 */
	bool IsPartyCodeUnique(const FString& Code);

	/**
	 * 오래된 파티 정리 — HoursOld 시간 이상 된 파티 삭제
	 *
	 * @param HoursOld  시간 기준 (기본 24시간)
	 * @return 삭제된 파티 수
	 */
	int32 CleanupStaleParties(int32 HoursOld = 24);

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

	/** Loadout 전송 파일의 전체 경로를 반환 */
	FString GetLoadoutTransferFilePath(const FString& PlayerId) const;

	/** Game Result 전송 파일의 전체 경로를 반환 */
	FString GetGameResultTransferFilePath(const FString& PlayerId) const;

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

	/**
	 * 파일 전송 전용 모드 플래그
	 * Initialize()에서 게임서버로 감지되면 true로 설정
	 * → TryReopenDatabase()에서 DB 열기를 영구적으로 차단
	 * → Export/Import 파일 함수만 사용 가능
	 */
	bool bFileTransferOnly = false;
};
