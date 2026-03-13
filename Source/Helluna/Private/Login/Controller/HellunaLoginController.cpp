#include "Login/Controller/HellunaLoginController.h"
#include "Helluna.h"
#include "Login/Widget/HellunaLoginWidget.h"
#include "Login/Widget/HellunaCharacterSelectWidget.h"
#include "Login/Widget/HellunaCharSelectWidget_V1.h"
#include "Login/Widget/HellunaCharSelectWidget_V2.h"
#include "Login/Preview/HellunaCharacterPreviewActor.h"
#include "Login/Preview/HellunaCharacterSelectSceneV2.h"
#include "GameMode/HellunaBaseGameMode.h"
#include "GameFramework/PlayerState.h"
#include "Player/HellunaPlayerState.h"
#include "Blueprint/UserWidget.h"
#include "MDF_Function/MDF_Instance/MDF_GameInstance.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/SkeletalMesh.h"
#include "Kismet/GameplayStatics.h"

AHellunaLoginController::AHellunaLoginController()
{
	bShowMouseCursor = true;
	bEnableClickEvents = true;
	bEnableMouseOverEvents = true;
}

void AHellunaLoginController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// ════════════════════════════════════════════
	// 📌 프리뷰 액터 안전 정리
	// ════════════════════════════════════════════
	// Controller 파괴 시 프리뷰 액터가 월드에 잔존하는 것을 방지
	DestroyPreviewActors();
	DestroyPreviewSceneV2();

	Super::EndPlay(EndPlayReason);
}

void AHellunaLoginController::BeginPlay()
{
	Super::BeginPlay();

#if HELLUNA_DEBUG_LOGINCONTROLLER
	// 📌 디버깅: 클라이언트/서버 구분을 위한 태그
	FString RoleTag = HasAuthority() ? TEXT("SERVER") : TEXT("CLIENT");
#endif

#if HELLUNA_DEBUG_LOGINCONTROLLER
	UE_LOG(LogHelluna, Warning, TEXT(""));
	UE_LOG(LogHelluna, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogHelluna, Warning, TEXT("║     [LoginController] BeginPlay [%s]                  ║"), *RoleTag);
	UE_LOG(LogHelluna, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogHelluna, Warning, TEXT("║ Controller: %s"), *GetName());
	UE_LOG(LogHelluna, Warning, TEXT("║ ControllerID: %d"), GetUniqueID());
	UE_LOG(LogHelluna, Warning, TEXT("║ IsLocalController: %s"), IsLocalController() ? TEXT("TRUE ✅") : TEXT("FALSE"));
	UE_LOG(LogHelluna, Warning, TEXT("║ HasAuthority: %s"), HasAuthority() ? TEXT("TRUE") : TEXT("FALSE"));
	UE_LOG(LogHelluna, Warning, TEXT("║ NetMode: %d (0=Standalone, 1=DedicatedServer, 2=ListenServer, 3=Client)"), static_cast<int32>(GetNetMode()));
	UE_LOG(LogHelluna, Warning, TEXT("║ NetConnection: %s"), GetNetConnection() ? TEXT("Valid") : TEXT("nullptr"));

	APlayerState* PS = GetPlayerState<APlayerState>();
	UE_LOG(LogHelluna, Warning, TEXT("║ PlayerState: %s"), PS ? *PS->GetName() : TEXT("nullptr"));

	UE_LOG(LogHelluna, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogHelluna, Warning, TEXT("║ LoginWidgetClass: %s"), LoginWidgetClass ? *LoginWidgetClass->GetName() : TEXT("미설정!"));
	UE_LOG(LogHelluna, Warning, TEXT("║ GameControllerClass: %s"), GameControllerClass ? *GameControllerClass->GetName() : TEXT("미설정!"));
	UE_LOG(LogHelluna, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
#endif

	if (!LoginWidgetClass)
	{
#if HELLUNA_DEBUG_LOGINCONTROLLER
		UE_LOG(LogHelluna, Error, TEXT("[LoginController][%s] LoginWidgetClass 미설정!"), *RoleTag);
#endif
// [Step4] 프로덕션 빌드에서 디버그 메시지 제거
#if HELLUNA_DEBUG_LOGINCONTROLLER
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 10.f, FColor::Red,
				TEXT("LoginWidgetClass 미설정! BP에서 설정 필요"));
		}
#endif
	}

	if (!GameControllerClass)
	{
#if HELLUNA_DEBUG_LOGINCONTROLLER
		UE_LOG(LogHelluna, Error, TEXT("[LoginController][%s] GameControllerClass 미설정!"), *RoleTag);
#endif
#if HELLUNA_DEBUG_LOGINCONTROLLER
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 10.f, FColor::Red,
				TEXT("GameControllerClass 미설정! BP에서 설정 필요"));
		}
#endif
	}

	// 📌 클라이언트에서만 위젯 표시
	if (IsLocalController() && LoginWidgetClass)
	{
#if HELLUNA_DEBUG_LOGINCONTROLLER
		UE_LOG(LogHelluna, Warning, TEXT("[LoginController][%s] ⭐ IsLocalController=TRUE! 위젯 타이머 시작!"), *RoleTag);
#endif

		// 📌 화면에 디버그 메시지 표시 (클라이언트에서만 보임)
#if HELLUNA_DEBUG_LOGINCONTROLLER
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Green,
				FString::Printf(TEXT("✅ LoginController BeginPlay - 위젯 타이머 시작! (IsLocal: TRUE)")));
		}
#endif

		FInputModeUIOnly InputMode;
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		SetInputMode(InputMode);

		GetWorldTimerManager().SetTimer(RetryTimerHandle, this, &AHellunaLoginController::ShowLoginWidget, 0.3f, false);
	}
	else
	{
#if HELLUNA_DEBUG_LOGINCONTROLLER
		UE_LOG(LogHelluna, Warning, TEXT("[LoginController][%s] 위젯 표시 스킵 (IsLocalController=%s, LoginWidgetClass=%s)"),
			*RoleTag,
			IsLocalController() ? TEXT("TRUE") : TEXT("FALSE"),
			LoginWidgetClass ? TEXT("Valid") : TEXT("nullptr"));
#endif

		// 📌 화면에 디버그 메시지 표시 (왜 스킵되는지 확인)
#if HELLUNA_DEBUG_LOGINCONTROLLER
		if (GEngine && !HasAuthority())  // 클라이언트에서만 표시
		{
			GEngine->AddOnScreenDebugMessage(-1, 10.f, FColor::Red,
				FString::Printf(TEXT("❌ LoginController: 위젯 스킵! IsLocal=%s, WidgetClass=%s"),
					IsLocalController() ? TEXT("T") : TEXT("F"),
					LoginWidgetClass ? TEXT("OK") : TEXT("NULL")));
		}
#endif
	}

#if HELLUNA_DEBUG_LOGINCONTROLLER
	UE_LOG(LogHelluna, Warning, TEXT(""));
#endif
}

void AHellunaLoginController::ShowLoginWidget()
{
#if HELLUNA_DEBUG_LOGINCONTROLLER
	FString RoleTag = HasAuthority() ? TEXT("SERVER") : TEXT("CLIENT");
#endif

#if HELLUNA_DEBUG_LOGINCONTROLLER
	UE_LOG(LogHelluna, Warning, TEXT(""));
	UE_LOG(LogHelluna, Warning, TEXT("┌────────────────────────────────────────────────────────────┐"));
	UE_LOG(LogHelluna, Warning, TEXT("│ [LoginController][%s] ShowLoginWidget 호출됨!         │"), *RoleTag);
	UE_LOG(LogHelluna, Warning, TEXT("├────────────────────────────────────────────────────────────┤"));
	UE_LOG(LogHelluna, Warning, TEXT("│ IsLocalController: %s"), IsLocalController() ? TEXT("TRUE ✅") : TEXT("FALSE"));
	UE_LOG(LogHelluna, Warning, TEXT("│ LoginWidgetClass: %s"), LoginWidgetClass ? *LoginWidgetClass->GetName() : TEXT("nullptr"));
	UE_LOG(LogHelluna, Warning, TEXT("└────────────────────────────────────────────────────────────┘"));
#endif

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
	UMDF_GameInstance* GI = Cast<UMDF_GameInstance>(GetGameInstance());
	if (GI)
	{
		if (GI->bIsMapTransitioning)
		{
#if HELLUNA_DEBUG_LOGINCONTROLLER
			UE_LOG(LogHelluna, Warning, TEXT("[LoginController] ⚠️ SeamlessTravel 진행 중 (bIsMapTransitioning=true) → UI 표시 스킵!"));

			// PlayerState에서 PlayerId 확인 (디버깅용)
			if (AHellunaPlayerState* PS = GetPlayerState<AHellunaPlayerState>())
			{
				UE_LOG(LogHelluna, Warning, TEXT("[LoginController]    PlayerId: '%s'"), *PS->GetPlayerUniqueId());
			}

			// ⭐ Controller 스왑 요청! (서버에서 SwapToGameController 실행)
			UE_LOG(LogHelluna, Warning, TEXT("[LoginController] → Server_RequestSwapAfterTravel() 호출!"));
#endif
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
#if HELLUNA_DEBUG_LOGINCONTROLLER
			UE_LOG(LogHelluna, Warning, TEXT("[LoginController] ⚠️ 이미 로그인됨 (SeamlessTravel) → UI 표시 스킵!"));
			UE_LOG(LogHelluna, Warning, TEXT("[LoginController]    PlayerId: '%s'"), *PS->GetPlayerUniqueId());
#endif
			return;
		}
	}

	// 로딩 화면 해제 (서버 접속 완료 후)
	if (GI)
	{
		GI->HideLoadingScreen();
	}

	if (!LoginWidgetClass)
	{
		UE_LOG(LogHelluna, Error, TEXT("[LoginController] LoginWidgetClass가 nullptr!"));
		return;
	}

	if (!LoginWidget)
	{
		LoginWidget = CreateWidget<UHellunaLoginWidget>(this, LoginWidgetClass);
#if HELLUNA_DEBUG_LOGINCONTROLLER
		UE_LOG(LogHelluna, Warning, TEXT("[LoginController] 위젯 생성: %s"), LoginWidget ? TEXT("✅ 성공") : TEXT("❌ 실패"));
#endif

#if HELLUNA_DEBUG_LOGINCONTROLLER
		if (GEngine)
		{
			if (LoginWidget)
			{
				GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Cyan,
					FString::Printf(TEXT("로그인 위젯 생성됨: %s"), *LoginWidget->GetName()));
			}
			else
			{
				GEngine->AddOnScreenDebugMessage(-1, 10.f, FColor::Red,
					TEXT("❌ 로그인 위젯 생성 실패!"));
			}
		}
#endif
	}

	if (LoginWidget && !LoginWidget->IsInViewport())
	{
		LoginWidget->AddToViewport(100);
#if HELLUNA_DEBUG_LOGINCONTROLLER
		UE_LOG(LogHelluna, Warning, TEXT("[LoginController] ✅ 위젯 Viewport에 추가됨!"));
#endif

		// 📌 화면에 성공 메시지 표시
#if HELLUNA_DEBUG_LOGINCONTROLLER
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Green,
				TEXT("✅ 로그인 위젯이 Viewport에 추가됨!"));
		}
#endif
	}
	else
	{
#if HELLUNA_DEBUG_LOGINCONTROLLER
		UE_LOG(LogHelluna, Warning, TEXT("[LoginController] ⚠️ 위젯 추가 실패 또는 이미 Viewport에 있음 (LoginWidget=%s, IsInViewport=%s)"),
			LoginWidget ? TEXT("Valid") : TEXT("nullptr"),
			(LoginWidget && LoginWidget->IsInViewport()) ? TEXT("TRUE") : TEXT("FALSE"));
#endif
	}

#if HELLUNA_DEBUG_LOGINCONTROLLER
	UE_LOG(LogHelluna, Warning, TEXT(""
								  ""));
#endif
}

void AHellunaLoginController::HideLoginWidget()
{
#if HELLUNA_DEBUG_LOGINCONTROLLER
	UE_LOG(LogHelluna, Warning, TEXT("[LoginController] HideLoginWidget"));
#endif

	if (LoginWidget && LoginWidget->IsInViewport())
	{
		LoginWidget->RemoveFromParent();
#if HELLUNA_DEBUG_LOGINCONTROLLER
		UE_LOG(LogHelluna, Warning, TEXT("[LoginController] 위젯 숨김"));
#endif
	}
}

void AHellunaLoginController::OnLoginButtonClicked(const FString& PlayerId, const FString& Password)
{
#if HELLUNA_DEBUG_LOGINCONTROLLER
	UE_LOG(LogHelluna, Warning, TEXT(""));
	UE_LOG(LogHelluna, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogHelluna, Warning, TEXT("║     [LoginController] OnLoginButtonClicked                 ║"));
	UE_LOG(LogHelluna, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogHelluna, Warning, TEXT("║ PlayerId: '%s'"), *PlayerId);
	UE_LOG(LogHelluna, Warning, TEXT("║ Controller: %s (ID: %d)"), *GetName(), GetUniqueID());
	UE_LOG(LogHelluna, Warning, TEXT("║ → Server_RequestLogin RPC 호출!                            ║"));
	UE_LOG(LogHelluna, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
#endif

	if (LoginWidget)
	{
		LoginWidget->ShowMessage(TEXT("로그인 중..."), false);
		LoginWidget->SetLoadingState(true);
	}

	Server_RequestLogin(PlayerId, Password);

#if HELLUNA_DEBUG_LOGINCONTROLLER
	UE_LOG(LogHelluna, Warning, TEXT(""));
#endif
}

// ============================================
// 📌 SeamlessTravel 후 Controller 스왑 요청
// ============================================
// ShowLoginWidget()에서 이미 로그인된 상태 감지 시 호출
// 서버에서 SwapToGameController() 실행
// ============================================
bool AHellunaLoginController::Server_RequestSwapAfterTravel_Validate()
{
	return true;
}

void AHellunaLoginController::Server_RequestSwapAfterTravel_Implementation()
{
#if HELLUNA_DEBUG_LOGINCONTROLLER
	UE_LOG(LogHelluna, Warning, TEXT(""));
	UE_LOG(LogHelluna, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogHelluna, Warning, TEXT("║  [LoginController] Server_RequestSwapAfterTravel (서버)    ║"));
	UE_LOG(LogHelluna, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogHelluna, Warning, TEXT("║ Controller: %s"), *GetName());
#endif

	// PlayerState에서 PlayerId 가져오기
	FString PlayerId;
	if (AHellunaPlayerState* PS = GetPlayerState<AHellunaPlayerState>())
	{
		PlayerId = PS->GetPlayerUniqueId();
#if HELLUNA_DEBUG_LOGINCONTROLLER
		UE_LOG(LogHelluna, Warning, TEXT("║ PlayerId: '%s'"), *PlayerId);
#endif
	}
	else
	{
#if HELLUNA_DEBUG_LOGINCONTROLLER
		UE_LOG(LogHelluna, Warning, TEXT("║ ⚠️ PlayerState nullptr!"));
#endif
	}

#if HELLUNA_DEBUG_LOGINCONTROLLER
	UE_LOG(LogHelluna, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
#endif

	// GameMode에서 SwapToGameController 호출
	// [Fix26] GetWorld() null 체크
	UWorld* World = GetWorld();
	if (!World) return;
	if (AHellunaBaseGameMode* GM = World->GetAuthGameMode<AHellunaBaseGameMode>())
	{
		if (!PlayerId.IsEmpty())
		{
#if HELLUNA_DEBUG_LOGINCONTROLLER
			UE_LOG(LogHelluna, Warning, TEXT("[LoginController] → GameMode::SwapToGameController 호출!"));
#endif
			GM->SwapToGameController(this, PlayerId);
		}
		else
		{
			UE_LOG(LogHelluna, Error, TEXT("[LoginController] ⚠️ PlayerId가 비어있어 Controller 스왑 불가!"));
		}
	}
	else
	{
		UE_LOG(LogHelluna, Error, TEXT("[LoginController] ⚠️ GameMode를 찾을 수 없음!"));
	}
}

bool AHellunaLoginController::Server_RequestLogin_Validate(const FString& PlayerId, const FString& Password)
{
	return PlayerId.Len() <= 64 && Password.Len() <= 128;
}

void AHellunaLoginController::Server_RequestLogin_Implementation(const FString& PlayerId, const FString& Password)
{
#if HELLUNA_DEBUG_LOGINCONTROLLER
	UE_LOG(LogHelluna, Warning, TEXT(""));
	UE_LOG(LogHelluna, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogHelluna, Warning, TEXT("║     [LoginController] Server_RequestLogin (서버)           ║"));
	UE_LOG(LogHelluna, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogHelluna, Warning, TEXT("║ PlayerId: '%s'"), *PlayerId);
	UE_LOG(LogHelluna, Warning, TEXT("║ Controller: %s (ID: %d)"), *GetName(), GetUniqueID());
	UE_LOG(LogHelluna, Warning, TEXT("║ HasAuthority: %s"), HasAuthority() ? TEXT("TRUE") : TEXT("FALSE"));
	UE_LOG(LogHelluna, Warning, TEXT("║ → DefenseGameMode::ProcessLogin 호출!                      ║"));
	UE_LOG(LogHelluna, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
#endif

	// [Fix26] GetWorld() null 체크
	UWorld* LoginWorld = GetWorld();
	AHellunaBaseGameMode* GM = LoginWorld ? Cast<AHellunaBaseGameMode>(LoginWorld->GetAuthGameMode()) : nullptr;
	if (GM)
	{
		GM->ProcessLogin(this, PlayerId, Password);
	}
	else
	{
		UE_LOG(LogHelluna, Error, TEXT("[LoginController] BaseGameMode 없음!"));
		Client_LoginResult(false, TEXT("서버 오류: GameMode를 찾을 수 없습니다."));
	}

#if HELLUNA_DEBUG_LOGINCONTROLLER
	UE_LOG(LogHelluna, Warning, TEXT(""));
#endif
}

void AHellunaLoginController::Client_LoginResult_Implementation(bool bSuccess, const FString& ErrorMessage)
{
#if HELLUNA_DEBUG_LOGINCONTROLLER
	UE_LOG(LogHelluna, Warning, TEXT(""));
	UE_LOG(LogHelluna, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogHelluna, Warning, TEXT("║     [LoginController] Client_LoginResult (클라이언트)      ║"));
	UE_LOG(LogHelluna, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogHelluna, Warning, TEXT("║ bSuccess: %s"), bSuccess ? TEXT("TRUE ✅") : TEXT("FALSE ❌"));
	UE_LOG(LogHelluna, Warning, TEXT("║ ErrorMessage: '%s'"), *ErrorMessage);
	UE_LOG(LogHelluna, Warning, TEXT("║ Controller: %s (ID: %d)"), *GetName(), GetUniqueID());
	UE_LOG(LogHelluna, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
#endif

	ShowLoginResult(bSuccess, ErrorMessage);

#if HELLUNA_DEBUG_LOGINCONTROLLER
	UE_LOG(LogHelluna, Warning, TEXT(""));
#endif
}

void AHellunaLoginController::Client_PrepareControllerSwap_Implementation()
{
#if HELLUNA_DEBUG_LOGINCONTROLLER
	UE_LOG(LogHelluna, Warning, TEXT(""));
	UE_LOG(LogHelluna, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogHelluna, Warning, TEXT("║     [LoginController] Client_PrepareControllerSwap         ║"));
	UE_LOG(LogHelluna, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogHelluna, Warning, TEXT("║ Controller 교체 준비 중...                                 ║"));
	UE_LOG(LogHelluna, Warning, TEXT("║ UI 정리 시작                                               ║"));
	UE_LOG(LogHelluna, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
#endif

	HideLoginWidget();

	FInputModeGameOnly InputMode;
	SetInputMode(InputMode);
	bShowMouseCursor = false;

#if HELLUNA_DEBUG_LOGINCONTROLLER
	UE_LOG(LogHelluna, Warning, TEXT("[LoginController] UI 정리 완료, Controller 교체 대기"));
	UE_LOG(LogHelluna, Warning, TEXT(""));
#endif
}

void AHellunaLoginController::ShowLoginResult(bool bSuccess, const FString& Message)
{
	// 로그인 실패 시 로딩 화면 해제
	if (!bSuccess)
	{
		if (UMDF_GameInstance* GI = Cast<UMDF_GameInstance>(GetGameInstance()))
		{
			GI->HideLoadingScreen();
		}
	}

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

bool AHellunaLoginController::Server_SelectCharacter_Validate(int32 CharacterIndex)
{
	return CharacterIndex >= 0 && CharacterIndex <= 2;
}

void AHellunaLoginController::Server_SelectCharacter_Implementation(int32 CharacterIndex)
{
#if HELLUNA_DEBUG_CHARACTER_SELECT
	UE_LOG(LogHelluna, Warning, TEXT(""));
	UE_LOG(LogHelluna, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogHelluna, Warning, TEXT("║  🎭 [LoginController] Server_SelectCharacter (서버)        ║"));
	UE_LOG(LogHelluna, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogHelluna, Warning, TEXT("║ CharacterIndex: %d"), CharacterIndex);
	UE_LOG(LogHelluna, Warning, TEXT("║ Controller: %s"), *GetName());
	UE_LOG(LogHelluna, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
#endif

	// [Fix26] GetWorld() null 체크
	UWorld* SelectWorld = GetWorld();
	AHellunaBaseGameMode* GM = SelectWorld ? Cast<AHellunaBaseGameMode>(SelectWorld->GetAuthGameMode()) : nullptr;
	if (GM)
	{
		// int32 → EHellunaHeroType 변환
		EHellunaHeroType HeroType = AHellunaBaseGameMode::IndexToHeroType(CharacterIndex);
#if HELLUNA_DEBUG_CHARACTER_SELECT
		UE_LOG(LogHelluna, Warning, TEXT("[LoginController] 🎭 HeroType: %s"), *UEnum::GetValueAsString(HeroType));
#endif
		GM->ProcessCharacterSelection(this, HeroType);
	}
	else
	{
		UE_LOG(LogHelluna, Error, TEXT("[LoginController] BaseGameMode 없음!"));
		Client_CharacterSelectionResult(false, TEXT("서버 오류"));
	}
}

void AHellunaLoginController::Client_CharacterSelectionResult_Implementation(bool bSuccess, const FString& ErrorMessage)
{
#if HELLUNA_DEBUG_CHARACTER_SELECT
	UE_LOG(LogHelluna, Warning, TEXT(""));
	UE_LOG(LogHelluna, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogHelluna, Warning, TEXT("║  🎭 [LoginController] Client_CharacterSelectionResult      ║"));
	UE_LOG(LogHelluna, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogHelluna, Warning, TEXT("║ bSuccess: %s"), bSuccess ? TEXT("TRUE ✅") : TEXT("FALSE ❌"));
	UE_LOG(LogHelluna, Warning, TEXT("║ ErrorMessage: '%s'"), *ErrorMessage);
	UE_LOG(LogHelluna, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
#endif

	// CharacterSelectWidget에 결과 전달
	if (LoginWidget)
	{
		UHellunaCharacterSelectWidget* CharSelectWidget = LoginWidget->GetCharacterSelectWidget();
		if (CharSelectWidget)
		{
			CharSelectWidget->OnSelectionResult(bSuccess, ErrorMessage);
		}
	}

	// ════════════════════════════════════════════
	// 선택 성공 시 프리뷰 액터 파괴 + 로딩 화면 표시
	// ════════════════════════════════════════════
	if (bSuccess)
	{
		DestroyPreviewActors();   // V1
		DestroyPreviewSceneV2();  // V2

		// 게임 진입 대기 로딩 화면 표시 (맵 전환 시 자동 파괴됨)
		if (UMDF_GameInstance* GI = Cast<UMDF_GameInstance>(GetGameInstance()))
		{
			GI->ShowLoadingScreen(TEXT("게임 준비 중..."));
		}
	}
}

void AHellunaLoginController::Client_ShowCharacterSelectUI_Implementation(const TArray<bool>& AvailableCharacters)
{
#if HELLUNA_DEBUG_CHARACTER_SELECT
	UE_LOG(LogHelluna, Warning, TEXT(""));
	UE_LOG(LogHelluna, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogHelluna, Warning, TEXT("║  🎭 [LoginController] Client_ShowCharacterSelectUI         ║"));
	UE_LOG(LogHelluna, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogHelluna, Warning, TEXT("║ 선택 가능한 캐릭터:"));
	for (int32 i = 0; i < AvailableCharacters.Num(); i++)
	{
		UE_LOG(LogHelluna, Warning, TEXT("║   [%d] %s"), i, AvailableCharacters[i] ? TEXT("✅ 선택 가능") : TEXT("❌ 사용 중"));
	}
	UE_LOG(LogHelluna, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
#endif

	// 로딩 화면 해제 (로그인 성공 → 캐릭터 선택 UI 표시 전)
	if (UMDF_GameInstance* GI = Cast<UMDF_GameInstance>(GetGameInstance()))
	{
		GI->HideLoadingScreen();
	}

	// LoginWidget에 캐릭터 선택 UI 표시 요청
	if (!LoginWidget) return;

	LoginWidget->ShowCharacterSelection(AvailableCharacters);

	UHellunaCharacterSelectWidget* CharSelectWidget = LoginWidget->GetCharacterSelectWidget();
	if (!CharSelectWidget) return;

	// ════════════════════════════════════════════
	// 📌 위젯 타입으로 V1/V2 분기 (다형성)
	// ════════════════════════════════════════════
	if (UHellunaCharSelectWidget_V2* V2Widget = Cast<UHellunaCharSelectWidget_V2>(CharSelectWidget))
	{
		// ════════════════════════════════════════════
		// 📌 V2 경로: 3캐릭터 1카메라 통합 씬
		// ════════════════════════════════════════════
		SpawnPreviewSceneV2();

		if (IsValid(SpawnedPreviewSceneV2))
		{
			V2Widget->SetupPreviewV2(PreviewV2RenderTarget, SpawnedPreviewSceneV2);

#if HELLUNA_DEBUG_CHARACTER_PREVIEW_V2
			UE_LOG(LogHelluna, Warning, TEXT("[로그인컨트롤러] V2 프리뷰 시스템 위젯 연동 완료"));
#endif
		}
	}
	else if (UHellunaCharSelectWidget_V1* V1Widget = Cast<UHellunaCharSelectWidget_V1>(CharSelectWidget))
	{
		// ════════════════════════════════════════════
		// 📌 V1 경로: 캐릭터별 개별 프리뷰
		// ════════════════════════════════════════════
		SpawnPreviewActors();

		if (SpawnedPreviewActors.Num() > 0)
		{
			TArray<UTextureRenderTarget2D*> RTs;
			for (const TObjectPtr<UTextureRenderTarget2D>& RT : PreviewRenderTargets)
			{
				RTs.Add(RT.Get());
			}

			TArray<AHellunaCharacterPreviewActor*> Actors;
			for (const TObjectPtr<AHellunaCharacterPreviewActor>& Actor : SpawnedPreviewActors)
			{
				Actors.Add(Actor.Get());
			}

			V1Widget->SetupPreviewV1(RTs, Actors);

#if HELLUNA_DEBUG_CHARACTER_PREVIEW
			UE_LOG(LogHelluna, Warning, TEXT("[로그인컨트롤러] V1 프리뷰 시스템 위젯 연동 완료 (Actors: %d, RTs: %d)"),
				Actors.Num(), RTs.Num());
#endif
		}
	}
	else
	{
#if HELLUNA_DEBUG_CHARACTER_SELECT
		UE_LOG(LogHelluna, Warning, TEXT("[로그인컨트롤러] 위젯이 V1/V2 서브클래스가 아님 - 프리뷰 스킵"));
#endif
	}
}

// ============================================
// 📌 캐릭터 프리뷰 시스템
// ============================================

void AHellunaLoginController::SpawnPreviewActors()
{
	// ════════════════════════════════════════════
	// 📌 네트워크 안전 - 클라이언트에서만 실행
	// ════════════════════════════════════════════

	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogHelluna, Error, TEXT("[로그인컨트롤러] 프리뷰 액터 스폰 실패 - World가 nullptr!"));
		return;
	}

	if (World->GetNetMode() == NM_DedicatedServer)
	{
		UE_LOG(LogHelluna, Warning, TEXT("[로그인컨트롤러] 프리뷰 액터 스폰 스킵 - 데디케이티드 서버"));
		return;
	}

	if (!PreviewActorClass)
	{
		UE_LOG(LogHelluna, Error, TEXT("[로그인컨트롤러] 프리뷰 액터 스폰 실패 - PreviewActorClass 미설정! BP에서 설정 필요"));
		return;
	}

	if (PreviewAnimClassMap.Num() == 0)
	{
		UE_LOG(LogHelluna, Error, TEXT("[로그인컨트롤러] 프리뷰 액터 스폰 실패 - PreviewAnimClassMap 비어있음! BP에서 설정 필요"));
		return;
	}

#if HELLUNA_DEBUG_CHARACTER_PREVIEW
	UE_LOG(LogHelluna, Warning, TEXT(""));
	UE_LOG(LogHelluna, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogHelluna, Warning, TEXT("║  🎭 [로그인컨트롤러] 프리뷰 액터 스폰                      ║"));
	UE_LOG(LogHelluna, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogHelluna, Warning, TEXT("║ PreviewActorClass: %s"), *PreviewActorClass->GetName());
	UE_LOG(LogHelluna, Warning, TEXT("║ PreviewAnimClassMap: %d개 등록"), PreviewAnimClassMap.Num());
	UE_LOG(LogHelluna, Warning, TEXT("║ SpawnBase: %s"), *PreviewSpawnBaseLocation.ToString());
	UE_LOG(LogHelluna, Warning, TEXT("║ Spacing: %.1f"), PreviewSpawnSpacing);
	UE_LOG(LogHelluna, Warning, TEXT("║ RT Size: %dx%d"), PreviewRenderTargetSize.X, PreviewRenderTargetSize.Y);
	UE_LOG(LogHelluna, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
#endif

	// ════════════════════════════════════════════
	// 📌 기존 프리뷰 정리 (중복 스폰 방지)
	// ════════════════════════════════════════════
	DestroyPreviewActors();

	// ════════════════════════════════════════════
	// 📌 캐릭터 타입별 스폰
	// ════════════════════════════════════════════

	const TArray<EHellunaHeroType> HeroTypes = { EHellunaHeroType::Lui, EHellunaHeroType::Luna, EHellunaHeroType::Liam };

	for (int32 i = 0; i < HeroTypes.Num(); i++)
	{
		const EHellunaHeroType HeroType = HeroTypes[i];

		// 메시 로드
		const TSoftObjectPtr<USkeletalMesh>* MeshPtr = PreviewMeshMap.Find(HeroType);
		if (!MeshPtr)
		{
			UE_LOG(LogHelluna, Warning, TEXT("[로그인컨트롤러] ⚠️ PreviewMeshMap에 %s 타입 미등록 - 스킵"),
				*UEnum::GetValueAsString(HeroType));
			SpawnedPreviewActors.Add(nullptr);
			PreviewRenderTargets.Add(nullptr);
			continue;
		}

		USkeletalMesh* LoadedMesh = MeshPtr->LoadSynchronous();
		if (!LoadedMesh)
		{
			UE_LOG(LogHelluna, Warning, TEXT("[로그인컨트롤러] ⚠️ %s SkeletalMesh 로드 실패 - 스킵"),
				*UEnum::GetValueAsString(HeroType));
			SpawnedPreviewActors.Add(nullptr);
			PreviewRenderTargets.Add(nullptr);
			continue;
		}

		// AnimClass 조회 (캐릭터별 개별 AnimBP)
		const TSubclassOf<UAnimInstance>* AnimClassPtr = PreviewAnimClassMap.Find(HeroType);
		if (!AnimClassPtr || !*AnimClassPtr)
		{
			UE_LOG(LogHelluna, Warning, TEXT("[로그인컨트롤러] ⚠️ PreviewAnimClassMap에 %s 타입 미등록 - 스킵"),
				*UEnum::GetValueAsString(HeroType));
			SpawnedPreviewActors.Add(nullptr);
			PreviewRenderTargets.Add(nullptr);
			continue;
		}

		// 액터 스폰
		FVector PreviewSpawnLocation = PreviewSpawnBaseLocation + FVector(i * PreviewSpawnSpacing, 0.f, 0.f);

		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AHellunaCharacterPreviewActor* PreviewActor = World->SpawnActor<AHellunaCharacterPreviewActor>(
			PreviewActorClass, PreviewSpawnLocation, FRotator::ZeroRotator, SpawnParams);

		if (!PreviewActor)
		{
			UE_LOG(LogHelluna, Error, TEXT("[로그인컨트롤러] ❌ %s 프리뷰 액터 스폰 실패!"),
				*UEnum::GetValueAsString(HeroType));
			SpawnedPreviewActors.Add(nullptr);
			PreviewRenderTargets.Add(nullptr);
			continue;
		}

		// RenderTarget 생성 (RGBA - 알파 채널 포함으로 배경 투명화)
		UTextureRenderTarget2D* RT = NewObject<UTextureRenderTarget2D>(this);
		RT->InitCustomFormat(PreviewRenderTargetSize.X, PreviewRenderTargetSize.Y, PF_FloatRGBA, false);
		RT->ClearColor = FLinearColor::Transparent;
		RT->UpdateResourceImmediate(true);

		// 프리뷰 초기화
		PreviewActor->InitializePreview(LoadedMesh, *AnimClassPtr, RT);

		// ════════════════════════════════════════════
		// 📌 하이라이트 오버레이 머티리얼 설정
		// ════════════════════════════════════════════
		UMaterialInterface* HighlightMat = nullptr;

		// BP 맵에서 먼저 조회
		if (const TObjectPtr<UMaterialInterface>* HighlightMatPtr = PreviewHighlightMaterialMap.Find(HeroType))
		{
			HighlightMat = *HighlightMatPtr;
		}

		// 맵이 비어있으면 하드코딩 경로로 폴백
		if (!HighlightMat)
		{
			static const TMap<EHellunaHeroType, FString> HighlightPaths = {
				{EHellunaHeroType::Lui,  TEXT("/Game/Login/Preview/Materials/M_Highlight_Lui.M_Highlight_Lui")},
				{EHellunaHeroType::Luna, TEXT("/Game/Login/Preview/Materials/M_Highlight_Luna.M_Highlight_Luna")},
				{EHellunaHeroType::Liam, TEXT("/Game/Login/Preview/Materials/M_Highlight_Liam.M_Highlight_Liam")}
			};

			if (const FString* Path = HighlightPaths.Find(HeroType))
			{
				HighlightMat = Cast<UMaterialInterface>(StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, **Path));
#if HELLUNA_DEBUG_CHARACTER_PREVIEW
				UE_LOG(LogTemp, Warning, TEXT("[V1 Highlight Fallback] %s → %s"),
					*UEnum::GetValueAsString(HeroType),
					HighlightMat ? *HighlightMat->GetName() : TEXT("LOAD FAILED"));
#endif
			}
		}

		if (HighlightMat)
		{
			PreviewActor->SetHighlightMaterial(HighlightMat);
		}

		SpawnedPreviewActors.Add(PreviewActor);
		PreviewRenderTargets.Add(RT);

#if HELLUNA_DEBUG_CHARACTER_PREVIEW
		UE_LOG(LogHelluna, Warning, TEXT("║ [%d] %s → ✅ 스폰 완료 (위치: %s)"),
			i, *UEnum::GetValueAsString(HeroType), *PreviewSpawnLocation.ToString());
#endif
	}

	// ════════════════════════════════════════════
	// 📌 배경 액터 검색 및 ShowOnlyList 등록
	// ════════════════════════════════════════════

	TArray<AActor*> BackgroundActors;
	UGameplayStatics::GetAllActorsWithTag(World, PreviewBackgroundActorTag, BackgroundActors);

#if HELLUNA_DEBUG_CHARACTER_PREVIEW
	UE_LOG(LogHelluna, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogHelluna, Warning, TEXT("║ 배경 액터 (태그: %s): %d개 발견"), *PreviewBackgroundActorTag.ToString(), BackgroundActors.Num());
#endif

	for (AHellunaCharacterPreviewActor* PreviewActor : SpawnedPreviewActors)
	{
		if (!IsValid(PreviewActor)) continue;
		for (AActor* BgActor : BackgroundActors)
		{
			PreviewActor->AddShowOnlyActor(BgActor);
		}
	}

#if HELLUNA_DEBUG_CHARACTER_PREVIEW
	UE_LOG(LogHelluna, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogHelluna, Warning, TEXT("║ 총 스폰: %d / %d"), SpawnedPreviewActors.Num(), HeroTypes.Num());
	UE_LOG(LogHelluna, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
	UE_LOG(LogHelluna, Warning, TEXT(""));
#endif
}

void AHellunaLoginController::DestroyPreviewActors()
{
#if HELLUNA_DEBUG_CHARACTER_PREVIEW
	if (SpawnedPreviewActors.Num() > 0)
	{
		UE_LOG(LogHelluna, Warning, TEXT("[로그인컨트롤러] 🗑️ 프리뷰 액터 파괴 - %d개"), SpawnedPreviewActors.Num());
	}
#endif

	for (TObjectPtr<AHellunaCharacterPreviewActor>& Actor : SpawnedPreviewActors)
	{
		if (IsValid(Actor))
		{
			Actor->Destroy();
		}
	}

	SpawnedPreviewActors.Empty();
	PreviewRenderTargets.Empty();
}

AHellunaCharacterPreviewActor* AHellunaLoginController::GetPreviewActor(int32 Index) const
{
	if (SpawnedPreviewActors.IsValidIndex(Index))
	{
		return SpawnedPreviewActors[Index];
	}
	return nullptr;
}

UTextureRenderTarget2D* AHellunaLoginController::GetPreviewRenderTarget(int32 Index) const
{
	if (PreviewRenderTargets.IsValidIndex(Index))
	{
		return PreviewRenderTargets[Index];
	}
	return nullptr;
}

// ============================================
// 📌 캐릭터 프리뷰 V2 시스템
// ============================================

void AHellunaLoginController::SpawnPreviewSceneV2()
{
	// ════════════════════════════════════════════
	// 📌 네트워크 안전 - 클라이언트에서만 실행
	// ════════════════════════════════════════════

	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogHelluna, Error, TEXT("[로그인컨트롤러] V2 프리뷰 씬 스폰 실패 - World가 nullptr!"));
		return;
	}

	if (World->GetNetMode() == NM_DedicatedServer)
	{
		UE_LOG(LogHelluna, Warning, TEXT("[로그인컨트롤러] V2 프리뷰 씬 스폰 스킵 - 데디케이티드 서버"));
		return;
	}

	if (!PreviewSceneV2Class)
	{
		UE_LOG(LogHelluna, Error, TEXT("[로그인컨트롤러] V2 프리뷰 씬 스폰 실패 - PreviewSceneV2Class 미설정!"));
		return;
	}

	if (PreviewMeshMap.Num() == 0)
	{
		UE_LOG(LogHelluna, Error, TEXT("[로그인컨트롤러] V2 프리뷰 씬 스폰 실패 - PreviewMeshMap 비어있음!"));
		return;
	}

	if (PreviewAnimClassMap.Num() == 0)
	{
		UE_LOG(LogHelluna, Error, TEXT("[로그인컨트롤러] V2 프리뷰 씬 스폰 실패 - PreviewAnimClassMap 비어있음!"));
		return;
	}

#if HELLUNA_DEBUG_CHARACTER_PREVIEW_V2
	UE_LOG(LogHelluna, Warning, TEXT(""));
	UE_LOG(LogHelluna, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogHelluna, Warning, TEXT("║  [로그인컨트롤러] V2 프리뷰 씬 스폰                        ║"));
	UE_LOG(LogHelluna, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogHelluna, Warning, TEXT("║ PreviewSceneV2Class: %s"), *PreviewSceneV2Class->GetName());
	UE_LOG(LogHelluna, Warning, TEXT("║ SpawnBase: %s"), *PreviewSpawnBaseLocation.ToString());
	UE_LOG(LogHelluna, Warning, TEXT("║ V2 RT Size: %dx%d"), PreviewV2RenderTargetSize.X, PreviewV2RenderTargetSize.Y);
#endif

	// ════════════════════════════════════════════
	// 📌 기존 V2 정리 (중복 스폰 방지)
	// ════════════════════════════════════════════
	DestroyPreviewSceneV2();

	// ════════════════════════════════════════════
	// 📌 V2 씬 액터 스폰
	// ════════════════════════════════════════════
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnedPreviewSceneV2 = World->SpawnActor<AHellunaCharacterSelectSceneV2>(
		PreviewSceneV2Class, PreviewSpawnBaseLocation, FRotator::ZeroRotator, SpawnParams);

	if (!SpawnedPreviewSceneV2)
	{
		UE_LOG(LogHelluna, Error, TEXT("[로그인컨트롤러] ❌ V2 프리뷰 씬 액터 스폰 실패!"));
		return;
	}

	// ════════════════════════════════════════════
	// 📌 RenderTarget 1개 생성
	// ════════════════════════════════════════════
	PreviewV2RenderTarget = NewObject<UTextureRenderTarget2D>(this);
	PreviewV2RenderTarget->InitCustomFormat(PreviewV2RenderTargetSize.X, PreviewV2RenderTargetSize.Y, PF_FloatRGBA, false);
	PreviewV2RenderTarget->ClearColor = FLinearColor::Transparent;
	PreviewV2RenderTarget->UpdateResourceImmediate(true);

	// ════════════════════════════════════════════
	// 📌 메시/애님 배열 구성
	// ════════════════════════════════════════════
	const TArray<EHellunaHeroType> HeroTypes = { EHellunaHeroType::Lui, EHellunaHeroType::Luna, EHellunaHeroType::Liam };

	TArray<USkeletalMesh*> Meshes;
	TArray<TSubclassOf<UAnimInstance>> AnimClasses;

	for (const EHellunaHeroType HeroType : HeroTypes)
	{
		// 메시 로드
		const TSoftObjectPtr<USkeletalMesh>* MeshPtr = PreviewMeshMap.Find(HeroType);
		USkeletalMesh* LoadedMesh = MeshPtr ? MeshPtr->LoadSynchronous() : nullptr;
		Meshes.Add(LoadedMesh);

		// AnimClass 조회
		const TSubclassOf<UAnimInstance>* AnimClassPtr = PreviewAnimClassMap.Find(HeroType);
		AnimClasses.Add(AnimClassPtr ? *AnimClassPtr : nullptr);

#if HELLUNA_DEBUG_CHARACTER_PREVIEW_V2
		UE_LOG(LogHelluna, Warning, TEXT("║ %s → Mesh: %s, AnimClass: %s"),
			*UEnum::GetValueAsString(HeroType),
			LoadedMesh ? *LoadedMesh->GetName() : TEXT("nullptr"),
			(AnimClassPtr && *AnimClassPtr) ? *(*AnimClassPtr)->GetName() : TEXT("nullptr"));
#endif
	}

	// ════════════════════════════════════════════
	// 📌 씬 초기화 (RT는 더 이상 Scene에 전달하지 않음)
	// ════════════════════════════════════════════
	SpawnedPreviewSceneV2->InitializeScene(Meshes, AnimClasses);

	// ════════════════════════════════════════════
	// 📌 로그인 전용 SceneCapture 생성 (로비에서는 직접 뷰포트 사용)
	// ════════════════════════════════════════════
	LoginSceneCapture = NewObject<USceneCaptureComponent2D>(SpawnedPreviewSceneV2, TEXT("LoginSceneCapture"));
	if (LoginSceneCapture)
	{
		LoginSceneCapture->RegisterComponent();
		LoginSceneCapture->AttachToComponent(SpawnedPreviewSceneV2->GetRootComponent(),
			FAttachmentTransformRules::KeepRelativeTransform);

		// 카메라 설정 (Scene의 카메라 값 참조)
		LoginSceneCapture->SetRelativeLocation(SpawnedPreviewSceneV2->GetCameraOffset());
		LoginSceneCapture->SetRelativeRotation(SpawnedPreviewSceneV2->GetCameraRotation());
		LoginSceneCapture->FOVAngle = SpawnedPreviewSceneV2->GetCameraFOV();

		// RT 바인딩
		LoginSceneCapture->TextureTarget = PreviewV2RenderTarget;

		// 캡처 설정
		LoginSceneCapture->bCaptureEveryFrame = true;
		LoginSceneCapture->bCaptureOnMovement = false;
		LoginSceneCapture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
		LoginSceneCapture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;

		// ShowFlags
		LoginSceneCapture->ShowFlags.SetAtmosphere(false);
		LoginSceneCapture->ShowFlags.SetFog(false);
		LoginSceneCapture->ShowFlags.SetVolumetricFog(false);
		LoginSceneCapture->ShowFlags.SetSkyLighting(false);
		LoginSceneCapture->ShowFlags.SetDynamicShadows(false);
		LoginSceneCapture->ShowFlags.SetGlobalIllumination(false);
		LoginSceneCapture->ShowFlags.SetScreenSpaceReflections(false);
		LoginSceneCapture->ShowFlags.SetAmbientOcclusion(false);
		LoginSceneCapture->ShowFlags.SetReflectionEnvironment(false);

		// AutoExposure
		LoginSceneCapture->PostProcessSettings.bOverride_AutoExposureBias = true;
		LoginSceneCapture->PostProcessSettings.AutoExposureBias = 3.0f;
		LoginSceneCapture->PostProcessBlendWeight = 1.0f;

		// ShowOnlyActors
		LoginSceneCapture->ShowOnlyActors.Empty();
		LoginSceneCapture->ShowOnlyActors.Add(SpawnedPreviewSceneV2);
	}

#if HELLUNA_DEBUG_CHARACTER_PREVIEW_V2
	UE_LOG(LogHelluna, Warning, TEXT("║ V2 프리뷰 씬 스폰 및 초기화 완료 (로그인 전용 SceneCapture)"));
	UE_LOG(LogHelluna, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
	UE_LOG(LogHelluna, Warning, TEXT(""));
#endif
}

void AHellunaLoginController::DestroyPreviewSceneV2()
{
#if HELLUNA_DEBUG_CHARACTER_PREVIEW_V2
	if (IsValid(SpawnedPreviewSceneV2))
	{
		UE_LOG(LogHelluna, Warning, TEXT("[로그인컨트롤러] 🗑️ V2 프리뷰 씬 파괴"));
	}
#endif

	// LoginSceneCapture는 SpawnedPreviewSceneV2의 자식이므로 Destroy 시 함께 정리됨
	LoginSceneCapture = nullptr;

	if (IsValid(SpawnedPreviewSceneV2))
	{
		SpawnedPreviewSceneV2->Destroy();
		SpawnedPreviewSceneV2 = nullptr;
	}

	PreviewV2RenderTarget = nullptr;
}
