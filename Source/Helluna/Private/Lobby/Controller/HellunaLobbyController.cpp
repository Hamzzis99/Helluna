// ════════════════════════════════════════════════════════════════════════════════
// HellunaLobbyController.cpp
// ════════════════════════════════════════════════════════════════════════════════
//
// ============================================================================
// 📌 로비 전용 PlayerController (APlayerController 직접 상속 — 경량화)
// ============================================================================
//
// 📌 역할:
//   - StashComp / LoadoutComp 2개 InventoryComponent 관리
//   - Server_TransferItem: 클라→서버 RPC, Stash↔Loadout 아이템 이동
//   - Server_Deploy: 클라→서버 RPC, 출격 (SQLite 저장 + ClientTravel)
//   - Client_ExecuteDeploy: 서버→클라 RPC, 클라이언트 맵 이동
//   - Client_ShowLobbyUI: 서버→클라 RPC, 로비 위젯 생성 지시
//
// 📌 네트워크 아키텍처:
//   - 모든 Server RPC는 이 Controller에서 선언 (클라이언트의 NetConnection 소유)
//   - GameState/GameMode에서 Server RPC 선언하면 클라에서 호출 불가!
//     (GameState에는 클라이언트 NetConnection이 없기 때문 — MEMORY.md 참조)
//   - 아이템 전송 로직은 서버에서만 실행 (HasAuthority 체크)
//
// 📌 InvComp 듀얼 구조:
//   StashComp ← SQLite player_stash에서 로드 (PostLogin 시 GameMode가 복원)
//   LoadoutComp ← 빈 상태로 시작, 플레이어가 Stash에서 아이템을 옮김
//
// 📌 출격(Deploy) 흐름:
//   1) 클라: Button_Deploy 클릭 → Server_Deploy() RPC
//   2) 서버: StashComp → SQLite SavePlayerStash (잔여 아이템)
//   3) 서버: LoadoutComp → SQLite SavePlayerLoadout (출격 장비)
//   4) 서버: Client_ExecuteDeploy(TravelURL) → 클라에 맵 이동 지시
//   5) 클라: ClientTravel(TravelURL) → 게임 맵으로 이동
//
// 작성자: Gihyeon (Claude Code 보조)
// ════════════════════════════════════════════════════════════════════════════════

#include "Lobby/Controller/HellunaLobbyController.h"
#include "Lobby/GameMode/HellunaLobbyGameMode.h"
#include "Lobby/ServerManager/HellunaGameServerManager.h"
#include "Lobby/Widget/HellunaLobbyStashWidget.h"
#include "Lobby/Widget/HellunaLobbyLoginWidget.h"
#include "Lobby/Widget/HellunaPartyWidget.h"
#include "Lobby/Widget/HellunaLobbyCharSelectWidget.h"
#include "Lobby/Widget/HellunaRejoinWidget.h"
#include "Lobby/Database/HellunaSQLiteSubsystem.h"
#include "Login/Preview/HellunaCharacterSelectSceneV2.h"
#include "Lobby/Camera/HellunaLobbyCameraAnchor.h"
#include "InventoryManagement/Components/Inv_InventoryComponent.h"
#include "Blueprint/UserWidget.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "MDF_Function/MDF_Instance/MDF_GameInstance.h"

// 로그 카테고리 (공유 헤더 — DEFINE은 HellunaLobbyGameMode.cpp)
#include "Lobby/HellunaLobbyLog.h"

// ════════════════════════════════════════════════════════════════════════════════
// [Fix46-M1] GetLobbyGameMode — LobbyGameMode 안전 획득 헬퍼
// ════════════════════════════════════════════════════════════════════════════════
AHellunaLobbyGameMode* AHellunaLobbyController::GetLobbyGameMode() const
{
	UWorld* World = GetWorld();
	return World ? World->GetAuthGameMode<AHellunaLobbyGameMode>() : nullptr;
}

// ════════════════════════════════════════════════════════════════════════════════
// 생성자 — StashComp + LoadoutComp 생성
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 CreateDefaultSubobject는 CDO(Class Default Object) 생성 시 실행됨
//    → 런타임에 스폰되는 모든 AHellunaLobbyController 인스턴스에 자동으로 붙음
//    → 서버/클라 모두에서 생성됨 (리플리케이션으로 동기화)
//
// 📌 두 컴포넌트의 역할:
//   StashInventoryComponent: 플레이어가 보유한 전체 아이템 (창고)
//   LoadoutInventoryComponent: 출격 시 가져갈 아이템 (출격장비)
//
// ════════════════════════════════════════════════════════════════════════════════
AHellunaLobbyController::AHellunaLobbyController()
{
	// ── StashComp 생성 (전체 보유 아이템 = 창고) ──
	// PostLogin에서 GameMode가 SQLite → StashComp로 데이터 복원
	StashInventoryComponent = CreateDefaultSubobject<UInv_InventoryComponent>(TEXT("StashInventoryComponent"));

	// ── LoadoutComp 생성 (출격할 아이템 = 출격장비) ──
	// 처음에는 비어있음 → 플레이어가 Stash에서 Transfer로 채움
	LoadoutInventoryComponent = CreateDefaultSubobject<UInv_InventoryComponent>(TEXT("LoadoutInventoryComponent"));

	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] ========================================"));
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] 생성자: StashComp=%s, LoadoutComp=%s"),
		StashInventoryComponent ? TEXT("생성됨") : TEXT("실패"),
		LoadoutInventoryComponent ? TEXT("생성됨") : TEXT("실패"));
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] ========================================"));
}

// ════════════════════════════════════════════════════════════════════════════════
// BeginPlay — 로비 진입 시 초기화
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 서버+클라 모두 호출됨
// 📌 [Phase 13] UI 표시는 서버가 제어:
//    bDebugSkipLogin=true → PostLogin에서 바로 초기화 + Client_ShowLobbyUI
//    bDebugSkipLogin=false → PostLogin에서 Client_ShowLobbyLoginUI → 로그인 후 초기화
//
// ════════════════════════════════════════════════════════════════════════════════
void AHellunaLobbyController::BeginPlay()
{
	Super::BeginPlay();

	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] ──────────────────────────────────────"));
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] BeginPlay | IsLocalController=%s | HasAuthority=%s | NetMode=%d"),
		IsLocalController() ? TEXT("true") : TEXT("false"),
		HasAuthority() ? TEXT("true") : TEXT("false"),
		GetWorld() ? (int32)GetWorld()->GetNetMode() : -1);
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC]   StashComp=%s | LoadoutComp=%s"),
		StashInventoryComponent ? TEXT("O") : TEXT("X"),
		LoadoutInventoryComponent ? TEXT("O") : TEXT("X"));
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC]   LobbyStashWidgetClass=%s"),
		LobbyStashWidgetClass ? TEXT("설정됨") : TEXT("미설정 (BP에서 지정 필요!)"));
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC]   DeployMapURL=%s"),
		DeployMapURL.IsEmpty() ? TEXT("(비어있음 — BP에서 설정 필요!)") : *DeployMapURL);
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] ──────────────────────────────────────"));

	// [Phase 13] UI 표시는 서버 PostLogin에서 RPC로 제어
	// bDebugSkipLogin=true → 서버가 0.5초 후 Client_ShowLobbyUI 호출
	// bDebugSkipLogin=false → 서버가 Client_ShowLobbyLoginUI 호출 → 로그인 성공 후 Client_ShowLobbyUI
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] [Phase 13] UI 표시는 서버 PostLogin에서 RPC로 제어"));
}

// ════════════════════════════════════════════════════════════════════════════════
// Server_TransferItem — Stash ↔ Loadout 아이템 전송 (Server RPC)
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 호출 경로:
//   클라이언트: UI 버튼 → TransferItemToLoadout/ToStash → Server_TransferItem()
//   서버: _Implementation 실행 → ExecuteTransfer → TransferItemTo
//
// 📌 Validate:
//   EntryIndex >= 0 기본 검증 (음수 인덱스 차단)
//   추후 속도 제한(Rate Limiting)이나 추가 검증 가능
//
// 📌 Direction:
//   StashToLoadout: Source=StashComp, Target=LoadoutComp (창고→출격장비)
//   LoadoutToStash: Source=LoadoutComp, Target=StashComp (출격장비→창고)
//
// TODO: [DragDrop] 추후 드래그앤드롭 크로스 패널 구현 시 여기에 연결
// ════════════════════════════════════════════════════════════════════════════════
bool AHellunaLobbyController::Server_TransferItem_Validate(int32 ItemEntryIndex, ELobbyTransferDirection Direction, int32 TargetGridIndex)
{
	// [Fix29-B] Deploy 진행 중에는 Transfer 차단 — Deploy의 Stash/Loadout 저장과 동시 수정 시 아이템 복제 위험
	// [Fix31] TargetGridIndex 검증: INDEX_NONE(자동 배치) 또는 유효 범위
	// [Fix46-M3] 공통 상수 사용
	return !bDeployInProgress
		&& ItemEntryIndex >= 0 && ItemEntryIndex < LobbyValidation::MaxInventoryIndex
		&& (Direction == ELobbyTransferDirection::StashToLoadout || Direction == ELobbyTransferDirection::LoadoutToStash)
		&& (TargetGridIndex == INDEX_NONE || (TargetGridIndex >= 0 && TargetGridIndex < LobbyValidation::MaxInventoryIndex));
}

void AHellunaLobbyController::Server_TransferItem_Implementation(int32 ItemEntryIndex, ELobbyTransferDirection Direction, int32 TargetGridIndex)
{
	const FString DirectionStr = (Direction == ELobbyTransferDirection::StashToLoadout) ? TEXT("Stash→Loadout") : TEXT("Loadout→Stash");
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] ── Server_TransferItem 시작 ──"));
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC]   RepID=%d | Direction=%s | TargetGridIndex=%d"), ItemEntryIndex, *DirectionStr, TargetGridIndex);

	// ── Source/Target 결정 ──
	UInv_InventoryComponent* SourceComp = nullptr;
	UInv_InventoryComponent* TargetComp = nullptr;

	switch (Direction)
	{
	case ELobbyTransferDirection::StashToLoadout:
		SourceComp = StashInventoryComponent;
		TargetComp = LoadoutInventoryComponent;
		break;
	case ELobbyTransferDirection::LoadoutToStash:
		SourceComp = LoadoutInventoryComponent;
		TargetComp = StashInventoryComponent;
		break;
	}

	if (!SourceComp || !TargetComp)
	{
		UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyPC] Server_TransferItem: 컴포넌트 nullptr!"));
		UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyPC]   SourceComp=%s, TargetComp=%s"),
			SourceComp ? TEXT("O") : TEXT("X"), TargetComp ? TEXT("O") : TEXT("X"));
		return;
	}

	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC]   Source=%s → Target=%s"),
		*SourceComp->GetName(), *TargetComp->GetName());

	// ── ExecuteTransfer 실행 (ReplicationID → 실제 인덱스 변환 후 TransferItemTo 호출) ──
	// [Fix31] TargetGridIndex를 ExecuteTransfer → TransferItemTo까지 전달
	const bool bSuccess = ExecuteTransfer(SourceComp, TargetComp, ItemEntryIndex, TargetGridIndex);
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] Server_TransferItem %s | RepID=%d | Direction=%s | TargetGridIndex=%d"),
		bSuccess ? TEXT("성공") : TEXT("실패"), ItemEntryIndex, *DirectionStr, TargetGridIndex);

	// [Fix35] Per-interaction save
	if (bSuccess)
	{
		SaveBothComponentsAfterInteraction();
	}
}

// ════════════════════════════════════════════════════════════════════════════════
// ExecuteTransfer — 실제 아이템 전송 (서버 전용, 내부 헬퍼)
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 왜 TransferItemTo()에 위임하는가?
//   FInv_InventoryFastArray는 USTRUCT이며 INVENTORY_API가 없음
//   → Entries, Item 등 private 멤버에 Helluna 모듈에서 직접 접근 불가 (LNK2019 에러)
//   → UInv_InventoryComponent는 INVENTORY_API UCLASS이므로 friend 접근 가능
//   → SourceComp->TransferItemTo()에 위임하면 플러그인 내부에서 FastArray를 조작
//
// 📌 TransferItemTo 내부 동작:
//   1) InventoryList.Entries를 순회하여 유효한 아이템 목록 구축
//   2) ItemIndex번째 아이템의 Manifest 복사
//   3) TargetComp->AddItemFromManifest()로 대상에 추가
//   4) InventoryList.RemoveEntry()로 원본에서 제거
//   5) FastArray Mark/Dirty → 리플리케이션 트리거
//
// ════════════════════════════════════════════════════════════════════════════════
bool AHellunaLobbyController::ExecuteTransfer(
	UInv_InventoryComponent* SourceComp,
	UInv_InventoryComponent* TargetComp,
	int32 ItemReplicationID,
	int32 TargetGridIndex)
{
	if (!SourceComp || !TargetComp)
	{
		UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyPC] ExecuteTransfer: nullptr! | Source=%s, Target=%s"),
			SourceComp ? TEXT("O") : TEXT("X"), TargetComp ? TEXT("O") : TEXT("X"));
		return false;
	}

	// ⭐ [Phase 4 Fix] ReplicationID → ValidItems 배열 인덱스 변환
	// 클라이언트가 보내는 값은 FastArray Entry의 ReplicationID (배열 밀림에 안전)
	// TransferItemTo()는 ValidItems 배열 인덱스를 기대하므로 여기서 변환
	const int32 ActualIndex = SourceComp->FindValidItemIndexByReplicationID(ItemReplicationID);
	if (ActualIndex == INDEX_NONE)
	{
		UE_LOG(LogHellunaLobby, Warning, TEXT("[LobbyPC] ExecuteTransfer: RepID=%d에 해당하는 아이템 미발견 | Source=%s"),
			ItemReplicationID, *SourceComp->GetName());
		return false;
	}

	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] ExecuteTransfer: %s RepID=%d → ValidIndex=%d → %s"),
		*SourceComp->GetName(), ItemReplicationID, ActualIndex, *TargetComp->GetName());

	// TransferItemTo: 플러그인 내부에서 FastArray 조작 (INVENTORY_API 경계 내)
	// ⭐ [Phase 8 Fix] TransferItemTo 내부에서 HasRoomInInventoryList 체크 포함
	// [Fix31] TargetGridIndex 전달 → 새 Entry에 GridIndex 설정 후 리플리케이션
	const bool bResult = SourceComp->TransferItemTo(ActualIndex, TargetComp, TargetGridIndex);

	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] ExecuteTransfer 결과: %s"), bResult ? TEXT("성공") : TEXT("실패"));
	return bResult;
}

// ════════════════════════════════════════════════════════════════════════════════
// Server_SwapTransferItem — 크로스 Grid 아이템 Swap (Server RPC)
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 호출 경로:
//   클라이언트: TryCrossGridSwap → 델리게이트 → StashWidget → Server_SwapTransferItem()
//   서버: _Implementation 실행 → 양쪽 아이템을 찾아 교차 전송
//
// 📌 Swap 로직:
//   1) RepID_A → CompA에서 아이템A 찾기
//   2) RepID_B → CompB에서 아이템B 찾기
//   3) A: CompA→CompB 전송 (TransferItemTo)
//   4) B: CompB→CompA 전송 (TransferItemTo)
//   ⚠️ 첫 번째 전송 후 인덱스 시프트 → 두 번째는 RepID 재검색 필수
//
// ════════════════════════════════════════════════════════════════════════════════
bool AHellunaLobbyController::Server_SwapTransferItem_Validate(int32 RepID_A, int32 RepID_B, int32 TargetGridIndex)
{
	// [Fix28] RepID_A == RepID_B 허용: StashComp과 LoadoutComp은 별도 FastArray 카운터 사용
	// → 다른 Comp의 아이템이 같은 숫자 RepID를 가질 수 있음
	// 자기 자신과의 Swap은 Implementation에서 CompA != CompB + 실제 아이템 검증으로 방지
	// [Fix29-B] Deploy 진행 중에는 Swap 차단
	// [Fix31] TargetGridIndex 검증
	// [Fix46-M3] 공통 상수 사용 (100000→10000 통일)
	return !bDeployInProgress
		&& RepID_A >= 0 && RepID_A < LobbyValidation::MaxInventoryIndex && RepID_B >= 0 && RepID_B < LobbyValidation::MaxInventoryIndex
		&& (TargetGridIndex == INDEX_NONE || (TargetGridIndex >= 0 && TargetGridIndex < LobbyValidation::MaxInventoryIndex));
}

void AHellunaLobbyController::Server_SwapTransferItem_Implementation(int32 RepID_A, int32 RepID_B, int32 TargetGridIndex)
{
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] ── Server_SwapTransferItem 시작 ── RepID_A=%d ↔ RepID_B=%d | TargetGridIndex=%d"), RepID_A, RepID_B, TargetGridIndex);

	if (!StashInventoryComponent || !LoadoutInventoryComponent)
	{
		UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyPC] SwapTransfer: InvComp nullptr!"));
		return;
	}

	// ── 아이템A가 어느 Comp에 있는지 탐색 ──
	UInv_InventoryComponent* CompA = nullptr;
	UInv_InventoryComponent* CompB = nullptr;

	if (StashInventoryComponent->FindValidItemIndexByReplicationID(RepID_A) != INDEX_NONE)
	{
		CompA = StashInventoryComponent;
	}
	else if (LoadoutInventoryComponent->FindValidItemIndexByReplicationID(RepID_A) != INDEX_NONE)
	{
		CompA = LoadoutInventoryComponent;
	}

	if (!CompA)
	{
		UE_LOG(LogHellunaLobby, Warning, TEXT("[LobbyPC] SwapTransfer: RepID_A=%d 미발견!"), RepID_A);
		return;
	}

	// ── 아이템B는 반대쪽 Comp에 있어야 함 ──
	CompB = (CompA == StashInventoryComponent) ? LoadoutInventoryComponent : StashInventoryComponent;

	if (CompB->FindValidItemIndexByReplicationID(RepID_B) == INDEX_NONE)
	{
		// 같은 Comp에 있을 수도 있으니 폴백 체크
		CompB = CompA;
		if (CompB->FindValidItemIndexByReplicationID(RepID_B) == INDEX_NONE)
		{
			UE_LOG(LogHellunaLobby, Warning, TEXT("[LobbyPC] SwapTransfer: RepID_B=%d 미발견!"), RepID_B);
			return;
		}
	}

	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] SwapTransfer: A(RepID=%d) in %s ↔ B(RepID=%d) in %s"),
		RepID_A, *CompA->GetName(), RepID_B, *CompB->GetName());

	// [Fix28] 같은 Comp의 같은 아이템을 자기 자신과 Swap하는 것 방지
	if (CompA == CompB && RepID_A == RepID_B)
	{
		UE_LOG(LogHellunaLobby, Warning, TEXT("[LobbyPC] SwapTransfer: 같은 Comp의 같은 아이템 → 무시 | RepID=%d"), RepID_A);
		return;
	}

	// ── ValidIndex 변환 ──
	const int32 IndexA = CompA->FindValidItemIndexByReplicationID(RepID_A);
	const int32 IndexB = CompB->FindValidItemIndexByReplicationID(RepID_B);

	if (IndexA == INDEX_NONE || IndexB == INDEX_NONE)
	{
		UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyPC] SwapTransfer: 인덱스 변환 실패! IndexA=%d, IndexB=%d"), IndexA, IndexB);
		return;
	}

	// ── SwapItemWith: 양쪽 제거 후 교차 추가 (HasRoom 우회 + 롤백 지원) ──
	// [Fix31] TargetGridIndex 전달 → ItemA(HoverItem)가 갈 위치를 명시적으로 지정
	const bool bSuccess = CompA->SwapItemWith(IndexA, CompB, IndexB, TargetGridIndex);

	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] ── Server_SwapTransferItem %s ── RepID_A=%d ↔ RepID_B=%d | TargetGridIndex=%d"),
		bSuccess ? TEXT("완료") : TEXT("실패"), RepID_A, RepID_B, TargetGridIndex);

	// [Fix35] Per-interaction save
	if (bSuccess)
	{
		SaveBothComponentsAfterInteraction();
	}
}

// ════════════════════════════════════════════════════════════════════════════════
// [Fix35] SaveBothComponentsAfterInteraction — Transfer/Swap 성공 후 즉시 DB 저장
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 목적:
//   타르코프 방식 per-interaction save — 매 Transfer/Swap 성공 후 Stash+Loadout을
//   SQLite에 즉시 저장하여 로비 서버 크래시 시 아이템 유실 방지
//
// 📌 저장 순서:
//   Loadout 먼저 → Stash 나중 (Fix29-C 크래시 복구 순서 준수)
//   → 크래시 시 Loadout이 DB에 보존되므로 다음 PostLogin에서 복구 가능
//
// 📌 Best-effort:
//   저장 실패 시 로그만 출력, RPC 결과에 영향 없음
//   (Logout 시 최종 저장이 있으므로 per-interaction은 추가 안전장치)
//
// ════════════════════════════════════════════════════════════════════════════════
void AHellunaLobbyController::SaveBothComponentsAfterInteraction()
{
	// ── DB 서브시스템 획득 (Deploy 흐름과 동일 패턴) ──
	UGameInstance* GI = GetGameInstance();
	UHellunaSQLiteSubsystem* DB = GI ? GI->GetSubsystem<UHellunaSQLiteSubsystem>() : nullptr;
	// [Fix41] ReleaseDatabaseConnection 후 DB가 닫혀있을 수 있으므로 재오픈 시도
	if (DB && !DB->IsDatabaseReady())
	{
		DB->TryReopenDatabase();
	}
	if (!DB || !DB->IsDatabaseReady())
	{
		UE_LOG(LogHellunaLobby, Warning, TEXT("[LobbyPC] [Fix35] SaveAfterInteraction: DB 미준비 → 스킵"));
		return;
	}

	// ── PlayerId 획득 ── [Fix46-M1] 헬퍼 사용
	AHellunaLobbyGameMode* LobbyGM = GetLobbyGameMode();
	if (!LobbyGM)
	{
		UE_LOG(LogHellunaLobby, Warning, TEXT("[LobbyPC] [Fix35] SaveAfterInteraction: GameMode 캐스팅 실패 → 스킵"));
		return;
	}

	const FString PlayerId = LobbyGM->GetLobbyPlayerId(this);
	if (PlayerId.IsEmpty())
	{
		UE_LOG(LogHellunaLobby, Warning, TEXT("[LobbyPC] [Fix35] SaveAfterInteraction: PlayerId 비어있음 → 스킵"));
		return;
	}

	// ── Loadout 먼저 저장 (Fix29-C 크래시 복구 순서) ──
	if (LoadoutInventoryComponent)
	{
		TArray<FInv_SavedItemData> LoadoutItems = LoadoutInventoryComponent->CollectInventoryDataForSave();
		if (LoadoutItems.Num() > 0)
		{
			const bool bLoadoutOk = DB->SavePlayerLoadout(PlayerId, LoadoutItems);
			UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] [Fix35] SavePlayerLoadout %s | %d개 | PlayerId=%s"),
				bLoadoutOk ? TEXT("성공") : TEXT("실패"), LoadoutItems.Num(), *PlayerId);

			// 장착 상태 동기화 (per-interaction)
			TArray<FHellunaEquipmentSlotData> EquipSlots;
			for (const FInv_SavedItemData& Item : LoadoutItems)
			{
				if (Item.bEquipped && Item.WeaponSlotIndex >= 0)
				{
					FHellunaEquipmentSlotData Slot;
					Slot.SlotId = FString::Printf(TEXT("weapon_%d"), Item.WeaponSlotIndex);
					Slot.ItemType = Item.ItemType;
					EquipSlots.Add(Slot);
				}
			}
			// [Fix44-C4] Equipment 저장 반환값 검증
			const bool bEquipOk = DB->SavePlayerEquipment(PlayerId, EquipSlots);
			if (!bEquipOk)
			{
				UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyPC] [Fix44] SavePlayerEquipment 실패! Loadout/Equipment 불일치 가능 | PlayerId=%s"), *PlayerId);
			}
		}
		else
		{
			// Loadout이 비어있으면 기존 player_loadout 삭제 (빈 행 정리, Fix36: 크래시 감지와 무관)
			// [Fix44-C3] Delete 반환값 검증
			const bool bDelLoadout = DB->DeletePlayerLoadout(PlayerId);
			const bool bDelEquip = DB->DeletePlayerEquipment(PlayerId);
			if (!bDelLoadout || !bDelEquip)
			{
				UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyPC] [Fix44] Delete 실패: Loadout=%s Equipment=%s | PlayerId=%s"),
					bDelLoadout ? TEXT("OK") : TEXT("FAIL"), bDelEquip ? TEXT("OK") : TEXT("FAIL"), *PlayerId);
			}
			UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] [Fix35] Loadout 비어있음 → DeletePlayerLoadout (빈 행 정리) | PlayerId=%s"), *PlayerId);
		}
	}

	// ── Stash 저장 ──
	if (StashInventoryComponent)
	{
		TArray<FInv_SavedItemData> StashItems = StashInventoryComponent->CollectInventoryDataForSave();
		const bool bStashOk = DB->SavePlayerStash(PlayerId, StashItems);
		UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] [Fix35] SavePlayerStash %s | %d개 | PlayerId=%s"),
			bStashOk ? TEXT("성공") : TEXT("실패"), StashItems.Num(), *PlayerId);

		// [Fix44-C2] Stash 저장 실패 시 Loadout과 불일치 경고
		if (!bStashOk)
		{
			UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyPC] [Fix44] SavePlayerStash 실패! Loadout 성공+Stash 실패 → 데이터 불일치 가능 | PlayerId=%s"), *PlayerId);
		}
	}
}

// ════════════════════════════════════════════════════════════════════════════════
// Server_Deploy — 출격 (Server RPC)
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 호출 경로:
//   클라이언트: 출격 버튼 → OnDeployClicked → Server_Deploy()
//   서버: _Implementation 실행
//
// 📌 처리 순서 (서버):
//   1) SQLite 서브시스템 획득
//   2) PlayerId 획득 (GameMode의 GetLobbyPlayerId 래퍼 사용)
//   3) StashComp → SQLite SavePlayerStash (잔여 아이템 저장)
//   4) LoadoutComp → SQLite SavePlayerLoadout (출격 장비 저장)
//   5) Client_ExecuteDeploy(TravelURL) → 클라이언트에 맵 이동 지시
//
// 📌 SavePlayerLoadout의 원자성:
//   SQLite 트랜잭션 내에서 INSERT(loadout) + DELETE(stash 중 출격 항목)을 수행
//   → 중간에 크래시 나도 데이터 불일치 없음
//
// 📌 빈손 출격:
//   LoadoutComp이 비어있어도 출격 자체는 허용
//   (빈손으로 게임에 들어가는 것은 허용, 나중에 정책 변경 가능)
//
// ════════════════════════════════════════════════════════════════════════════════
bool AHellunaLobbyController::Server_Deploy_Validate()
{
	return !bDeployInProgress;
}

void AHellunaLobbyController::Server_Deploy_Implementation()
{
	bDeployInProgress = true;

	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] ══════════════════════════════════════"));
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] Server_Deploy 시작"));
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] ══════════════════════════════════════"));

	// [Fix46-M1/M2] GetLobbyGameMode 헬퍼 사용 (GetWorld() 3회→1회)
	AHellunaLobbyGameMode* LobbyGM = GetLobbyGameMode();

	// ── [Phase 12e] 파티 가입 상태면 솔로 Deploy 차단 ──
	{
		if (LobbyGM)
		{
			const FString CheckId = LobbyGM->GetLobbyPlayerId(this);
			if (LobbyGM->PlayerToPartyMap.Contains(CheckId))
			{
				UE_LOG(LogHellunaLobby, Warning, TEXT("[LobbyPC] Server_Deploy: 파티 가입 상태 — 솔로 Deploy 차단"));
				Client_DeployFailed(TEXT("파티 준비 버튼을 사용하세요"));
				bDeployInProgress = false;
				return;
			}

			// [Phase 15] 매칭 큐 대기 중 솔로 Deploy 차단
			if (LobbyGM->IsPlayerInQueue(CheckId))
			{
				UE_LOG(LogHellunaLobby, Warning, TEXT("[LobbyPC] Server_Deploy: 매칭 큐 대기 중 — 솔로 Deploy 차단"));
				Client_DeployFailed(TEXT("매칭 대기 중입니다. 매칭을 취소 후 시도하세요."));
				bDeployInProgress = false;
				return;
			}
		}
	}

	// ── 캐릭터 미선택 체크 ──
	if (SelectedHeroType == EHellunaHeroType::None)
	{
		UE_LOG(LogHellunaLobby, Warning, TEXT("[LobbyPC] Server_Deploy: 캐릭터가 선택되지 않았습니다! 출격 거부."));
		Client_DeployFailed(TEXT("캐릭터가 선택되지 않았습니다"));
		bDeployInProgress = false;
		return;
	}

	// ── [1단계] SQLite 서브시스템 획득 ──
	UGameInstance* GI = GetGameInstance();
	UHellunaSQLiteSubsystem* DB = GI ? GI->GetSubsystem<UHellunaSQLiteSubsystem>() : nullptr;
	// [Fix41] ReleaseDatabaseConnection 후 DB가 닫혀있을 수 있으므로 재오픈 시도
	if (DB && !DB->IsDatabaseReady())
	{
		UE_LOG(LogHellunaLobby, Warning, TEXT("[LobbyPC] Server_Deploy: DB 미준비 → TryReopenDatabase 시도"));
		DB->TryReopenDatabase();
	}
	if (!DB || !DB->IsDatabaseReady())
	{
		UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyPC] Server_Deploy: SQLite 서브시스템 없음!"));
		UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyPC]   GI=%s, DB=%s, Ready=%s"),
			GI ? TEXT("O") : TEXT("X"),
			DB ? TEXT("O") : TEXT("X"),
			(DB && DB->IsDatabaseReady()) ? TEXT("true") : TEXT("false"));
		Client_DeployFailed(TEXT("SQLite 서브시스템 없음"));
		bDeployInProgress = false;
		return;
	}

	// ── [2단계] PlayerId 획득 ── [Fix46-M1] LobbyGM은 함수 초반에 캐시됨
	FString PlayerId;
	if (LobbyGM)
	{
		PlayerId = LobbyGM->GetLobbyPlayerId(this);
		UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] Deploy: PlayerId 획득 → '%s'"), *PlayerId);
	}
	else
	{
		UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyPC] Deploy: GameMode를 AHellunaLobbyGameMode로 캐스팅 실패!"));
	}

	if (PlayerId.IsEmpty())
	{
		UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyPC] Server_Deploy: PlayerId가 비어있음! 출격 중단"));
		Client_DeployFailed(TEXT("PlayerId가 비어있음"));
		bDeployInProgress = false;
		return;
	}

	// ── [3단계] Loadout → Stash 순서로 저장 ──
	// [Fix29-C] Loadout을 먼저 저장: 크래시 발생 시 크래시 복구(Loadout→Stash)로 아이템 복원 가능
	// 이전: Stash 먼저 → 크래시 → Loadout 미저장 → 아이템 영구 손실
	// 수정: Loadout 먼저 → 크래시 → Stash에서 Loadout 아이템이 빠졌지만 player_loadout에 보존 → 복구 가능
	if (!LoadoutInventoryComponent)
	{
		UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyPC] Deploy: LoadoutComp가 nullptr!"));
		Client_DeployFailed(TEXT("LoadoutComp가 nullptr"));
		bDeployInProgress = false;
		return;
	}

	TArray<FInv_SavedItemData> LoadoutItems = LoadoutInventoryComponent->CollectInventoryDataForSave();
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] Deploy [3]: Loadout 아이템 %d개 수집 완료"), LoadoutItems.Num());

	if (LoadoutItems.Num() > 0)
	{
		// ── [3a] Loadout 저장 (먼저!) ──
		const bool bLoadoutOk = DB->SavePlayerLoadout(PlayerId, LoadoutItems);
		UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] Deploy [3a]: SavePlayerLoadout %s | %d개 아이템"),
			bLoadoutOk ? TEXT("성공") : TEXT("실패"), LoadoutItems.Num());

		if (!bLoadoutOk)
		{
			UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyPC] Deploy: Loadout 저장 실패! 출격 중단"));
			UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyPC]   → DB 오류 확인 필요 (디스크 용량, 권한 등)"));
			Client_DeployFailed(TEXT("Loadout 저장 실패"));
			bDeployInProgress = false;
			return;
		}

		// ── [3a-2] 출격 시점 장착 스냅샷 저장 ──
		{
			TArray<FHellunaEquipmentSlotData> EquipSlots;
			for (const FInv_SavedItemData& Item : LoadoutItems)
			{
				if (Item.bEquipped && Item.WeaponSlotIndex >= 0)
				{
					FHellunaEquipmentSlotData Slot;
					Slot.SlotId = FString::Printf(TEXT("weapon_%d"), Item.WeaponSlotIndex);
					Slot.ItemType = Item.ItemType;
					EquipSlots.Add(Slot);
				}
			}
			DB->SavePlayerEquipment(PlayerId, EquipSlots);
			UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] Deploy [3a-2]: SavePlayerEquipment %d개 슬롯"), EquipSlots.Num());
		}

		// ── [3b] Loadout을 JSON 파일로도 내보내기 (DB 잠금 회피용) ──
		const int32 HeroIndex = HeroTypeToIndex(SelectedHeroType);
		const bool bExportOk = DB->ExportLoadoutToFile(PlayerId, LoadoutItems, HeroIndex);
		UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] Deploy [3b]: ExportLoadoutToFile %s"),
			bExportOk ? TEXT("성공") : TEXT("실패"));

		if (!bExportOk)
		{
			UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyPC] Deploy [3b]: JSON 파일 내보내기 실패! 출격 중단"));
			Client_DeployFailed(TEXT("Loadout 파일 내보내기 실패"));
			bDeployInProgress = false;
			return;
		}
	}
	else
	{
		UE_LOG(LogHellunaLobby, Warning, TEXT("[LobbyPC] Deploy: Loadout이 비어있음! 빈손 출격 진행"));
	}

	// ── [3c] 잔여 Stash 저장 (Loadout 이후) ──
	if (StashInventoryComponent)
	{
		TArray<FInv_SavedItemData> RemainingStash = StashInventoryComponent->CollectInventoryDataForSave();
		const bool bStashOk = DB->SavePlayerStash(PlayerId, RemainingStash);
		UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] Deploy [3c]: 잔여 Stash 저장 %s | %d개 아이템"),
			bStashOk ? TEXT("성공") : TEXT("실패"), RemainingStash.Num());

		if (!bStashOk)
		{
			UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyPC] Deploy [3c]: 잔여 Stash 저장 실패! 출격 중단"));
			Client_DeployFailed(TEXT("Stash 저장 실패"));
			bDeployInProgress = false;
			return;
		}
	}

	// ── [4단계] [Phase 12e+16] 채널 기반 Deploy (빈 채널 자동 배정 + 동적 스폰) ──
	{
		if (LobbyGM)
		{
			// [Phase 16] 맵 키 기반 채널 검색
			const FString DeployMapKey = SelectedMapKey.IsEmpty() ? LobbyGM->DefaultMapKey : SelectedMapKey;
			FGameChannelInfo EmptyChannel;

			if (LobbyGM->FindEmptyChannelForMap(DeployMapKey, EmptyChannel))
			{
				LobbyGM->MarkChannelAsPendingDeploy(EmptyChannel.Port);
				DB->SetPlayerDeployedWithPort(PlayerId, true, EmptyChannel.Port, HeroTypeToIndex(SelectedHeroType));
				UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] Deploy [4]: 채널 배정 | Port=%d | Map=%s"), EmptyChannel.Port, *DeployMapKey);
				Client_ExecutePartyDeploy(EmptyChannel.Port);
			}
			else if (LobbyGM->GameServerManager)
			{
				// [Phase 16] 빈 채널 없음 → 동적 서버 스폰
				const FString MapPath = LobbyGM->GetMapPathByKey(DeployMapKey);
				if (MapPath.IsEmpty())
				{
					UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyPC] Deploy: 맵 경로를 찾을 수 없음 | MapKey=%s"), *DeployMapKey);
					Client_DeployFailed(TEXT("맵 설정 오류"));
					bDeployInProgress = false;
					return;
				}

				const int32 SpawnedPort = LobbyGM->GameServerManager->SpawnGameServer(MapPath);
				if (SpawnedPort < 0)
				{
					UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyPC] Deploy: 서버 스폰 실패"));
					Client_DeployFailed(TEXT("서버 용량 초과 또는 스폰 실패"));
					bDeployInProgress = false;
					return;
				}

				LobbyGM->MarkChannelAsPendingDeploy(SpawnedPort);

				// WaitAndDeploy로 비동기 대기
				FMatchmakingQueueEntry SoloEntry;
				SoloEntry.EntryId = 0;
				SoloEntry.PartyId = 0;
				SoloEntry.PlayerIds.Add(PlayerId);
				SoloEntry.HeroTypes.Add(HeroTypeToIndex(SelectedHeroType));
				SoloEntry.SelectedMapKey = DeployMapKey;

				TArray<FMatchmakingQueueEntry> SoloMatched;
				SoloMatched.Add(MoveTemp(SoloEntry));

				UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] Deploy [4]: 서버 스폰 시작 → WaitAndDeploy | Port=%d"), SpawnedPort);
				LobbyGM->WaitAndDeploy(SpawnedPort, MoveTemp(SoloMatched));
			}
			else
			{
				// 폴백: DeployMapURL 사용
				if (!DeployMapURL.IsEmpty())
				{
					const int32 HeroIndex = HeroTypeToIndex(SelectedHeroType);
					DB->SetPlayerDeployed(PlayerId, true);
					const FString FinalURL = FString::Printf(TEXT("%s?HeroType=%d?PlayerId=%s"),
						*DeployMapURL, HeroIndex, *PlayerId);
					UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] Deploy [4]: DeployMapURL 폴백 | %s"), *FinalURL);
					Client_ExecuteDeploy(FinalURL);
				}
				else
				{
					UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyPC] Deploy: 빈 채널 없음 + 서버매니저 없음 + DeployMapURL도 없음!"));
					Client_DeployFailed(TEXT("빈 채널이 없습니다"));
					bDeployInProgress = false;
					return;
				}
			}
		}
		else
		{
			UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyPC] Deploy [4]: LobbyGameMode 없음!"));
			Client_DeployFailed(TEXT("서버 오류"));
			bDeployInProgress = false;
			return;
		}
	}

	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] Server_Deploy 완료 | PlayerId=%s"), *PlayerId);
}

// ════════════════════════════════════════════════════════════════════════════════
// Client_DeployFailed — 출격 실패 알림 (Client RPC)
// ════════════════════════════════════════════════════════════════════════════════
void AHellunaLobbyController::Client_DeployFailed_Implementation(const FString& Reason)
{
	UE_LOG(LogHellunaLobby, Warning, TEXT("[LobbyPC] 출격 실패: %s"), *Reason);
	bDeployInProgress = false;
	// TODO: UI 알림 표시
}

// ════════════════════════════════════════════════════════════════════════════════
// Client_ExecuteDeploy — 클라이언트에서 맵 이동 (Client RPC)
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 호출 경로: 서버의 Server_Deploy_Implementation → Client_ExecuteDeploy()
// 📌 실행 위치: 클라이언트 (이 Controller를 소유한 클라이언트에서만 실행)
//
// 📌 ClientTravel(URL, TRAVEL_Absolute):
//   - 현재 맵에서 완전히 빠져나와 새 맵으로 이동
//   - 로비 서버와의 연결이 끊기고 게임 서버에 새로 접속
//   - TRAVEL_Absolute: URL을 그대로 사용 (상대 경로 아님)
//
// ════════════════════════════════════════════════════════════════════════════════
void AHellunaLobbyController::Client_ExecuteDeploy_Implementation(const FString& TravelURL)
{
	// [Fix44-C1] Deploy 성공 → 플래그 리셋 (ClientTravel 실패 시 영구 차단 방지)
	bDeployInProgress = false;

	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] ══════════════════════════════════════"));
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] Client_ExecuteDeploy: ClientTravel 시작"));
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC]   TravelURL=%s"), *TravelURL);
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC]   → 로비 서버 연결 해제 → 게임 맵으로 이동"));
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] ══════════════════════════════════════"));
	ClientTravel(TravelURL, TRAVEL_Absolute);
}

// ════════════════════════════════════════════════════════════════════════════════
// Client_ShowLobbyUI — 서버에서 UI 생성 지시 (Client RPC)
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 서버가 Stash 복원 완료 후 클라이언트에게 "이제 UI 만들어" 지시
// 📌 현재는 BeginPlay에서 타이머로 자동 호출하므로 이 RPC는 백업용
//
// ════════════════════════════════════════════════════════════════════════════════
void AHellunaLobbyController::Client_ShowLobbyUI_Implementation()
{
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] Client_ShowLobbyUI 수신 → ShowLobbyWidget 호출"));
	ShowLobbyWidget();
}

// ════════════════════════════════════════════════════════════════════════════════
// ShowLobbyWidget — 로비 UI 생성 및 표시 (클라이언트 전용)
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 호출 시점: BeginPlay 타이머(0.5초 후) 또는 Client_ShowLobbyUI RPC
// 📌 실행 위치: 클라이언트 (로컬 컨트롤러에서만)
//
// 📌 처리 순서:
//   1) 중복 생성 방지 (이미 인스턴스 있으면 스킵)
//   2) LobbyStashWidgetClass 검증 (BP에서 WBP_HellunaLobbyStashWidget 설정 필요)
//   3) CreateWidget → AddToViewport
//   4) InitializePanels(StashComp, LoadoutComp) → 양쪽 패널 바인딩
//   5) 마우스 커서 표시 + UI 전용 입력 모드 설정
//
// 📌 InitializePanels 호출 체인:
//   StashWidget.InitializePanels(StashComp, LoadoutComp)
//     → StashPanel.InitializeWithComponent(StashComp)
//       → Grid_Equippables.SetInventoryComponent(StashComp)
//       → Grid_Consumables.SetInventoryComponent(StashComp)
//       → Grid_Craftables.SetInventoryComponent(StashComp)
//     → LoadoutPanel.InitializeWithComponent(LoadoutComp)
//       → Grid_Equippables.SetInventoryComponent(LoadoutComp)
//       → Grid_Consumables.SetInventoryComponent(LoadoutComp)
//       → Grid_Craftables.SetInventoryComponent(LoadoutComp)
//
// ════════════════════════════════════════════════════════════════════════════════
void AHellunaLobbyController::ShowLobbyWidget()
{
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] ShowLobbyWidget 호출"));

	// ── 중복 생성 방지 ──
	if (LobbyStashWidgetInstance)
	{
		UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] ShowLobbyWidget: 이미 위젯 존재 → 스킵"));
		return;
	}

	// ── WidgetClass 검증 ──
	if (!LobbyStashWidgetClass)
	{
		UE_LOG(LogHellunaLobby, Warning, TEXT("[LobbyPC] ShowLobbyWidget: LobbyStashWidgetClass가 설정되지 않음!"));
		UE_LOG(LogHellunaLobby, Warning, TEXT("[LobbyPC]   → BP_HellunaLobbyController에서 LobbyStashWidgetClass를 WBP_HellunaLobbyStashWidget으로 설정하세요"));
		return;
	}

	// ── 위젯 생성 ──
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] CreateWidget 시작 | WidgetClass=%s"), *LobbyStashWidgetClass->GetName());
	LobbyStashWidgetInstance = CreateWidget<UHellunaLobbyStashWidget>(this, LobbyStashWidgetClass);

	if (LobbyStashWidgetInstance)
	{
		// Viewport에 추가
		LobbyStashWidgetInstance->AddToViewport();
		UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] 위젯 AddToViewport 완료"));

		// 양쪽 패널 초기화 (StashComp → 좌측, LoadoutComp → 우측)
		UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] InitializePanels 호출 | StashComp=%s, LoadoutComp=%s"),
			StashInventoryComponent ? TEXT("O") : TEXT("X"),
			LoadoutInventoryComponent ? TEXT("O") : TEXT("X"));
		LobbyStashWidgetInstance->InitializePanels(StashInventoryComponent, LoadoutInventoryComponent);

		// 마우스 커서 표시 (로비는 UI 기반 조작)
		SetShowMouseCursor(true);
		SetInputMode(FInputModeUIOnly());

		// V2 프리뷰 스폰 + 직접 뷰포트 카메라 설정
		SpawnPreviewSceneV2();

		UHellunaLobbyCharSelectWidget* CharSelectPanel = LobbyStashWidgetInstance->GetCharacterSelectPanel();
		if (CharSelectPanel && SpawnedPreviewSceneV2)
		{
			CharSelectPanel->SetupPreviewV2(SpawnedPreviewSceneV2);
			UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] CharSelectPanel V2 프리뷰 설정 완료"));
		}

		// Play 탭 중앙 프리뷰 설정 (Scene 캐시 — 직접 뷰포트이므로 RT 불필요)
		if (SpawnedPreviewSceneV2)
		{
			LobbyStashWidgetInstance->SetupCenterPreview(SpawnedPreviewSceneV2);
			UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] Play 탭 중앙 프리뷰 설정 완료"));
		}

		// 초기 배경 레벨 로드 (Play 탭이 기본 — TabIndex 0)
		LoadBackgroundForTab(0);

		UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] ── ShowLobbyWidget 완료 ──"));
		UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC]   위젯 생성 성공 | 마우스 커서 ON | UI 전용 입력 모드 | 직접 뷰포트 카메라"));
	}
	else
	{
		UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyPC] ShowLobbyWidget: CreateWidget 실패!"));
		UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyPC]   → WBP_HellunaLobbyStashWidget의 BindWidget이 올바른지 확인하세요"));
		UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyPC]   → StashPanel, LoadoutPanel, Button_Deploy가 모두 있어야 합니다"));
	}
}

// ════════════════════════════════════════════════════════════════════════════════
// GetLifetimeReplicatedProps — SelectedHeroType 리플리케이션
// ════════════════════════════════════════════════════════════════════════════════
void AHellunaLobbyController::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION(AHellunaLobbyController, SelectedHeroType, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(AHellunaLobbyController, ReplicatedPlayerId, COND_OwnerOnly);
}

// ════════════════════════════════════════════════════════════════════════════════
// EndPlay — V2 프리뷰 정리
// ════════════════════════════════════════════════════════════════════════════════
void AHellunaLobbyController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (LobbyLoginWidgetInstance)
	{
		LobbyLoginWidgetInstance->RemoveFromParent();
		LobbyLoginWidgetInstance = nullptr;
	}

	if (LobbyStashWidgetInstance)
	{
		LobbyStashWidgetInstance->RemoveFromParent();
		LobbyStashWidgetInstance = nullptr;
	}

	// 배경 레벨 언로드
	if (!CurrentLoadedLevel.IsNone())
	{
		UnloadBackgroundLevel(CurrentLoadedLevel);
	}

	DestroyPreviewSceneV2();
	Super::EndPlay(EndPlayReason);
}

// ════════════════════════════════════════════════════════════════════════════════
// [Phase 13] Server_RequestLobbyLogin — 로비 로그인 요청 (Server RPC)
// ════════════════════════════════════════════════════════════════════════════════
bool AHellunaLobbyController::Server_RequestLobbyLogin_Validate(const FString& PlayerId, const FString& Password)
{
	if (PlayerId.Len() > 64 || Password.Len() > 128) return false;
	return !PlayerId.IsEmpty() && !Password.IsEmpty();
}

void AHellunaLobbyController::Server_RequestLobbyLogin_Implementation(const FString& PlayerId, const FString& Password)
{
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] Server_RequestLobbyLogin | PlayerId=%s"), *PlayerId);

	AHellunaLobbyGameMode* LobbyGM = GetLobbyGameMode(); // [Fix46-M1]
	if (!LobbyGM)
	{
		UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyPC] Server_RequestLobbyLogin: GameMode 캐스팅 실패!"));
		Client_LobbyLoginResult(false, TEXT("서버 오류"));
		return;
	}

	LobbyGM->ProcessLobbyLogin(this, PlayerId, Password);
}

// ════════════════════════════════════════════════════════════════════════════════
// [Phase 13] Server_RequestLobbySignup — 로비 회원가입 요청 (Server RPC)
// ════════════════════════════════════════════════════════════════════════════════
bool AHellunaLobbyController::Server_RequestLobbySignup_Validate(const FString& PlayerId, const FString& Password)
{
	if (PlayerId.Len() > 64 || Password.Len() > 128) return false;
	return !PlayerId.IsEmpty() && !Password.IsEmpty();
}

void AHellunaLobbyController::Server_RequestLobbySignup_Implementation(const FString& PlayerId, const FString& Password)
{
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] Server_RequestLobbySignup | PlayerId=%s"), *PlayerId);

	AHellunaLobbyGameMode* LobbyGM = GetLobbyGameMode(); // [Fix46-M1]
	if (!LobbyGM)
	{
		UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyPC] Server_RequestLobbySignup: GameMode 캐스팅 실패!"));
		Client_LobbySignupResult(false, TEXT("서버 오류"));
		return;
	}

	LobbyGM->ProcessLobbySignup(this, PlayerId, Password);
}

// ════════════════════════════════════════════════════════════════════════════════
// [Phase 13] Client_LobbySignupResult — 회원가입 결과 (Client RPC)
// ════════════════════════════════════════════════════════════════════════════════
void AHellunaLobbyController::Client_LobbySignupResult_Implementation(bool bSuccess, const FString& Message)
{
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] Client_LobbySignupResult | bSuccess=%s | Msg=%s"),
		bSuccess ? TEXT("true") : TEXT("false"), *Message);

	if (LobbyLoginWidgetInstance)
	{
		LobbyLoginWidgetInstance->HandleSignupResult(bSuccess, Message);
	}
}

// ════════════════════════════════════════════════════════════════════════════════
// [Phase 13] Client_ShowLobbyLoginUI — 로그인 위젯 표시 (Client RPC)
// ════════════════════════════════════════════════════════════════════════════════
void AHellunaLobbyController::Client_ShowLobbyLoginUI_Implementation()
{
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] Client_ShowLobbyLoginUI 수신"));

	// ── [Phase 13] 게임에서 로비 복귀 시 자동 재로그인 ──
	// GameInstance는 ClientTravel 사이에도 유지됨
	UMDF_GameInstance* GI = Cast<UMDF_GameInstance>(GetGameInstance());
	if (GI && !GI->CachedLoginId.IsEmpty() && !GI->CachedLoginPassword.IsEmpty())
	{
		UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] 캐시된 자격증명 발견 → 자동 재로그인 | PlayerId=%s"), *GI->CachedLoginId);
		Server_RequestLobbyLogin(GI->CachedLoginId, GI->CachedLoginPassword);
		return;
	}

	if (!LobbyLoginWidgetClass)
	{
		UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyPC] LobbyLoginWidgetClass 미설정! BP에서 지정 필요"));
		return;
	}

	if (!LobbyLoginWidgetInstance)
	{
		LobbyLoginWidgetInstance = CreateWidget<UHellunaLobbyLoginWidget>(this, LobbyLoginWidgetClass);
		if (LobbyLoginWidgetInstance)
		{
			LobbyLoginWidgetInstance->AddToViewport(10);

			// 마우스 커서 표시 + UI 입력 모드
			SetShowMouseCursor(true);
			FInputModeUIOnly InputMode;
			InputMode.SetWidgetToFocus(LobbyLoginWidgetInstance->TakeWidget());
			SetInputMode(InputMode);

			UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] 로그인 위젯 생성 + Viewport 추가 완료"));
		}
	}
}

// ════════════════════════════════════════════════════════════════════════════════
// [Phase 13] Client_LobbyLoginResult — 로그인 결과 (Client RPC)
// ════════════════════════════════════════════════════════════════════════════════
void AHellunaLobbyController::Client_LobbyLoginResult_Implementation(bool bSuccess, const FString& ErrorMessage)
{
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] Client_LobbyLoginResult | bSuccess=%s | Msg=%s"),
		bSuccess ? TEXT("true") : TEXT("false"), *ErrorMessage);

	if (bSuccess)
	{
		bIsLoggedIn = true;

		// ── [Phase 13] 자격증명 캐시 (로비 복귀 시 자동 재로그인용) ──
		UMDF_GameInstance* GI = Cast<UMDF_GameInstance>(GetGameInstance());
		if (GI)
		{
			if (LobbyLoginWidgetInstance)
			{
				// 위젯에서 입력값 가져오기
				GI->CachedLoginId = LobbyLoginWidgetInstance->GetEnteredPlayerId();
				GI->CachedLoginPassword = LobbyLoginWidgetInstance->GetEnteredPassword();
			}
			// 자동 재로그인 경로(위젯 없음)에서는 이미 캐시에 값이 있으므로 스킵
			UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] 자격증명 캐시 완료 | PlayerId=%s"), *GI->CachedLoginId);
		}

		// 로그인 위젯 제거
		if (LobbyLoginWidgetInstance)
		{
			LobbyLoginWidgetInstance->ShowSuccess();

			// 0.3초 후 위젯 제거 + 로비 UI 표시
			FTimerHandle RemoveTimer;
			TWeakObjectPtr<AHellunaLobbyController> WeakThis(this);
			GetWorldTimerManager().SetTimer(RemoveTimer, [WeakThis]()
			{
				if (!WeakThis.IsValid()) return;
				if (WeakThis->LobbyLoginWidgetInstance)
				{
					WeakThis->LobbyLoginWidgetInstance->RemoveFromParent();
					WeakThis->LobbyLoginWidgetInstance = nullptr;
				}
				// 로비 위젯은 서버가 Client_ShowLobbyUI로 표시 지시
				// (InitializeLobbyForPlayer에서 0.5초 후 호출)
			}, 0.3f, false);
		}
	}
	else
	{
		// 실패: 위젯에 에러 메시지 표시
		if (LobbyLoginWidgetInstance)
		{
			LobbyLoginWidgetInstance->ShowError(ErrorMessage);
		}
	}
}

// ════════════════════════════════════════════════════════════════════════════════
// Server_SelectLobbyCharacter — 캐릭터 선택 (Server RPC)
// ════════════════════════════════════════════════════════════════════════════════
bool AHellunaLobbyController::Server_SelectLobbyCharacter_Validate(int32 CharacterIndex)
{
	return CharacterIndex >= 0 && CharacterIndex <= 2;
}

void AHellunaLobbyController::Server_SelectLobbyCharacter_Implementation(int32 CharacterIndex)
{
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] Server_SelectLobbyCharacter | Index=%d"), CharacterIndex);

	// [Fix46-M1] 헬퍼 사용
	AHellunaLobbyGameMode* LobbyGM = GetLobbyGameMode();
	if (!LobbyGM)
	{
		UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyPC] Server_SelectLobbyCharacter: GameMode 캐스팅 실패!"));
		Client_LobbyCharacterSelectionResult(false, TEXT("서버 오류"));
		return;
	}

	const EHellunaHeroType HeroType = IndexToHeroType(CharacterIndex);
	if (HeroType == EHellunaHeroType::None)
	{
		Client_LobbyCharacterSelectionResult(false, TEXT("잘못된 캐릭터 인덱스"));
		return;
	}

	// GameMode에서 가용성 체크 + 등록 (같은 파티 내에서만 중복 제한)
	const FString PlayerId = LobbyGM->GetLobbyPlayerId(this);
	if (!LobbyGM->IsLobbyCharacterAvailable(HeroType, PlayerId))
	{
		UE_LOG(LogHellunaLobby, Warning, TEXT("[LobbyPC] 캐릭터 %d 이미 사용 중!"), CharacterIndex);
		Client_LobbyCharacterSelectionResult(false, TEXT("다른 플레이어가 사용 중입니다"));
		return;
	}

	// 이전 선택 해제 (재선택 허용)
	if (SelectedHeroType != EHellunaHeroType::None)
	{
		LobbyGM->UnregisterLobbyCharacterUse(PlayerId);
	}

	// 등록
	LobbyGM->RegisterLobbyCharacterUse(HeroType, PlayerId);
	SelectedHeroType = HeroType;

	// [Phase 12d] 파티 멤버 영웅 타입 갱신
	LobbyGM->OnPlayerHeroChanged(PlayerId, CharacterIndex);

	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] 캐릭터 선택 성공 | Index=%d | PlayerId=%s"), CharacterIndex, *PlayerId);
	Client_LobbyCharacterSelectionResult(true, TEXT("캐릭터 선택 완료!"));
}

// ════════════════════════════════════════════════════════════════════════════════
// Client_LobbyCharacterSelectionResult — 선택 결과 (Client RPC)
// ════════════════════════════════════════════════════════════════════════════════
void AHellunaLobbyController::Client_LobbyCharacterSelectionResult_Implementation(bool bSuccess, const FString& Message)
{
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] Client_LobbyCharacterSelectionResult | 성공=%s | %s"),
		bSuccess ? TEXT("true") : TEXT("false"), *Message);

	if (LobbyStashWidgetInstance)
	{
		UHellunaLobbyCharSelectWidget* CharSelectPanel = LobbyStashWidgetInstance->GetCharacterSelectPanel();
		if (CharSelectPanel)
		{
			CharSelectPanel->OnSelectionResult(bSuccess, Message);
		}
	}
}

// ════════════════════════════════════════════════════════════════════════════════
// Client_ShowLobbyCharacterSelectUI — 가용 캐릭터 전달 (Client RPC)
// ════════════════════════════════════════════════════════════════════════════════
void AHellunaLobbyController::Client_ShowLobbyCharacterSelectUI_Implementation(const TArray<bool>& UsedCharacters)
{
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] Client_ShowLobbyCharacterSelectUI | %d개 항목"), UsedCharacters.Num());

	if (LobbyStashWidgetInstance)
	{
		UHellunaLobbyCharSelectWidget* CharSelectPanel = LobbyStashWidgetInstance->GetCharacterSelectPanel();
		if (CharSelectPanel)
		{
			CharSelectPanel->SetAvailableCharacters(UsedCharacters);
		}
	}
}

// ════════════════════════════════════════════════════════════════════════════════
// SpawnPreviewSceneV2 — V2 프리뷰 씬 스폰 (LoginController에서 복사)
// ════════════════════════════════════════════════════════════════════════════════
void AHellunaLobbyController::SpawnPreviewSceneV2()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyPC] V2 프리뷰 스폰 실패 - World nullptr"));
		return;
	}

	if (World->GetNetMode() == NM_DedicatedServer)
	{
		UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] V2 프리뷰 스폰 스킵 - 데디케이티드 서버"));
		return;
	}

	if (!PreviewSceneV2Class)
	{
		UE_LOG(LogHellunaLobby, Warning, TEXT("[LobbyPC] V2 프리뷰 스폰 스킵 - PreviewSceneV2Class 미설정"));
		return;
	}

	if (PreviewMeshMap.Num() == 0)
	{
		UE_LOG(LogHellunaLobby, Warning, TEXT("[LobbyPC] V2 프리뷰 스폰 스킵 - PreviewMeshMap 비어있음"));
		return;
	}

	// 기존 V2 정리
	DestroyPreviewSceneV2();

	// V2 씬 액터 스폰
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnedPreviewSceneV2 = World->SpawnActor<AHellunaCharacterSelectSceneV2>(
		PreviewSceneV2Class, PreviewSpawnBaseLocation, FRotator::ZeroRotator, SpawnParams);

	if (!SpawnedPreviewSceneV2)
	{
		UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyPC] V2 프리뷰 씬 액터 스폰 실패!"));
		return;
	}

	// 메시/애님 배열 구성
	const TArray<EHellunaHeroType> HeroTypes = { EHellunaHeroType::Lui, EHellunaHeroType::Luna, EHellunaHeroType::Liam };

	TArray<USkeletalMesh*> Meshes;
	TArray<TSubclassOf<UAnimInstance>> AnimClasses;

	for (const EHellunaHeroType HeroType : HeroTypes)
	{
		const TSoftObjectPtr<USkeletalMesh>* MeshPtr = PreviewMeshMap.Find(HeroType);
		USkeletalMesh* LoadedMesh = MeshPtr ? MeshPtr->LoadSynchronous() : nullptr;
		Meshes.Add(LoadedMesh);

		const TSubclassOf<UAnimInstance>* AnimClassPtr = PreviewAnimClassMap.Find(HeroType);
		AnimClasses.Add(AnimClassPtr ? *AnimClassPtr : nullptr);

		UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] V2 프리뷰: %s → Mesh=%s AnimClass=%s"),
			*UEnum::GetValueAsString(HeroType),
			LoadedMesh ? *LoadedMesh->GetName() : TEXT("nullptr"),
			(AnimClassPtr && *AnimClassPtr) ? *(*AnimClassPtr)->GetName() : TEXT("nullptr"));
	}

	// 씬 초기화
	SpawnedPreviewSceneV2->InitializeScene(Meshes, AnimClasses);

	// ── 카메라 스폰 (직접 뷰포트 렌더링) ──
	const FVector CamWorldPos = PreviewSpawnBaseLocation + SpawnedPreviewSceneV2->GetCameraOffset();
	const FRotator CamWorldRot = SpawnedPreviewSceneV2->GetCameraRotation();

	FActorSpawnParameters CamSpawnParams;
	CamSpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	LobbyCamera = World->SpawnActor<ACameraActor>(CamWorldPos, CamWorldRot, CamSpawnParams);

	if (LobbyCamera)
	{
		// FOV 적용
		UCameraComponent* CamComp = LobbyCamera->GetCameraComponent();
		if (CamComp)
		{
			CamComp->SetFieldOfView(SpawnedPreviewSceneV2->GetCameraFOV());
			CamComp->bConstrainAspectRatio = false;  // 레터박스(검은 여백) 방지

			// Auto Exposure 범위 제한 — 어두운 동굴에서 과도한 밝기 보정 방지
			CamComp->PostProcessSettings.bOverride_AutoExposureMinBrightness = true;
			CamComp->PostProcessSettings.AutoExposureMinBrightness = 0.5f;
			CamComp->PostProcessSettings.bOverride_AutoExposureMaxBrightness = true;
			CamComp->PostProcessSettings.AutoExposureMaxBrightness = 2.0f;
			CamComp->PostProcessSettings.bOverride_AutoExposureBias = true;
			CamComp->PostProcessSettings.AutoExposureBias = 0.0f;  // 노출 보정 없음
			CamComp->PostProcessBlendWeight = 1.0f;  // PostProcess 설정 활성화
		}

		// 직접 뷰포트에 카메라 설정 (블렌드 없이 즉시)
		SetViewTarget(LobbyCamera);

		UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] 로비 카메라 스폰 완료 | Pos=%s Rot=%s FOV=%.1f"),
			*CamWorldPos.ToString(), *CamWorldRot.ToString(), SpawnedPreviewSceneV2->GetCameraFOV());
	}
	else
	{
		UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyPC] 로비 카메라 스폰 실패!"));
	}

	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] V2 프리뷰 씬 스폰 및 초기화 완료 (직접 뷰포트 카메라)"));
}

// ════════════════════════════════════════════════════════════════════════════════
// DestroyPreviewSceneV2 — V2 프리뷰 씬 파괴
// ════════════════════════════════════════════════════════════════════════════════
// ════════════════════════════════════════════════════════════════════════════════
// [Phase 12g-2] 파티 프리뷰 갱신/해제
// ════════════════════════════════════════════════════════════════════════════════

void AHellunaLobbyController::UpdatePartyPreview(const FHellunaPartyInfo& PartyInfo)
{
	if (!IsLocalController()) return;
	if (!IsValid(SpawnedPreviewSceneV2)) return;

	// 파티 2명 이상이면 Party 모드
	if (PartyInfo.IsValid() && PartyInfo.Members.Num() >= 2)
	{
		// HeroType(int32) -> Mesh/Anim 맵 구성 (LobbyController의 TSoftObjectPtr 매핑을 int32 키로 변환)
		TMap<int32, USkeletalMesh*> MeshMap;
		TMap<int32, TSubclassOf<UAnimInstance>> AnimMap;

		for (const auto& Pair : PreviewMeshMap)
		{
			const int32 Idx = HeroTypeToIndex(Pair.Key);
			if (Idx >= 0)
			{
				USkeletalMesh* Mesh = Pair.Value.LoadSynchronous();
				if (Mesh)
				{
					MeshMap.Add(Idx, Mesh);
				}
			}
		}
		for (const auto& Pair : PreviewAnimClassMap)
		{
			const int32 Idx = HeroTypeToIndex(Pair.Key);
			if (Idx >= 0 && Pair.Value)
			{
				AnimMap.Add(Idx, Pair.Value);
			}
		}

		SpawnedPreviewSceneV2->SetPartyPreview(
			PartyInfo.Members,
			ReplicatedPlayerId,
			MeshMap,
			AnimMap);
	}
	else
	{
		// 1명 이하 → Solo 복귀
		ResetToSoloPreview();
	}
}

void AHellunaLobbyController::ResetToSoloPreview()
{
	if (!IsLocalController()) return;
	if (!IsValid(SpawnedPreviewSceneV2)) return;

	if (SpawnedPreviewSceneV2->IsPartyMode())
	{
		SpawnedPreviewSceneV2->ClearPartyPreview();
	}

	// 선택된 캐릭터가 있으면 Solo 모드 복귀
	if (SelectedHeroType != EHellunaHeroType::None)
	{
		const int32 HeroIndex = HeroTypeToIndex(SelectedHeroType);
		if (HeroIndex >= 0)
		{
			SpawnedPreviewSceneV2->SetSoloCharacter(HeroIndex);
		}
	}
}

// ════════════════════════════════════════════════════════════════════════════════
// DestroyPreviewSceneV2 — 프리뷰 씬 정리
// ════════════════════════════════════════════════════════════════════════════════

void AHellunaLobbyController::DestroyPreviewSceneV2()
{
	if (IsValid(SpawnedPreviewSceneV2))
	{
		SpawnedPreviewSceneV2->Destroy();
		SpawnedPreviewSceneV2 = nullptr;
		UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] V2 프리뷰 씬 파괴 완료"));
	}

	if (IsValid(LobbyCamera))
	{
		LobbyCamera->Destroy();
		LobbyCamera = nullptr;
		UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] 로비 카메라 파괴 완료"));
	}
}

// ════════════════════════════════════════════════════════════════════════════════
// Level Streaming — 배경 레벨 로드/언로드
// ════════════════════════════════════════════════════════════════════════════════

void AHellunaLobbyController::LoadBackgroundLevel(FName LevelName)
{
	if (LevelName.IsNone())
	{
		UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] LoadBackgroundLevel: 레벨 이름 비어있음 → 스킵"));
		return;
	}

	// 이미 같은 레벨이 로드되어 있으면 스킵
	if (CurrentLoadedLevel == LevelName)
	{
		UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] LoadBackgroundLevel: '%s' 이미 로드됨 → 스킵"), *LevelName.ToString());
		return;
	}

	// 이전 레벨 언로드
	if (!CurrentLoadedLevel.IsNone())
	{
		UnloadBackgroundLevel(CurrentLoadedLevel);
	}

	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] LoadBackgroundLevel: '%s' 로드 시작"), *LevelName.ToString());

	// FLatentActionInfo — 콜백은 UFUNCTION이어야 함 (리플렉션 호출)
	FLatentActionInfo LatentInfo;
	LatentInfo.CallbackTarget = this;
	LatentInfo.ExecutionFunction = FName(TEXT("OnBackgroundLevelLoaded"));
	LatentInfo.Linkage = 0;
	LatentInfo.UUID = GetUniqueID();

	UGameplayStatics::LoadStreamLevel(this, LevelName, true, false, LatentInfo);
	CurrentLoadedLevel = LevelName;
}

void AHellunaLobbyController::UnloadBackgroundLevel(FName LevelName)
{
	if (LevelName.IsNone())
	{
		return;
	}

	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] UnloadBackgroundLevel: '%s' 언로드 시작"), *LevelName.ToString());

	FLatentActionInfo LatentInfo;
	LatentInfo.CallbackTarget = this;
	LatentInfo.ExecutionFunction = FName(TEXT("OnBackgroundLevelUnloaded"));
	LatentInfo.Linkage = 1;
	LatentInfo.UUID = GetUniqueID() + 1;

	UGameplayStatics::UnloadStreamLevel(this, LevelName, LatentInfo, false);

	if (CurrentLoadedLevel == LevelName)
	{
		CurrentLoadedLevel = NAME_None;
	}
}

void AHellunaLobbyController::OnBackgroundLevelLoaded()
{
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] OnBackgroundLevelLoaded: '%s' 로드 완료"), *CurrentLoadedLevel.ToString());

	if (!LobbyCamera)
	{
		UE_LOG(LogHellunaLobby, Warning, TEXT("[LobbyPC] OnBackgroundLevelLoaded: LobbyCamera 없음 → 카메라 설정 스킵"));
		return;
	}

	// 1단계: 앵커 캐시 재구축 (새 서브레벨의 앵커 포함)
	RebuildAnchorCache();

	// 2단계: 앵커 우선 검색 (SlotIndex=0, 기본 뷰)
	const bool bAnchorApplied = ApplyCameraFromAnchor(PendingBackgroundTabIndex, 0);

	// 3단계: 앵커 실패 시 기존 FLobbyCameraTransform 폴백
	if (!bAnchorApplied)
	{
		UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] 앵커 없음 → FLobbyCameraTransform 폴백"));

		const FLobbyCameraTransform* CamTransform = nullptr;
		if (PendingBackgroundTabIndex == 0)
		{
			CamTransform = &PlayCameraTransform;
		}
		else if (PendingBackgroundTabIndex == 2)
		{
			CamTransform = &CharacterCameraTransform;
		}

		if (CamTransform)
		{
			LobbyCamera->SetActorLocation(CamTransform->Location);
			LobbyCamera->SetActorRotation(CamTransform->Rotation);

			UCameraComponent* CamComp = LobbyCamera->GetCameraComponent();
			if (CamComp)
			{
				CamComp->SetFieldOfView(CamTransform->FOV);
			CamComp->bConstrainAspectRatio = false;  // 레터박스 방지
			}

			UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] 폴백 카메라 적용 | Tab=%d Pos=%s Rot=%s FOV=%.1f"),
				PendingBackgroundTabIndex, *CamTransform->Location.ToString(),
				*CamTransform->Rotation.ToString(), CamTransform->FOV);

			// 프리뷰 씬 이동
			MovePreviewSceneToCamera(CamTransform->Location, CamTransform->Rotation);
		}
	}
}

// ════════════════════════════════════════════════════════════════════════════════
// 카메라 앵커 시스템
// ════════════════════════════════════════════════════════════════════════════════

void AHellunaLobbyController::RebuildAnchorCache()
{
	CachedAnchors.Empty();

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	TArray<AActor*> FoundAnchors;
	UGameplayStatics::GetAllActorsOfClass(World, AHellunaLobbyCameraAnchor::StaticClass(), FoundAnchors);

	for (AActor* Actor : FoundAnchors)
	{
		AHellunaLobbyCameraAnchor* Anchor = Cast<AHellunaLobbyCameraAnchor>(Actor);
		if (Anchor)
		{
			const int32 Key = Anchor->GetTabIndex() * 1000 + Anchor->GetSlotIndex();
			CachedAnchors.Add(Key, Anchor);
			UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] 앵커 캐시: Tab=%d Slot=%d Key=%d Pos=%s"),
				Anchor->GetTabIndex(), Anchor->GetSlotIndex(), Key, *Anchor->GetActorLocation().ToString());
		}
	}

	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] 앵커 캐시 구축 완료 | 총 %d개"), CachedAnchors.Num());
}

bool AHellunaLobbyController::ApplyCameraFromAnchor(int32 InTabIndex, int32 InSlotIndex)
{
	if (!LobbyCamera)
	{
		return false;
	}

	// 요청된 슬롯으로 검색
	const int32 Key = InTabIndex * 1000 + InSlotIndex;
	TObjectPtr<AHellunaLobbyCameraAnchor>* FoundPtr = CachedAnchors.Find(Key);

	// 못 찾으면 SlotIndex=0으로 폴백
	if (!FoundPtr && InSlotIndex != 0)
	{
		const int32 FallbackKey = InTabIndex * 1000;
		FoundPtr = CachedAnchors.Find(FallbackKey);
		if (FoundPtr)
		{
			UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] 앵커 Slot=%d 없음 → Slot=0 폴백"), InSlotIndex);
		}
	}

	if (!FoundPtr || !IsValid(*FoundPtr))
	{
		return false;
	}

	AHellunaLobbyCameraAnchor* Anchor = *FoundPtr;

	// 카메라 Transform 적용
	LobbyCamera->SetActorLocation(Anchor->GetActorLocation());
	LobbyCamera->SetActorRotation(Anchor->GetActorRotation());

	UCameraComponent* CamComp = LobbyCamera->GetCameraComponent();
	if (CamComp)
	{
		CamComp->SetFieldOfView(Anchor->GetFOV());
		CamComp->bConstrainAspectRatio = false;  // 레터박스 방지
	}

	CurrentSlotIndex = InSlotIndex;

	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] 앵커 카메라 적용 | Tab=%d Slot=%d Pos=%s Rot=%s FOV=%.1f"),
		InTabIndex, InSlotIndex,
		*Anchor->GetActorLocation().ToString(),
		*Anchor->GetActorRotation().ToString(),
		Anchor->GetFOV());

	// 프리뷰 씬 이동
	MovePreviewSceneToCamera(Anchor->GetActorLocation(), Anchor->GetActorRotation());

	return true;
}

void AHellunaLobbyController::SwitchCameraSlot(int32 NewSlotIndex)
{
	if (!LobbyCamera)
	{
		return;
	}

	if (!ApplyCameraFromAnchor(PendingBackgroundTabIndex, NewSlotIndex))
	{
		UE_LOG(LogHellunaLobby, Warning, TEXT("[LobbyPC] SwitchCameraSlot: Tab=%d Slot=%d 앵커 없음"),
			PendingBackgroundTabIndex, NewSlotIndex);
	}
}

void AHellunaLobbyController::MovePreviewSceneToCamera(const FVector& CamLocation, const FRotator& CamRotation)
{
	if (!IsValid(SpawnedPreviewSceneV2))
	{
		return;
	}

	const FVector CamOffset = SpawnedPreviewSceneV2->GetCameraOffset();
	// CameraOffset = 씬 루트 → 카메라 로컬 오프셋 (X=전방거리, Z=높이)
	const FVector CamForward = CamRotation.Vector();
	const FVector CamRight = FRotationMatrix(CamRotation).GetScaledAxis(EAxis::Y);
	const FVector CamUp = FRotationMatrix(CamRotation).GetScaledAxis(EAxis::Z);

	// 씬 위치 = 카메라 위치 + Forward * 전방거리 + Right * 좌우 - Up * 높이
	const FVector NewScenePos = CamLocation
		+ CamForward * CamOffset.X
		+ CamRight * CamOffset.Y
		- CamUp * CamOffset.Z;

	// 씬 회전: 카메라를 바라보도록 (카메라 Forward의 반대 방향)
	const FRotator SceneRot = (-CamForward).Rotation();

	SpawnedPreviewSceneV2->SetActorLocation(NewScenePos);
	SpawnedPreviewSceneV2->SetActorRotation(SceneRot);

	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] 프리뷰 씬 이동 | NewPos=%s Rot=%s (CamOffset=%s)"),
		*NewScenePos.ToString(), *SceneRot.ToString(), *CamOffset.ToString());
}

void AHellunaLobbyController::OnBackgroundLevelUnloaded()
{
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] OnBackgroundLevelUnloaded: 언로드 완료"));
}

void AHellunaLobbyController::LoadBackgroundForTab(int32 TabIndex)
{
	// 탭 인덱스에 맞는 배경 레벨 로드 (0=Play, 2=Character, 기타=유지)
	if (TabIndex == 0) // LobbyTab::Play
	{
		PendingBackgroundTabIndex = 0;
		LoadBackgroundLevel(PlayBackgroundLevel);
	}
	else if (TabIndex == 2) // LobbyTab::Character
	{
		PendingBackgroundTabIndex = 2;
		LoadBackgroundLevel(CharacterBackgroundLevel);
	}
	// Loadout 탭(1): 배경 유지 (변경 없음)
}


// ════════════════════════════════════════════════════════════════════════════════
// [Phase 12d] 파티 시스템 RPC — Validate + Implementation
// ════════════════════════════════════════════════════════════════════════════════

// ── Server_CreateParty ──
bool AHellunaLobbyController::Server_CreateParty_Validate()
{
	return !bDeployInProgress;
}

void AHellunaLobbyController::Server_CreateParty_Implementation()
{
	AHellunaLobbyGameMode* LobbyGM = GetLobbyGameMode(); // [Fix46-M1]
	if (!LobbyGM)
	{
		UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyPC] Server_CreateParty: GameMode 없음"));
		return;
	}

	const FString PlayerId = LobbyGM->GetLobbyPlayerId(this);
	if (PlayerId.IsEmpty())
	{
		Client_PartyError(TEXT("플레이어 ID를 가져올 수 없습니다"));
		return;
	}

	LobbyGM->CreatePartyForPlayer(PlayerId, PlayerId); // DisplayName = PlayerId (임시)
}

// ── Server_JoinParty ──
bool AHellunaLobbyController::Server_JoinParty_Validate(const FString& PartyCode)
{
	return !bDeployInProgress && PartyCode.Len() == 6;
}

void AHellunaLobbyController::Server_JoinParty_Implementation(const FString& PartyCode)
{
	AHellunaLobbyGameMode* LobbyGM = GetLobbyGameMode(); // [Fix46-M1]
	if (!LobbyGM)
	{
		return;
	}

	const FString PlayerId = LobbyGM->GetLobbyPlayerId(this);
	if (PlayerId.IsEmpty())
	{
		Client_PartyError(TEXT("플레이어 ID를 가져올 수 없습니다"));
		return;
	}

	LobbyGM->JoinPartyForPlayer(PlayerId, PlayerId, PartyCode);
}

// ── Server_LeaveParty ──
bool AHellunaLobbyController::Server_LeaveParty_Validate()
{
	return true;
}

void AHellunaLobbyController::Server_LeaveParty_Implementation()
{
	AHellunaLobbyGameMode* LobbyGM = GetLobbyGameMode(); // [Fix46-M1]
	if (!LobbyGM)
	{
		return;
	}

	const FString PlayerId = LobbyGM->GetLobbyPlayerId(this);
	LobbyGM->LeavePartyForPlayer(PlayerId);
}

// ── Server_SetPartyReady ──
bool AHellunaLobbyController::Server_SetPartyReady_Validate(bool bReady)
{
	return true;
}

void AHellunaLobbyController::Server_SetPartyReady_Implementation(bool bReady)
{
	AHellunaLobbyGameMode* LobbyGM = GetLobbyGameMode(); // [Fix46-M1]
	if (!LobbyGM)
	{
		return;
	}

	const FString PlayerId = LobbyGM->GetLobbyPlayerId(this);
	LobbyGM->SetPlayerReady(PlayerId, bReady);
}

// ── Server_KickPartyMember ──
bool AHellunaLobbyController::Server_KickPartyMember_Validate(const FString& TargetPlayerId)
{
	return TargetPlayerId.Len() > 0 && TargetPlayerId.Len() < 256;
}

void AHellunaLobbyController::Server_KickPartyMember_Implementation(const FString& TargetPlayerId)
{
	AHellunaLobbyGameMode* LobbyGM = GetLobbyGameMode(); // [Fix46-M1]
	if (!LobbyGM)
	{
		return;
	}

	const FString PlayerId = LobbyGM->GetLobbyPlayerId(this);
	LobbyGM->KickPartyMember(PlayerId, TargetPlayerId);
}

// ── Server_SendPartyChatMessage ──
bool AHellunaLobbyController::Server_SendPartyChatMessage_Validate(const FString& Message)
{
	return Message.Len() > 0 && Message.Len() <= 200;
}

void AHellunaLobbyController::Server_SendPartyChatMessage_Implementation(const FString& Message)
{
	AHellunaLobbyGameMode* LobbyGM = GetLobbyGameMode(); // [Fix46-M1]
	if (!LobbyGM)
	{
		return;
	}

	const FString PlayerId = LobbyGM->GetLobbyPlayerId(this);
	const int32* PartyIdPtr = LobbyGM->PlayerToPartyMap.Find(PlayerId);
	if (!PartyIdPtr || *PartyIdPtr <= 0)
	{
		return;
	}

	FHellunaPartyChatMessage ChatMsg;
	ChatMsg.SenderName = PlayerId;
	ChatMsg.Message = Message;
	UWorld* World = GetWorld();
	ChatMsg.ServerTime = World ? World->GetTimeSeconds() : 0.f;

	LobbyGM->BroadcastPartyChatMessage(*PartyIdPtr, ChatMsg);
}


// ════════════════════════════════════════════════════════════════════════════════
// [Phase 12d] Client RPCs
// ════════════════════════════════════════════════════════════════════════════════

void AHellunaLobbyController::Client_UpdatePartyState_Implementation(const FHellunaPartyInfo& PartyInfo)
{
	CurrentPartyInfo = PartyInfo;
	OnPartyStateChanged.Broadcast(PartyInfo);

	// [Phase 12g-2] 파티 프리뷰 갱신
	UpdatePartyPreview(PartyInfo);

	UE_LOG(LogHellunaLobby, Verbose, TEXT("[LobbyPC] Client_UpdatePartyState | PartyId=%d | Members=%d"),
		PartyInfo.PartyId, PartyInfo.Members.Num());
}

void AHellunaLobbyController::Client_PartyDisbanded_Implementation(const FString& Reason)
{
	CurrentPartyInfo = FHellunaPartyInfo(); // 초기화
	OnPartyStateChanged.Broadcast(CurrentPartyInfo);

	// [Phase 12g-2] Solo 프리뷰 복귀
	ResetToSoloPreview();

	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] Client_PartyDisbanded: %s"), *Reason);
}

void AHellunaLobbyController::Client_PartyError_Implementation(const FString& ErrorMessage)
{
	OnPartyError.Broadcast(ErrorMessage);

	UE_LOG(LogHellunaLobby, Warning, TEXT("[LobbyPC] Client_PartyError: %s"), *ErrorMessage);
}

void AHellunaLobbyController::Client_ReceivePartyChatMessage_Implementation(const FHellunaPartyChatMessage& Msg)
{
	OnPartyChatReceived.Broadcast(Msg);

	UE_LOG(LogHellunaLobby, Verbose, TEXT("[LobbyPC] Client_ReceivePartyChatMessage | Sender=%s | Msg=%s"), *Msg.SenderName, *Msg.Message);
}

void AHellunaLobbyController::Client_ExecutePartyDeploy_Implementation(int32 GameServerPort)
{
	// [Fix44-C1] Deploy 성공 → 플래그 리셋 (ClientTravel 실패 시 영구 차단 방지)
	bDeployInProgress = false;

	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] Client_ExecutePartyDeploy | Port=%d"), GameServerPort);

	if (GameServerPort <= 0)
	{
		UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyPC] Client_ExecutePartyDeploy: invalid port"));
		Server_ReportPartyDeployFailure(GameServerPort, TEXT("유효하지 않은 게임 서버 포트입니다."));
		return;
	}

	UMDF_GameInstance* GI = Cast<UMDF_GameInstance>(GetGameInstance());
	if (!GI || GI->ConnectedServerIP.IsEmpty())
	{
		UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyPC] Client_ExecutePartyDeploy: ConnectedServerIP가 비어있음!"));
		Server_ReportPartyDeployFailure(GameServerPort, TEXT("ConnectedServerIP가 비어 있어 이동할 수 없습니다."));
		return;
	}

	const int32 HeroIndex = HeroTypeToIndex(SelectedHeroType);
	const FString PlayerId = ReplicatedPlayerId.IsEmpty() ? TEXT("Unknown") : ReplicatedPlayerId;

	const FString TravelURL = FString::Printf(TEXT("%s:%d?HeroType=%d?PlayerId=%s"),
		*GI->ConnectedServerIP, GameServerPort, HeroIndex, *PlayerId);

	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] ClientTravel: %s"), *TravelURL);
	ClientTravel(TravelURL, TRAVEL_Absolute);
}

bool AHellunaLobbyController::Server_ReportPartyDeployFailure_Validate(int32 GameServerPort, const FString& Reason)
{
	return GameServerPort >= 0 && GameServerPort <= 65535 && Reason.Len() <= 256;
}

void AHellunaLobbyController::Server_ReportPartyDeployFailure_Implementation(int32 GameServerPort, const FString& Reason)
{
	UE_LOG(LogHellunaLobby, Warning,
		TEXT("[LobbyPC] Server_ReportPartyDeployFailure | Port=%d | Reason=%s"),
		GameServerPort, *Reason);

	bDeployInProgress = false;
	PendingRejoinPort = 0;

	AHellunaLobbyGameMode* LobbyGM = GetLobbyGameMode();
	if (LobbyGM)
	{
		const FString PlayerId = LobbyGM->GetLobbyPlayerId(this);
		if (!PlayerId.IsEmpty())
		{
			LobbyGM->RollbackDeployStateForPlayer(PlayerId, GameServerPort);
		}
	}

	Client_DeployFailed(Reason);
}

void AHellunaLobbyController::TogglePartyWidget()
{
	if (!PartyWidgetClass)
	{
		UE_LOG(LogHellunaLobby, Warning, TEXT("[LobbyPC] TogglePartyWidget: PartyWidgetClass가 설정되지 않음"));
		return;
	}

	// [Phase 12 Fix] RemoveFromParent → SetVisibility 전환
	// RemoveFromParent는 NativeDestruct를 트리거하여 Controller 델리게이트 바인딩이 영구 소실됨
	if (!PartyWidgetInstance)
	{
		PartyWidgetInstance = CreateWidget<UHellunaPartyWidget>(this, PartyWidgetClass);
		if (PartyWidgetInstance)
		{
			PartyWidgetInstance->AddToViewport(50);
			UE_LOG(LogHellunaLobby, Verbose, TEXT("[LobbyPC] 파티 위젯 생성 + 열기"));
		}
	}
	else if (PartyWidgetInstance->GetVisibility() == ESlateVisibility::Visible)
	{
		PartyWidgetInstance->SetVisibility(ESlateVisibility::Collapsed);
		UE_LOG(LogHellunaLobby, Verbose, TEXT("[LobbyPC] 파티 위젯 숨기기"));
	}
	else
	{
		PartyWidgetInstance->SetVisibility(ESlateVisibility::Visible);
		UE_LOG(LogHellunaLobby, Verbose, TEXT("[LobbyPC] 파티 위젯 보이기"));
	}
}

// ════════════════════════════════════════════════════════════════════════════════
// [Phase 14f] 재참가 시스템 RPC
// ════════════════════════════════════════════════════════════════════════════════

void AHellunaLobbyController::Client_ShowRejoinPrompt_Implementation(int32 GameServerPort)
{
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] Client_ShowRejoinPrompt 수신 | Port=%d"), GameServerPort);
	PendingRejoinPort = GameServerPort;

	if (!RejoinWidgetClass)
	{
		UE_LOG(LogHellunaLobby, Warning, TEXT("[LobbyPC] RejoinWidgetClass 미설정! 자동 재참가 시도"));
		Server_RejoinGame();
		return;
	}

	RejoinWidgetInstance = CreateWidget<UHellunaRejoinWidget>(this, RejoinWidgetClass);
	if (RejoinWidgetInstance)
	{
		RejoinWidgetInstance->SetRejoinInfo(GameServerPort);
		RejoinWidgetInstance->AddToViewport(200);
	}
}

bool AHellunaLobbyController::Server_RejoinGame_Validate()
{
	return PendingRejoinPort >= 0;
}

void AHellunaLobbyController::Server_RejoinGame_Implementation()
{
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] Server_RejoinGame 수신"));

	AHellunaLobbyGameMode* LobbyGM = GetLobbyGameMode();
	if (!LobbyGM)
	{
		Client_DeployFailed(TEXT("로비 게임 모드를 찾을 수 없습니다."));
		return;
	}

	if (PendingRejoinPort <= 0)
	{
		Client_DeployFailed(TEXT("재참가 가능한 세션이 없습니다."));
		return;
	}

	LobbyGM->HandleRejoinAccepted(this);
}

bool AHellunaLobbyController::Server_AbandonGame_Validate()
{
	return PendingRejoinPort >= 0;
}

void AHellunaLobbyController::Server_AbandonGame_Implementation()
{
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] Server_AbandonGame 수신"));

	AHellunaLobbyGameMode* LobbyGM = GetLobbyGameMode();
	if (!LobbyGM)
	{
		Client_DeployFailed(TEXT("로비 게임 모드를 찾을 수 없습니다."));
		return;
	}

	LobbyGM->HandleRejoinDeclined(this);
}

// ============================================================================
// [Phase 15] 매치메이킹 RPC
// ============================================================================

// ════════════════════════════════════════════════════════════════════════════════
// [Phase 16] Server_SetSelectedMap
// ════════════════════════════════════════════════════════════════════════════════

bool AHellunaLobbyController::Server_SetSelectedMap_Validate(const FString& MapKey)
{
	return !MapKey.IsEmpty() && MapKey.Len() <= 64;
}

void AHellunaLobbyController::Server_SetSelectedMap_Implementation(const FString& MapKey)
{
	AHellunaLobbyGameMode* LobbyGM = GetLobbyGameMode();
	if (LobbyGM && !LobbyGM->IsValidConfiguredMapKey(MapKey))
	{
		Client_MatchmakingError(TEXT("유효하지 않은 맵 선택입니다."));
		return;
	}

	SelectedMapKey = MapKey;
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] Server_SetSelectedMap | MapKey=%s"), *MapKey);
}

// [Phase 18] 게임 모드 설정

bool AHellunaLobbyController::Server_SetGameMode_Validate(ELobbyGameMode Mode)
{
	return static_cast<uint8>(Mode) <= 2;
}

void AHellunaLobbyController::Server_SetGameMode_Implementation(ELobbyGameMode Mode)
{
	AHellunaLobbyGameMode* LobbyGM = GetLobbyGameMode();
	if (!LobbyGM)
	{
		return;
	}

	const FString PlayerId = LobbyGM->GetLobbyPlayerId(this);
	FString ErrorMessage;
	if (!LobbyGM->ValidateRequestedGameModeForPlayer(PlayerId, Mode, ErrorMessage))
	{
		Client_PartyError(ErrorMessage);
		return;
	}

	SelectedGameMode = Mode;
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] [Phase18] Server_SetGameMode | Mode=%d"), static_cast<int32>(Mode));
}

// ════════════════════════════════════════════════════════════════════════════════

bool AHellunaLobbyController::Server_EnterMatchmaking_Validate()
{
	return !bDeployInProgress;
}

void AHellunaLobbyController::Server_EnterMatchmaking_Implementation()
{
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] Server_EnterMatchmaking 수신 | SelectedMapKey=%s"), *SelectedMapKey);

	AHellunaLobbyGameMode* LobbyGM = GetLobbyGameMode();
	if (!LobbyGM)
	{
		return;
	}

	const FString PlayerId = LobbyGM->GetLobbyPlayerId(this);
	if (PlayerId.IsEmpty())
	{
		Client_MatchmakingError(TEXT("플레이어 ID를 가져올 수 없습니다"));
		return;
	}

	// [Phase 16] 맵 키 전달
	LobbyGM->EnterMatchmakingQueue(PlayerId, SelectedMapKey);
}

bool AHellunaLobbyController::Server_LeaveMatchmaking_Validate()
{
	return true;
}

void AHellunaLobbyController::Server_LeaveMatchmaking_Implementation()
{
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] Server_LeaveMatchmaking 수신"));

	AHellunaLobbyGameMode* LobbyGM = GetLobbyGameMode();
	if (!LobbyGM)
	{
		return;
	}

	const FString PlayerId = LobbyGM->GetLobbyPlayerId(this);
	if (LobbyGM->IsPlayerInQueue(PlayerId))
	{
		LobbyGM->LeaveMatchmakingQueue(PlayerId);
	}
	else
	{
		FMatchmakingStatusInfo CancelInfo;
		CancelInfo.Status = EMatchmakingStatus::None;
		Client_MatchmakingStatusUpdate(CancelInfo);
		return;
	}

	// 큐 퇴장 상태를 클라이언트에 전송
}

void AHellunaLobbyController::Client_MatchmakingStatusUpdate_Implementation(const FMatchmakingStatusInfo& StatusInfo)
{
	UE_LOG(LogHellunaLobby, Verbose, TEXT("[LobbyPC] Client_MatchmakingStatusUpdate | Status=%d | Elapsed=%.1f | %d/%d"),
		static_cast<int32>(StatusInfo.Status), StatusInfo.ElapsedTime,
		StatusInfo.CurrentPlayerCount, StatusInfo.TargetPlayerCount);

	OnMatchmakingStatusChanged.Broadcast(StatusInfo);
}

void AHellunaLobbyController::Client_MatchmakingError_Implementation(const FString& ErrorMessage)
{
	UE_LOG(LogHellunaLobby, Warning, TEXT("[LobbyPC] Client_MatchmakingError: %s"), *ErrorMessage);

	// 에러 시 None 상태로 전환
	FMatchmakingStatusInfo CancelInfo;
	CancelInfo.Status = EMatchmakingStatus::None;
	OnMatchmakingStatusChanged.Broadcast(CancelInfo);
	OnMatchmakingCancelled.Broadcast(ErrorMessage);
}

// ════════════════════════════════════════════════════════════════════════════════
// [Phase 17] 매칭 카운트다운 RPC
// ════════════════════════════════════════════════════════════════════════════════

void AHellunaLobbyController::Client_MatchmakingFound_Implementation(const FMatchmakingFoundInfo& FoundInfo)
{
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] [Phase17] 매칭 완료 수신 | Members=%d | Reassigned=%s"),
		FoundInfo.MatchedMembers.Num(), FoundInfo.bHeroWasReassigned ? TEXT("YES") : TEXT("NO"));

	// 프리뷰 씬에 3명 캐릭터 모이는 연출
	if (IsValid(SpawnedPreviewSceneV2))
	{
		TMap<int32, USkeletalMesh*> MeshMap;
		TMap<int32, TSubclassOf<UAnimInstance>> AnimMap;

		for (const auto& Pair : PreviewMeshMap)
		{
			const int32 Idx = HeroTypeToIndex(Pair.Key);
			if (Idx < 0) continue;
			USkeletalMesh* LoadedMesh = Pair.Value.Get();
			if (!LoadedMesh) LoadedMesh = Pair.Value.LoadSynchronous();
			if (LoadedMesh)
			{
				MeshMap.Add(Idx, LoadedMesh);
			}
		}
		for (const auto& Pair : PreviewAnimClassMap)
		{
			const int32 Idx = HeroTypeToIndex(Pair.Key);
			if (Idx >= 0 && Pair.Value)
			{
				AnimMap.Add(Idx, Pair.Value);
			}
		}

		SpawnedPreviewSceneV2->SetPartyPreview(
			FoundInfo.MatchedMembers,
			ReplicatedPlayerId,
			MeshMap,
			AnimMap);
	}

	// 재배정된 경우 SelectedHeroType 갱신
	if (FoundInfo.bHeroWasReassigned)
	{
		SelectedHeroType = IndexToHeroType(FoundInfo.AssignedHeroType);
	}

	OnMatchmakingFound.Broadcast(FoundInfo);
}

void AHellunaLobbyController::Client_MatchmakingCountdown_Implementation(int32 RemainingSeconds)
{
	OnMatchmakingCountdown.Broadcast(RemainingSeconds);
}

void AHellunaLobbyController::Client_MatchmakingCancelled_Implementation(const FString& Reason)
{
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] [Phase17] 카운트다운 취소 | Reason=%s"), *Reason);

	// Solo 프리뷰로 복귀
	if (IsValid(SpawnedPreviewSceneV2))
	{
		SpawnedPreviewSceneV2->ClearPartyPreview();
		if (SelectedHeroType != EHellunaHeroType::None)
		{
			const int32 HeroIndex = HeroTypeToIndex(SelectedHeroType);
			if (HeroIndex >= 0)
			{
				SpawnedPreviewSceneV2->SetSoloCharacter(HeroIndex);
			}
		}
	}

	OnMatchmakingCancelled.Broadcast(Reason);
}
