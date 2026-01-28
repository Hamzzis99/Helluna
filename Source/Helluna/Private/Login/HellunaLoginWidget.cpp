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
