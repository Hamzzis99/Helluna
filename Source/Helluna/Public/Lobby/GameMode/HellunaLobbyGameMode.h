// ════════════════════════════════════════════════════════════════════════════════
// HellunaLobbyGameMode.h
// ════════════════════════════════════════════════════════════════════════════════
//
// 로비 전용 GameMode — Stash/Loadout 듀얼 Grid UI 관리
//
// 📌 상속 구조:
//    AGameMode → AInv_SaveGameMode → AHellunaBaseGameMode → AHellunaLobbyGameMode
//
// 📌 역할:
//    - PostLogin: 크래시 복구 → SQLite Stash 로드 → StashComp에 RestoreFromSaveData
//    - Logout: 현재 Stash/Loadout 상태를 SQLite에 저장
//    - 인게임 캐릭터 스폰/전투 로직은 전혀 없음 (로비 전용!)
//
// 📌 사용법:
//    BP_HellunaLobbyGameMode에서 이 클래스를 부모로 지정
//    로비 맵(L_Lobby)의 WorldSettings에서 GameMode Override로 설정
//
// 작성자: Gihyeon (Claude Code 보조)
// ════════════════════════════════════════════════════════════════════════════════

#pragma once

#include "CoreMinimal.h"
#include "GameMode/HellunaBaseGameMode.h"
#include "HellunaTypes.h"
#include "Lobby/Party/HellunaPartyTypes.h"
#include "Lobby/Party/HellunaMatchmakingTypes.h"
#include "HellunaLobbyGameMode.generated.h"

// 전방 선언
class AHellunaLobbyController;
class UHellunaSQLiteSubsystem;
class UInv_InventoryComponent;
class UHellunaAccountSaveGame;
class UHellunaGameServerManager;

UCLASS()
class HELLUNA_API AHellunaLobbyGameMode : public AHellunaBaseGameMode
{
	GENERATED_BODY()

public:
	AHellunaLobbyGameMode();

	// ════════════════════════════════════════════════════════════════
	// GameMode 오버라이드
	// ════════════════════════════════════════════════════════════════

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void Logout(AController* Exiting) override;

	/** PlayerId 획득 (public 래퍼 — Controller에서 호출용) */
	// 📌 로그인/디버그 공통 PlayerId 반환
	//    PostLogin에서도 동일한 ID를 사용하므로 Deploy 시에도 일치
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "로비",
		meta = (DisplayName = "플레이어 ID 가져오기"))
	FString GetLobbyPlayerId(APlayerController* PC) const { return GetPlayerSaveId(PC); }

	// ════════════════════════════════════════════════════════════════
	// [Phase 13] 로비 로그인 시스템
	// ════════════════════════════════════════════════════════════════

	/** 로비 로그인 처리 (AccountSaveGame 검증 + 동시접속 체크) */
	void ProcessLobbyLogin(AHellunaLobbyController* LobbyPC, const FString& PlayerId, const FString& Password);

	/** 로비 회원가입 처리 (ID 중복 체크 + 계정 생성) */
	void ProcessLobbySignup(AHellunaLobbyController* LobbyPC, const FString& PlayerId, const FString& Password);

	/** 로그인 성공 후 로비 초기화 (게임결과 처리, Stash/Loadout 로드, 캐릭터선택, 파티복구) */
	void InitializeLobbyForPlayer(AHellunaLobbyController* LobbyPC, const FString& PlayerId);

protected:
	UPROPERTY(EditDefaultsOnly, Category = "Lobby|Server")
	FString LobbyReturnURL;
	// ════════════════════════════════════════════════════════════════
	// Stash 로드/저장 헬퍼
	// ════════════════════════════════════════════════════════════════

	/**
	 * SQLite에서 Stash 로드 → StashComp에 RestoreFromSaveData
	 *
	 * @param LobbyPC  대상 로비 컨트롤러
	 * @param PlayerId 플레이어 고유 ID
	 */
	void LoadStashToComponent(AHellunaLobbyController* LobbyPC, const FString& PlayerId);

	/**
	 * [Fix23] SQLite에서 Loadout 로드 → LoadoutComp에 RestoreFromSaveData
	 * 게임 생존 후 복귀 시 Loadout 아이템을 LoadoutComp에 복원
	 * 로드 완료 후 player_loadout 삭제 (Logout 시 중복 방지)
	 *
	 * @param LobbyPC  대상 로비 컨트롤러
	 * @param PlayerId 플레이어 고유 ID
	 */
	void LoadLoadoutToComponent(AHellunaLobbyController* LobbyPC, const FString& PlayerId);

	/**
	 * 현재 StashComp + LoadoutComp 상태를 SQLite에 저장
	 *
	 * @param LobbyPC  대상 로비 컨트롤러
	 * @param PlayerId 플레이어 고유 ID
	 */
	void SaveComponentsToDatabase(AHellunaLobbyController* LobbyPC, const FString& PlayerId);

	/**
	 * ItemType → UInv_ItemComponent* 리졸버 (RestoreFromSaveData에 전달)
	 * 기존 HellunaBaseGameMode::ResolveItemClass()를 활용
	 */
	UInv_ItemComponent* ResolveItemTemplate(const FGameplayTag& ItemType);

	// ════════════════════════════════════════════════════════════════
	// SQLite 서브시스템 캐시
	// ════════════════════════════════════════════════════════════════

	/** SQLite 서브시스템 참조 (BeginPlay에서 캐시) */
	UPROPERTY()
	TObjectPtr<UHellunaSQLiteSubsystem> SQLiteSubsystem;

public:
	/** [Phase 16] 서버 프로세스 매니저 (Controller에서 접근) */
	UPROPERTY()
	TObjectPtr<UHellunaGameServerManager> GameServerManager;

protected:

	/** [Phase 13] 계정 SaveGame (BeginPlay에서 LoadOrCreate) */
	UPROPERTY()
	TObjectPtr<UHellunaAccountSaveGame> LobbyAccountSaveGame;

	// ════════════════════════════════════════════════════════════════
	// 캐릭터 중복 방지 (같은 로비 내)
	// ════════════════════════════════════════════════════════════════

public:
	/**
	 * 해당 캐릭터가 현재 로비에서 사용 가능한지 확인
	 * 메모리 맵(같은 로비) + SQLite(다른 서버 간) 교차 체크
	 */
	bool IsLobbyCharacterAvailable(EHellunaHeroType HeroType, const FString& RequestingPlayerId) const;

	/** 현재 로비에서 가용한 캐릭터 목록 (3개 bool, true=사용중, 파티 기준) */
	TArray<bool> GetLobbyAvailableCharacters(const FString& RequestingPlayerId) const;

	/** 캐릭터 사용 등록 (같은 로비 + SQLite) */
	void RegisterLobbyCharacterUse(EHellunaHeroType HeroType, const FString& PlayerId);

	/** 캐릭터 사용 해제 (같은 로비 + SQLite) */
	void UnregisterLobbyCharacterUse(const FString& PlayerId);

	// ════════════════════════════════════════════════════════════════
	// [Phase 12b] 채널 레지스트리 스캔
	// ════════════════════════════════════════════════════════════════

	/** ServerRegistry 폴더 스캔 → 채널 목록 반환 */
	TArray<FGameChannelInfo> ScanAvailableChannels();

	/** 빈 채널(status=empty, PendingDeploy 제외) 찾기 — null이면 빈 채널 없음 */
	bool FindEmptyChannel(FGameChannelInfo& OutChannel);

	/** [Phase 16] 특정 맵의 빈 채널 검색 */
	bool FindEmptyChannelForMap(const FString& MapKey, FGameChannelInfo& OutChannel);

	/** [Phase 16] 맵키로 MapPath 조회 */
	FString GetMapPathByKey(const FString& MapKey) const;

	/** 설정된 맵 키인지 검증 */
	bool IsValidConfiguredMapKey(const FString& MapKey) const;

	/** Deploy 결정 후 즉시 채널 예약 (이중 배정 방지) */
	void MarkChannelAsPendingDeploy(int32 Port);

	/** 레지스트리 디렉토리 경로 */
	FString GetRegistryDirectoryPath() const;

	// ════════════════════════════════════════════════════════════════
	// [Phase 12d] 파티 시스템 — 서버 로직
	// ════════════════════════════════════════════════════════════════

	/** 파티 코드 생성 (6자리, 유니크 보장) */
	FString GeneratePartyCode();

	/** 파티 생성 */
	void CreatePartyForPlayer(const FString& PlayerId, const FString& DisplayName);

	/** 파티 참가 */
	void JoinPartyForPlayer(const FString& PlayerId, const FString& DisplayName, const FString& PartyCode);

	/** 파티 탈퇴 (리더 이전/해산 포함) */
	void LeavePartyForPlayer(const FString& PlayerId);

	/** 파티 멤버 추방 (리더만) */
	void KickPartyMember(const FString& RequesterId, const FString& TargetId);

	/** 멤버 Ready 상태 설정 + Auto-Deploy 체크 */
	void SetPlayerReady(const FString& PlayerId, bool bReady);

	/** 캐릭터 선택 변경 알림 */
	void OnPlayerHeroChanged(const FString& PlayerId, int32 HeroType);

	/** 파티 내 영웅 중복 검사 — true = 중복 있음 */
	bool ValidatePartyHeroDuplication(int32 PartyId);

	/** 전원 Ready + 중복 없음 → Deploy */
	void TryAutoDeployParty(int32 PartyId);

	/** 파티 Deploy 실행 (채널 선택 + Save + Travel) */
	void ExecutePartyDeploy(int32 PartyId);

	// ════════════════════════════════════════════════════════════════
	// [Phase 14d] 재참가 시스템
	// ════════════════════════════════════════════════════════════════

	/** 게임서버 포트의 레지스트리가 유효한지 확인 (status=playing + 60초 이내) */
	bool IsGameServerRunning(int32 Port);

	/** 플레이어가 현재 게임 모드를 사용할 수 있는지 검증 */
	bool ValidateRequestedGameModeForPlayer(const FString& PlayerId, ELobbyGameMode Mode, FString& OutError) const;

	/** 재참가 수락 → 게임서버로 Travel */
	void HandleRejoinAccepted(AHellunaLobbyController* LobbyPC);

	/** 재참가 거부 → 아이템 포기 + 로비 정상 진입 */
	void HandleRejoinDeclined(AHellunaLobbyController* LobbyPC);

	/** InitializeLobbyForPlayer의 Step 1~5 실행 (재참가 결정 후 호출) */
	void ContinueLobbyInitAfterRejoinDecision(AHellunaLobbyController* LobbyPC, const FString& PlayerId);

	// ════════════════════════════════════════════════════════════════
	// [Phase 15] 매치메이킹 시스템
	// ════════════════════════════════════════════════════════════════

	/** [Phase 16] 매칭 큐 참가 (파티면 파티 전원, 솔로면 1인 엔트리) */
	void EnterMatchmakingQueue(const FString& PlayerId, const FString& MapKey = TEXT(""));

	/** 매칭 큐 퇴장 */
	void LeaveMatchmakingQueue(const FString& PlayerId);

	/** 큐 상태 확인 */
	bool IsPlayerInQueue(const FString& PlayerId) const;

	/** 파티 상태를 전원에게 RPC */
	void BroadcastPartyState(int32 PartyId);

	/** 파티 채팅 메시지 전원 전송 */
	void BroadcastPartyChatMessage(int32 PartyId, const FHellunaPartyChatMessage& Msg);

	// ════════════════════════════════════════════════════════════════
	// [Phase 12d] 파티 캐시 + 타이머
	// ════════════════════════════════════════════════════════════════

	/** 메모리 캐시: PartyId → 파티 정보 */
	TMap<int32, FHellunaPartyInfo> ActivePartyCache;

	/** PlayerId → PartyId 빠른 조회 */
	TMap<FString, int32> PlayerToPartyMap;

	/** PlayerId → LobbyController 매핑 */
	TMap<FString, TWeakObjectPtr<AHellunaLobbyController>> PlayerIdToControllerMap;

	/** 파티 채팅 기록 (메모리 전용, 최대 50개/파티) */
	TMap<int32, TArray<FHellunaPartyChatMessage>> PartyChatHistory;

	/** [Phase 16] 사용 가능한 맵 목록 (BP에서 설정) */
	UPROPERTY(EditDefaultsOnly, Category = "Lobby|Maps",
		meta = (DisplayName = "Available Map List (사용 가능 맵 목록)"))
	TArray<FHellunaGameMapInfo> AvailableMapConfigs;

	/** [Phase 16] 기본 맵 키 */
	UPROPERTY(EditDefaultsOnly, Category = "Lobby|Maps",
		meta = (DisplayName = "Default Map Key (기본 맵 키)"))
	FString DefaultMapKey = TEXT("GihyeonMap");

	/** Deploy 예약된 채널 포트 (이중 배정 방지) */
	TSet<int32> PendingDeployChannels;

	/** PendingDeploy 자동 해제 타이머 */
	TMap<int32, FTimerHandle> PendingDeployTimers;

	/** [Fix46-M6] 오래된 파티 주기적 정리 타이머 */
	FTimerHandle StalePartyCleanupTimer;

	/** 연결 끊김 시 파티 탈퇴 유예 타이머 (30초) */
	TMap<FString, FTimerHandle> PartyLeaveTimers;

	// ── [Phase 15] 매치메이킹 큐 데이터 ──

	/** FIFO 매칭 큐 */
	TArray<FMatchmakingQueueEntry> MatchmakingQueue;

	/** PlayerId → EntryId 빠른 조회 */
	TMap<FString, int32> PlayerToQueueEntryMap;

	/** 다음 엔트리 ID */
	int32 NextQueueEntryId = 1;

	/** 1초 매칭 틱 타이머 */
	FTimerHandle MatchmakingTickTimer;

	/** [Phase 16] WaitAndDeploy 폴링 타이머 (포트별) */
	TMap<int32, FTimerHandle> WaitAndDeployTimers;

	/** 매초 매칭 시도 */
	void TickMatchmaking();

	/** 큐에 있는 전원에게 상태 브로드캐스트 */
	void BroadcastMatchmakingStatus();

	/** 지정된 플레이어들에게 매칭 상태를 명시적으로 리셋 */
	void ResetMatchmakingStatusForPlayers(const TArray<FString>& PlayerIds, EMatchmakingStatus Status = EMatchmakingStatus::None);

	/** 지정된 플레이어들에게 매칭 에러를 전송 */
	void SendMatchmakingErrorToPlayers(const TArray<FString>& PlayerIds, const FString& ErrorMessage);

	/** 현재 로비에 유효한 컨트롤러가 연결된 플레이어인지 확인 */
	bool IsPlayerOnlineInLobby(const FString& PlayerId) const;

	/** 플레이어 1명의 deploy 전 영속화 수행 */
	bool PersistDeployDataForPlayer(AHellunaLobbyController* LobbyPC, const FString& PlayerId, int32 HeroType, int32 ServerPort, FString& OutError);

	/** 플레이어 1명의 deploy 상태 롤백 */
	void RollbackDeployStateForPlayer(const FString& PlayerId, int32 ExpectedPort = INDEX_NONE);

	/** 여러 플레이어의 deploy 상태 롤백 */
	void RollbackDeployStateForPlayers(const TArray<FString>& PlayerIds, int32 ExpectedPort = INDEX_NONE);

	/** 매칭 알고리즘 — 조합 찾으면 true */
	bool TryFormMatch();

	/** 매칭 완료 → Deploy 실행 (서버 없으면 비동기 스폰 대기) */
	void ExecuteMatchedDeploy(const TArray<FMatchmakingQueueEntry>& Matched, bool bRequeueOnFailure = false);

	/** [Phase 16/19] 비동기 Deploy (서버 스폰/맵 전환 대기). MapKey 비어있으면 맵 무관 체크 */
	void WaitAndDeploy(int32 Port, TArray<FMatchmakingQueueEntry> Matched, const FString& MapKey = TEXT(""), bool bRequeueOnFailure = false);

	/** [Phase 19] 빈 서버에 맵 전환 커맨드 파일 작성 */
	void WriteMapSwitchCommand(int32 Port, const FString& MapPath);

	/** 큐 엔트리 제거 */
	void RemoveQueueEntry(int32 EntryId);

	/** [Phase 17 deprecated] 매칭 간 영웅 중복 검사 — ResolveHeroDuplication으로 대체 */
	bool ValidateMatchHeroDuplication(const TArray<FMatchmakingQueueEntry>& Entries) const;

	/** 큐 엔트리 영웅 타입 갱신 */
	void UpdateQueueEntryHeroType(const FString& PlayerId, int32 NewHeroType);

	/** 큐 엔트리 추가 + 공통 후처리 */
	void EnqueueMatchmakingEntry(FMatchmakingQueueEntry Entry, bool bTryMatchImmediately = true);

	/** 재큐잉용 엔트리 복구 */
	void RequeueMatchEntries(
		const TArray<FMatchmakingQueueEntry>& Entries,
		const FString& ExcludedPlayerId = FString(),
		const TMap<FString, TPair<int32, int32>>* ReassignedHeroes = nullptr,
		bool bTryMatchImmediately = false);

	// ── [Phase 17] 매칭 카운트다운 + 영웅 자동 재배정 ──

	/** 카운트다운 대기 중인 매칭 그룹 */
	struct FPendingCountdownMatch
	{
		TArray<FMatchmakingQueueEntry> MatchedEntries;
		/** PlayerId -> {원래HeroType, 새HeroType} */
		TMap<FString, TPair<int32, int32>> ReassignedHeroes;
		int32 RemainingSeconds = 5;
		FTimerHandle CountdownTimerHandle;
		int32 MatchGroupId = 0;
	};

	TArray<FPendingCountdownMatch> PendingCountdownMatches;
	int32 NextMatchGroupId = 1;

	/** 영웅 중복 해소 — Matched 배열 직접 수정 + 변경 맵 반환 */
	TMap<FString, TPair<int32, int32>> ResolveHeroDuplication(TArray<FMatchmakingQueueEntry>& Matched);

	/** 카운트다운 시작 (TryFormMatch에서 호출) */
	void StartMatchCountdown(TArray<FMatchmakingQueueEntry> Matched, TMap<FString, TPair<int32, int32>> ReassignedHeroes);

	/** 매초 카운트다운 틱 */
	void TickMatchCountdown(int32 MatchGroupId);

	/** 카운트다운 완료 -> Deploy 시작 */
	void OnCountdownFinished(int32 MatchGroupId);

	/** 카운트다운 중 플레이어 이탈 처리 */
	void HandleCountdownPlayerDisconnect(const FString& PlayerId);

	/** 캐시 갱신 (DB에서 다시 로드) */
	void RefreshPartyCache(int32 PartyId);

	/** 로비 디버그 로그인용 고유 ID 생성 */
	FString CreateDebugLobbyPlayerId(APlayerController* LobbyPC) const;

private:
	/**
	 * 플레이어별 선택 캐릭터 맵 (메모리)
	 * Key: PlayerId, Value: HeroType
	 * 같은 캐릭터를 여러 플레이어가 선택 가능 (파티 내에서만 중복 제한)
	 */
	TMap<FString, EHellunaHeroType> PlayerSelectedHeroMap;

	/** 로비 서버 고유 ID (active_game_characters.server_id용) */
	FString LobbyServerId;
};
