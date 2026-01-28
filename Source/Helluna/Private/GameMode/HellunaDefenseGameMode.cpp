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

	AccountSaveGame = UHellunaAccountSaveGame::LoadOrCreate();

	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║     [DefenseGameMode] BeginPlay                            ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ PlayerControllerClass: %s"), PlayerControllerClass ? *PlayerControllerClass->GetName() : TEXT("nullptr"));
	UE_LOG(LogTemp, Warning, TEXT("║ PlayerStateClass: %s"), PlayerStateClass ? *PlayerStateClass->GetName() : TEXT("nullptr"));
	UE_LOG(LogTemp, Warning, TEXT("║ DefaultPawnClass: %s"), DefaultPawnClass ? *DefaultPawnClass->GetName() : TEXT("nullptr"));
	UE_LOG(LogTemp, Warning, TEXT("║ HeroCharacterClass: %s"), HeroCharacterClass ? *HeroCharacterClass->GetName() : TEXT("미설정!"));
	UE_LOG(LogTemp, Warning, TEXT("║ AccountCount: %d"), AccountSaveGame ? AccountSaveGame->GetAccountCount() : 0);
	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
	UE_LOG(LogTemp, Warning, TEXT(""));

	CacheBossSpawnPoints();
	CacheMonsterSpawnPoints();
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
	UE_LOG(LogTemp, Warning, TEXT("║ NetConnection: %s"), (NewPlayer && NewPlayer->GetNetConnection()) ? TEXT("Valid") : TEXT("nullptr"));

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
	
	// 이미 로그인된 상태 (SeamlessTravel)
	if (PS && PS->IsLoggedIn() && !PS->GetPlayerUniqueId().IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] 이미 로그인됨! → Controller 교체 + HeroCharacter 소환"));
		
		AHellunaLoginController* LoginController = Cast<AHellunaLoginController>(NewPlayer);
		if (LoginController)
		{
			FTimerHandle TimerHandle;
			FString SavedPlayerId = PS->GetPlayerUniqueId();
			GetWorldTimerManager().SetTimer(TimerHandle, [this, LoginController, SavedPlayerId]()
			{
				SwapToGameController(LoginController, SavedPlayerId);
			}, 0.5f, false);
		}
	}
	else
	{
		// 로그인 필요 → 타임아웃 타이머 시작
		UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] 로그인 필요! 타임아웃: %.0f초"), LoginTimeoutSeconds);
		
		FTimerHandle& TimeoutTimer = LoginTimeoutTimers.FindOrAdd(NewPlayer);
		GetWorldTimerManager().SetTimer(TimeoutTimer, [this, NewPlayer]()
		{
			OnLoginTimeout(NewPlayer);
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
		
		// 약간의 딜레이 후 Controller 교체
		FTimerHandle TimerHandle;
		GetWorldTimerManager().SetTimer(TimerHandle, [this, LoginController, PlayerId]()
		{
			SwapToGameController(LoginController, PlayerId);
		}, 0.5f, false);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[DefenseGameMode] LoginController 아님! 직접 HeroCharacter 소환"));
		SpawnHeroCharacter(PlayerController);
	}

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
	UE_LOG(LogTemp, Warning, TEXT("║     [DefenseGameMode] SwapToGameController                 ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ OldController: %s (ID: %d)"), *GetNameSafe(LoginController), LoginController ? LoginController->GetUniqueID() : -1);
	UE_LOG(LogTemp, Warning, TEXT("║ PlayerId: '%s'"), *PlayerId);

	if (!LoginController)
	{
		UE_LOG(LogTemp, Error, TEXT("║ ❌ LoginController가 nullptr!                              ║"));
		UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
		return;
	}

	// GameControllerClass 가져오기
	TSubclassOf<APlayerController> GameControllerClass = LoginController->GetGameControllerClass();
	
	UE_LOG(LogTemp, Warning, TEXT("║ GameControllerClass: %s"), GameControllerClass ? *GameControllerClass->GetName() : TEXT("미설정!"));

	if (!GameControllerClass)
	{
		UE_LOG(LogTemp, Error, TEXT("║ ❌ GameControllerClass 미설정! BP에서 설정 필요!           ║"));
		UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
		// 직접 HeroCharacter만 소환
		SpawnHeroCharacter(LoginController);
		return;
	}

	// 네트워크 연결 정보 저장
	UNetConnection* ClientConnection = LoginController->GetNetConnection();
	UE_LOG(LogTemp, Warning, TEXT("║ NetConnection: %s"), ClientConnection ? TEXT("Valid") : TEXT("nullptr (Listen Server)"));

	// PlayerState 저장
	AHellunaPlayerState* OldPS = LoginController->GetPlayerState<AHellunaPlayerState>();
	UE_LOG(LogTemp, Warning, TEXT("║ OldPlayerState: %s"), OldPS ? *OldPS->GetName() : TEXT("nullptr"));

	// 클라이언트에 UI 정리 요청
	LoginController->Client_PrepareControllerSwap();
	UE_LOG(LogTemp, Warning, TEXT("║ Client_PrepareControllerSwap 호출됨"));

	// 기존 Pawn UnPossess
	APawn* OldPawn = LoginController->GetPawn();
	if (OldPawn)
	{
		LoginController->UnPossess();
		UE_LOG(LogTemp, Warning, TEXT("║ OldPawn UnPossess: %s"), *OldPawn->GetName());
	}

	// 새 Controller 스폰
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	
	APlayerController* NewController = GetWorld()->SpawnActor<APlayerController>(
		GameControllerClass,
		LoginController->GetActorLocation(),
		LoginController->GetActorRotation(),
		SpawnParams
	);

	UE_LOG(LogTemp, Warning, TEXT("║ NewController: %s (ID: %d)"), *GetNameSafe(NewController), NewController ? NewController->GetUniqueID() : -1);

	if (!NewController)
	{
		UE_LOG(LogTemp, Error, TEXT("║ ❌ NewController 스폰 실패!                                ║"));
		UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
		return;
	}

	// 네트워크 연결 이전 (Listen Server가 아닌 경우)
	if (ClientConnection)
	{
		// 기존 Controller에서 연결 해제
		LoginController->SetPlayer(nullptr);
		
		// 새 Controller에 연결
		NewController->SetPlayer(ClientConnection);
		NewController->NetPlayerIndex = LoginController->NetPlayerIndex;
		
		UE_LOG(LogTemp, Warning, TEXT("║ NetConnection 이전 완료"));
	}
	else
	{
		// Listen Server의 경우
		NewController->SetPlayer(GEngine->GetFirstGamePlayer(GetWorld()));
		UE_LOG(LogTemp, Warning, TEXT("║ ListenServer: LocalPlayer 설정"));
	}

	// PlayerState 이전
	if (OldPS)
	{
		NewController->PlayerState = OldPS;
		OldPS->SetOwner(NewController);
		UE_LOG(LogTemp, Warning, TEXT("║ PlayerState 이전: %s"), *OldPS->GetName());
	}

	// 기존 Controller 삭제
	UE_LOG(LogTemp, Warning, TEXT("║ OldController 삭제 예정..."));
	LoginController->Destroy();

	// 기존 Pawn 삭제
	if (OldPawn)
	{
		OldPawn->Destroy();
		UE_LOG(LogTemp, Warning, TEXT("║ OldPawn 삭제됨"));
	}

	UE_LOG(LogTemp, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ → HeroCharacter 소환 시작!                                 ║"));
	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));

	// HeroCharacter 소환
	SpawnHeroCharacter(NewController);

	UE_LOG(LogTemp, Warning, TEXT(""));
}

void AHellunaDefenseGameMode::SpawnHeroCharacter(APlayerController* NewController)
{
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("┌────────────────────────────────────────────────────────────┐"));
	UE_LOG(LogTemp, Warning, TEXT("│ [DefenseGameMode] SpawnHeroCharacter                       │"));
	UE_LOG(LogTemp, Warning, TEXT("├────────────────────────────────────────────────────────────┤"));
	UE_LOG(LogTemp, Warning, TEXT("│ Controller: %s (ID: %d)"), *GetNameSafe(NewController), NewController ? NewController->GetUniqueID() : -1);

	if (!NewController)
	{
		UE_LOG(LogTemp, Error, TEXT("│ ❌ Controller nullptr!                                     │"));
		UE_LOG(LogTemp, Warning, TEXT("└────────────────────────────────────────────────────────────┘"));
		return;
	}

	if (!HeroCharacterClass)
	{
		UE_LOG(LogTemp, Error, TEXT("│ ❌ HeroCharacterClass 미설정!                              │"));
		UE_LOG(LogTemp, Warning, TEXT("└────────────────────────────────────────────────────────────┘"));
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 10.f, FColor::Red,
				TEXT("HeroCharacterClass 미설정! GameMode BP에서 설정 필요"));
		}
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("│ HeroCharacterClass: %s"), *HeroCharacterClass->GetName());

	// 스폰 위치
	FVector SpawnLocation = FVector::ZeroVector;
	FRotator SpawnRotation = FRotator::ZeroRotator;

	AActor* PlayerStart = FindPlayerStart(NewController);
	if (PlayerStart)
	{
		SpawnLocation = PlayerStart->GetActorLocation();
		SpawnRotation = PlayerStart->GetActorRotation();
		UE_LOG(LogTemp, Warning, TEXT("│ SpawnLocation: PlayerStart (%s)"), *SpawnLocation.ToString());
	}
	else
	{
		SpawnLocation = FVector(0.f, 0.f, 200.f);
		UE_LOG(LogTemp, Warning, TEXT("│ SpawnLocation: Default (%s)"), *SpawnLocation.ToString());
	}

	// HeroCharacter 스폰
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	SpawnParams.Owner = NewController;

	APawn* NewPawn = GetWorld()->SpawnActor<APawn>(HeroCharacterClass, SpawnLocation, SpawnRotation, SpawnParams);

	if (!NewPawn)
	{
		UE_LOG(LogTemp, Error, TEXT("│ ❌ HeroCharacter 스폰 실패!                                │"));
		UE_LOG(LogTemp, Warning, TEXT("└────────────────────────────────────────────────────────────┘"));
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("│ NewPawn: %s"), *NewPawn->GetName());

	// Possess
	NewController->Possess(NewPawn);
	
	UE_LOG(LogTemp, Warning, TEXT("│ ✅ Possess 완료!                                           │"));
	UE_LOG(LogTemp, Warning, TEXT("│ ✅ HeroCharacter 소환 성공!                                │"));
	UE_LOG(LogTemp, Warning, TEXT("└────────────────────────────────────────────────────────────┘"));
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
// 기존 게임 로직
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
	if (!HasAuthority()) return;

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
	if (!HasAuthority() || !BossClass || BossSpawnPoints.IsEmpty())
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
	if (!HasAuthority()) return;

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
	if (!HasAuthority() || !Monster) return;

	AHellunaDefenseGameState* GS = GetGameState<AHellunaDefenseGameState>();
	if (!GS || GS->GetPhase() != EDefensePhase::Night) return;

	if (AliveMonsters.Contains(Monster)) return;

	AliveMonsters.Add(Monster);
	GS->SetAliveMonsterCount(AliveMonsters.Num());
}

void AHellunaDefenseGameMode::NotifyMonsterDied(AActor* DeadMonster)
{
	if (!HasAuthority() || !DeadMonster) return;

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
