// File: Source/Helluna/Public/Lobby/Database/HellunaSQLiteSubsystem.h
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Lobby/Database/IInventoryDatabase.h"
#include "HellunaSQLiteSubsystem.generated.h"

// 전방선언 — FSQLiteDatabase, FSQLitePreparedStatement는 UObject가 아닌 POD 클래스
// #include "SQLiteDatabase.h"와 "SQLitePreparedStatement.h"는 cpp에서만 수행
class FSQLiteDatabase;
class FSQLitePreparedStatement;

/**
 * UHellunaSQLiteSubsystem
 *
 * IInventoryDatabase 인터페이스의 SQLite 구현체.
 * UGameInstanceSubsystem으로 GameInstance 수명 동안 유지되므로
 * 맵 전환(ClientTravel) 시에도 DB 연결이 살아있다.
 *
 * [DB 파일 위치] {ProjectSavedDir}/Database/Helluna.db
 * [로비서버 + 게임서버] 같은 PC, 같은 DB 파일을 공유
 *
 * [현재 상태] Phase 1 — 뼈대 (stub 구현)
 * Phase 2에서 각 함수 내부에 실제 SQL 쿼리를 채워넣을 예정.
 *
 * // TODO: [SQL전환] REST API/PostgreSQL 전환 시 이 클래스를 새 구현으로 교체
 */
UCLASS()
class HELLUNA_API UHellunaSQLiteSubsystem : public UGameInstanceSubsystem, public IInventoryDatabase
{
	GENERATED_BODY()

public:
	// ── USubsystem 오버라이드 ──
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	// ── DB 상태 확인 ──

	/** DB가 열려있고 유효한지 확인 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Helluna|Database")
	bool IsDatabaseReady() const;

	/** DB 파일의 절대 경로를 반환 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Helluna|Database")
	FString GetDatabasePath() const;

	// ── IInventoryDatabase 인터페이스 구현 ──

	virtual TArray<FInv_SavedItemData> LoadPlayerStash(const FString& PlayerId) override;
	virtual bool SavePlayerStash(const FString& PlayerId, const TArray<FInv_SavedItemData>& Items) override;
	virtual bool IsPlayerExists(const FString& PlayerId) override;

	virtual TArray<FInv_SavedItemData> LoadPlayerLoadout(const FString& PlayerId) override;
	virtual bool SavePlayerLoadout(const FString& PlayerId, const TArray<FInv_SavedItemData>& Items) override;
	virtual bool DeletePlayerLoadout(const FString& PlayerId) override;

	virtual bool MergeGameResultToStash(const FString& PlayerId, const TArray<FInv_SavedItemData>& ResultItems) override;

	virtual bool HasPendingLoadout(const FString& PlayerId) override;
	virtual bool RecoverFromCrash(const FString& PlayerId) override;

private:
	// ── DB 관리 ──

	/** DB 열기 — Initialize()에서 호출 */
	bool OpenDatabase();

	/** DB 닫기 — Deinitialize()에서 호출 */
	void CloseDatabase();

	/** 테이블 스키마 생성 + PRAGMA 설정 — OpenDatabase() 성공 직후 호출 */
	bool InitializeSchema();

	// ── FInv_SavedItemData ↔ DB 변환 헬퍼 ──

	/** SELECT 결과 1행 → FInv_SavedItemData 파싱 */
	static FInv_SavedItemData ParseRowToSavedItem(const FSQLitePreparedStatement& Statement);

	/** FInv_SavedAttachmentData 배열 → JSON 문자열 */
	static FString SerializeAttachmentsToJson(const TArray<FInv_SavedAttachmentData>& Attachments);

	/** JSON 문자열 → FInv_SavedAttachmentData 배열 */
	static TArray<FInv_SavedAttachmentData> DeserializeAttachmentsFromJson(const FString& JsonString);

	/**
	 * SQLite DB 인스턴스 (UObject 아님, 수동 수명 관리)
	 * UPROPERTY 불가 — 소멸자/Deinitialize에서 delete 필수
	 */
	FSQLiteDatabase* Database = nullptr;

	/** DB 파일 경로 캐시 */
	FString CachedDatabasePath;

	/** DB 열림 상태 플래그 */
	bool bDatabaseOpen = false;
};
