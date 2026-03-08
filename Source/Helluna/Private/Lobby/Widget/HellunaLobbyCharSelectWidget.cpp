// ════════════════════════════════════════════════════════════════════════════════
// HellunaLobbyCharSelectWidget.cpp
// ════════════════════════════════════════════════════════════════════════════════
//
// 로비 캐릭터 선택 위젯 — 버튼 3개 + V2 3D 프리뷰 + 서버 RPC 연동
//
// 📌 흐름:
//   1. SetAvailableCharacters(UsedChars) → 사용 중인 캐릭터 버튼 Disable
//   2. SetupPreviewV2(RT, Scene) → 프리뷰 이미지에 머티리얼 바인딩
//   3. 버튼 클릭 → RequestCharacterSelection(Index)
//      → LobbyController->Server_SelectLobbyCharacter(Index)
//   4. 서버 응답 → OnSelectionResult(bSuccess, Message)
//      → 성공 시 OnCharacterSelected broadcast → StashWidget이 인벤토리 페이지로 전환
//
// 작성자: Gihyeon (Claude Code 보조)
// ════════════════════════════════════════════════════════════════════════════════

#include "Lobby/Widget/HellunaLobbyCharSelectWidget.h"
#include "Lobby/Controller/HellunaLobbyController.h"
#include "Login/Preview/HellunaCharacterSelectSceneV2.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"

// 로그 카테고리 (공유 헤더 — DEFINE은 HellunaLobbyGameMode.cpp)
#include "Lobby/HellunaLobbyLog.h"

// ════════════════════════════════════════════════════════════════════════════════
// NativeOnInitialized — 위젯 초기화
// ════════════════════════════════════════════════════════════════════════════════
void UHellunaLobbyCharSelectWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	UE_LOG(LogHellunaLobby, Log, TEXT("[CharSelect] NativeOnInitialized 시작"));

	// ── 버튼 OnClicked 바인딩 ──
	if (LuiButton)
	{
		LuiButton->OnClicked.AddUniqueDynamic(this, &ThisClass::OnLuiClicked);
		LuiButton->OnHovered.AddUniqueDynamic(this, &ThisClass::OnLuiHovered);
		LuiButton->OnUnhovered.AddUniqueDynamic(this, &ThisClass::OnLuiUnhovered);
	}

	if (LunaButton)
	{
		LunaButton->OnClicked.AddUniqueDynamic(this, &ThisClass::OnLunaClicked);
		LunaButton->OnHovered.AddUniqueDynamic(this, &ThisClass::OnLunaHovered);
		LunaButton->OnUnhovered.AddUniqueDynamic(this, &ThisClass::OnLunaUnhovered);
	}

	if (LiamButton)
	{
		LiamButton->OnClicked.AddUniqueDynamic(this, &ThisClass::OnLiamClicked);
		LiamButton->OnHovered.AddUniqueDynamic(this, &ThisClass::OnLiamHovered);
		LiamButton->OnUnhovered.AddUniqueDynamic(this, &ThisClass::OnLiamUnhovered);
	}

	UE_LOG(LogHellunaLobby, Log, TEXT("[CharSelect]   LuiButton=%s LunaButton=%s LiamButton=%s"),
		LuiButton ? TEXT("O") : TEXT("X"),
		LunaButton ? TEXT("O") : TEXT("X"),
		LiamButton ? TEXT("O") : TEXT("X"));
	UE_LOG(LogHellunaLobby, Log, TEXT("[CharSelect]   MessageText=%s (직접 뷰포트 모드 — RT 불필요)"),
		MessageText ? TEXT("O") : TEXT("X (Optional)"));
	UE_LOG(LogHellunaLobby, Log, TEXT("[CharSelect]   LuiNameText=%s LunaNameText=%s LiamNameText=%s"),
		LuiNameText ? TEXT("O") : TEXT("X (Optional)"),
		LunaNameText ? TEXT("O") : TEXT("X (Optional)"),
		LiamNameText ? TEXT("O") : TEXT("X (Optional)"));
	UE_LOG(LogHellunaLobby, Log, TEXT("[CharSelect] NativeOnInitialized 완료"));
}

// ════════════════════════════════════════════════════════════════════════════════
// NativeDestruct — 외부 참조 정리
// ════════════════════════════════════════════════════════════════════════════════
void UHellunaLobbyCharSelectWidget::NativeDestruct()
{
	CachedPreviewScene = nullptr;

	Super::NativeDestruct();
}

// ════════════════════════════════════════════════════════════════════════════════
// SetAvailableCharacters — 사용 중인 캐릭터 버튼 비활성화
// ════════════════════════════════════════════════════════════════════════════════
void UHellunaLobbyCharSelectWidget::SetAvailableCharacters(const TArray<bool>& InUsedCharacters)
{
	UE_LOG(LogHellunaLobby, Log, TEXT("[CharSelect] SetAvailableCharacters: %d개"),
		InUsedCharacters.Num());

	// InUsedCharacters: true = 사용중 = 비활성화
	if (LuiButton && InUsedCharacters.IsValidIndex(0))
	{
		LuiButton->SetIsEnabled(!InUsedCharacters[0]);
		UE_LOG(LogHellunaLobby, Log, TEXT("[CharSelect]   Lui: %s"),
			InUsedCharacters[0] ? TEXT("사용중 (비활성)") : TEXT("선택 가능"));
	}

	if (LunaButton && InUsedCharacters.IsValidIndex(1))
	{
		LunaButton->SetIsEnabled(!InUsedCharacters[1]);
		UE_LOG(LogHellunaLobby, Log, TEXT("[CharSelect]   Luna: %s"),
			InUsedCharacters[1] ? TEXT("사용중 (비활성)") : TEXT("선택 가능"));
	}

	if (LiamButton && InUsedCharacters.IsValidIndex(2))
	{
		LiamButton->SetIsEnabled(!InUsedCharacters[2]);
		UE_LOG(LogHellunaLobby, Log, TEXT("[CharSelect]   Liam: %s"),
			InUsedCharacters[2] ? TEXT("사용중 (비활성)") : TEXT("선택 가능"));
	}
}

// ════════════════════════════════════════════════════════════════════════════════
// SetupPreviewV2 — V2 프리뷰 씬 캐시 (직접 뷰포트 모드)
// ════════════════════════════════════════════════════════════════════════════════
void UHellunaLobbyCharSelectWidget::SetupPreviewV2(AHellunaCharacterSelectSceneV2* InScene)
{
	UE_LOG(LogHellunaLobby, Log, TEXT("[CharSelect] SetupPreviewV2 | Scene=%s (직접 뷰포트 모드)"),
		InScene ? TEXT("O") : TEXT("X"));

	CachedPreviewScene = InScene;
}

// ════════════════════════════════════════════════════════════════════════════════
// OnSelectionResult — 서버 응답 처리
// ════════════════════════════════════════════════════════════════════════════════
void UHellunaLobbyCharSelectWidget::OnSelectionResult(bool bSuccess, const FString& Message)
{
	UE_LOG(LogHellunaLobby, Log, TEXT("[CharSelect] OnSelectionResult | 성공=%s | 메시지=%s"),
		bSuccess ? TEXT("true") : TEXT("false"), *Message);

	// 메시지 표시
	if (MessageText)
	{
		MessageText->SetText(FText::FromString(Message));
	}

	if (bSuccess && PendingSelectionIndex >= 0 && PendingSelectionIndex <= 2)
	{
		const EHellunaHeroType SelectedType = IndexToHeroType(PendingSelectionIndex);
		UE_LOG(LogHellunaLobby, Log, TEXT("[CharSelect] 캐릭터 선택 성공 → OnCharacterSelected broadcast | Hero=%d"), PendingSelectionIndex);
		OnCharacterSelected.Broadcast(SelectedType);
	}

	PendingSelectionIndex = -1;
}

// ════════════════════════════════════════════════════════════════════════════════
// 버튼 콜백
// ════════════════════════════════════════════════════════════════════════════════
void UHellunaLobbyCharSelectWidget::OnLuiClicked()    { RequestCharacterSelection(0); }
void UHellunaLobbyCharSelectWidget::OnLunaClicked()   { RequestCharacterSelection(1); }
void UHellunaLobbyCharSelectWidget::OnLiamClicked()   { RequestCharacterSelection(2); }

void UHellunaLobbyCharSelectWidget::OnLuiHovered()
{
	if (CachedPreviewScene) CachedPreviewScene->SetCharacterHovered(0, true);
}
void UHellunaLobbyCharSelectWidget::OnLunaHovered()
{
	if (CachedPreviewScene) CachedPreviewScene->SetCharacterHovered(1, true);
}
void UHellunaLobbyCharSelectWidget::OnLiamHovered()
{
	if (CachedPreviewScene) CachedPreviewScene->SetCharacterHovered(2, true);
}

void UHellunaLobbyCharSelectWidget::OnLuiUnhovered()
{
	if (CachedPreviewScene) CachedPreviewScene->SetCharacterHovered(0, false);
}
void UHellunaLobbyCharSelectWidget::OnLunaUnhovered()
{
	if (CachedPreviewScene) CachedPreviewScene->SetCharacterHovered(1, false);
}
void UHellunaLobbyCharSelectWidget::OnLiamUnhovered()
{
	if (CachedPreviewScene) CachedPreviewScene->SetCharacterHovered(2, false);
}

// ════════════════════════════════════════════════════════════════════════════════
// RequestCharacterSelection — 서버에 캐릭터 선택 요청
// ════════════════════════════════════════════════════════════════════════════════
void UHellunaLobbyCharSelectWidget::RequestCharacterSelection(int32 CharacterIndex)
{
	UE_LOG(LogHellunaLobby, Log, TEXT("[CharSelect] RequestCharacterSelection | Index=%d"), CharacterIndex);

	AHellunaLobbyController* LobbyPC = GetLobbyController();
	if (!LobbyPC)
	{
		UE_LOG(LogHellunaLobby, Warning, TEXT("[CharSelect] LobbyController 없음!"));
		return;
	}

	PendingSelectionIndex = CharacterIndex;

	// 선택 연출 — 클릭한 캐릭터를 즉시 앞으로 이동 + 조명 강조
	if (CachedPreviewScene)
	{
		CachedPreviewScene->SetCharacterSelected(CharacterIndex);
	}

	if (MessageText)
	{
		MessageText->SetText(FText::FromString(TEXT("선택 중...")));
	}

	LobbyPC->Server_SelectLobbyCharacter(CharacterIndex);
}

// ════════════════════════════════════════════════════════════════════════════════
// GetLobbyController
// ════════════════════════════════════════════════════════════════════════════════
AHellunaLobbyController* UHellunaLobbyCharSelectWidget::GetLobbyController() const
{
	APlayerController* PC = GetOwningPlayer();
	return PC ? Cast<AHellunaLobbyController>(PC) : nullptr;
}
