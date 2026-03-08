#include "Login/Widget/HellunaCharacterSelectWidget.h"
#include "Login/Controller/HellunaLoginController.h"
#include "Helluna.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Kismet/GameplayStatics.h"
#include "GameMode/HellunaBaseGameState.h"

// ════════════════════════════════════════════════════════════════════════════════
// 📌 NativeConstruct — 공통 초기화
// ════════════════════════════════════════════════════════════════════════════════

void UHellunaCharacterSelectWidget::NativeConstruct()
{
	Super::NativeConstruct();

#if HELLUNA_DEBUG_CHARACTER_SELECT
	UE_LOG(LogHelluna, Warning, TEXT(""));
	UE_LOG(LogHelluna, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogHelluna, Warning, TEXT("║  [캐릭터선택위젯] NativeConstruct (베이스)                  ║"));
	UE_LOG(LogHelluna, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
#endif

	// ════════════════════════════════════════════
	// 📌 버튼 클릭 이벤트 바인딩 (중복 바인딩 방지)
	// ════════════════════════════════════════════
	if (LuiButton && LunaButton && LiamButton
		&& !LuiButton->OnClicked.IsAlreadyBound(this, &UHellunaCharacterSelectWidget::OnLuiButtonClicked))
	{
		LuiButton->OnClicked.AddUniqueDynamic(this, &UHellunaCharacterSelectWidget::OnLuiButtonClicked);
		LunaButton->OnClicked.AddUniqueDynamic(this, &UHellunaCharacterSelectWidget::OnLunaButtonClicked);
		LiamButton->OnClicked.AddUniqueDynamic(this, &UHellunaCharacterSelectWidget::OnLiamButtonClicked);

		// ════════════════════════════════════════════
		// 📌 호버 이벤트 바인딩 (베이스에서 한 번만)
		// ════════════════════════════════════════════
		LuiButton->OnHovered.AddUniqueDynamic(this, &UHellunaCharacterSelectWidget::OnPreviewHovered_Lui);
		LuiButton->OnUnhovered.AddUniqueDynamic(this, &UHellunaCharacterSelectWidget::OnPreviewUnhovered_Lui);
		LunaButton->OnHovered.AddUniqueDynamic(this, &UHellunaCharacterSelectWidget::OnPreviewHovered_Luna);
		LunaButton->OnUnhovered.AddUniqueDynamic(this, &UHellunaCharacterSelectWidget::OnPreviewUnhovered_Luna);
		LiamButton->OnHovered.AddUniqueDynamic(this, &UHellunaCharacterSelectWidget::OnPreviewHovered_Liam);
		LiamButton->OnUnhovered.AddUniqueDynamic(this, &UHellunaCharacterSelectWidget::OnPreviewUnhovered_Liam);
	}
	else if (!LuiButton || !LunaButton || !LiamButton)
	{
		UE_LOG(LogTemp, Error, TEXT("[캐릭터선택위젯] 버튼 nullptr! Lui=%d, Luna=%d, Liam=%d"),
			LuiButton != nullptr, LunaButton != nullptr, LiamButton != nullptr);
	}

#if HELLUNA_DEBUG_CHARACTER_SELECT
	UE_LOG(LogHelluna, Warning, TEXT("[캐릭터선택위젯] 버튼 클릭/호버 바인딩 완료"));
#endif

	// ════════════════════════════════════════════
	// 📌 GameState 델리게이트 바인딩 — 다른 플레이어 캐릭터 선택 시 UI 자동 갱신
	// ════════════════════════════════════════════
	UWorld* World = GetWorld();
	if (!World) return;
	if (AHellunaBaseGameState* GS = World->GetGameState<AHellunaBaseGameState>())
	{
		GS->OnUsedCharactersChanged.AddUniqueDynamic(this, &UHellunaCharacterSelectWidget::OnCharacterAvailabilityChanged);
#if HELLUNA_DEBUG_CHARACTER_SELECT
		UE_LOG(LogHelluna, Warning, TEXT("[캐릭터선택위젯] GameState 델리게이트 바인딩 완료"));
#endif
		RefreshAvailableCharacters();
	}
	else
	{
#if HELLUNA_DEBUG_CHARACTER_SELECT
		UE_LOG(LogHelluna, Warning, TEXT("[캐릭터선택위젯] GameState 없음 - 델리게이트 바인딩 스킵"));
#endif
	}

	ShowMessage(TEXT("캐릭터를 선택하세요"), false);

#if HELLUNA_DEBUG_CHARACTER_SELECT
	UE_LOG(LogHelluna, Warning, TEXT("[캐릭터선택위젯] 초기화 완료"));
#endif
}

// ════════════════════════════════════════════════════════════════════════════════
// 📌 NativeDestruct — 델리게이트 해제
// ════════════════════════════════════════════════════════════════════════════════

void UHellunaCharacterSelectWidget::NativeDestruct()
{
	if (UWorld* World = GetWorld())
	{
		if (AHellunaBaseGameState* GS = World->GetGameState<AHellunaBaseGameState>())
		{
			GS->OnUsedCharactersChanged.RemoveDynamic(this, &UHellunaCharacterSelectWidget::OnCharacterAvailabilityChanged);
		}
	}

	Super::NativeDestruct();
}

// ════════════════════════════════════════════════════════════════════════════════
// 📌 공통 함수
// ════════════════════════════════════════════════════════════════════════════════

void UHellunaCharacterSelectWidget::SetAvailableCharacters(const TArray<bool>& AvailableCharacters)
{
#if HELLUNA_DEBUG_CHARACTER_SELECT
	UE_LOG(LogHelluna, Warning, TEXT(""));
	UE_LOG(LogHelluna, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogHelluna, Warning, TEXT("║  [캐릭터선택위젯] SetAvailableCharacters                   ║"));
	UE_LOG(LogHelluna, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
#endif

	CachedAvailableCharacters = AvailableCharacters;

	if (AvailableCharacters.IsValidIndex(0) && LuiButton)
	{
		LuiButton->SetIsEnabled(AvailableCharacters[0]);
#if HELLUNA_DEBUG_CHARACTER_SELECT
		UE_LOG(LogHelluna, Warning, TEXT("║   [0] Lui: %s"), AvailableCharacters[0] ? TEXT("선택 가능") : TEXT("사용 중"));
#endif
	}

	if (AvailableCharacters.IsValidIndex(1) && LunaButton)
	{
		LunaButton->SetIsEnabled(AvailableCharacters[1]);
#if HELLUNA_DEBUG_CHARACTER_SELECT
		UE_LOG(LogHelluna, Warning, TEXT("║   [1] Luna: %s"), AvailableCharacters[1] ? TEXT("선택 가능") : TEXT("사용 중"));
#endif
	}

	if (AvailableCharacters.IsValidIndex(2) && LiamButton)
	{
		LiamButton->SetIsEnabled(AvailableCharacters[2]);
#if HELLUNA_DEBUG_CHARACTER_SELECT
		UE_LOG(LogHelluna, Warning, TEXT("║   [2] Liam: %s"), AvailableCharacters[2] ? TEXT("선택 가능") : TEXT("사용 중"));
#endif
	}

#if HELLUNA_DEBUG_CHARACTER_SELECT
	UE_LOG(LogHelluna, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
#endif
}

void UHellunaCharacterSelectWidget::ShowMessage(const FString& Message, bool bIsError)
{
	if (MessageText)
	{
		MessageText->SetText(FText::FromString(Message));
		MessageText->SetColorAndOpacity(FSlateColor(bIsError ? FLinearColor::Red : FLinearColor::White));
	}

#if HELLUNA_DEBUG_CHARACTER_SELECT
	UE_LOG(LogHelluna, Warning, TEXT("[캐릭터선택위젯] 메시지: %s (Error=%s)"),
		*Message, bIsError ? TEXT("YES") : TEXT("NO"));
#endif
}

void UHellunaCharacterSelectWidget::SetLoadingState(bool bLoading)
{
	bIsLoading = bLoading;

	if (LuiButton)
	{
		LuiButton->SetIsEnabled(!bLoading && CachedAvailableCharacters.IsValidIndex(0) && CachedAvailableCharacters[0]);
	}
	if (LunaButton)
	{
		LunaButton->SetIsEnabled(!bLoading && CachedAvailableCharacters.IsValidIndex(1) && CachedAvailableCharacters[1]);
	}
	if (LiamButton)
	{
		LiamButton->SetIsEnabled(!bLoading && CachedAvailableCharacters.IsValidIndex(2) && CachedAvailableCharacters[2]);
	}

#if HELLUNA_DEBUG_CHARACTER_SELECT
	UE_LOG(LogHelluna, Warning, TEXT("[캐릭터선택위젯] 로딩 상태: %s"), bLoading ? TEXT("ON") : TEXT("OFF"));
#endif
}

void UHellunaCharacterSelectWidget::OnSelectionResult(bool bSuccess, const FString& ErrorMessage)
{
#if HELLUNA_DEBUG_CHARACTER_SELECT
	UE_LOG(LogHelluna, Warning, TEXT(""));
	UE_LOG(LogHelluna, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogHelluna, Warning, TEXT("║  [캐릭터선택위젯] OnSelectionResult                        ║"));
	UE_LOG(LogHelluna, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogHelluna, Warning, TEXT("║ Success: %s"), bSuccess ? TEXT("TRUE") : TEXT("FALSE"));
	if (!bSuccess)
	{
		UE_LOG(LogHelluna, Warning, TEXT("║ Error: %s"), *ErrorMessage);
	}
	UE_LOG(LogHelluna, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
#endif

	if (bSuccess)
	{
		ShowMessage(TEXT("캐릭터 선택 완료! 게임 시작..."), false);
		CleanupPreview();

#if HELLUNA_DEBUG_CHARACTER_SELECT
		UE_LOG(LogHelluna, Warning, TEXT("[캐릭터선택위젯] 위젯 제거 (RemoveFromParent)"));
#endif
		RemoveFromParent();
	}
	else
	{
		ShowMessage(ErrorMessage.IsEmpty() ? TEXT("캐릭터 선택 실패") : ErrorMessage, true);
		SetLoadingState(false);
	}
}

// ════════════════════════════════════════════════════════════════════════════════
// 📌 버튼 클릭 핸들러
// ════════════════════════════════════════════════════════════════════════════════

void UHellunaCharacterSelectWidget::OnLuiButtonClicked()
{
#if HELLUNA_DEBUG_CHARACTER_SELECT
	UE_LOG(LogHelluna, Warning, TEXT("[캐릭터선택위젯] Lui 버튼 클릭"));
#endif
	SelectCharacter(0);
}

void UHellunaCharacterSelectWidget::OnLunaButtonClicked()
{
#if HELLUNA_DEBUG_CHARACTER_SELECT
	UE_LOG(LogHelluna, Warning, TEXT("[캐릭터선택위젯] Luna 버튼 클릭"));
#endif
	SelectCharacter(1);
}

void UHellunaCharacterSelectWidget::OnLiamButtonClicked()
{
#if HELLUNA_DEBUG_CHARACTER_SELECT
	UE_LOG(LogHelluna, Warning, TEXT("[캐릭터선택위젯] Liam 버튼 클릭"));
#endif
	SelectCharacter(2);
}

void UHellunaCharacterSelectWidget::SelectCharacter(int32 CharacterIndex)
{
	if (bIsLoading)
	{
#if HELLUNA_DEBUG_CHARACTER_SELECT
		UE_LOG(LogHelluna, Warning, TEXT("[캐릭터선택위젯] 이미 처리 중, 무시"));
#endif
		return;
	}

#if HELLUNA_DEBUG_CHARACTER_SELECT
	UE_LOG(LogHelluna, Warning, TEXT(""));
	UE_LOG(LogHelluna, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogHelluna, Warning, TEXT("║  [캐릭터선택위젯] SelectCharacter                          ║"));
	UE_LOG(LogHelluna, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogHelluna, Warning, TEXT("║ CharacterIndex: %d"), CharacterIndex);
	UE_LOG(LogHelluna, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
#endif

	SetLoadingState(true);
	ShowMessage(TEXT("캐릭터 선택 중..."), false);

	APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0);
	if (AHellunaLoginController* LoginController = Cast<AHellunaLoginController>(PC))
	{
#if HELLUNA_DEBUG_CHARACTER_SELECT
		UE_LOG(LogHelluna, Warning, TEXT("[캐릭터선택위젯] Server_SelectCharacter(%d) RPC 호출"), CharacterIndex);
#endif
		LoginController->Server_SelectCharacter(CharacterIndex);
	}
	else
	{
		UE_LOG(LogHelluna, Error, TEXT("[캐릭터선택위젯] LoginController를 찾을 수 없음!"));
		ShowMessage(TEXT("컨트롤러 오류"), true);
		SetLoadingState(false);
	}
}

// ════════════════════════════════════════════════════════════════════════════════
// 📌 GameState 델리게이트 핸들러 — 실시간 UI 동기화
// ════════════════════════════════════════════════════════════════════════════════

void UHellunaCharacterSelectWidget::OnCharacterAvailabilityChanged()
{
#if HELLUNA_DEBUG_CHARACTER_SELECT
	UE_LOG(LogHelluna, Warning, TEXT(""));
	UE_LOG(LogHelluna, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogHelluna, Warning, TEXT("║  [캐릭터선택위젯] OnCharacterAvailabilityChanged           ║"));
	UE_LOG(LogHelluna, Warning, TEXT("║     다른 플레이어가 캐릭터를 선택/해제함!                  ║"));
	UE_LOG(LogHelluna, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
#endif

	RefreshAvailableCharacters();
}

void UHellunaCharacterSelectWidget::RefreshAvailableCharacters()
{
	UWorld* RefreshWorld = GetWorld();
	if (!RefreshWorld) return;
	AHellunaBaseGameState* GS = RefreshWorld->GetGameState<AHellunaBaseGameState>();
	if (!GS)
	{
#if HELLUNA_DEBUG_CHARACTER_SELECT
		UE_LOG(LogHelluna, Warning, TEXT("[캐릭터선택위젯] RefreshAvailableCharacters - GameState 없음"));
#endif
		return;
	}

	TArray<bool> AvailableCharacters;
	AvailableCharacters.Add(!GS->IsCharacterUsed(EHellunaHeroType::Lui));
	AvailableCharacters.Add(!GS->IsCharacterUsed(EHellunaHeroType::Luna));
	AvailableCharacters.Add(!GS->IsCharacterUsed(EHellunaHeroType::Liam));

#if HELLUNA_DEBUG_CHARACTER_SELECT
	UE_LOG(LogHelluna, Warning, TEXT("[캐릭터선택위젯] UI 갱신: Lui=%s, Luna=%s, Liam=%s"),
		AvailableCharacters[0] ? TEXT("가능") : TEXT("사용중"),
		AvailableCharacters[1] ? TEXT("가능") : TEXT("사용중"),
		AvailableCharacters[2] ? TEXT("가능") : TEXT("사용중"));
#endif

	SetAvailableCharacters(AvailableCharacters);
}

// ════════════════════════════════════════════════════════════════════════════════
// 📌 프리뷰 Hover 이벤트 핸들러 → virtual OnCharacterHovered 호출
// ════════════════════════════════════════════════════════════════════════════════

void UHellunaCharacterSelectWidget::OnPreviewHovered_Lui()
{
	OnCharacterHovered(0, true);
}

void UHellunaCharacterSelectWidget::OnPreviewUnhovered_Lui()
{
	OnCharacterHovered(0, false);
}

void UHellunaCharacterSelectWidget::OnPreviewHovered_Luna()
{
	OnCharacterHovered(1, true);
}

void UHellunaCharacterSelectWidget::OnPreviewUnhovered_Luna()
{
	OnCharacterHovered(1, false);
}

void UHellunaCharacterSelectWidget::OnPreviewHovered_Liam()
{
	OnCharacterHovered(2, true);
}

void UHellunaCharacterSelectWidget::OnPreviewUnhovered_Liam()
{
	OnCharacterHovered(2, false);
}
