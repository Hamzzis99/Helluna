#include "GameMode/HellunaDefenseGameMode.h"

#include "Engine/TargetPoint.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"                 
#include "GameFramework/PlayerController.h"
#include "GameMode/HellunaDefenseGameState.h"
#include "Object/ResourceUsingObject/ResourceUsingObject_SpaceShip.h"
#include "MDF_Function/MDF_Instance/MDF_GameInstance.h"
#include "Player/HellunaPlayerState.h"
#include "Login/HellunaAccountSaveGame.h"
#include "Login/HellunaLoginController.h"
#include "GameFramework/SpectatorPawn.h"

#include "debughelper.h"

AHellunaDefenseGameMode::AHellunaDefenseGameMode()
{
	PrimaryActorTick.bCanEverTick = false;
	bUseSeamlessTravel = true;
	PlayerStateClass = AHellunaPlayerState::StaticClass();
	PlayerControllerClass = AHellunaLoginController::StaticClass();
	DefaultPawnClass = ASpectatorPawn::StaticClass();
}

void AHellunaDefenseGameMode::BeginPlay()
{
	Super::BeginPlay();

	if (!HasAuthority())
		return;

	// 계정 데이터 로드
	AccountSaveGame = UHellunaAccountSaveGame::LoadOrCreate();

	// 스폰 포인트 캐싱 (미리 해둠)
	CacheBossSpawnPoints();
	CacheMonsterSpawnPoints();

	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║     [DefenseGameMode] BeginPlay                            ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ PlayerControllerClass: %s"), PlayerControllerClass ? *PlayerControllerClass->GetName() : TEXT("nullptr"));
	UE_LOG(LogTemp, Warning, TEXT("║ PlayerStateClass: %s"), PlayerStateClass ? *PlayerStateClass->GetName() : TEXT("nullptr"));
	UE_LOG(LogTemp, Warning, TEXT("║ DefaultPawnClass: %s"), DefaultPawnClass ? *DefaultPawnClass->GetName() : TEXT("nullptr"));
	UE_LOG(LogTemp, Warning, TEXT("║ HeroCharacterClass: %s"), HeroCharacterClass ? *HeroCharacterClass->GetName() : TEXT("미설정!"));
	UE_LOG(LogTemp, Warning, TEXT("║ AccountCount: %d"), AccountSaveGame ? AccountSaveGame->GetAccountCount() : 0);
	UE_LOG(LogTemp, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ ※ 게임 초기화 대기 중...                                  ║"));
	UE_LOG(LogTemp, Warning, TEXT("║ ※ 첫 플레이어가 로그인 + 캐릭터 소환되면 게임 시작!       ║"));
	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
	UE_LOG(LogTemp, Warning, TEXT(""));

	// ※ EnterDay() 여기서 호출 안함!
	// ※ 첫 플레이어 캐릭터 소환 후 InitializeGame()에서 호출
}

void AHellunaDefenseGameMode::InitializeGame()
{
	if (bGameInitialized)
	{
		UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] 이미 초기화됨, 스킵"));
		return;
	}

	bGameInitialized = true;

	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║     [DefenseGameMode] InitializeGame 🎮                    ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ 첫 플레이어 캐릭터 소환 완료!                              ║"));
	UE_LOG(LogTemp, Warning, TEXT("║ → 게임 환경 초기화 시작...                                 ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ • 낮/밤 사이클 시작                                        ║"));
	UE_LOG(LogTemp, Warning, TEXT("║ • 몬스터 스포너 활성화                                     ║"));
	UE_LOG(LogTemp, Warning, TEXT("║ • 보스 시스템 대기                                         ║"));
	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
	UE_LOG(LogTemp, Warning, TEXT(""));

	// 낮/밤 사이클 시작!
	EnterDay();
}

void AHellunaDefenseGameMode::PostLogin(APlayerController* NewPlayer)
{
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║     [DefenseGameMode] PostLogin                            ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ Controller: %s"), *GetNameSafe(NewPlayer));
	UE_LOG(LogTemp, Warning, TEXT("║ ControllerClass: %s"), NewPlayer ? *NewPlayer->GetClass()->GetName() : TEXT("nullptr"));
	UE_LOG(LogTemp, Warning, TEXT("║ ControllerID: %d"), NewPlayer ? NewPlayer->GetUniqueID() : -1);
	UE_LOG(LogTemp, Warning, TEXT("║ NetConnection: %s"), (NewPlayer && NewPlayer->GetNetConnection()) ? TEXT("Valid") : TEXT("nullptr (ListenServer)"));
	UE_LOG(LogTemp, Warning, TEXT("║ GameInitialized: %s"), bGameInitialized ? TEXT("TRUE") : TEXT("FALSE"));

	if (NewPlayer)
	{
		AHellunaPlayerState* PS = NewPlayer->GetPlayerState<AHellunaPlayerState>();
		UE_LOG(LogTemp, Warning, TEXT("║ PlayerState: %s"), PS ? *PS->GetName() : TEXT("nullptr"));
		
		if (PS)
		{
			UE_LOG(LogTemp, Warning, TEXT("║   - PlayerId: '%s'"), *PS->GetPlayerUniqueId());
			UE_LOG(LogTemp, Warning, TEXT("║   - IsLoggedIn: %s"), PS->IsLoggedIn() ? TEXT("TRUE") : TEXT("FALSE"));
		}
	}
	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));

	if (!NewPlayer)
	{
		Super::PostLogin(NewPlayer);
		return;
	}

	AHellunaPlayerState* PS = NewPlayer->GetPlayerState<AHellunaPlayerState>();
	
	// 이미 로그인된 상태 (SeamlessTravel 등)
	if (PS && PS->IsLoggedIn() && !PS->GetPlayerUniqueId().IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] 이미 로그인됨! → HeroCharacter 소환"));
		
		FTimerHandle TimerHandle;
		GetWorldTimerManager().SetTimer(TimerHandle, [this, NewPlayer]()
		{
			if (IsValid(NewPlayer))
			{
				SpawnHeroCharacter(NewPlayer);
			}
		}, 0.5f, false);
	}
	else
	{
		// 로그인 필요 → 타임아웃 타이머 시작
		UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] 로그인 필요! 타임아웃: %.0f초"), LoginTimeoutSeconds);
		
		FTimerHandle& TimeoutTimer = LoginTimeoutTimers.FindOrAdd(NewPlayer);
		GetWorldTimerManager().SetTimer(TimeoutTimer, [this, NewPlayer]()
		{
			if (IsValid(NewPlayer))
			{
				OnLoginTimeout(NewPlayer);
			}
		}, LoginTimeoutSeconds, false);
	}

	UE_LOG(LogTemp, Warning, TEXT(""));

	Super::PostLogin(NewPlayer);
}

void AHellunaDefenseGameMode::ProcessLogin(APlayerController* PlayerController, const FString& PlayerId, const FString& Password)
{
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║     [DefenseGameMode] ProcessLogin                         ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ PlayerId: '%s'"), *PlayerId);
	UE_LOG(LogTemp, Warning, TEXT("║ Controller: %s (ID: %d)"), *GetNameSafe(PlayerController), PlayerController ? PlayerController->GetUniqueID() : -1);
	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));

	if (!HasAuthority())
	{
		UE_LOG(LogTemp, Error, TEXT("[DefenseGameMode] 서버 권한 없음!"));
		return;
	}

	if (!PlayerController)
	{
		UE_LOG(LogTemp, Error, TEXT("[DefenseGameMode] PlayerController nullptr!"));
		return;
	}

	// 동시 접속 체크
	if (IsPlayerLoggedIn(PlayerId))
	{
		UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] 동시 접속 거부: '%s'"), *PlayerId);
		OnLoginFailed(PlayerController, TEXT("이미 접속 중인 계정입니다."));
		return;
	}

	if (!AccountSaveGame)
	{
		UE_LOG(LogTemp, Error, TEXT("[DefenseGameMode] AccountSaveGame nullptr!"));
		OnLoginFailed(PlayerController, TEXT("서버 오류"));
		return;
	}

	// 계정 검증
	if (AccountSaveGame->HasAccount(PlayerId))
	{
		if (AccountSaveGame->ValidatePassword(PlayerId, Password))
		{
			UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] 비밀번호 일치!"));
			OnLoginSuccess(PlayerController, PlayerId);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] 비밀번호 불일치!"));
			OnLoginFailed(PlayerController, TEXT("비밀번호를 확인해주세요."));
		}
	}
	else
	{
		if (AccountSaveGame->CreateAccount(PlayerId, Password))
		{
			UHellunaAccountSaveGame::Save(AccountSaveGame);
			UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] 새 계정 생성: '%s'"), *PlayerId);
			OnLoginSuccess(PlayerController, PlayerId);
		}
		else
		{
			OnLoginFailed(PlayerController, TEXT("계정 생성 실패"));
		}
	}

	UE_LOG(LogTemp, Warning, TEXT(""));
}

void AHellunaDefenseGameMode::OnLoginSuccess(APlayerController* PlayerController, const FString& PlayerId)
{
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║     [DefenseGameMode] OnLoginSuccess ✅                    ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ PlayerId: '%s'"), *PlayerId);
	UE_LOG(LogTemp, Warning, TEXT("║ Controller: %s (ID: %d)"), *GetNameSafe(PlayerController), PlayerController ? PlayerController->GetUniqueID() : -1);
	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));

	if (!PlayerController) return;

	// 타임아웃 타이머 취소
	if (FTimerHandle* Timer = LoginTimeoutTimers.Find(PlayerController))
	{
		GetWorldTimerManager().ClearTimer(*Timer);
		LoginTimeoutTimers.Remove(PlayerController);
		UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] 타임아웃 타이머 취소됨"));
	}

	// GameInstance에 등록
	if (UMDF_GameInstance* GI = Cast<UMDF_GameInstance>(UGameplayStatics::GetGameInstance(GetWorld())))
	{
		GI->RegisterLogin(PlayerId);
		UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] GameInstance에 등록됨"));
	}

	// PlayerState에 저장
	if (AHellunaPlayerState* PS = PlayerController->GetPlayerState<AHellunaPlayerState>())
	{
		PS->SetLoginInfo(PlayerId);
		UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] PlayerState에 저장됨"));
	}

	// Client RPC로 결과 전달
	AHellunaLoginController* LoginController = Cast<AHellunaLoginController>(PlayerController);
	if (LoginController)
	{
		LoginController->Client_LoginResult(true, TEXT(""));
		UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] Client_LoginResult(true) 호출됨"));
	}

	// HeroCharacter 소환 (딜레이)
	FTimerHandle TimerHandle;
	GetWorldTimerManager().SetTimer(TimerHandle, [this, PlayerController, PlayerId]()
	{
		if (IsValid(PlayerController))
		{
			// LoginController인 경우 GameController로 교체
			AHellunaLoginController* LoginController = Cast<AHellunaLoginController>(PlayerController);
			if (LoginController && LoginController->GetGameControllerClass())
			{
				SwapToGameController(LoginController, PlayerId);
			}
			else
			{
				// GameControllerClass 미설정 시 기존 방식 사용
				UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] GameControllerClass 미설정! 기존 방식으로 소환"));
				SpawnHeroCharacter(PlayerController);
			}
		}
	}, 0.5f, false);

	UE_LOG(LogTemp, Warning, TEXT(""));
}

void AHellunaDefenseGameMode::OnLoginFailed(APlayerController* PlayerController, const FString& ErrorMessage)
{
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║     [DefenseGameMode] OnLoginFailed ❌                     ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ ErrorMessage: '%s'"), *ErrorMessage);
	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));

	AHellunaLoginController* LoginController = Cast<AHellunaLoginController>(PlayerController);
	if (LoginController)
	{
		LoginController->Client_LoginResult(false, ErrorMessage);
	}

	UE_LOG(LogTemp, Warning, TEXT(""));
}

void AHellunaDefenseGameMode::OnLoginTimeout(APlayerController* PlayerController)
{
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║     [DefenseGameMode] OnLoginTimeout ⏰                    ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ Controller: %s"), *GetNameSafe(PlayerController));
	UE_LOG(LogTemp, Warning, TEXT("║ → 킥 처리!                                                 ║"));
	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));

	if (!PlayerController) return;

	LoginTimeoutTimers.Remove(PlayerController);

	if (PlayerController->GetNetConnection())
	{
		FString KickReason = FString::Printf(TEXT("로그인 타임아웃 (%.0f초)"), LoginTimeoutSeconds);
		PlayerController->ClientReturnToMainMenuWithTextReason(FText::FromString(KickReason));
	}

	UE_LOG(LogTemp, Warning, TEXT(""));
}

void AHellunaDefenseGameMode::SwapToGameController(AHellunaLoginController* LoginController, const FString& PlayerId)
{
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║     [DefenseGameMode] SwapToGameController 🔄              ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ PlayerId: '%s'"), *PlayerId);
	UE_LOG(LogTemp, Warning, TEXT("║ LoginController: %s"), *GetNameSafe(LoginController));

	if (!LoginController)
	{
		UE_LOG(LogTemp, Error, TEXT("║ ❌ LoginController nullptr!                                ║"));
		UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
		return;
	}

	TSubclassOf<APlayerController> GameControllerClass = LoginController->GetGameControllerClass();
	if (!GameControllerClass)
	{
		UE_LOG(LogTemp, Error, TEXT("║ ❌ GameControllerClass 미설정!                             ║"));
		UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
		SpawnHeroCharacter(LoginController);
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("║ GameControllerClass: %s"), *GameControllerClass->GetName());

	// ============================================
	// ⭐ 중요: LoginController의 PlayerState 정리
	// SwapPlayerControllers 후 LoginController가 파괴될 때
	// Logout에서 중복으로 로그아웃 처리되는 것을 방지
	// ============================================
	if (AHellunaPlayerState* OldPS = LoginController->GetPlayerState<AHellunaPlayerState>())
	{
		OldPS->ClearLoginInfo();
		UE_LOG(LogTemp, Warning, TEXT("║ LoginController PlayerState 정리됨 (중복 로그아웃 방지)   ║"));
	}

	// 1. 새 GameController 스폰
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.Owner = this;

	FVector SpawnLocation = LoginController->GetFocalLocation();
	FRotator SpawnRotation = LoginController->GetControlRotation();

	APlayerController* NewController = GetWorld()->SpawnActor<APlayerController>(
		GameControllerClass,
		SpawnLocation,
		SpawnRotation,
		SpawnParams
	);

	if (!NewController)
	{
		UE_LOG(LogTemp, Error, TEXT("║ ❌ 새 Controller 스폰 실패!                                ║"));
		UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
		SpawnHeroCharacter(LoginController);
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("║ NewController: %s"), *NewController->GetName());

	// 2. LoginController의 로그인 UI 정리
	LoginController->Client_PrepareControllerSwap();

	// 3. SwapPlayerControllers로 안전하게 교체
	// ⭐ PlayerState도 새 Controller로 전이됨!
	UE_LOG(LogTemp, Warning, TEXT("║ → SwapPlayerControllers 호출...                            ║"));
	SwapPlayerControllers(LoginController, NewController);

	UE_LOG(LogTemp, Warning, TEXT("║ ✅ Controller 교체 완료!                                   ║"));

	// 4. 새 Controller의 PlayerState에 PlayerId 복원
	if (AHellunaPlayerState* NewPS = NewController->GetPlayerState<AHellunaPlayerState>())
	{
		NewPS->SetLoginInfo(PlayerId);
		UE_LOG(LogTemp, Warning, TEXT("║ 새 Controller PlayerState에 PlayerId 복원: '%s'           ║"), *PlayerId);
	}

	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));

	// 5. HeroCharacter 소환 (새 Controller로)
	FTimerHandle TimerHandle;
	GetWorldTimerManager().SetTimer(TimerHandle, [this, NewController]()
	{
		if (IsValid(NewController))
		{
			SpawnHeroCharacter(NewController);
		}
	}, 0.3f, false);

	UE_LOG(LogTemp, Warning, TEXT(""));
}

void AHellunaDefenseGameMode::SpawnHeroCharacter(APlayerController* PlayerController)
{
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║     [DefenseGameMode] SpawnHeroCharacter                   ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ Controller: %s (ID: %d)"), *GetNameSafe(PlayerController), PlayerController ? PlayerController->GetUniqueID() : -1);

	if (!PlayerController)
	{
		UE_LOG(LogTemp, Error, TEXT("║ ❌ Controller nullptr!                                     ║"));
		UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
		return;
	}

	if (!HeroCharacterClass)
	{
		UE_LOG(LogTemp, Error, TEXT("║ ❌ HeroCharacterClass 미설정!                              ║"));
		UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 10.f, FColor::Red,
				TEXT("HeroCharacterClass 미설정! GameMode BP에서 설정 필요"));
		}
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("║ HeroCharacterClass: %s"), *HeroCharacterClass->GetName());

	// 기존 Pawn 제거
	APawn* OldPawn = PlayerController->GetPawn();
	if (OldPawn)
	{
		UE_LOG(LogTemp, Warning, TEXT("║ OldPawn 제거: %s"), *OldPawn->GetName());
		PlayerController->UnPossess();
		OldPawn->Destroy();
	}

	// 스폰 위치
	FVector SpawnLocation = FVector::ZeroVector;
	FRotator SpawnRotation = FRotator::ZeroRotator;

	AActor* PlayerStart = FindPlayerStart(PlayerController);
	if (PlayerStart)
	{
		SpawnLocation = PlayerStart->GetActorLocation();
		SpawnRotation = PlayerStart->GetActorRotation();
		UE_LOG(LogTemp, Warning, TEXT("║ SpawnLocation: PlayerStart (%s)"), *SpawnLocation.ToString());
	}
	else
	{
		SpawnLocation = FVector(0.f, 0.f, 200.f);
		UE_LOG(LogTemp, Warning, TEXT("║ SpawnLocation: Default (%s)"), *SpawnLocation.ToString());
	}

	// HeroCharacter 스폰
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	SpawnParams.Owner = PlayerController;

	APawn* NewPawn = GetWorld()->SpawnActor<APawn>(HeroCharacterClass, SpawnLocation, SpawnRotation, SpawnParams);

	if (!NewPawn)
	{
		UE_LOG(LogTemp, Error, TEXT("║ ❌ HeroCharacter 스폰 실패!                                ║"));
		UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("║ NewPawn: %s"), *NewPawn->GetName());

	// Possess
	PlayerController->Possess(NewPawn);
	
	// 로그인 UI 숨기기
	AHellunaLoginController* LoginController = Cast<AHellunaLoginController>(PlayerController);
	if (LoginController)
	{
		LoginController->Client_PrepareControllerSwap();
	}

	UE_LOG(LogTemp, Warning, TEXT("║ ✅ Possess 완료!                                           ║"));
	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));

	// ============================================
	// 📌 첫 플레이어 캐릭터 소환 → 게임 초기화!
	// ============================================
	if (!bGameInitialized)
	{
		UE_LOG(LogTemp, Warning, TEXT(""));
		UE_LOG(LogTemp, Warning, TEXT("┌────────────────────────────────────────────────────────────┐"));
		UE_LOG(LogTemp, Warning, TEXT("│ 🎮 첫 플레이어 캐릭터 소환 완료!                          │"));
		UE_LOG(LogTemp, Warning, TEXT("│ → 게임 초기화 시작...                                      │"));
		UE_LOG(LogTemp, Warning, TEXT("└────────────────────────────────────────────────────────────┘"));
		
		InitializeGame();
	}

	UE_LOG(LogTemp, Warning, TEXT(""));
}

bool AHellunaDefenseGameMode::IsPlayerLoggedIn(const FString& PlayerId) const
{
	if (UMDF_GameInstance* GI = Cast<UMDF_GameInstance>(UGameplayStatics::GetGameInstance(GetWorld())))
	{
		return GI->IsPlayerLoggedIn(PlayerId);
	}
	return false;
}

void AHellunaDefenseGameMode::Logout(AController* Exiting)
{
	UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] Logout: %s"), *GetNameSafe(Exiting));
	
	if (Exiting)
	{
		if (APlayerController* PC = Cast<APlayerController>(Exiting))
		{
			if (FTimerHandle* Timer = LoginTimeoutTimers.Find(PC))
			{
				GetWorldTimerManager().ClearTimer(*Timer);
				LoginTimeoutTimers.Remove(PC);
			}
		}

		if (AHellunaPlayerState* PS = Exiting->GetPlayerState<AHellunaPlayerState>())
		{
			FString PlayerId = PS->GetPlayerUniqueId();
			if (!PlayerId.IsEmpty())
			{
				if (UMDF_GameInstance* GI = Cast<UMDF_GameInstance>(UGameplayStatics::GetGameInstance(GetWorld())))
				{
					GI->RegisterLogout(PlayerId);
					UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] 로그아웃: '%s'"), *PlayerId);
				}
			}
		}
	}

	Super::Logout(Exiting);
}

void AHellunaDefenseGameMode::HandleSeamlessTravelPlayer(AController*& C)
{
	UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] HandleSeamlessTravelPlayer"));
	
	FString SavedPlayerId;
	bool bSavedIsLoggedIn = false;
	
	if (C)
	{
		if (AHellunaPlayerState* OldPS = C->GetPlayerState<AHellunaPlayerState>())
		{
			SavedPlayerId = OldPS->GetPlayerUniqueId();
			bSavedIsLoggedIn = OldPS->IsLoggedIn();
			UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] 저장: PlayerId='%s', IsLoggedIn=%s"), 
				*SavedPlayerId, bSavedIsLoggedIn ? TEXT("TRUE") : TEXT("FALSE"));
		}
	}

	Super::HandleSeamlessTravelPlayer(C);
	
	if (C && !SavedPlayerId.IsEmpty())
	{
		if (AHellunaPlayerState* NewPS = C->GetPlayerState<AHellunaPlayerState>())
		{
			NewPS->SetLoginInfo(SavedPlayerId);
			UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] 복원: '%s' → %s"), *SavedPlayerId, *NewPS->GetName());
		}
	}
}

// ============================================
// 게임 로직
// ============================================

void AHellunaDefenseGameMode::CacheBossSpawnPoints()
{
	BossSpawnPoints.Empty();
	TArray<AActor*> Found;
	UGameplayStatics::GetAllActorsOfClass(this, ATargetPoint::StaticClass(), Found);

	for (AActor* A : Found)
	{
		if (ATargetPoint* TP = Cast<ATargetPoint>(A))
		{
			if (TP->ActorHasTag(BossSpawnPointTag))
				BossSpawnPoints.Add(TP);
		}
	}
}

void AHellunaDefenseGameMode::CacheMonsterSpawnPoints()
{
	MonsterSpawnPoints.Empty();
	TArray<AActor*> Found;
	UGameplayStatics::GetAllActorsOfClass(this, ATargetPoint::StaticClass(), Found);

	for (AActor* A : Found)
	{
		if (ATargetPoint* TP = Cast<ATargetPoint>(A))
		{
			if (TP->ActorHasTag(MonsterSpawnPointTag))
				MonsterSpawnPoints.Add(TP);
		}
	}
}

void AHellunaDefenseGameMode::SpawnTestMonsters()
{
	if (!HasAuthority() || !bGameInitialized) return;

	if (!TestMonsterClass)
	{
		Debug::Print(TEXT("[Defense] TestMonsterClass is null"), FColor::Red);
		return;
	}

	if (MonsterSpawnPoints.IsEmpty())
	{
		Debug::Print(TEXT("[Defense] No MonsterSpawn TargetPoints"), FColor::Red);
		return;
	}

	for (int32 i = 0; i < TestMonsterSpawnCount; ++i)
	{
		ATargetPoint* TP = MonsterSpawnPoints[FMath::RandRange(0, MonsterSpawnPoints.Num() - 1)];
		if (!TP) continue;

		FActorSpawnParameters Param;
		Param.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

		GetWorld()->SpawnActor<APawn>(TestMonsterClass, TP->GetActorLocation(), TP->GetActorRotation(), Param);
	}
}

void AHellunaDefenseGameMode::TrySummonBoss()
{
	if (!HasAuthority() || !bGameInitialized || !BossClass || BossSpawnPoints.IsEmpty())
		return;

	ATargetPoint* TP = BossSpawnPoints[FMath::RandRange(0, BossSpawnPoints.Num() - 1)];
	const FVector SpawnLoc = TP->GetActorLocation() + FVector(0, 0, SpawnZOffset);

	FActorSpawnParameters Param;
	Param.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	APawn* Boss = GetWorld()->SpawnActor<APawn>(BossClass, SpawnLoc, TP->GetActorRotation(), Param);
	if (Boss) bBossReady = false;
}

void AHellunaDefenseGameMode::RestartGame()
{
	if (!HasAuthority()) return;
	
	bGameInitialized = false; // 리셋
	GetWorld()->ServerTravel(TEXT("/Game/Minwoo/MinwooTestMap?listen"));
}

void AHellunaDefenseGameMode::SetBossReady(bool bReady)
{
	if (!HasAuthority() || bBossReady == bReady) return;
	bBossReady = bReady;
	if (bBossReady) TrySummonBoss();
}

void AHellunaDefenseGameMode::EnterDay()
{
	if (!bGameInitialized)
	{
		UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] EnterDay 스킵 - 게임 미초기화"));
		return;
	}

	AliveMonsters.Empty();

	if (AHellunaDefenseGameState* GS = GetGameState<AHellunaDefenseGameState>())
	{
		GS->SetPhase(EDefensePhase::Day);
		GS->SetAliveMonsterCount(0);
		GS->MulticastPrintDay();
	}

	GetWorldTimerManager().ClearTimer(TimerHandle_ToNight);
	GetWorldTimerManager().SetTimer(TimerHandle_ToNight, this, &ThisClass::EnterNight, TestDayDuration, false);
}

void AHellunaDefenseGameMode::EnterNight()
{
	if (!HasAuthority() || !bGameInitialized) return;

	AliveMonsters.Empty();

	if (AHellunaDefenseGameState* GS = GetGameState<AHellunaDefenseGameState>())
	{
		GS->SetPhase(EDefensePhase::Night);
		GS->SetAliveMonsterCount(0);
	}

	int32 Current = 0, Need = 0;
	if (IsSpaceShipFullyRepaired(Current, Need))
	{
		SetBossReady(true);
		return;
	}

	SpawnTestMonsters();
}

bool AHellunaDefenseGameMode::IsSpaceShipFullyRepaired(int32& OutCurrent, int32& OutNeed) const
{
	OutCurrent = 0;
	OutNeed = 0;

	const AHellunaDefenseGameState* GS = GetGameState<AHellunaDefenseGameState>();
	if (!GS) return false;

	AResourceUsingObject_SpaceShip* Ship = GS->GetSpaceShip();
	if (!Ship) return false;

	OutCurrent = Ship->GetCurrentResource();
	OutNeed = Ship->GetNeedResource();

	return (OutNeed > 0) && (OutCurrent >= OutNeed);
}

void AHellunaDefenseGameMode::RegisterAliveMonster(AActor* Monster)
{
	if (!HasAuthority() || !Monster || !bGameInitialized) return;

	AHellunaDefenseGameState* GS = GetGameState<AHellunaDefenseGameState>();
	if (!GS || GS->GetPhase() != EDefensePhase::Night) return;

	if (AliveMonsters.Contains(Monster)) return;

	AliveMonsters.Add(Monster);
	GS->SetAliveMonsterCount(AliveMonsters.Num());
}

void AHellunaDefenseGameMode::NotifyMonsterDied(AActor* DeadMonster)
{
	if (!HasAuthority() || !DeadMonster || !bGameInitialized) return;

	AHellunaDefenseGameState* GS = GetGameState<AHellunaDefenseGameState>();
	if (!GS) return;

	AliveMonsters.Remove(TWeakObjectPtr<AActor>(DeadMonster));
	GS->SetAliveMonsterCount(AliveMonsters.Num());

	if (AliveMonsters.Num() <= 0)
	{
		GetWorldTimerManager().ClearTimer(TimerHandle_ToDay);
		GetWorldTimerManager().SetTimer(TimerHandle_ToDay, this, &ThisClass::EnterDay, TestNightFailToDayDelay, false);
	}
}
