// ════════════════════════════════════════════════════════════════════════════════
// HellunaLobbyController.h
// ════════════════════════════════════════════════════════════════════════════════
//
// 로비 전용 PlayerController — StashComp + LoadoutComp 듀얼 인벤토리 관리
//
// 📌 상속 구조:
//    APlayerController → AInv_PlayerController → AHellunaLobbyController
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
#include "Player/Inv_PlayerController.h"
#include "HellunaLobbyController.generated.h"

// 전방 선언
class UInv_InventoryComponent;
class UInv_InventoryItem;
class UHellunaLobbyStashWidget;

// ════════════════════════════════════════════════════════════════════════════════
// 전송 방향 열거형
// ════════════════════════════════════════════════════════════════════════════════
UENUM(BlueprintType)
enum class ELobbyTransferDirection : uint8
{
	StashToLoadout  UMETA(DisplayName = "Stash → Loadout"),
	LoadoutToStash  UMETA(DisplayName = "Loadout → Stash"),
};

UCLASS()
class HELLUNA_API AHellunaLobbyController : public AInv_PlayerController
{
	GENERATED_BODY()

public:
	AHellunaLobbyController();

	// ════════════════════════════════════════════════════════════════
	// 컴포넌트 Getter
	// ════════════════════════════════════════════════════════════════

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "로비|인벤토리",
		meta = (DisplayName = "Stash 컴포넌트 가져오기"))
	UInv_InventoryComponent* GetStashComponent() const { return StashInventoryComponent; }

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "로비|인벤토리",
		meta = (DisplayName = "Loadout 컴포넌트 가져오기"))
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
	void Server_TransferItem(int32 ItemEntryIndex, ELobbyTransferDirection Direction);

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

protected:
	virtual void BeginPlay() override;

	// ════════════════════════════════════════════════════════════════
	// 인벤토리 컴포넌트 (Stash + Loadout)
	// ════════════════════════════════════════════════════════════════

	/**
	 * Stash 인벤토리 — 전체 보유 아이템
	 * SQLite player_stash에서 로드된 데이터가 여기에 복원됨
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "로비|인벤토리",
		meta = (DisplayName = "Stash 인벤토리 컴포넌트 (창고)"))
	TObjectPtr<UInv_InventoryComponent> StashInventoryComponent;

	/**
	 * Loadout 인벤토리 — 출격할 아이템
	 * 처음에는 비어있으며, 플레이어가 Stash에서 이동
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "로비|인벤토리",
		meta = (DisplayName = "Loadout 인벤토리 컴포넌트 (출격장비)"))
	TObjectPtr<UInv_InventoryComponent> LoadoutInventoryComponent;

	// ════════════════════════════════════════════════════════════════
	// 로비 UI 위젯
	// ════════════════════════════════════════════════════════════════

	/** 로비 메인 위젯 클래스 (BP에서 WBP_HellunaLobbyStashWidget 지정) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "로비|UI",
		meta = (DisplayName = "로비 Stash 위젯 클래스"))
	TSubclassOf<UHellunaLobbyStashWidget> LobbyStashWidgetClass;

	/** 현재 생성된 로비 위젯 인스턴스 */
	UPROPERTY()
	TObjectPtr<UHellunaLobbyStashWidget> LobbyStashWidgetInstance;

	// ════════════════════════════════════════════════════════════════
	// 출격 설정
	// ════════════════════════════════════════════════════════════════

	/** 출격 시 이동할 게임 맵 URL (BP에서 설정) */
	UPROPERTY(EditDefaultsOnly, Category = "로비|출격",
		meta = (DisplayName = "게임 맵 URL", Tooltip = "출격 시 ClientTravel로 이동할 맵 URL입니다. 예: /Game/Maps/L_Defense?listen"))
	FString DeployMapURL;

private:
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
	bool ExecuteTransfer(UInv_InventoryComponent* SourceComp, UInv_InventoryComponent* TargetComp, int32 ItemEntryIndex);
};
