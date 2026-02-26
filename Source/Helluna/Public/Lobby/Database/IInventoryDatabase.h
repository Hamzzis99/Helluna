// File: Source/Helluna/Public/Lobby/Database/IInventoryDatabase.h
#pragma once

#include "CoreMinimal.h"
#include "Player/Inv_PlayerController.h"    // FInv_SavedItemData
#include "Persistence/Inv_SaveTypes.h"      // FInv_PlayerSaveData

/**
 * IInventoryDatabase - 인벤토리 백엔드 추상화 인터페이스
 *
 * [목적]
 * 데디케이티드 서버가 인벤토리 데이터를 저장/로드할 때 사용하는 백엔드 추상화 계층.
 * 구체적인 저장소 구현(SQLite, REST API, PostgreSQL 등)에 의존하지 않고,
 * 순수 가상함수를 통해 저장소를 교체할 수 있도록 설계되었다.
 *
 * [현재 구현]
 * - UHellunaSQLiteSubsystem (Phase 1-3에서 구현 예정)
 *
 * [추후 확장 예시]
 * - UHellunaRestApiSubsystem   : 원격 REST API 서버 연동
 * - UHellunaPostgreSQLSubsystem : PostgreSQL 데이터베이스 연동
 *
 * [데이터 타입 의존성]
 * 이 인터페이스는 Inventory 플러그인의 데이터 타입에 의존한다:
 * - FInv_SavedItemData  (Player/Inv_PlayerController.h)
 * - FInv_PlayerSaveData (Persistence/Inv_SaveTypes.h)
 * Inventory 플러그인이 반드시 빌드에 포함되어야 한다.
 *
 * // TODO: [SQL전환] 이 인터페이스를 새 클래스에서 구현하면 백엔드 교체 완료
 *
 * // TODO: [비동기] 추후 비동기 버전 필요 시:
 * // DECLARE_DELEGATE_OneParam(FOnStashLoaded, const TArray<FInv_SavedItemData>&);
 * // DECLARE_DELEGATE_OneParam(FOnOperationComplete, bool);
 * // virtual void AsyncLoadPlayerStash(const FString& PlayerId, FOnStashLoaded OnComplete) = 0;
 * // virtual void AsyncSavePlayerStash(const FString& PlayerId, const TArray<FInv_SavedItemData>& Items, FOnOperationComplete OnComplete) = 0;
 */
class HELLUNA_API IInventoryDatabase
{
public:
	virtual ~IInventoryDatabase() = default;

	// ============================================================
	// Stash (로비 창고) 관련
	// ============================================================

	/**
	 * 로비 창고(Stash) 전체 아이템을 로드한다.
	 *
	 * @param PlayerId  플레이어 고유 ID (UniqueNetId 등)
	 * @return 저장된 아이템 배열. 데이터가 없으면 빈 배열 반환 (신규 유저).
	 *
	 * [호출 시점] 로비 서버 접속 시 (PostLogin), Stash UI 초기화 시
	 */
	virtual TArray<FInv_SavedItemData> LoadPlayerStash(const FString& PlayerId) = 0;

	/**
	 * 창고 상태를 DB에 저장한다.
	 * 기존 데이터를 전부 DELETE 후 새로운 데이터를 INSERT하는 방식.
	 *
	 * @param PlayerId  플레이어 고유 ID
	 * @param Items     저장할 아이템 배열 (창고 전체 상태)
	 * @return 저장 성공 여부
	 *
	 * [호출 시점] 로비에서 창고 변경 시, 출격 전 최종 저장 시
	 */
	virtual bool SavePlayerStash(const FString& PlayerId, const TArray<FInv_SavedItemData>& Items) = 0;

	/**
	 * 해당 플레이어의 Stash 데이터가 존재하는지 확인한다.
	 *
	 * @param PlayerId  플레이어 고유 ID
	 * @return 데이터 존재 여부 (true = 기존 유저, false = 신규 유저)
	 *
	 * [호출 시점] 로비 접속 시 신규/기존 유저 분기 판단
	 */
	virtual bool IsPlayerExists(const FString& PlayerId) = 0;

	// ============================================================
	// Loadout (출격 장비) 관련 — 비행기표 패턴
	// ============================================================

	/**
	 * 출격 장비(Loadout)를 로드한다.
	 * 게임서버 PostLogin에서 호출하여 플레이어의 출격 장비를 인게임에 반영한다.
	 * 데이터가 없으면 출격 정보가 없는 것으로 간주한다.
	 *
	 * @param PlayerId  플레이어 고유 ID
	 * @return 출격 장비 아이템 배열. 데이터 없으면 빈 배열.
	 *
	 * [호출 시점] 게임 데디서버 PostLogin 시
	 */
	virtual TArray<FInv_SavedItemData> LoadPlayerLoadout(const FString& PlayerId) = 0;

	/**
	 * 출격 시 Loadout을 기록하고, Stash에서 해당 아이템을 차감한다.
	 *
	 * [중요] 이 두 동작(Loadout INSERT + Stash에서 차감)은
	 *        반드시 하나의 트랜잭션으로 처리되어야 한다.
	 *        트랜잭션 실패 시 양쪽 모두 롤백되어야 한다.
	 *
	 * @param PlayerId  플레이어 고유 ID
	 * @param Items     출격할 아이템 배열
	 * @return 저장 성공 여부 (트랜잭션 성공/실패)
	 *
	 * [호출 시점] 로비에서 출격 버튼 클릭 → ClientTravel 직전
	 */
	virtual bool SavePlayerLoadout(const FString& PlayerId, const TArray<FInv_SavedItemData>& Items) = 0;

	/**
	 * 게임 종료 후 Loadout을 삭제한다. (비행기표 소멸)
	 *
	 * @param PlayerId  플레이어 고유 ID
	 * @return 삭제 성공 여부
	 *
	 * [호출 시점] 게임 종료 후 결과 처리 완료 시
	 */
	virtual bool DeletePlayerLoadout(const FString& PlayerId) = 0;

	// ============================================================
	// 게임 결과 반영
	// ============================================================

	/**
	 * 게임 결과 아이템을 기존 Stash에 병합(MERGE)한다.
	 * 기존 Stash 데이터는 유지하고, 결과 아이템만 추가(INSERT)하는 방식.
	 * 덮어쓰기(REPLACE)가 아닌 병합(MERGE) 방식임에 유의.
	 *
	 * @param PlayerId     플레이어 고유 ID
	 * @param ResultItems  게임 결과로 획득한 아이템 배열
	 * @return 병합 성공 여부
	 *
	 * [호출 시점] 게임 종료 → 결과 화면 → Stash 반영 시
	 */
	virtual bool MergeGameResultToStash(const FString& PlayerId, const TArray<FInv_SavedItemData>& ResultItems) = 0;

	// ============================================================
	// 크래시 복구
	// ============================================================

	/**
	 * 크래시 복구용: Loadout이 아직 남아있는지 확인한다.
	 * Loadout이 남아있다 = 이전 게임에서 비정상 종료된 것.
	 *
	 * @param PlayerId  플레이어 고유 ID
	 * @return Loadout 잔존 여부 (true = 비정상 종료 감지)
	 *
	 * [호출 시점] 로비 서버 PostLogin 직후, Stash 로드 전
	 */
	virtual bool HasPendingLoadout(const FString& PlayerId) = 0;

	/**
	 * 크래시 복구: Loadout 잔존 아이템을 Stash로 복귀시키고 Loadout을 삭제한다.
	 *
	 * @param PlayerId  플레이어 고유 ID
	 * @return 복구 성공 여부
	 *
	 * [호출 시점] HasPendingLoadout()이 true를 반환한 직후
	 */
	virtual bool RecoverFromCrash(const FString& PlayerId) = 0;

	// ============================================================
	// 게임 캐릭터 중복 방지 (active_game_characters)
	// ============================================================

	/**
	 * 현재 모든 서버에서 사용 중인 캐릭터 조회
	 *
	 * @return 3개 bool 배열 (인덱스 0=Lui, 1=Luna, 2=Liam, true=사용중)
	 *
	 * [호출 시점] 로비 캐릭터 선택 UI 초기화 시
	 *
	 * TODO: [실시간] 현재는 폴링 방식. 다른 플레이어가 선택하면 즉시 반영되지 않음.
	 * TODO: [확장] 서버별 필터링이 필요하면 ServerId 파라미터 추가
	 */
	virtual TArray<bool> GetActiveGameCharacters() = 0;

	/**
	 * 캐릭터 사용 등록 (출격 시)
	 *
	 * @param HeroType  등록할 캐릭터 타입 (0=Lui, 1=Luna, 2=Liam)
	 * @param PlayerId  플레이어 고유 ID
	 * @param ServerId  게임서버 고유 ID
	 * @return 등록 성공 여부 (이미 사용 중이면 false)
	 *
	 * [호출 시점] 로비에서 캐릭터 선택 확정 시
	 *
	 * TODO: [Race Condition] 두 플레이어가 동시에 같은 캐릭터를 선택하면
	 *       UNIQUE INDEX로 한쪽은 실패하지만, UI에 즉시 반영되지는 않음.
	 * TODO: [크래시 복구] 서버 크래시 시 등록 레코드가 남아있을 수 있음.
	 *       heartbeat/TTL 기반 자동 정리 필요.
	 */
	virtual bool RegisterActiveGameCharacter(int32 HeroType, const FString& PlayerId, const FString& ServerId) = 0;

	/**
	 * 플레이어의 캐릭터 사용 해제
	 *
	 * @param PlayerId  플레이어 고유 ID
	 * @return 해제 성공 여부
	 *
	 * [호출 시점] 로비 Logout 시, 캐릭터 재선택 시
	 */
	virtual bool UnregisterActiveGameCharacter(const FString& PlayerId) = 0;

	/**
	 * 특정 서버의 모든 캐릭터 등록 해제
	 *
	 * @param ServerId  게임서버 고유 ID
	 * @return 해제 성공 여부
	 *
	 * [호출 시점] 게임서버 종료 시 (정리용)
	 *
	 * TODO: [크래시 복구] 서버가 비정상 종료되면 이 함수가 호출되지 않음.
	 *       외부 watchdog 또는 TTL 기반 자동 정리 필요.
	 */
	virtual bool UnregisterAllActiveGameCharactersForServer(const FString& ServerId) = 0;
};
