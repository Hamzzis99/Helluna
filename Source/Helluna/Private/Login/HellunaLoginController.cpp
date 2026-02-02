#include "Login/HellunaLoginController.h"
#include "Login/HellunaLoginWidget.h"
#include "Login/HellunaCharacterSelectWidget.h"
#include "GameMode/HellunaDefenseGameMode.h"
#include "GameFramework/PlayerState.h"
#include "Player/HellunaPlayerState.h"
#include "Blueprint/UserWidget.h"
#include "MDF_Function/MDF_Instance/MDF_GameInstance.h"

AHellunaLoginController::AHellunaLoginController()
{
	bShowMouseCursor = true;
	bEnableClickEvents = true;
	bEnableMouseOverEvents = true;
}

void AHellunaLoginController::BeginPlay()
{
	Super::BeginPlay();

	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║     [LoginController] BeginPlay                            ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ Controller: %s"), *GetName());
	UE_LOG(LogTemp, Warning, TEXT("║ ControllerID: %d"), GetUniqueID());
	UE_LOG(LogTemp, Warning, TEXT("║ IsLocalController: %s"), IsLocalController() ? TEXT("TRUE") : TEXT("FALSE"));
	UE_LOG(LogTemp, Warning, TEXT("║ HasAuthority: %s"), HasAuthority() ? TEXT("TRUE") : TEXT("FALSE"));
	UE_LOG(LogTemp, Warning, TEXT("║ NetMode: %d"), static_cast<int32>(GetNetMode()));
	UE_LOG(LogTemp, Warning, TEXT("║ NetConnection: %s"), GetNetConnection() ? TEXT("Valid") : TEXT("nullptr"));
	
	APlayerState* PS = GetPlayerState<APlayerState>();
	UE_LOG(LogTemp, Warning, TEXT("║ PlayerState: %s"), PS ? *PS->GetName() : TEXT("nullptr"));
	
	UE_LOG(LogTemp, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ LoginWidgetClass: %s"), LoginWidgetClass ? *LoginWidgetClass->GetName() : TEXT("미설정!"));
	UE_LOG(LogTemp, Warning, TEXT("║ GameControllerClass: %s"), GameControllerClass ? *GameControllerClass->GetName() : TEXT("미설정!"));
	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));

	if (!LoginWidgetClass)
	{
		UE_LOG(LogTemp, Error, TEXT("[LoginController] LoginWidgetClass 미설정!"));
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 10.f, FColor::Red,
				TEXT("LoginWidgetClass 미설정! BP에서 설정 필요"));
		}
	}

	if (!GameControllerClass)
	{
		UE_LOG(LogTemp, Error, TEXT("[LoginController] GameControllerClass 미설정!"));
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 10.f, FColor::Red,
				TEXT("GameControllerClass 미설정! BP에서 설정 필요"));
		}
	}

	if (IsLocalController() && LoginWidgetClass)
	{
		FInputModeUIOnly InputMode;
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		SetInputMode(InputMode);

		FTimerHandle TimerHandle;
		GetWorldTimerManager().SetTimer(TimerHandle, this, &AHellunaLoginController::ShowLoginWidget, 0.3f, false);
	}

	UE_LOG(LogTemp, Warning, TEXT(""));
}

void AHellunaLoginController::ShowLoginWidget()
{
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("┌────────────────────────────────────────────────────────────┐"));
	UE_LOG(LogTemp, Warning, TEXT("│ [LoginController] ShowLoginWidget                          │"));
	UE_LOG(LogTemp, Warning, TEXT("└────────────────────────────────────────────────────────────┘"));

	// ========================================
	// ⭐ [Fix 1] SeamlessTravel 중이면 UI 표시 안 함
	// ========================================
	// 
	// SeamlessTravel 시:
	// 1. GameState::Server_SaveAndMoveLevel에서 bIsMapTransitioning = true 설정
	// 2. Super::HandleSeamlessTravelPlayer() 내부에서 새 LoginController 생성
	// 3. LoginController::BeginPlay() → ShowLoginWidget() 호출 (이 시점)
	//    → 아직 PlayerState에 PlayerId 복원 안 됨!
	// 4. Super 반환 후 PlayerState에 PlayerId 복원
	// 5. 0.5초 후 HandleSeamlessTravelPlayer() 타이머 → SwapToGameController()
	// 
	// 문제: PlayerState 복원 전에 ShowLoginWidget이 먼저 호출됨
	// 해결: bIsMapTransitioning 플래그로 SeamlessTravel 상황 감지
	// ========================================
	if (UMDF_GameInstance* GI = Cast<UMDF_GameInstance>(GetGameInstance()))
	{
		if (GI->bIsMapTransitioning)
		{
			UE_LOG(LogTemp, Warning, TEXT("[LoginController] ⚠️ SeamlessTravel 진행 중 (bIsMapTransitioning=true) → UI 표시 스킵!"));
			
			// PlayerState에서 PlayerId 확인 (디버깅용)
			if (AHellunaPlayerState* PS = GetPlayerState<AHellunaPlayerState>())
			{
				UE_LOG(LogTemp, Warning, TEXT("[LoginController]    PlayerId: '%s'"), *PS->GetPlayerUniqueId());
			}
			
			// ⭐ Controller 스왑 요청! (서버에서 SwapToGameController 실행)
			UE_LOG(LogTemp, Warning, TEXT("[LoginController] → Server_RequestSwapAfterTravel() 호출!"));
			Server_RequestSwapAfterTravel();
			return;
		}
	}

	// ========================================
	// ⭐ [Fix 2] 이미 로그인된 상태면 UI 표시 안 함
	// ========================================
	// 
	// (기존 체크 유지 - PlayerState 복원 후 호출되는 경우 대비)
	// ========================================
	if (AHellunaPlayerState* PS = GetPlayerState<AHellunaPlayerState>())
	{
		if (PS->IsLoggedIn() && !PS->GetPlayerUniqueId().IsEmpty())
		{
			UE_LOG(LogTemp, Warning, TEXT("[LoginController] ⚠️ 이미 로그인됨 (SeamlessTravel) → UI 표시 스킵!"));
			UE_LOG(LogTemp, Warning, TEXT("[LoginController]    PlayerId: '%s'"), *PS->GetPlayerUniqueId());
			return;
		}
	}

	if (!LoginWidgetClass)
	{
		UE_LOG(LogTemp, Error, TEXT("[LoginController] LoginWidgetClass가 nullptr!"));
		return;
	}

	if (!LoginWidget)
	{
		LoginWidget = CreateWidget<UHellunaLoginWidget>(this, LoginWidgetClass);
		UE_LOG(LogTemp, Warning, TEXT("[LoginController] 위젯 생성됨: %s"), LoginWidget ? *LoginWidget->GetName() : TEXT("실패"));
	}

	if (LoginWidget && !LoginWidget->IsInViewport())
	{
		LoginWidget->AddToViewport(100);
		UE_LOG(LogTemp, Warning, TEXT("[LoginController] 위젯 Viewport에 추가됨"));
	}

	UE_LOG(LogTemp, Warning, TEXT(""));
}

void AHellunaLoginController::HideLoginWidget()
{
	UE_LOG(LogTemp, Warning, TEXT("[LoginController] HideLoginWidget"));

	if (LoginWidget && LoginWidget->IsInViewport())
	{
		LoginWidget->RemoveFromParent();
		UE_LOG(LogTemp, Warning, TEXT("[LoginController] 위젯 숨김"));
	}
}

void AHellunaLoginController::OnLoginButtonClicked(const FString& PlayerId, const FString& Password)
{
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║     [LoginController] OnLoginButtonClicked                 ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ PlayerId: '%s'"), *PlayerId);
	UE_LOG(LogTemp, Warning, TEXT("║ Controller: %s (ID: %d)"), *GetName(), GetUniqueID());
	UE_LOG(LogTemp, Warning, TEXT("║ → Server_RequestLogin RPC 호출!                            ║"));
	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));

	if (LoginWidget)
	{
		LoginWidget->ShowMessage(TEXT("로그인 중..."), false);
		LoginWidget->SetLoadingState(true);
	}

	Server_RequestLogin(PlayerId, Password);

	UE_LOG(LogTemp, Warning, TEXT(""));
}

// ============================================
// 📌 SeamlessTravel 후 Controller 스왑 요청
// ============================================
// ShowLoginWidget()에서 이미 로그인된 상태 감지 시 호출
// 서버에서 SwapToGameController() 실행
// ============================================
void AHellunaLoginController::Server_RequestSwapAfterTravel_Implementation()
{
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║  [LoginController] Server_RequestSwapAfterTravel (서버)    ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ Controller: %s"), *GetName());
	
	// PlayerState에서 PlayerId 가져오기
	FString PlayerId;
	if (AHellunaPlayerState* PS = GetPlayerState<AHellunaPlayerState>())
	{
		PlayerId = PS->GetPlayerUniqueId();
		UE_LOG(LogTemp, Warning, TEXT("║ PlayerId: '%s'"), *PlayerId);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("║ ⚠️ PlayerState nullptr!"));
	}
	
	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
	
	// GameMode에서 SwapToGameController 호출
	if (AHellunaDefenseGameMode* GM = GetWorld()->GetAuthGameMode<AHellunaDefenseGameMode>())
	{
		if (!PlayerId.IsEmpty())
		{
			UE_LOG(LogTemp, Warning, TEXT("[LoginController] → GameMode::SwapToGameController 호출!"));
			GM->SwapToGameController(this, PlayerId);
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("[LoginController] ⚠️ PlayerId가 비어있어 Controller 스왑 불가!"));
		}
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[LoginController] ⚠️ GameMode를 찾을 수 없음!"));
	}
}

void AHellunaLoginController::Server_RequestLogin_Implementation(const FString& PlayerId, const FString& Password)
{
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║     [LoginController] Server_RequestLogin (서버)           ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ PlayerId: '%s'"), *PlayerId);
	UE_LOG(LogTemp, Warning, TEXT("║ Controller: %s (ID: %d)"), *GetName(), GetUniqueID());
	UE_LOG(LogTemp, Warning, TEXT("║ HasAuthority: %s"), HasAuthority() ? TEXT("TRUE") : TEXT("FALSE"));
	UE_LOG(LogTemp, Warning, TEXT("║ → DefenseGameMode::ProcessLogin 호출!                      ║"));
	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));

	AHellunaDefenseGameMode* GM = Cast<AHellunaDefenseGameMode>(GetWorld()->GetAuthGameMode());
	if (GM)
	{
		GM->ProcessLogin(this, PlayerId, Password);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[LoginController] DefenseGameMode 없음!"));
		Client_LoginResult(false, TEXT("서버 오류: GameMode를 찾을 수 없습니다."));
	}

	UE_LOG(LogTemp, Warning, TEXT(""));
}

void AHellunaLoginController::Client_LoginResult_Implementation(bool bSuccess, const FString& ErrorMessage)
{
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║     [LoginController] Client_LoginResult (클라이언트)      ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ bSuccess: %s"), bSuccess ? TEXT("TRUE ✅") : TEXT("FALSE ❌"));
	UE_LOG(LogTemp, Warning, TEXT("║ ErrorMessage: '%s'"), *ErrorMessage);
	UE_LOG(LogTemp, Warning, TEXT("║ Controller: %s (ID: %d)"), *GetName(), GetUniqueID());
	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));

	ShowLoginResult(bSuccess, ErrorMessage);

	UE_LOG(LogTemp, Warning, TEXT(""));
}

void AHellunaLoginController::Client_PrepareControllerSwap_Implementation()
{
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║     [LoginController] Client_PrepareControllerSwap         ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ Controller 교체 준비 중...                                 ║"));
	UE_LOG(LogTemp, Warning, TEXT("║ UI 정리 시작                                               ║"));
	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));

	HideLoginWidget();

	FInputModeGameOnly InputMode;
	SetInputMode(InputMode);
	bShowMouseCursor = false;

	UE_LOG(LogTemp, Warning, TEXT("[LoginController] UI 정리 완료, Controller 교체 대기"));
	UE_LOG(LogTemp, Warning, TEXT(""));
}

void AHellunaLoginController::ShowLoginResult(bool bSuccess, const FString& Message)
{
	if (!LoginWidget) return;

	if (bSuccess)
	{
		LoginWidget->ShowMessage(TEXT("로그인 성공!"), false);
	}
	else
	{
		LoginWidget->ShowMessage(Message, true);
		LoginWidget->SetLoadingState(false);
	}
}

// ============================================
// 🎭 캐릭터 선택 시스템 (Phase 3)
// ============================================

void AHellunaLoginController::Server_SelectCharacter_Implementation(int32 CharacterIndex)
{
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║  🎭 [LoginController] Server_SelectCharacter (서버)        ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ CharacterIndex: %d"), CharacterIndex);
	UE_LOG(LogTemp, Warning, TEXT("║ Controller: %s"), *GetName());
	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));

	AHellunaDefenseGameMode* GM = Cast<AHellunaDefenseGameMode>(GetWorld()->GetAuthGameMode());
	if (GM)
	{
		GM->ProcessCharacterSelection(this, CharacterIndex);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[LoginController] DefenseGameMode 없음!"));
		Client_CharacterSelectionResult(false, TEXT("서버 오류"));
	}
}

void AHellunaLoginController::Client_CharacterSelectionResult_Implementation(bool bSuccess, const FString& ErrorMessage)
{
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║  🎭 [LoginController] Client_CharacterSelectionResult      ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ bSuccess: %s"), bSuccess ? TEXT("TRUE ✅") : TEXT("FALSE ❌"));
	UE_LOG(LogTemp, Warning, TEXT("║ ErrorMessage: '%s'"), *ErrorMessage);
	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));

	// CharacterSelectWidget에 결과 전달
	if (LoginWidget)
	{
		UHellunaCharacterSelectWidget* CharSelectWidget = LoginWidget->GetCharacterSelectWidget();
		if (CharSelectWidget)
		{
			CharSelectWidget->OnSelectionResult(bSuccess, ErrorMessage);
		}
	}
}

void AHellunaLoginController::Client_ShowCharacterSelectUI_Implementation(const TArray<bool>& AvailableCharacters)
{
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║  🎭 [LoginController] Client_ShowCharacterSelectUI         ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ 선택 가능한 캐릭터:"));
	for (int32 i = 0; i < AvailableCharacters.Num(); i++)
	{
		UE_LOG(LogTemp, Warning, TEXT("║   [%d] %s"), i, AvailableCharacters[i] ? TEXT("✅ 선택 가능") : TEXT("❌ 사용 중"));
	}
	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));

	// LoginWidget에 캐릭터 선택 UI 표시 요청
	if (LoginWidget)
	{
		LoginWidget->ShowCharacterSelection(AvailableCharacters);
	}
}
