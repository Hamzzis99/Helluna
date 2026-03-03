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
#include "HellunaLobbyController.generated.h"

// 전방 선언
class UInv_InventoryComponent;
class UInv_InventoryItem;
class UHellunaLobbyStashWidget;
class AHellunaCharacterSelectSceneV2;
class ACameraActor;
class USkeletalMesh;
class UHellunaLobbyCharSelectWidget;

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

	/** 출격 시 이동할 게임 맵 URL (BP에서 설정) */
	UPROPERTY(EditDefaultsOnly, Category = "로비|출격",
		meta = (DisplayName = "게임 맵 URL", Tooltip = "출격 시 ClientTravel로 이동할 맵의 URL입니다. 예: /Game/Maps/L_Defense?listen"))
	FString DeployMapURL;

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

	/** 현재 로딩된 배경 레벨 이름 */
	FName CurrentLoadedLevel;

	// ════════════════════════════════════════════════════════════════
	// 캐릭터 선택 상태
	// ════════════════════════════════════════════════════════════════

	/** 선택된 히어로 타입 (Replicated — 서버→클라 동기화) */
	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "로비|캐릭터선택",
		meta = (DisplayName = "선택된 히어로 타입"))
	EHellunaHeroType SelectedHeroType = EHellunaHeroType::None;

	/** Deploy 중복 실행 방지 플래그 */
	bool bDeployInProgress = false;

	// ════════════════════════════════════════════════════════════════
	// 파괴적 캐스케이드 방지 — DB에서 로드된 Stash 아이템 수 기록
	// ════════════════════════════════════════════════════════════════

	/** DB에서 로드된 원본 Stash 아이템 수 (SaveComponentsToDatabase에서 비교용)
	 * [Fix29-D] 기본값 -1 = 미로드 상태 → PostLogin 완료 전 Logout 시 저장 차단 */
	int32 LoadedStashItemCount = -1;

public:
	void SetLoadedStashItemCount(int32 Count) { LoadedStashItemCount = Count; }
	int32 GetLoadedStashItemCount() const { return LoadedStashItemCount; }

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
	// Per-Interaction Save
	// ════════════════════════════════════════════════════════════════

	/** [Fix35] Per-interaction save: Transfer/Swap 성공 후 Stash+Loadout DB 즉시 저장 */
	void SaveBothComponentsAfterInteraction();

	// ════════════════════════════════════════════════════════════════
	// 내부 전송 로직
	// ════════════════════════════════════════════════════════════════

	/**
	 * 실제 아이템 전송 처리 (서버에서만 실행)
	 *
	 * @param SourceComp     원본 InvComp (아이템 출처)
	 * @param TargetComp     대상 InvComp (아이템 목적지)
	 * @param ItemEntryIndex 전송할 아이템의 Entry 인덱스
	 * @return 전송 성공 여부
	 */
	bool ExecuteTransfer(UInv_InventoryComponent* SourceComp, UInv_InventoryComponent* TargetComp, int32 ItemEntryIndex, int32 TargetGridIndex = INDEX_NONE);
};
