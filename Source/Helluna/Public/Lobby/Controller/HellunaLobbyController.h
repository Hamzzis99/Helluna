// ════════════════════════════════════════════════════════════════════════════════
// HellunaLobbyController.h
// ════════════════════════════════════════════════════════════════════════════════
//
// 로비 전용 PlayerController — StashComp + LoadoutComp 듀얼 인벤토리 관리
//
// 📌 상속 구조:
//    APlayerController → AHellunaLobbyController (AInv_PlayerController 미사용 — 로비 전용 경량화)
//
// 📌 역할:
//    - StashComp: 전체 보유 아이템 (SQLite player_stash에서 로드)
//    - LoadoutComp: 출격할 아이템 (플레이어가 Stash에서 옮김)
//    - Server_TransferItem: Stash↔Loadout 간 아이템 이동 RPC
//    - Deploy: Loadout → SQLite 저장 → ClientTravel (게임 서버로 이동)
//
// 📌 네트워크:
//    Server RPCs는 이 Controller에서 선언 (클라이언트 NetConnection 소유)
//    GameMode에서 직접 호출하지 않음 (GameState에서 Server RPC 불가 법칙)
//
// 작성자: Gihyeon (Claude Code 보조)
// ════════════════════════════════════════════════════════════════════════════════

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Player/Inv_PlayerController.h"  // FInv_SavedItemData 구조체 사용
#include "HellunaTypes.h"
#include "Lobby/Party/HellunaPartyTypes.h"
#include "Lobby/Party/HellunaMatchmakingTypes.h"
#include "HellunaLobbyController.generated.h"

class AHellunaLobbyGameMode;

// 전방 선언
class UInv_InventoryComponent;
class UInv_InventoryItem;
class UHellunaLobbyStashWidget;
class UHellunaLobbyLoginWidget;
class UHellunaPartyWidget;
class UHellunaRejoinWidget;
class AHellunaCharacterSelectSceneV2;
class ACameraActor;
class USkeletalMesh;
class UHellunaLobbyCharSelectWidget;
class AHellunaLobbyCameraAnchor;

// [Fix46-M3] Validate 상한 공통 상수
namespace LobbyValidation
{
	/** RPC 파라미터 인덱스 상한 (실제 Grid 크기 기반) */
	constexpr int32 MaxInventoryIndex = 10000;
}

// ════════════════════════════════════════════════════════════════════════════════
// 배경 카메라 설정 구조체
// ════════════════════════════════════════════════════════════════════════════════
USTRUCT(BlueprintType)
struct FLobbyCameraTransform
{
	GENERATED_BODY()

	/** 카메라 위치 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta = (DisplayName = "카메라 위치 (Location)"))
	FVector Location = FVector::ZeroVector;

	/** 카메라 회전 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta = (DisplayName = "카메라 회전 (Rotation)"))
	FRotator Rotation = FRotator::ZeroRotator;

	/** 카메라 시야각 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta = (DisplayName = "카메라 FOV"))
	float FOV = 90.0f;
};

// ════════════════════════════════════════════════════════════════════════════════
// 전송 방향 열거형
// ════════════════════════════════════════════════════════════════════════════════
UENUM(BlueprintType)
enum class ELobbyTransferDirection : uint8
{
	StashToLoadout  UMETA(DisplayName = "창고 → 출격장비 (Stash → Loadout)"),
	LoadoutToStash  UMETA(DisplayName = "출격장비 → 창고 (Loadout → Stash)"),
};

UCLASS()
class HELLUNA_API AHellunaLobbyController : public APlayerController
{
	GENERATED_BODY()

public:
	AHellunaLobbyController();

	// ════════════════════════════════════════════════════════════════
	// [Phase 13] 로비 로그인 RPC
	// ════════════════════════════════════════════════════════════════

	/** [클라이언트 → 서버] 로비 로그인 요청 */
	UFUNCTION(Server, Reliable, WithValidation)
	void Server_RequestLobbyLogin(const FString& PlayerId, const FString& Password);

	/** [클라이언트 → 서버] 로비 회원가입 요청 */
	UFUNCTION(Server, Reliable, WithValidation)
	void Server_RequestLobbySignup(const FString& PlayerId, const FString& Password);

	/** [서버 → 클라이언트] 로그인 위젯 표시 지시 */
	UFUNCTION(Client, Reliable)
	void Client_ShowLobbyLoginUI();

	/** [서버 → 클라이언트] 로그인 결과 통보 */
	UFUNCTION(Client, Reliable)
	void Client_LobbyLoginResult(bool bSuccess, const FString& ErrorMessage);

	/** [서버 → 클라이언트] 회원가입 결과 통보 */
	UFUNCTION(Client, Reliable)
	void Client_LobbySignupResult(bool bSuccess, const FString& Message);

	/** 로그인 완료 여부 */
	bool IsLoggedIn() const { return bIsLoggedIn; }

	// ════════════════════════════════════════════════════════════════
	// [Phase 14f] 재참가 시스템 RPC
	// ════════════════════════════════════════════════════════════════

	/** [서버 → 클라이언트] 재참가 프롬프트 표시 */
	UFUNCTION(Client, Reliable)
	void Client_ShowRejoinPrompt(int32 GameServerPort);

	/** [클라이언트 → 서버] 재참가 수락 */
	UFUNCTION(Server, Reliable, WithValidation)
	void Server_RejoinGame();

	/** [클라이언트 → 서버] 재참가 포기 (아이템 손실) */
	UFUNCTION(Server, Reliable, WithValidation)
	void Server_AbandonGame();

	/** 대기 중인 재참가 포트 (Client RPC에서 설정) */
	int32 PendingRejoinPort = 0;

	// ════════════════════════════════════════════════════════════════
	// 컴포넌트 Getter
	// ════════════════════════════════════════════════════════════════

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "로비|인벤토리",
		meta = (DisplayName = "창고(Stash) 컴포넌트 가져오기"))
	UInv_InventoryComponent* GetStashComponent() const { return StashInventoryComponent; }

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "로비|인벤토리",
		meta = (DisplayName = "출격장비(Loadout) 컴포넌트 가져오기"))
	UInv_InventoryComponent* GetLoadoutComponent() const { return LoadoutInventoryComponent; }

	// ════════════════════════════════════════════════════════════════
	// 아이템 전송 RPC (Stash ↔ Loadout)
	// ════════════════════════════════════════════════════════════════

	/**
	 * [클라이언트 → 서버] 아이템을 Stash↔Loadout 간 전송
	 *
	 * 📌 처리 흐름:
	 *   1. SourceComp에서 아이템 찾기 (EntryIndex로)
	 *   2. TargetComp에 공간 확인
	 *   3. SourceComp에서 제거 → TargetComp에 추가
	 *
	 * @param ItemEntryIndex  전송할 아이템의 FastArray Entry 인덱스
	 * @param Direction       전송 방향 (StashToLoadout / LoadoutToStash)
	 *
	 * TODO: [DragDrop] 추후 드래그앤드롭 크로스 패널 구현 시 여기에 연결
	 */
	UFUNCTION(Server, Reliable, WithValidation)
	void Server_TransferItem(int32 ItemEntryIndex, ELobbyTransferDirection Direction, int32 TargetGridIndex = INDEX_NONE);

	/**
	 * [클라이언트 → 서버] 크로스 Grid 아이템 Swap
	 *
	 * 📌 처리 흐름:
	 *   1. RepID_A와 RepID_B로 아이템 찾기 (각각 다른 InvComp)
	 *   2. 양쪽 Manifest 수집
	 *   3. 양쪽 제거 후 교차 추가
	 *
	 * @param RepID_A  상대 Grid에서 온 아이템의 ReplicationID
	 * @param RepID_B  이 Grid에 있던 아이템의 ReplicationID
	 */
	UFUNCTION(Server, Reliable, WithValidation)
	void Server_SwapTransferItem(int32 RepID_A, int32 RepID_B, int32 TargetGridIndex = INDEX_NONE);

	/**
	 * [클라이언트 → 서버] 출격 요청
	 *
	 * 📌 처리 흐름:
	 *   1. LoadoutComp 데이터 수집 → SQLite SavePlayerLoadout
	 *   2. StashComp 데이터 수집 → SQLite SavePlayerStash (잔여 아이템)
	 *   3. Client_ExecuteDeploy() RPC로 클라이언트에게 맵 이동 지시
	 */
	UFUNCTION(Server, Reliable, WithValidation)
	void Server_Deploy();

	/**
	 * [서버 → 클라이언트] 출격 실행 (ClientTravel)
	 *
	 * @param TravelURL  이동할 맵 URL
	 */
	UFUNCTION(Client, Reliable)
	void Client_ExecuteDeploy(const FString& TravelURL);

	/** 출격 실패 시 클라이언트에 알림 */
	UFUNCTION(Client, Reliable)
	void Client_DeployFailed(const FString& Reason);

	// ════════════════════════════════════════════════════════════════
	// [Phase 12d] 파티 시스템 RPC
	// ════════════════════════════════════════════════════════════════

	// ── Server RPCs (Client → Server) ──

	UFUNCTION(Server, Reliable, WithValidation)
	void Server_CreateParty();

	UFUNCTION(Server, Reliable, WithValidation)
	void Server_JoinParty(const FString& PartyCode);

	UFUNCTION(Server, Reliable, WithValidation)
	void Server_LeaveParty();

	UFUNCTION(Server, Reliable, WithValidation)
	void Server_SetPartyReady(bool bReady);

	UFUNCTION(Server, Reliable, WithValidation)
	void Server_KickPartyMember(const FString& TargetPlayerId);

	UFUNCTION(Server, Reliable, WithValidation)
	void Server_SendPartyChatMessage(const FString& Message);

	// ── Client RPCs (Server → Client) ──

	UFUNCTION(Client, Reliable)
	void Client_UpdatePartyState(const FHellunaPartyInfo& PartyInfo);

	UFUNCTION(Client, Reliable)
	void Client_PartyDisbanded(const FString& Reason);

	UFUNCTION(Client, Reliable)
	void Client_PartyError(const FString& ErrorMessage);

	UFUNCTION(Client, Reliable)
	void Client_ReceivePartyChatMessage(const FHellunaPartyChatMessage& Msg);

	/** 파티 Deploy — 포트만 전달 (IP는 클라이언트 GameInstance에서) */
	UFUNCTION(Client, Reliable)
	void Client_ExecutePartyDeploy(int32 GameServerPort);

	/** ClientTravel 준비 실패를 서버에 알려 deploy 상태를 롤백 */
	UFUNCTION(Server, Reliable, WithValidation)
	void Server_ReportPartyDeployFailure(int32 GameServerPort, const FString& Reason);

	/** Deploy 중 여부 (public getter/setter) */
	bool IsDeployInProgress() const { return bDeployInProgress; }
	void SetDeployInProgress(bool bInProgress) { bDeployInProgress = bInProgress; }

	// ════════════════════════════════════════════════════════════════
	// [Phase 15] 매치메이킹 RPC
	// ════════════════════════════════════════════════════════════════

	/** [Phase 16] 맵 선택 변경 RPC */
	UFUNCTION(Server, Reliable, WithValidation)
	void Server_SetSelectedMap(const FString& MapKey);

	/** [Phase 18] 게임 모드 변경 RPC (Solo/Duo/Squad) */
	UFUNCTION(Server, Reliable, WithValidation)
	void Server_SetGameMode(ELobbyGameMode Mode);

	/** [클라이언트 → 서버] 매칭 큐 참가 */
	UFUNCTION(Server, Reliable, WithValidation)
	void Server_EnterMatchmaking();

	/** [클라이언트 → 서버] 매칭 큐 취소 */
	UFUNCTION(Server, Reliable, WithValidation)
	void Server_LeaveMatchmaking();

	/** [서버 → 클라이언트] 매칭 상태 업데이트 */
	UFUNCTION(Client, Reliable)
	void Client_MatchmakingStatusUpdate(const FMatchmakingStatusInfo& StatusInfo);

	/** [서버 → 클라이언트] 매칭 에러 알림 */
	UFUNCTION(Client, Reliable)
	void Client_MatchmakingError(const FString& ErrorMessage);

	/** 매칭 상태 변경 이벤트 (위젯 바인딩용) */
	UPROPERTY(BlueprintAssignable, Category = "로비|매치메이킹")
	FOnMatchmakingStatusChanged OnMatchmakingStatusChanged;

	// ── [Phase 17] 매칭 카운트다운 RPC ──

	/** [서버 -> 클라이언트] 매칭 완료 + 멤버 정보 + 카운트다운 시작 */
	UFUNCTION(Client, Reliable)
	void Client_MatchmakingFound(const FMatchmakingFoundInfo& FoundInfo);

	/** [서버 -> 클라이언트] 카운트다운 동기화 */
	UFUNCTION(Client, Reliable)
	void Client_MatchmakingCountdown(int32 RemainingSeconds);

	/** [서버 -> 클라이언트] 카운트다운 취소 */
	UFUNCTION(Client, Reliable)
	void Client_MatchmakingCancelled(const FString& Reason);

	/** 매칭 완료 이벤트 */
	UPROPERTY(BlueprintAssignable, Category = "로비|매치메이킹")
	FOnMatchmakingFoundChanged OnMatchmakingFound;

	/** 카운트다운 숫자 변경 이벤트 */
	UPROPERTY(BlueprintAssignable, Category = "로비|매치메이킹")
	FOnMatchmakingCountdownChanged OnMatchmakingCountdown;

	/** 카운트다운 취소 이벤트 */
	UPROPERTY(BlueprintAssignable, Category = "로비|매치메이킹")
	FOnMatchmakingCancelledChanged OnMatchmakingCancelled;

	// ── [Phase 12g-2] 파티 프리뷰 ──

	/** 파티 상태 변경 시 3D 프리뷰 갱신 (2명 이상이면 Party 모드, 1명 이하면 Solo 복귀) */
	void UpdatePartyPreview(const FHellunaPartyInfo& PartyInfo);

	/** 파티 해산/탈퇴 시 Solo 프리뷰로 복귀 */
	void ResetToSoloPreview();

	// ── 파티 위젯 ──

	/** 파티 위젯 토글 */
	UFUNCTION(BlueprintCallable, Category = "로비|파티",
		meta = (DisplayName = "Toggle Party Widget (파티 위젯 토글)"))
	void TogglePartyWidget();

	// ── 파티 상태 + 델리게이트 ──

	/** 현재 파티 정보 (Client RPC로 갱신) */
	UPROPERTY(BlueprintReadOnly, Category = "로비|파티")
	FHellunaPartyInfo CurrentPartyInfo;

	/** 파티 상태 변경 이벤트 (위젯 갱신용) */
	UPROPERTY(BlueprintAssignable, Category = "로비|파티")
	FOnPartyStateChanged OnPartyStateChanged;

	/** 파티 채팅 수신 이벤트 */
	UPROPERTY(BlueprintAssignable, Category = "로비|파티")
	FOnPartyChatReceived OnPartyChatReceived;

	/** 파티 에러 이벤트 */
	UPROPERTY(BlueprintAssignable, Category = "로비|파티")
	FOnPartyError OnPartyError;

	// ════════════════════════════════════════════════════════════════
	// 로비 UI
	// ════════════════════════════════════════════════════════════════

	/**
	 * [서버 → 클라이언트] 로비 UI 생성 지시
	 * GameMode에서 Stash 복원 완료 후 호출
	 */
	UFUNCTION(Client, Reliable)
	void Client_ShowLobbyUI();

	/** 로비 UI를 생성하고 화면에 표시 */
	UFUNCTION(BlueprintCallable, Category = "로비|UI",
		meta = (DisplayName = "로비 UI 표시"))
	void ShowLobbyWidget();

	// ════════════════════════════════════════════════════════════════
	// 캐릭터 선택 RPC
	// ════════════════════════════════════════════════════════════════

	/**
	 * [클라이언트 → 서버] 로비 캐릭터 선택 요청
	 *
	 * @param CharacterIndex  선택한 캐릭터 인덱스 (0=Lui, 1=Luna, 2=Liam)
	 */
	UFUNCTION(Server, Reliable, WithValidation)
	void Server_SelectLobbyCharacter(int32 CharacterIndex);

	/**
	 * [서버 → 클라이언트] 캐릭터 선택 결과 통보
	 *
	 * @param bSuccess  성공 여부
	 * @param Message   결과 메시지
	 */
	UFUNCTION(Client, Reliable)
	void Client_LobbyCharacterSelectionResult(bool bSuccess, const FString& Message);

	/**
	 * [서버 → 클라이언트] 가용 캐릭터 정보 전달
	 *
	 * @param UsedCharacters  3개 bool (true=사용중)
	 */
	UFUNCTION(Client, Reliable)
	void Client_ShowLobbyCharacterSelectUI(const TArray<bool>& UsedCharacters);

	/** 선택한 히어로 타입 Getter */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "로비|캐릭터선택")
	EHellunaHeroType GetSelectedHeroType() const { return SelectedHeroType; }

	/** [Fix43] 캐릭터 선택 리셋 (파티 충돌 시 서버에서 호출) */
	void ResetSelectedHeroType() { SelectedHeroType = EHellunaHeroType::None; }

	/** [Fix43] 캐릭터 선택 강제 설정 (서버 권한, 파티 복원 시 사용) */
	void ForceSetSelectedHeroType(EHellunaHeroType InType) { SelectedHeroType = InType; }

	/** 로그인 PlayerId Getter (서버에서 설정, Replicated) */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "로비|플레이어")
	FString GetPlayerId() const { return ReplicatedPlayerId; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// ════════════════════════════════════════════════════════════════
	// 인벤토리 컴포넌트 (Stash + Loadout)
	// ════════════════════════════════════════════════════════════════

	/**
	 * Stash 인벤토리 — 전체 보유 아이템
	 * SQLite player_stash에서 로드된 데이터가 여기에 복원됨
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "로비|인벤토리",
		meta = (DisplayName = "창고(Stash) 인벤토리 컴포넌트"))
	TObjectPtr<UInv_InventoryComponent> StashInventoryComponent;

	/**
	 * Loadout 인벤토리 — 출격할 아이템
	 * 처음에는 비어있으며, 플레이어가 Stash에서 이동
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "로비|인벤토리",
		meta = (DisplayName = "출격장비(Loadout) 인벤토리 컴포넌트"))
	TObjectPtr<UInv_InventoryComponent> LoadoutInventoryComponent;

	// ════════════════════════════════════════════════════════════════
	// 로비 UI 위젯
	// ════════════════════════════════════════════════════════════════

	/** 로비 메인 위젯 클래스 (BP에서 WBP_HellunaLobbyStashWidget 지정) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "로비|UI",
		meta = (DisplayName = "로비 창고 위젯 클래스"))
	TSubclassOf<UHellunaLobbyStashWidget> LobbyStashWidgetClass;

	/** 현재 생성된 로비 위젯 인스턴스 */
	UPROPERTY()
	TObjectPtr<UHellunaLobbyStashWidget> LobbyStashWidgetInstance;

	// ════════════════════════════════════════════════════════════════
	// 출격 설정
	// ════════════════════════════════════════════════════════════════

	/** 출격 시 이동할 게임 맵 URL (BP에서 설정, Phase 12e 이후 폴백용) */
	UPROPERTY(EditDefaultsOnly, Category = "로비|출격",
		meta = (DisplayName = "게임 맵 URL (Fallback)", Tooltip = "Phase 12 이전 호환용. 채널 시스템 사용 시 무시됩니다."))
	FString DeployMapURL;

	// ════════════════════════════════════════════════════════════════
	// [Phase 12d] 파티 위젯 설정
	// ════════════════════════════════════════════════════════════════

	/** 파티 팝업 위젯 클래스 (BP에서 WBP_HellunaPartyWidget 지정) */
	UPROPERTY(EditDefaultsOnly, Category = "로비|파티",
		meta = (DisplayName = "Party Widget Class (파티 위젯 클래스)"))
	TSubclassOf<UHellunaPartyWidget> PartyWidgetClass;

	/** 현재 생성된 파티 위젯 인스턴스 */
	UPROPERTY()
	TObjectPtr<UHellunaPartyWidget> PartyWidgetInstance;

	// ════════════════════════════════════════════════════════════════
	// [Phase 13] 로비 로그인 위젯 설정
	// ════════════════════════════════════════════════════════════════

	/** 로비 로그인 위젯 클래스 (BP에서 WBP_LobbyLoginWidget 지정) */
	UPROPERTY(EditDefaultsOnly, Category = "로비|로그인",
		meta = (DisplayName = "Lobby Login Widget Class (로비 로그인 위젯 클래스)"))
	TSubclassOf<UHellunaLobbyLoginWidget> LobbyLoginWidgetClass;

	/** [Phase 14f] 재참가 위젯 클래스 (BP에서 WBP_RejoinWidget 지정) */
	UPROPERTY(EditDefaultsOnly, Category = "로비|재참가",
		meta = (DisplayName = "Rejoin Widget Class (재참가 위젯 클래스)"))
	TSubclassOf<UHellunaRejoinWidget> RejoinWidgetClass;

	/** 현재 생성된 재참가 위젯 인스턴스 */
	UPROPERTY()
	TObjectPtr<UHellunaRejoinWidget> RejoinWidgetInstance;

	/** 현재 생성된 로그인 위젯 인스턴스 */
	UPROPERTY()
	TObjectPtr<UHellunaLobbyLoginWidget> LobbyLoginWidgetInstance;

	/** 로그인 완료 여부 */
	bool bIsLoggedIn = false;

	// ════════════════════════════════════════════════════════════════
	// 캐릭터 프리뷰 V2 시스템 (LoginController에서 복사)
	// ════════════════════════════════════════════════════════════════

	/** V2 씬 액터 클래스 (BP에서 세팅) */
	UPROPERTY(EditDefaultsOnly, Category = "로비|캐릭터프리뷰V2",
		meta = (DisplayName = "V2 씬 액터 클래스"))
	TSubclassOf<AHellunaCharacterSelectSceneV2> PreviewSceneV2Class;

	/** 캐릭터 타입별 SkeletalMesh 매핑 */
	UPROPERTY(EditDefaultsOnly, Category = "로비|캐릭터프리뷰V2",
		meta = (DisplayName = "프리뷰 메시 맵"))
	TMap<EHellunaHeroType, TSoftObjectPtr<USkeletalMesh>> PreviewMeshMap;

	/** 캐릭터 타입별 프리뷰 AnimInstance 클래스 매핑 */
	UPROPERTY(EditDefaultsOnly, Category = "로비|캐릭터프리뷰V2",
		meta = (DisplayName = "프리뷰 AnimClass 맵"))
	TMap<EHellunaHeroType, TSubclassOf<UAnimInstance>> PreviewAnimClassMap;

	/** V2 씬 스폰 위치 (월드 지하) */
	UPROPERTY(EditDefaultsOnly, Category = "로비|캐릭터프리뷰V2",
		meta = (DisplayName = "V2 프리뷰 스폰 위치"))
	FVector PreviewSpawnBaseLocation = FVector(0.f, 0.f, -5000.f);

	/** 스폰된 V2 씬 액터 (런타임) */
	UPROPERTY()
	TObjectPtr<AHellunaCharacterSelectSceneV2> SpawnedPreviewSceneV2;

	/** 로비 카메라 (직접 뷰포트 렌더링용) */
	UPROPERTY()
	TObjectPtr<ACameraActor> LobbyCamera;

	// ════════════════════════════════════════════════════════════════
	// Level Streaming 배경 (탭별 서브레벨)
	// ════════════════════════════════════════════════════════════════

	/** Play 탭 배경 서브레벨 이름 (예: /Game/Maps/Lobby/Sub_Play) */
	UPROPERTY(EditDefaultsOnly, Category = "로비|배경",
		meta = (DisplayName = "Play Tab Background Level (Play 탭 배경 레벨)"))
	FName PlayBackgroundLevel;

	/** Character 탭 배경 서브레벨 이름 (예: /Game/Maps/Lobby/Sub_Character) */
	UPROPERTY(EditDefaultsOnly, Category = "로비|배경",
		meta = (DisplayName = "Character Tab Background Level (Character 탭 배경 레벨)"))
	FName CharacterBackgroundLevel;

	/** Play 탭 배경 카메라 설정 */
	UPROPERTY(EditDefaultsOnly, Category = "로비|배경",
		meta = (DisplayName = "Play Tab Camera Transform (Play 탭 카메라 설정)"))
	FLobbyCameraTransform PlayCameraTransform;

	/** Character 탭 배경 카메라 설정 */
	UPROPERTY(EditDefaultsOnly, Category = "로비|배경",
		meta = (DisplayName = "Character Tab Camera Transform (Character 탭 카메라 설정)"))
	FLobbyCameraTransform CharacterCameraTransform = { FVector(-837.4, -488.3, 98.0), FRotator(0.0, -140.0, 0.0), 90.0f };

	/** 현재 로딩된 배경 레벨 이름 */
	FName CurrentLoadedLevel;

	/** 로드 완료 콜백에서 사용할 대기 중인 탭 인덱스 */
	int32 PendingBackgroundTabIndex = -1;

	// ════════════════════════════════════════════════════════════════
	// 카메라 앵커 시스템
	// ════════════════════════════════════════════════════════════════

	/** 앵커 캐시 (Key = TabIndex * 1000 + SlotIndex) */
	UPROPERTY()
	TMap<int32, TObjectPtr<AHellunaLobbyCameraAnchor>> CachedAnchors;

	/** 현재 활성 슬롯 인덱스 */
	int32 CurrentSlotIndex = 0;

	// ════════════════════════════════════════════════════════════════
	// 캐릭터 선택 상태
	// ════════════════════════════════════════════════════════════════

	/** 선택된 히어로 타입 (Replicated — 서버→클라 동기화) */
	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "로비|캐릭터선택",
		meta = (DisplayName = "선택된 히어로 타입"))
	EHellunaHeroType SelectedHeroType = EHellunaHeroType::None;

	/** [Phase 12] 서버에서 설정된 PlayerId (Replicated — Client_ExecutePartyDeploy에서 사용) */
	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "로비|플레이어",
		meta = (DisplayName = "Replicated Player ID"))
	FString ReplicatedPlayerId;

	/** Deploy 중복 실행 방지 플래그 */
	bool bDeployInProgress = false;

public:
	/** [Phase 16] 플레이어가 선택한 맵 키 */
	FString SelectedMapKey;

	/** [Phase 18] 플레이어가 선택한 게임 모드 */
	ELobbyGameMode SelectedGameMode = ELobbyGameMode::Solo;

protected:

	// ════════════════════════════════════════════════════════════════
	// 파괴적 캐스케이드 방지 — DB에서 로드된 Stash 아이템 수 기록
	// ════════════════════════════════════════════════════════════════

	/** DB에서 로드된 원본 Stash 아이템 수 (SaveComponentsToDatabase에서 비교용)
	 * [Fix29-D] 기본값 -1 = 미로드 상태 → PostLogin 완료 전 Logout 시 저장 차단 */
	int32 LoadedStashItemCount = -1;

	/** [Fix36] DB에서 로드된 원본 Loadout 아이템 수 (파괴적 캐스케이드 방지)
	 * 기본값 -1 = 미로드 → Logout 시 저장 차단 */
	int32 LoadedLoadoutItemCount = -1;

public:
	void SetLoadedStashItemCount(int32 Count) { LoadedStashItemCount = Count; }
	int32 GetLoadedStashItemCount() const { return LoadedStashItemCount; }

	void SetLoadedLoadoutItemCount(int32 Count) { LoadedLoadoutItemCount = Count; }
	int32 GetLoadedLoadoutItemCount() const { return LoadedLoadoutItemCount; }

	void SetReplicatedPlayerId(const FString& InPlayerId) { ReplicatedPlayerId = InPlayerId; }

public:
	// ════════════════════════════════════════════════════════════════
	// Level Streaming 제어 (StashWidget에서 탭 전환 시 호출)
	// ════════════════════════════════════════════════════════════════

	/** 배경 레벨 로드 (이전 레벨 자동 언로드) */
	UFUNCTION(BlueprintCallable, Category = "로비|배경",
		meta = (DisplayName = "Load Background Level (배경 레벨 로드)"))
	void LoadBackgroundLevel(FName LevelName);

	/** 배경 레벨 언로드 */
	UFUNCTION(BlueprintCallable, Category = "로비|배경",
		meta = (DisplayName = "Unload Background Level (배경 레벨 언로드)"))
	void UnloadBackgroundLevel(FName LevelName);

	/** 탭 인덱스에 맞는 배경 레벨 로드 (StashWidget에서 호출) */
	UFUNCTION(BlueprintCallable, Category = "로비|배경",
		meta = (DisplayName = "Load Background For Tab (탭별 배경 로드)"))
	void LoadBackgroundForTab(int32 TabIndex);

	/** 같은 탭 내 카메라 슬롯 전환 (스킨/무기 뷰 전환용) */
	UFUNCTION(BlueprintCallable, Category = "로비|카메라",
		meta = (DisplayName = "Switch Camera Slot (카메라 슬롯 전환)"))
	void SwitchCameraSlot(int32 NewSlotIndex);

private:
	// ════════════════════════════════════════════════════════════════
	// V2 프리뷰 내부 함수
	// ════════════════════════════════════════════════════════════════

	/** V2 프리뷰 씬 스폰 (클라이언트 전용) */
	void SpawnPreviewSceneV2();

	/** V2 프리뷰 씬 파괴 */
	void DestroyPreviewSceneV2();

	// ════════════════════════════════════════════════════════════════
	// Level Streaming 콜백 (UFUNCTION 필수 — FLatentActionInfo 리플렉션)
	// ════════════════════════════════════════════════════════════════

	/** 배경 레벨 로드 완료 콜백 */
	UFUNCTION()
	void OnBackgroundLevelLoaded();

	/** 배경 레벨 언로드 완료 콜백 */
	UFUNCTION()
	void OnBackgroundLevelUnloaded();

	// ════════════════════════════════════════════════════════════════
	// 카메라 앵커 시스템
	// ════════════════════════════════════════════════════════════════

	/** 서브레벨의 앵커 액터를 검색하여 캐시 구축 */
	void RebuildAnchorCache();

	/** 앵커 기반 카메라 적용 (성공 시 true, 앵커 없으면 false) */
	bool ApplyCameraFromAnchor(int32 InTabIndex, int32 InSlotIndex);

	/** 프리뷰 씬을 카메라 앞 적절한 위치로 이동 */
	void MovePreviewSceneToCamera(const FVector& CamLocation, const FRotator& CamRotation);

	// ════════════════════════════════════════════════════════════════
	// Per-Interaction Save
	// ════════════════════════════════════════════════════════════════

	/** [Fix35] Per-interaction save: Transfer/Swap 성공 후 Stash+Loadout DB 즉시 저장 */
	void SaveBothComponentsAfterInteraction();

	/** [Fix46-M1] LobbyGameMode + PlayerId 획득 헬퍼 — 10곳 이상의 반복 패턴 통합 */
	AHellunaLobbyGameMode* GetLobbyGameMode() const;

	// ════════════════════════════════════════════════════════════════
	// 내부 전송 로직
	// ════════════════════════════════════════════════════════════════

	/**
	 * 실제 아이템 전송 처리 (서버에서만 실행)
	 *
	 * @param SourceComp     원본 InvComp (아이템 출처)
	 * @param TargetComp     대상 InvComp (아이템 목적지?h 
	 * @param ItemEntryIndex 전송할 아이템의 Entry 인덱스
	 * @return 전송 성공 여부
	 */
	bool ExecuteTransfer(UInv_InventoryComponent* SourceComp, UInv_InventoryComponent* TargetComp, int32 ItemEntryIndex, int32 TargetGridIndex = INDEX_NONE);
};
