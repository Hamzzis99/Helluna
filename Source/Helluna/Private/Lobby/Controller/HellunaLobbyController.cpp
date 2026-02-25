// ════════════════════════════════════════════════════════════════════════════════
// HellunaLobbyController.cpp
// ════════════════════════════════════════════════════════════════════════════════
//
// 로비 전용 PlayerController — StashComp + LoadoutComp 듀얼 인벤토리
//
// 📌 핵심 기능:
//   - StashComp / LoadoutComp 2개 InventoryComponent 생성
//   - Server_TransferItem RPC로 아이템 이동
//   - Server_Deploy RPC로 출격 (SQLite 저장 + ClientTravel)
//
// 작성자: Gihyeon (Claude Code 보조)
// ════════════════════════════════════════════════════════════════════════════════

#include "Lobby/Controller/HellunaLobbyController.h"
#include "Lobby/GameMode/HellunaLobbyGameMode.h"
#include "Lobby/Widget/HellunaLobbyStashWidget.h"
#include "Lobby/Database/HellunaSQLiteSubsystem.h"
#include "InventoryManagement/Components/Inv_InventoryComponent.h"
#include "Blueprint/UserWidget.h"

// 로그 카테고리 (HellunaLobbyGameMode.cpp에서 정의됨)
DECLARE_LOG_CATEGORY_EXTERN(LogHellunaLobby, Log, All);

// ════════════════════════════════════════════════════════════════════════════════
// 생성자
// ════════════════════════════════════════════════════════════════════════════════
AHellunaLobbyController::AHellunaLobbyController()
{
	// ── StashComp 생성 (전체 보유 아이템) ──
	StashInventoryComponent = CreateDefaultSubobject<UInv_InventoryComponent>(TEXT("StashInventoryComponent"));

	// ── LoadoutComp 생성 (출격할 아이템) ──
	LoadoutInventoryComponent = CreateDefaultSubobject<UInv_InventoryComponent>(TEXT("LoadoutInventoryComponent"));

	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] 생성자: StashComp + LoadoutComp 생성 완료"));
}

// ════════════════════════════════════════════════════════════════════════════════
// BeginPlay
// ════════════════════════════════════════════════════════════════════════════════
void AHellunaLobbyController::BeginPlay()
{
	Super::BeginPlay();

	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] BeginPlay | IsLocalController=%s | HasAuthority=%s"),
		IsLocalController() ? TEXT("true") : TEXT("false"),
		HasAuthority() ? TEXT("true") : TEXT("false"));

	// 클라이언트에서 자동으로 UI 표시 (로컬 컨트롤러일 때)
	if (IsLocalController())
	{
		// 약간의 딜레이 후 UI 생성 (컴포넌트 초기화 대기)
		FTimerHandle UITimerHandle;
		GetWorldTimerManager().SetTimer(UITimerHandle, this, &AHellunaLobbyController::ShowLobbyWidget, 0.5f, false);
	}
}

// ════════════════════════════════════════════════════════════════════════════════
// Server_TransferItem — Stash ↔ Loadout 아이템 전송
// ════════════════════════════════════════════════════════════════════════════════
bool AHellunaLobbyController::Server_TransferItem_Validate(int32 ItemEntryIndex, ELobbyTransferDirection Direction)
{
	return ItemEntryIndex >= 0;
}

void AHellunaLobbyController::Server_TransferItem_Implementation(int32 ItemEntryIndex, ELobbyTransferDirection Direction)
{
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] Server_TransferItem | EntryIndex=%d | Direction=%s"),
		ItemEntryIndex, Direction == ELobbyTransferDirection::StashToLoadout ? TEXT("Stash→Loadout") : TEXT("Loadout→Stash"));

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
		UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyPC] Server_TransferItem: Source 또는 Target 컴포넌트가 nullptr!"));
		return;
	}

	const bool bSuccess = ExecuteTransfer(SourceComp, TargetComp, ItemEntryIndex);
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] Server_TransferItem %s | EntryIndex=%d"),
		bSuccess ? TEXT("성공") : TEXT("실패"), ItemEntryIndex);

	// TODO: [DragDrop] 추후 드래그앤드롭 크로스 패널 구현 시 여기에 연결
}

// ════════════════════════════════════════════════════════════════════════════════
// ExecuteTransfer — 실제 아이템 전송 (서버 전용)
// ════════════════════════════════════════════════════════════════════════════════
//
// InvComp의 TransferItemTo()에 위임 — FastArray 내부 접근은 플러그인 내부에서 처리
// (FInv_InventoryFastArray의 멤버가 INVENTORY_API 미노출이므로 Helluna 모듈에서 직접 접근 불가)
//
bool AHellunaLobbyController::ExecuteTransfer(
	UInv_InventoryComponent* SourceComp,
	UInv_InventoryComponent* TargetComp,
	int32 ItemEntryIndex)
{
	if (!SourceComp || !TargetComp)
	{
		return false;
	}

	return SourceComp->TransferItemTo(ItemEntryIndex, TargetComp);
}

// ════════════════════════════════════════════════════════════════════════════════
// Server_Deploy — 출격
// ════════════════════════════════════════════════════════════════════════════════
bool AHellunaLobbyController::Server_Deploy_Validate()
{
	return true;
}

void AHellunaLobbyController::Server_Deploy_Implementation()
{
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] Server_Deploy 시작"));

	// ── SQLite 서브시스템 획득 ──
	UGameInstance* GI = GetGameInstance();
	UHellunaSQLiteSubsystem* DB = GI ? GI->GetSubsystem<UHellunaSQLiteSubsystem>() : nullptr;
	if (!DB || !DB->IsDatabaseReady())
	{
		UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyPC] Server_Deploy: SQLite 서브시스템 없음!"));
		return;
	}

	// ── PlayerId 획득 (GameMode의 GetLobbyPlayerId 사용) ──
	FString PlayerId;
	if (AHellunaLobbyGameMode* LobbyGM = GetWorld() ? GetWorld()->GetAuthGameMode<AHellunaLobbyGameMode>() : nullptr)
	{
		PlayerId = LobbyGM->GetLobbyPlayerId(this);
	}
	if (PlayerId.IsEmpty())
	{
		UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyPC] Server_Deploy: PlayerId가 비어있음!"));
		return;
	}

	// ── 1) LoadoutComp → SQLite SavePlayerLoadout (원자적: Loadout INSERT + Stash DELETE) ──
	if (LoadoutInventoryComponent)
	{
		TArray<FInv_SavedItemData> LoadoutItems = LoadoutInventoryComponent->CollectInventoryDataForSave();
		UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] Deploy: Loadout 아이템 %d개 → SQLite 저장"), LoadoutItems.Num());

		if (LoadoutItems.Num() > 0)
		{
			// 먼저 현재 Stash도 저장 (잔여 아이템)
			if (StashInventoryComponent)
			{
				TArray<FInv_SavedItemData> RemainingStash = StashInventoryComponent->CollectInventoryDataForSave();
				DB->SavePlayerStash(PlayerId, RemainingStash);
				UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] Deploy: 잔여 Stash %d개 저장 완료"), RemainingStash.Num());
			}

			// Loadout 저장 (원자적: INSERT loadout + DELETE stash)
			const bool bOk = DB->SavePlayerLoadout(PlayerId, LoadoutItems);
			UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] Deploy: SavePlayerLoadout %s | %d개 아이템"),
				bOk ? TEXT("성공") : TEXT("실패"), LoadoutItems.Num());

			if (!bOk)
			{
				UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyPC] Deploy: Loadout 저장 실패! 출격 중단"));
				return;
			}
		}
		else
		{
			UE_LOG(LogHellunaLobby, Warning, TEXT("[LobbyPC] Deploy: Loadout이 비어있음! 빈손 출격"));
		}
	}

	// ── 2) ClientTravel 지시 ──
	if (!DeployMapURL.IsEmpty())
	{
		UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] Deploy: ClientTravel → %s"), *DeployMapURL);
		Client_ExecuteDeploy(DeployMapURL);
	}
	else
	{
		UE_LOG(LogHellunaLobby, Warning, TEXT("[LobbyPC] Deploy: DeployMapURL이 비어있음! BP에서 설정 필요"));
	}
}

// ════════════════════════════════════════════════════════════════════════════════
// Client_ExecuteDeploy — 클라이언트에서 맵 이동
// ════════════════════════════════════════════════════════════════════════════════
void AHellunaLobbyController::Client_ExecuteDeploy_Implementation(const FString& TravelURL)
{
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] Client_ExecuteDeploy: ClientTravel → %s"), *TravelURL);
	ClientTravel(TravelURL, TRAVEL_Absolute);
}

// ════════════════════════════════════════════════════════════════════════════════
// Client_ShowLobbyUI — 서버에서 UI 생성 지시
// ════════════════════════════════════════════════════════════════════════════════
void AHellunaLobbyController::Client_ShowLobbyUI_Implementation()
{
	ShowLobbyWidget();
}

// ════════════════════════════════════════════════════════════════════════════════
// ShowLobbyWidget — 로비 UI 생성 및 표시
// ════════════════════════════════════════════════════════════════════════════════
void AHellunaLobbyController::ShowLobbyWidget()
{
	if (LobbyStashWidgetInstance)
	{
		UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] ShowLobbyWidget: 이미 위젯 존재 → 스킵"));
		return;
	}

	if (!LobbyStashWidgetClass)
	{
		UE_LOG(LogHellunaLobby, Warning, TEXT("[LobbyPC] ShowLobbyWidget: LobbyStashWidgetClass가 설정되지 않음! BP에서 설정 필요"));
		return;
	}

	LobbyStashWidgetInstance = CreateWidget<UHellunaLobbyStashWidget>(this, LobbyStashWidgetClass);
	if (LobbyStashWidgetInstance)
	{
		LobbyStashWidgetInstance->AddToViewport();
		LobbyStashWidgetInstance->InitializePanels(StashInventoryComponent, LoadoutInventoryComponent);

		// 마우스 커서 표시 (로비는 마우스 조작)
		SetShowMouseCursor(true);
		SetInputMode(FInputModeUIOnly());

		UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPC] ShowLobbyWidget: 로비 위젯 생성 + Viewport 추가 완료"));
	}
	else
	{
		UE_LOG(LogHellunaLobby, Error, TEXT("[LobbyPC] ShowLobbyWidget: 위젯 생성 실패!"));
	}
}
