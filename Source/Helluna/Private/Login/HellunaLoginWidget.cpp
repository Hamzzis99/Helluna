#include "Login/HellunaLoginWidget.h"
#include "Login/HellunaLoginController.h"
#include "Components/EditableTextBox.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Kismet/GameplayStatics.h"

void UHellunaLoginWidget::NativeConstruct()
{
	Super::NativeConstruct();

	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║     [LoginWidget] NativeConstruct                          ║"));
	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));

	bool bHasError = false;
	if (!IDInputTextBox) { UE_LOG(LogTemp, Error, TEXT("[LoginWidget] IDInputTextBox 없음!")); bHasError = true; }
	if (!PasswordInputTextBox) { UE_LOG(LogTemp, Error, TEXT("[LoginWidget] PasswordInputTextBox 없음!")); bHasError = true; }
	if (!LoginButton) { UE_LOG(LogTemp, Error, TEXT("[LoginWidget] LoginButton 없음!")); bHasError = true; }
	if (!MessageText) { UE_LOG(LogTemp, Error, TEXT("[LoginWidget] MessageText 없음!")); bHasError = true; }

	if (bHasError)
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 10.f, FColor::Red,
				TEXT("[LoginWidget] 필수 위젯 없음!"));
		}
		return;
	}

	if (LoginButton)
	{
		LoginButton->OnClicked.AddDynamic(this, &UHellunaLoginWidget::OnLoginButtonClicked);
	}

	ShowMessage(TEXT("ID와 비밀번호를 입력하세요"), false);

	UE_LOG(LogTemp, Warning, TEXT("[LoginWidget] 초기화 완료"));
	UE_LOG(LogTemp, Warning, TEXT(""));
}

void UHellunaLoginWidget::OnLoginButtonClicked()
{
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("┌────────────────────────────────────────────────────────────┐"));
	UE_LOG(LogTemp, Warning, TEXT("│ [LoginWidget] OnLoginButtonClicked                         │"));
	UE_LOG(LogTemp, Warning, TEXT("└────────────────────────────────────────────────────────────┘"));

	FString PlayerId = GetPlayerId();
	FString Password = GetPassword();

	UE_LOG(LogTemp, Warning, TEXT("[LoginWidget] PlayerId: '%s'"), *PlayerId);

	if (PlayerId.IsEmpty())
	{
		ShowMessage(TEXT("아이디를 입력해주세요."), true);
		return;
	}

	if (Password.IsEmpty())
	{
		ShowMessage(TEXT("비밀번호를 입력해주세요."), true);
		return;
	}

	APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0);
	if (AHellunaLoginController* LoginController = Cast<AHellunaLoginController>(PC))
	{
		UE_LOG(LogTemp, Warning, TEXT("[LoginWidget] → LoginController->OnLoginButtonClicked 호출"));
		LoginController->OnLoginButtonClicked(PlayerId, Password);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[LoginWidget] LoginController 없음! (PC: %s)"), PC ? *PC->GetClass()->GetName() : TEXT("nullptr"));
		ShowMessage(TEXT("Controller 오류!"), true);
	}

	UE_LOG(LogTemp, Warning, TEXT(""));
}

void UHellunaLoginWidget::ShowMessage(const FString& Message, bool bIsError)
{
	if (MessageText)
	{
		MessageText->SetText(FText::FromString(Message));
		MessageText->SetColorAndOpacity(FSlateColor(bIsError ? FLinearColor::Red : FLinearColor::White));
	}
}

void UHellunaLoginWidget::SetLoadingState(bool bLoading)
{
	if (LoginButton)
	{
		LoginButton->SetIsEnabled(!bLoading);
	}
}

FString UHellunaLoginWidget::GetPlayerId() const
{
	return IDInputTextBox ? IDInputTextBox->GetText().ToString() : TEXT("");
}

FString UHellunaLoginWidget::GetPassword() const
{
	return PasswordInputTextBox ? PasswordInputTextBox->GetText().ToString() : TEXT("");
}

// ============================================
// 🎭 캐릭터 선택 시스템 (Phase 3)
// ============================================

void UHellunaLoginWidget::ShowCharacterSelection_Implementation(const TArray<bool>& AvailableCharacters)
{
	// 기본 구현: 로그만 출력
	// 실제 UI 표시는 BP에서 구현
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║  🎭 [LoginWidget] ShowCharacterSelection (기본 구현)       ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ ※ BP에서 이 함수를 오버라이드하여 캐릭터 선택 UI 구현     ║"));
	UE_LOG(LogTemp, Warning, TEXT("║ 선택 가능한 캐릭터:"));
	for (int32 i = 0; i < AvailableCharacters.Num(); i++)
	{
		UE_LOG(LogTemp, Warning, TEXT("║   [%d] %s"), i, AvailableCharacters[i] ? TEXT("✅ 선택 가능") : TEXT("❌ 사용 중"));
	}
	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
	
	ShowMessage(TEXT("캐릭터를 선택하세요"), false);
}

void UHellunaLoginWidget::OnCharacterSelected(int32 CharacterIndex)
{
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║  🎭 [LoginWidget] OnCharacterSelected                      ║"));
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
		UE_LOG(LogTemp, Warning, TEXT("[LoginWidget] → Server_SelectCharacter RPC 호출"));
		LoginController->Server_SelectCharacter(CharacterIndex);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[LoginWidget] LoginController 없음!"));
		ShowMessage(TEXT("Controller 오류!"), true);
		SetLoadingState(false);
	}
}
