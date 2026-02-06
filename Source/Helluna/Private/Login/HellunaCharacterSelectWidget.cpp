#include "Login/HellunaCharacterSelectWidget.h"
#include "Login/HellunaLoginController.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Kismet/GameplayStatics.h"
#include "GameMode/HellunaBaseGameState.h"

void UHellunaCharacterSelectWidget::NativeConstruct()
{
	Super::NativeConstruct();

	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║  🎭 [CharacterSelectWidget] NativeConstruct                ║"));
	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));

	// 버튼 이벤트 바인딩 (BindWidget이므로 버튼은 항상 존재)
	LuiButton->OnClicked.AddDynamic(this, &UHellunaCharacterSelectWidget::OnLuiButtonClicked);
	LunaButton->OnClicked.AddDynamic(this, &UHellunaCharacterSelectWidget::OnLunaButtonClicked);
	LiamButton->OnClicked.AddDynamic(this, &UHellunaCharacterSelectWidget::OnLiamButtonClicked);

	UE_LOG(LogTemp, Warning, TEXT("[CharacterSelectWidget] 버튼 바인딩 완료: Lui, Luna, Liam"));

	// ════════════════════════════════════════════════════════════════════════════════
	// 🎭 GameState 델리게이트 바인딩 - 다른 플레이어 캐릭터 선택 시 UI 자동 갱신
	// ════════════════════════════════════════════════════════════════════════════════
	if (AHellunaBaseGameState* GS = GetWorld()->GetGameState<AHellunaBaseGameState>())
	{
		GS->OnUsedCharactersChanged.AddDynamic(this, &UHellunaCharacterSelectWidget::OnCharacterAvailabilityChanged);
		UE_LOG(LogTemp, Warning, TEXT("[CharacterSelectWidget] ✅ GameState 델리게이트 바인딩 완료"));

		// 초기 상태 동기화
		RefreshAvailableCharacters();
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[CharacterSelectWidget] ⚠️ GameState 없음 - 델리게이트 바인딩 스킵"));
	}

	ShowMessage(TEXT("캐릭터를 선택하세요"), false);

	UE_LOG(LogTemp, Warning, TEXT("[CharacterSelectWidget] 초기화 완료"));
	UE_LOG(LogTemp, Warning, TEXT(""));
}

void UHellunaCharacterSelectWidget::SetAvailableCharacters(const TArray<bool>& AvailableCharacters)
{
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║  🎭 [CharacterSelectWidget] SetAvailableCharacters         ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));

	CachedAvailableCharacters = AvailableCharacters;

	// Lui (Index 0)
	if (AvailableCharacters.IsValidIndex(0))
	{
		LuiButton->SetIsEnabled(AvailableCharacters[0]);
		UE_LOG(LogTemp, Warning, TEXT("║   [0] Lui: %s"), AvailableCharacters[0] ? TEXT("✅ 선택 가능") : TEXT("❌ 사용 중"));
	}

	// Luna (Index 1)
	if (AvailableCharacters.IsValidIndex(1))
	{
		LunaButton->SetIsEnabled(AvailableCharacters[1]);
		UE_LOG(LogTemp, Warning, TEXT("║   [1] Luna: %s"), AvailableCharacters[1] ? TEXT("✅ 선택 가능") : TEXT("❌ 사용 중"));
	}

	// Liam (Index 2)
	if (AvailableCharacters.IsValidIndex(2))
	{
		LiamButton->SetIsEnabled(AvailableCharacters[2]);
		UE_LOG(LogTemp, Warning, TEXT("║   [2] Liam: %s"), AvailableCharacters[2] ? TEXT("✅ 선택 가능") : TEXT("❌ 사용 중"));
	}

	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
	UE_LOG(LogTemp, Warning, TEXT(""));
}

void UHellunaCharacterSelectWidget::ShowMessage(const FString& Message, bool bIsError)
{
	if (MessageText)
	{
		MessageText->SetText(FText::FromString(Message));
		MessageText->SetColorAndOpacity(FSlateColor(bIsError ? FLinearColor::Red : FLinearColor::White));
	}

	UE_LOG(LogTemp, Warning, TEXT("[CharacterSelectWidget] 메시지: %s (Error=%s)"), 
		*Message, bIsError ? TEXT("YES") : TEXT("NO"));
}

void UHellunaCharacterSelectWidget::SetLoadingState(bool bLoading)
{
	bIsLoading = bLoading;

	LuiButton->SetIsEnabled(!bLoading && CachedAvailableCharacters.IsValidIndex(0) && CachedAvailableCharacters[0]);
	LunaButton->SetIsEnabled(!bLoading && CachedAvailableCharacters.IsValidIndex(1) && CachedAvailableCharacters[1]);
	LiamButton->SetIsEnabled(!bLoading && CachedAvailableCharacters.IsValidIndex(2) && CachedAvailableCharacters[2]);

	UE_LOG(LogTemp, Warning, TEXT("[CharacterSelectWidget] 로딩 상태: %s"), bLoading ? TEXT("ON") : TEXT("OFF"));
}

void UHellunaCharacterSelectWidget::OnSelectionResult(bool bSuccess, const FString& ErrorMessage)
{
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║  🎭 [CharacterSelectWidget] OnSelectionResult              ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ Success: %s"), bSuccess ? TEXT("TRUE") : TEXT("FALSE"));
	if (!bSuccess)
	{
		UE_LOG(LogTemp, Warning, TEXT("║ Error: %s"), *ErrorMessage);
	}
	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));

	if (bSuccess)
	{
		ShowMessage(TEXT("캐릭터 선택 완료! 게임 시작..."), false);
		
		// ✅ UI 제거!
		UE_LOG(LogTemp, Warning, TEXT("[CharacterSelectWidget] ✅ 위젯 제거 (RemoveFromParent)"));
		RemoveFromParent();
	}
	else
	{
		ShowMessage(ErrorMessage.IsEmpty() ? TEXT("캐릭터 선택 실패") : ErrorMessage, true);
		SetLoadingState(false);  // 다시 선택 가능하게
	}
}

void UHellunaCharacterSelectWidget::OnLuiButtonClicked()
{
	UE_LOG(LogTemp, Warning, TEXT("[CharacterSelectWidget] 🔘 Lui 버튼 클릭됨"));
	SelectCharacter(0);
}

void UHellunaCharacterSelectWidget::OnLunaButtonClicked()
{
	UE_LOG(LogTemp, Warning, TEXT("[CharacterSelectWidget] 🔘 Luna 버튼 클릭됨"));
	SelectCharacter(1);
}

void UHellunaCharacterSelectWidget::OnLiamButtonClicked()
{
	UE_LOG(LogTemp, Warning, TEXT("[CharacterSelectWidget] 🔘 Liam 버튼 클릭됨"));
	SelectCharacter(2);
}

void UHellunaCharacterSelectWidget::SelectCharacter(int32 CharacterIndex)
{
	if (bIsLoading)
	{
		UE_LOG(LogTemp, Warning, TEXT("[CharacterSelectWidget] 이미 처리 중, 무시"));
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║  🎭 [CharacterSelectWidget] SelectCharacter                ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ CharacterIndex: %d"), CharacterIndex);
	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));

	// 로딩 상태로 전환
	SetLoadingState(true);
	ShowMessage(TEXT("캐릭터 선택 중..."), false);

	// LoginController를 통해 서버로 전송
	APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0);
	if (AHellunaLoginController* LoginController = Cast<AHellunaLoginController>(PC))
	{
		UE_LOG(LogTemp, Warning, TEXT("[CharacterSelectWidget] → Server_SelectCharacter(%d) RPC 호출"), CharacterIndex);
		LoginController->Server_SelectCharacter(CharacterIndex);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[CharacterSelectWidget] ❌ LoginController를 찾을 수 없음!"));
		ShowMessage(TEXT("컨트롤러 오류"), true);
		SetLoadingState(false);
	}
}

// ════════════════════════════════════════════════════════════════════════════════
// 🎭 GameState 델리게이트 핸들러 - 실시간 UI 동기화
// ════════════════════════════════════════════════════════════════════════════════

void UHellunaCharacterSelectWidget::OnCharacterAvailabilityChanged()
{
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║  🔄 [CharacterSelectWidget] OnCharacterAvailabilityChanged ║"));
	UE_LOG(LogTemp, Warning, TEXT("║     다른 플레이어가 캐릭터를 선택/해제함!                  ║"));
	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));

	RefreshAvailableCharacters();
}

void UHellunaCharacterSelectWidget::RefreshAvailableCharacters()
{
	AHellunaBaseGameState* GS = GetWorld()->GetGameState<AHellunaBaseGameState>();
	if (!GS)
	{
		UE_LOG(LogTemp, Warning, TEXT("[CharacterSelectWidget] ⚠️ RefreshAvailableCharacters - GameState 없음"));
		return;
	}

	// GameState에서 사용 중인 캐릭터 목록 가져와서 AvailableCharacters 배열 생성
	TArray<bool> AvailableCharacters;

	// 캐릭터 인덱스 → HeroType 매핑 (0=Lui, 1=Luna, 2=Liam)
	// HellunaTypes.h의 EHellunaHeroType 순서와 일치해야 함
	AvailableCharacters.Add(!GS->IsCharacterUsed(EHellunaHeroType::Lui));   // Index 0
	AvailableCharacters.Add(!GS->IsCharacterUsed(EHellunaHeroType::Luna));  // Index 1
	AvailableCharacters.Add(!GS->IsCharacterUsed(EHellunaHeroType::Liam));  // Index 2

	UE_LOG(LogTemp, Warning, TEXT("[CharacterSelectWidget] 🔄 UI 갱신: Lui=%s, Luna=%s, Liam=%s"),
		AvailableCharacters[0] ? TEXT("✅") : TEXT("❌"),
		AvailableCharacters[1] ? TEXT("✅") : TEXT("❌"),
		AvailableCharacters[2] ? TEXT("✅") : TEXT("❌"));

	// 기존 SetAvailableCharacters 함수 재사용
	SetAvailableCharacters(AvailableCharacters);
}
