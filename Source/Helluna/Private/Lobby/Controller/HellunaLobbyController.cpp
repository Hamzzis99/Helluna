// ════════════════════════════════════════════════════════════════════════════════
// HellunaLobbyController.cpp
// ════════════════════════════════════════════════════════════════════════════════
//
// ============================================================================
// 📌 Phase 4 Step 4-2: 로비 전용 PlayerController
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
#include "Lobby/Widget/HellunaLobbyStashWidget.h"
#include "Lobby/Widget/HellunaLobbyCharSelectWidget.h"
#include "Lobby/Database/HellunaSQLiteSubsystem.h"
#include "Login/Preview/HellunaCharacterSelectSceneV2.h"
#include "InventoryManagement/Components/Inv_InventoryComponent.h"
#include "Blueprint/UserWidget.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Net/UnrealNetwork.h"

// 로그 카테고리 (공유 헤더 — DEFINE은 HellunaLobbyGameMode.cpp)
#include "Lobby/HellunaLobbyLog.h"

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
// 📌 클라이언트(IsLocalController)에서만 UI 생성
// 📌 0.5초 딜레이: 서버에서 StashComp에 데이터 복원이 완료되고
//    리플리케이션으로 클라이언트에 도달할 시간을 확보하기 위함
//    (BeginPlay 시점에서는 아직 Stash 데이터가 도착하지 않았을 수 있음)
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

	// 클라이언트에서 자동으로 UI 표시 (로컬 컨트롤러일 때만)
	// 서버의 Dedicated Server에는 로컬 컨트롤러가 없으므로 이 블록은 실행 안 됨
	if (IsLocalController())
	{
		UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] 로컬 컨트롤러 → 0.5초 후 ShowLobbyWidget 예약"));
		// 타이머 딜레이: 서버→클라 리플리케이션 대기 (Stash 데이터 도착 시간)
		FTimerHandle UITimerHandle;
		GetWorldTimerManager().SetTimer(UITimerHandle, this, &AHellunaLobbyController::ShowLobbyWidget, 0.5f, false);
	}
	else
	{
		UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] 비로컬 컨트롤러 → UI 생성 스킵 (서버 또는 다른 클라의 PC)"));
	}
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
bool AHellunaLobbyController::Server_TransferItem_Validate(int32 ItemEntryIndex, ELobbyTransferDirection Direction)
{
	return ItemEntryIndex >= 0 && ItemEntryIndex < 10000
		&& (Direction == ELobbyTransferDirection::StashToLoadout || Direction == ELobbyTransferDirection::LoadoutToStash);
}

void AHellunaLobbyController::Server_TransferItem_Implementation(int32 ItemEntryIndex, ELobbyTransferDirection Direction)
{
	const FString DirectionStr = (Direction == ELobbyTransferDirection::StashToLoadout) ? TEXT("Stash→Loadout") : TEXT("Loadout→Stash");
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] ── Server_TransferItem 시작 ──"));
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC]   RepID=%d | Direction=%s"), ItemEntryIndex, *DirectionStr);

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
	const bool bSuccess = ExecuteTransfer(SourceComp, TargetComp, ItemEntryIndex);
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] Server_TransferItem %s | RepID=%d | Direction=%s"),
		bSuccess ? TEXT("성공") : TEXT("실패"), ItemEntryIndex, *DirectionStr);

	// TODO: [DragDrop] 추후 드래그앤드롭 크로스 패널 구현 시 여기에 연결
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
	int32 ItemReplicationID)
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
	const bool bResult = SourceComp->TransferItemTo(ActualIndex, TargetComp);

	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] ExecuteTransfer 결과: %s"), bResult ? TEXT("성공") : TEXT("실패"));
	return bResult;
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

	// ── [2단계] PlayerId 획득 ──
	// GameMode의 public 래퍼 사용 (protected GetPlayerSaveId에 직접 접근 불가)
	FString PlayerId;
	if (AHellunaLobbyGameMode* LobbyGM = GetWorld() ? GetWorld()->GetAuthGameMode<AHellunaLobbyGameMode>() : nullptr)
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

	// ── [3단계] Stash + Loadout 저장 ──
	if (LoadoutInventoryComponent)
	{
		TArray<FInv_SavedItemData> LoadoutItems = LoadoutInventoryComponent->CollectInventoryDataForSave();
		UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] Deploy [3]: Loadout 아이템 %d개 수집 완료"), LoadoutItems.Num());

		// ── [3a] 잔여 Stash 저장 (항상 실행 — 빈 로드아웃이라도 Stash 상태를 DB에 반영) ──
		// ⭐ [Fix] 이전: LoadoutItems > 0일 때만 실행 → 빈 로드아웃 시 이전 Stash가 DB에 잔존
		//   → MergeGameResultToStash(INSERT-only)에서 이전 Stash + 신규 결과 = 중복 발생
		//   수정: 항상 SavePlayerStash 호출하여 현재 Stash 상태를 정확히 DB에 반영
		if (StashInventoryComponent)
		{
			TArray<FInv_SavedItemData> RemainingStash = StashInventoryComponent->CollectInventoryDataForSave();
			const bool bStashOk = DB->SavePlayerStash(PlayerId, RemainingStash);
			UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] Deploy [3a]: 잔여 Stash 저장 %s | %d개 아이템"),
				bStashOk ? TEXT("성공") : TEXT("실패"), RemainingStash.Num());

			if (!bStashOk)
			{
				UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyPC] Deploy [3a]: 잔여 Stash 저장 실패! 출격 중단"));
				Client_DeployFailed(TEXT("Stash 저장 실패"));
				bDeployInProgress = false;
				return;
			}
		}

		if (LoadoutItems.Num() > 0)
		{
			// ── [3b] Loadout 저장 ──
			const bool bLoadoutOk = DB->SavePlayerLoadout(PlayerId, LoadoutItems);
			UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] Deploy [3b]: SavePlayerLoadout %s | %d개 아이템"),
				bLoadoutOk ? TEXT("성공") : TEXT("실패"), LoadoutItems.Num());

			if (!bLoadoutOk)
			{
				UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyPC] Deploy: Loadout 저장 실패! 출격 중단"));
				UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyPC]   → DB 오류 확인 필요 (디스크 용량, 권한 등)"));
				Client_DeployFailed(TEXT("Loadout 저장 실패"));
				bDeployInProgress = false;
				return;
			}

			// ── [3c] Loadout을 JSON 파일로도 내보내기 (DB 잠금 회피용) ──
			// 게임서버(데디서버)가 같은 DB를 열 수 없을 때,
			// 이 JSON 파일에서 Loadout을 읽어 인벤토리를 복원
			const int32 HeroIndex = HeroTypeToIndex(SelectedHeroType);
			const bool bExportOk = DB->ExportLoadoutToFile(PlayerId, LoadoutItems, HeroIndex);
			UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] Deploy [3c]: ExportLoadoutToFile %s"),
				bExportOk ? TEXT("성공") : TEXT("실패"));

			if (!bExportOk)
			{
				UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyPC] Deploy [3c]: JSON 파일 내보내기 실패! 출격 중단"));
				Client_DeployFailed(TEXT("Loadout 파일 내보내기 실패"));
				bDeployInProgress = false;
				return;
			}
		}
		else
		{
			UE_LOG(LogHellunaLobby, Warning, TEXT("[LobbyPC] Deploy: Loadout이 비어있음! 빈손 출격 진행 (Stash는 이미 저장됨)"));
		}
	}
	else
	{
		UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyPC] Deploy: LoadoutComp가 nullptr!"));
		Client_DeployFailed(TEXT("LoadoutComp가 nullptr"));
		bDeployInProgress = false;
		return;
	}

	// ── [4단계] ClientTravel 지시 (HeroType 파라미터 추가) ──
	if (!DeployMapURL.IsEmpty())
	{
		const int32 HeroIndex = HeroTypeToIndex(SelectedHeroType);
		// UE URL Options는 항상 '?' 구분자 사용 (ParseOption이 '?Key=Value'만 인식)
		const FString FinalURL = FString::Printf(TEXT("%s?HeroType=%d?PlayerId=%s"),
			*DeployMapURL, HeroIndex, *PlayerId);
		UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] Deploy [4]: Client_ExecuteDeploy → %s"), *FinalURL);
		Client_ExecuteDeploy(FinalURL);
	}
	else
	{
		UE_LOG(LogHellunaLobby, Warning, TEXT("[LobbyPC] Deploy: DeployMapURL이 비어있음!"));
		UE_LOG(LogHellunaLobby, Warning, TEXT("[LobbyPC]   → BP_HellunaLobbyController에서 DeployMapURL을 설정하세요"));
		UE_LOG(LogHellunaLobby, Warning, TEXT("[LobbyPC]   → 예: /Game/Maps/L_Defense?listen"));
		Client_DeployFailed(TEXT("DeployMapURL이 비어있음"));
		bDeployInProgress = false;
		return;
	}

	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] Server_Deploy 완료 | PlayerId=%s"), *PlayerId);
	bDeployInProgress = false;
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

		// V2 프리뷰 스폰 + CharSelectWidget 초기화
		SpawnPreviewSceneV2();

		UHellunaLobbyCharSelectWidget* CharSelectPanel = LobbyStashWidgetInstance->GetCharacterSelectPanel();
		if (CharSelectPanel && SpawnedPreviewSceneV2 && PreviewV2RenderTarget)
		{
			CharSelectPanel->SetupPreviewV2(PreviewV2RenderTarget, SpawnedPreviewSceneV2);
			UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] CharSelectPanel V2 프리뷰 설정 완료"));
		}

		UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] ── ShowLobbyWidget 완료 ──"));
		UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC]   위젯 생성 성공 | 마우스 커서 ON | UI 전용 입력 모드"));
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
}

// ════════════════════════════════════════════════════════════════════════════════
// EndPlay — V2 프리뷰 정리
// ════════════════════════════════════════════════════════════════════════════════
void AHellunaLobbyController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (LobbyStashWidgetInstance)
	{
		LobbyStashWidgetInstance->RemoveFromParent();
		LobbyStashWidgetInstance = nullptr;
	}

	DestroyPreviewSceneV2();
	Super::EndPlay(EndPlayReason);
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

	AHellunaLobbyGameMode* LobbyGM = GetWorld() ? GetWorld()->GetAuthGameMode<AHellunaLobbyGameMode>() : nullptr;
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

	// GameMode에서 가용성 체크 + 등록
	const FString PlayerId = LobbyGM->GetLobbyPlayerId(this);
	if (!LobbyGM->IsLobbyCharacterAvailable(HeroType))
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

	// RenderTarget 생성
	PreviewV2RenderTarget = NewObject<UTextureRenderTarget2D>(this);
	PreviewV2RenderTarget->InitCustomFormat(PreviewV2RenderTargetSize.X, PreviewV2RenderTargetSize.Y, PF_FloatRGBA, false);
	PreviewV2RenderTarget->ClearColor = FLinearColor::Transparent;
	PreviewV2RenderTarget->UpdateResourceImmediate(true);

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
	SpawnedPreviewSceneV2->InitializeScene(Meshes, AnimClasses, PreviewV2RenderTarget);

	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] V2 프리뷰 씬 스폰 및 초기화 완료"));
}

// ════════════════════════════════════════════════════════════════════════════════
// DestroyPreviewSceneV2 — V2 프리뷰 씬 파괴
// ════════════════════════════════════════════════════════════════════════════════
void AHellunaLobbyController::DestroyPreviewSceneV2()
{
	if (IsValid(SpawnedPreviewSceneV2))
	{
		SpawnedPreviewSceneV2->Destroy();
		SpawnedPreviewSceneV2 = nullptr;
		UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] V2 프리뷰 씬 파괴 완료"));
	}

	PreviewV2RenderTarget = nullptr;
}
