#include "Login/Widget/HellunaLoginWidget.h"
#include "Login/Controller/HellunaLoginController.h"
#include "Login/Widget/HellunaCharacterSelectWidget.h"
#include "MDF_Function/MDF_Instance/MDF_GameInstance.h"
#include "Components/EditableTextBox.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Kismet/GameplayStatics.h"
#include "Helluna.h"

void UHellunaLoginWidget::NativeConstruct()
{
	Super::NativeConstruct();

#if HELLUNA_DEBUG_LOGIN
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║     [LoginWidget] NativeConstruct                          ║"));
	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
#endif

	bool bHasError = false;
#if HELLUNA_DEBUG_LOGIN
	if (!IDInputTextBox) { UE_LOG(LogTemp, Error, TEXT("[LoginWidget] IDInputTextBox 없음!")); bHasError = true; }
	if (!PasswordInputTextBox) { UE_LOG(LogTemp, Error, TEXT("[LoginWidget] PasswordInputTextBox 없음!")); bHasError = true; }
	if (!LoginButton) { UE_LOG(LogTemp, Error, TEXT("[LoginWidget] LoginButton 없음!")); bHasError = true; }
	if (!MessageText) { UE_LOG(LogTemp, Error, TEXT("[LoginWidget] MessageText 없음!")); bHasError = true; }
#else
	if (!IDInputTextBox) { bHasError = true; }
	if (!PasswordInputTextBox) { bHasError = true; }
	if (!LoginButton) { bHasError = true; }
	if (!MessageText) { bHasError = true; }
#endif

	if (bHasError)
	{
#if HELLUNA_DEBUG_LOGIN
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 10.f, FColor::Red,
				TEXT("[LoginWidget] 필수 위젯 없음!"));
		}
#endif
		return;
	}

	if (LoginButton)
	{
		LoginButton->OnClicked.AddUniqueDynamic(this, &UHellunaLoginWidget::OnLoginButtonClicked);
	}

	ShowMessage(TEXT("ID와 비밀번호를 입력하세요"), false);

#if HELLUNA_DEBUG_LOGIN
	UE_LOG(LogTemp, Warning, TEXT("[LoginWidget] 초기화 완료"));
	UE_LOG(LogTemp, Warning, TEXT(""));
#endif
}

void UHellunaLoginWidget::OnLoginButtonClicked()
{
#if HELLUNA_DEBUG_LOGIN
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("┌────────────────────────────────────────────────────────────┐"));
	UE_LOG(LogTemp, Warning, TEXT("│ [LoginWidget] OnLoginButtonClicked                         │"));
	UE_LOG(LogTemp, Warning, TEXT("└────────────────────────────────────────────────────────────┘"));
#endif

	FString PlayerId = GetPlayerId();
	FString Password = GetPassword();

#if HELLUNA_DEBUG_LOGIN
	UE_LOG(LogTemp, Warning, TEXT("[LoginWidget] PlayerId: '%s'"), *PlayerId);
#endif

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

	UWorld* World = GetWorld();
	if (!World) return;

	// 로딩 화면 표시 (RPC 호출 전)
	if (UGameInstance* GIBase = UGameplayStatics::GetGameInstance(World))
	{
		if (UMDF_GameInstance* GI = Cast<UMDF_GameInstance>(GIBase))
		{
			GI->ShowLoadingScreen(TEXT("로그인 중..."));
		}
	}

	APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0);
	if (AHellunaLoginController* LoginController = Cast<AHellunaLoginController>(PC))
	{
#if HELLUNA_DEBUG_LOGIN
		UE_LOG(LogTemp, Warning, TEXT("[LoginWidget] → LoginController->OnLoginButtonClicked 호출"));
#endif
		LoginController->OnLoginButtonClicked(PlayerId, Password);
	}
	else
	{
#if HELLUNA_DEBUG_LOGIN
		UE_LOG(LogTemp, Error, TEXT("[LoginWidget] LoginController 없음! (PC: %s)"), PC ? *PC->GetClass()->GetName() : TEXT("nullptr"));
#endif
		ShowMessage(TEXT("Controller 오류!"), true);

		// 로딩 화면 해제
		if (UGameInstance* GIBase2 = UGameplayStatics::GetGameInstance(World))
		{
			if (UMDF_GameInstance* GI2 = Cast<UMDF_GameInstance>(GIBase2))
			{
				GI2->HideLoadingScreen();
			}
		}
	}

#if HELLUNA_DEBUG_LOGIN
	UE_LOG(LogTemp, Warning, TEXT(""));
#endif
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
#if HELLUNA_DEBUG_LOGIN
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║  🎭 [LoginWidget] ShowCharacterSelection                   ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
#endif

	// 1. CharacterSelectWidgetClass 체크
	if (!CharacterSelectWidgetClass)
	{
#if HELLUNA_DEBUG_LOGIN
		UE_LOG(LogTemp, Error, TEXT("║ ❌ CharacterSelectWidgetClass가 설정되지 않음!"));
		UE_LOG(LogTemp, Error, TEXT("║    → BP에서 CharacterSelectWidgetClass 설정 필요"));
		UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
#endif

		ShowMessage(TEXT("캐릭터 선택 위젯 설정 오류!"), true);
		return;
	}

#if HELLUNA_DEBUG_LOGIN
	UE_LOG(LogTemp, Warning, TEXT("║ CharacterSelectWidgetClass: %s"), *CharacterSelectWidgetClass->GetName());
	UE_LOG(LogTemp, Warning, TEXT("║ → 로그인 UI 숨김"));
#endif

	// 2. 로그인 UI 숨김
	SetVisibility(ESlateVisibility::Collapsed);

	// 3. 캐릭터 선택 위젯 생성
	UWorld* CharSelectWorld = GetWorld();
	if (!CharSelectWorld)
	{
#if HELLUNA_DEBUG_LOGIN
		UE_LOG(LogTemp, Error, TEXT("║ ❌ World 없음!"));
		UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
#endif
		return;
	}
	APlayerController* PC = UGameplayStatics::GetPlayerController(CharSelectWorld, 0);
	if (!PC)
	{
#if HELLUNA_DEBUG_LOGIN
		UE_LOG(LogTemp, Error, TEXT("║ ❌ PlayerController 없음!"));
		UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
#endif
		return;
	}

	CharacterSelectWidget = CreateWidget<UHellunaCharacterSelectWidget>(PC, CharacterSelectWidgetClass);
	if (!CharacterSelectWidget)
	{
#if HELLUNA_DEBUG_LOGIN
		UE_LOG(LogTemp, Error, TEXT("║ ❌ 캐릭터 선택 위젯 생성 실패!"));
		UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
#endif

		SetVisibility(ESlateVisibility::Visible);
		ShowMessage(TEXT("캐릭터 선택 위젯 생성 실패!"), true);
		return;
	}

#if HELLUNA_DEBUG_LOGIN
	UE_LOG(LogTemp, Warning, TEXT("║ → 캐릭터 선택 위젯 생성 완료"));
#endif

	// 4. 뷰포트에 추가
	CharacterSelectWidget->AddToViewport(100);  // 높은 ZOrder
#if HELLUNA_DEBUG_LOGIN
	UE_LOG(LogTemp, Warning, TEXT("║ → 뷰포트에 추가 완료"));
#endif

	// 5. 선택 가능 캐릭터 설정
	CharacterSelectWidget->SetAvailableCharacters(AvailableCharacters);

#if HELLUNA_DEBUG_LOGIN
	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
	UE_LOG(LogTemp, Warning, TEXT(""));
#endif
}

