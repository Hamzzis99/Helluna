// ════════════════════════════════════════════════════════════════════════════════
// HellunaBaseGameMode.cpp
// ════════════════════════════════════════════════════════════════════════════════
//
// 로그인/인벤토리 시스템을 담당하는 Base GameMode
//
// 🔐 로그인 시스템:
//    - PostLogin, ProcessLogin, OnLoginSuccess, OnLoginFailed
//    - SwapToGameController, SpawnHeroCharacter
//
// 🎭 캐릭터 선택:
//    - ProcessCharacterSelection, RegisterCharacterUse, UnregisterCharacterUse
//
// 📦 인벤토리:
//    - SaveAllPlayersInventory, LoadAndSendInventoryToClient
//    - 자동저장 타이머
//
// 📌 작성자: Gihyeon
// ════════════════════════════════════════════════════════════════════════════════

#include "GameMode/HellunaBaseGameMode.h"
#include "GameMode/HellunaDefenseGameState.h"
#include "Login/HellunaLoginController.h"
#include "Login/HellunaAccountSaveGame.h"
#include "Player/HellunaPlayerState.h"
#include "Inventory/HellunaInventorySaveGame.h"
#include "Inventory/HellunaItemTypeMapping.h"
#include "MDF_Function/MDF_Instance/MDF_GameInstance.h"
#include "Player/Inv_PlayerController.h"
#include "InventoryManagement/Components/Inv_InventoryComponent.h"
#include "EquipmentManagement/Components/Inv_EquipmentComponent.h"
#include "EquipmentManagement/EquipActor/Inv_EquipActor.h"
#include "Items/Components/Inv_ItemComponent.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/SpectatorPawn.h"
#include "Engine/DataTable.h"
#include "GameplayTagContainer.h"
#include "debughelper.h"

// ════════════════════════════════════════════════════════════════════════════════
// 📌 팀원 가이드 - 이 파일 구조
// ════════════════════════════════════════════════════════════════════════════════
//
// 🔐 로그인 시스템 (건드리지 마세요!)
//    - PostLogin() : 플레이어 접속 시 호출
//    - ProcessLogin() : 아이디/비밀번호 검증
//    - OnLoginSuccess() : 로그인 성공 → 캐릭터 선택 UI 표시
//    - OnLoginFailed() : 로그인 실패 처리
//    - OnLoginTimeout() : 로그인 타임아웃 (60초) → 킥
//    - SwapToGameController() : LoginController → GameController 교체
//    - SpawnHeroCharacter() : 캐릭터 소환
//
// 🎭 캐릭터 선택 시스템
//    - ProcessCharacterSelection() : 캐릭터 선택 처리
//    - RegisterCharacterUse() : 캐릭터 사용 등록 (중복 방지)
//    - UnregisterCharacterUse() : 캐릭터 사용 해제 (로그아웃 시)
//    - GetAvailableCharacters() : 선택 가능한 캐릭터 목록
//
// 📦 인벤토리 시스템
//    - SaveAllPlayersInventory() : 모든 플레이어 인벤토리 저장
//    - LoadAndSendInventoryToClient() : 인벤토리 로드 후 클라이언트 전송
//    - SaveInventoryFromCharacterEndPlay() : 캐릭터 EndPlay 시 저장
//    - StartAutoSaveTimer() / OnAutoSaveTimer() : 자동저장 (5분 주기)
//
// ════════════════════════════════════════════════════════════════════════════════
// 📌 플레이어 접속 흐름
// ════════════════════════════════════════════════════════════════════════════════
//
//   플레이어 접속 → PostLogin()
//                      ↓
//                로그인 UI 표시 (LoginController)
//                      ↓
//   로그인 버튼 → ProcessLogin() → 계정 검증
//                      ↓
//              OnLoginSuccess() → 캐릭터 선택 UI 표시
//                      ↓
//   캐릭터 선택 → ProcessCharacterSelection()
//                      ↓
//              SwapToGameController() → Controller 교체
//                      ↓
//              SpawnHeroCharacter() → 캐릭터 소환
//                      ↓
//              InitializeGame() ⭐ (DefenseGameMode에서 override)
//                      ↓
//                  게임 시작!
//
// ════════════════════════════════════════════════════════════════════════════════

AHellunaBaseGameMode::AHellunaBaseGameMode()
{
	PrimaryActorTick.bCanEverTick = false;
	bUseSeamlessTravel = true;
	PlayerStateClass = AHellunaPlayerState::StaticClass();
	PlayerControllerClass = AHellunaLoginController::StaticClass();  // ⭐ 기존처럼 C++에서 직접 설정!
	DefaultPawnClass = ASpectatorPawn::StaticClass();
}

void AHellunaBaseGameMode::BeginPlay()
{
	Super::BeginPlay();

	if (!HasAuthority())
		return;

	AccountSaveGame = UHellunaAccountSaveGame::LoadOrCreate();
	InventorySaveGame = UHellunaInventorySaveGame::LoadOrCreate();

	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║     [BaseGameMode] BeginPlay                               ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ PlayerControllerClass: %s"), PlayerControllerClass ? *PlayerControllerClass->GetName() : TEXT("nullptr"));
	UE_LOG(LogTemp, Warning, TEXT("║ PlayerStateClass: %s"), PlayerStateClass ? *PlayerStateClass->GetName() : TEXT("nullptr"));
	UE_LOG(LogTemp, Warning, TEXT("║ DefaultPawnClass: %s"), DefaultPawnClass ? *DefaultPawnClass->GetName() : TEXT("nullptr"));
	UE_LOG(LogTemp, Warning, TEXT("║ HeroCharacterClass: %s"), HeroCharacterClass ? *HeroCharacterClass->GetName() : TEXT("미설정!"));
	UE_LOG(LogTemp, Warning, TEXT("║ AccountCount: %d"), AccountSaveGame ? AccountSaveGame->GetAccountCount() : 0);
	UE_LOG(LogTemp, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ HeroCharacterMap: %d개 매핑됨"), HeroCharacterMap.Num());
	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
	UE_LOG(LogTemp, Warning, TEXT(""));

#if WITH_EDITOR
	if (IsValid(ItemTypeMappingDataTable))
	{
		DebugTestItemTypeMapping();
	}
#endif

	StartAutoSaveTimer();
}

void AHellunaBaseGameMode::InitializeGame()
{
	UE_LOG(LogTemp, Warning, TEXT("[BaseGameMode] InitializeGame - 기본 구현 (override 필요)"));
}

// ════════════════════════════════════════════════════════════════════════════════
// 🔐 PostLogin - 플레이어 접속 시 호출
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 호출 시점: 플레이어가 서버에 접속했을 때 (엔진 자동 호출)
//
// 📌 처리 흐름:
//    1. PlayerState 확인
//    2. 이미 로그인됨? (SeamlessTravel)
//       → YES: SwapToGameController 또는 SpawnHeroCharacter
//       → NO: 로그인 타임아웃 타이머 시작 (60초)
//    3. LoginController.BeginPlay()에서 로그인 UI 자동 표시
//
// ════════════════════════════════════════════════════════════════════════════════
void AHellunaBaseGameMode::PostLogin(APlayerController* NewPlayer)
{
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║     [BaseGameMode] PostLogin                               ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ Controller: %s"), *GetNameSafe(NewPlayer));
	UE_LOG(LogTemp, Warning, TEXT("║ ControllerClass: %s"), NewPlayer ? *NewPlayer->GetClass()->GetName() : TEXT("nullptr"));
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

	if (PS && PS->IsLoggedIn() && !PS->GetPlayerUniqueId().IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("[BaseGameMode] 이미 로그인됨! → Controller 확인 후 처리"));
		FString PlayerId = PS->GetPlayerUniqueId();

		FTimerHandle TimerHandle;
		GetWorldTimerManager().SetTimer(TimerHandle, [this, NewPlayer, PlayerId]()
		{
			if (IsValid(NewPlayer))
			{
				AHellunaLoginController* LoginController = Cast<AHellunaLoginController>(NewPlayer);
				if (LoginController && LoginController->GetGameControllerClass())
				{
					int32 CharIdx = -1;
					if (AHellunaPlayerState* TempPS = NewPlayer->GetPlayerState<AHellunaPlayerState>())
					{
						CharIdx = TempPS->GetSelectedCharacterIndex();
					}
					SwapToGameController(LoginController, PlayerId, IndexToHeroType(CharIdx));
				}
				else
				{
					SpawnHeroCharacter(NewPlayer);
				}
			}
		}, 0.5f, false);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[BaseGameMode] 로그인 필요! 타임아웃: %.0f초"), LoginTimeoutSeconds);
		FTimerHandle& TimeoutTimer = LoginTimeoutTimers.FindOrAdd(NewPlayer);
		GetWorldTimerManager().SetTimer(TimeoutTimer, [this, NewPlayer]()
		{
			if (IsValid(NewPlayer))
			{
				OnLoginTimeout(NewPlayer);
			}
		}, LoginTimeoutSeconds, false);
	}

	Debug::Print(TEXT("[BaseGameMode] Login"), FColor::Yellow);
	Super::PostLogin(NewPlayer);
}

// ════════════════════════════════════════════════════════════════════════════════
// 🔐 ProcessLogin - 로그인 처리
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 호출 시점: LoginController.Server_RequestLogin() RPC에서 호출
//
// 📌 처리 흐름:
//    1. 동시 접속 체크 (IsPlayerLoggedIn)
//    2. 계정 존재 확인 (AccountSaveGame.HasAccount)
//       → 있으면 비밀번호 검증
//       → 없으면 새 계정 생성
//    3. OnLoginSuccess() 또는 OnLoginFailed() 호출
//
// 📌 계정 저장 위치: Saved/SaveGames/HellunaAccounts.sav
//
// ════════════════════════════════════════════════════════════════════════════════
void AHellunaBaseGameMode::ProcessLogin(APlayerController* PlayerController, const FString& PlayerId, const FString& Password)
{
	Debug::Print(TEXT("[BaseGameMode] ProcessLogin"), FColor::Yellow);

	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║     [BaseGameMode] ProcessLogin                            ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ PlayerId: '%s'"), *PlayerId);
	UE_LOG(LogTemp, Warning, TEXT("║ Controller: %s"), *GetNameSafe(PlayerController));
	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));

	if (!HasAuthority())
	{
		UE_LOG(LogTemp, Error, TEXT("[BaseGameMode] 서버 권한 없음!"));
		return;
	}

	if (!PlayerController)
	{
		UE_LOG(LogTemp, Error, TEXT("[BaseGameMode] PlayerController nullptr!"));
		return;
	}

	if (IsPlayerLoggedIn(PlayerId))
	{
		UE_LOG(LogTemp, Warning, TEXT("[BaseGameMode] 동시 접속 거부: '%s'"), *PlayerId);
		OnLoginFailed(PlayerController, TEXT("이미 접속 중인 계정입니다."));
		return;
	}

	if (!AccountSaveGame)
	{
		UE_LOG(LogTemp, Error, TEXT("[BaseGameMode] AccountSaveGame nullptr!"));
		OnLoginFailed(PlayerController, TEXT("서버 오류"));
		return;
	}

	if (AccountSaveGame->HasAccount(PlayerId))
	{
		if (AccountSaveGame->ValidatePassword(PlayerId, Password))
		{
			UE_LOG(LogTemp, Warning, TEXT("[BaseGameMode] 비밀번호 일치!"));
			OnLoginSuccess(PlayerController, PlayerId);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[BaseGameMode] 비밀번호 불일치!"));
			OnLoginFailed(PlayerController, TEXT("비밀번호를 확인해주세요."));
		}
	}
	else
	{
		if (AccountSaveGame->CreateAccount(PlayerId, Password))
		{
			UHellunaAccountSaveGame::Save(AccountSaveGame);
			UE_LOG(LogTemp, Warning, TEXT("[BaseGameMode] 새 계정 생성: '%s'"), *PlayerId);
			OnLoginSuccess(PlayerController, PlayerId);
		}
		else
		{
			OnLoginFailed(PlayerController, TEXT("계정 생성 실패"));
		}
	}
}

// ════════════════════════════════════════════════════════════════════════════════
// 🔐 OnLoginSuccess - 로그인 성공 처리
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 처리 흐름:
//    1. 로그인 타임아웃 타이머 취소
//    2. GameInstance.RegisterLogin() - 접속자 목록에 추가
//    3. PlayerState.SetLoginInfo() - PlayerId 저장
//    4. Client_LoginResult(true) - 클라이언트에 성공 알림
//    5. Client_ShowCharacterSelectUI() - 캐릭터 선택 UI 표시
//
// ════════════════════════════════════════════════════════════════════════════════
void AHellunaBaseGameMode::OnLoginSuccess(APlayerController* PlayerController, const FString& PlayerId)
{
	Debug::Print(TEXT("[BaseGameMode] Login Success"), FColor::Yellow);

	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║     [BaseGameMode] OnLoginSuccess                          ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ PlayerId: '%s'"), *PlayerId);
	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));

	if (!PlayerController) return;

	if (FTimerHandle* Timer = LoginTimeoutTimers.Find(PlayerController))
	{
		GetWorldTimerManager().ClearTimer(*Timer);
		LoginTimeoutTimers.Remove(PlayerController);
	}

	if (UMDF_GameInstance* GI = Cast<UMDF_GameInstance>(UGameplayStatics::GetGameInstance(GetWorld())))
	{
		GI->RegisterLogin(PlayerId);
	}

	if (AHellunaPlayerState* PS = PlayerController->GetPlayerState<AHellunaPlayerState>())
	{
		PS->SetLoginInfo(PlayerId);
	}

	AHellunaLoginController* LoginController = Cast<AHellunaLoginController>(PlayerController);
	if (LoginController)
	{
		LoginController->Client_LoginResult(true, TEXT(""));
		TArray<bool> AvailableCharacters = GetAvailableCharacters();
		LoginController->Client_ShowCharacterSelectUI(AvailableCharacters);
	}
}

void AHellunaBaseGameMode::OnLoginFailed(APlayerController* PlayerController, const FString& ErrorMessage)
{
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║     [BaseGameMode] OnLoginFailed                           ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ ErrorMessage: '%s'"), *ErrorMessage);
	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));

	AHellunaLoginController* LoginController = Cast<AHellunaLoginController>(PlayerController);
	if (LoginController)
	{
		LoginController->Client_LoginResult(false, ErrorMessage);
	}
}

void AHellunaBaseGameMode::OnLoginTimeout(APlayerController* PlayerController)
{
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║     [BaseGameMode] OnLoginTimeout                          ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ Controller: %s"), *GetNameSafe(PlayerController));
	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));

	if (!PlayerController) return;
	LoginTimeoutTimers.Remove(PlayerController);

	if (PlayerController->GetNetConnection())
	{
		FString KickReason = FString::Printf(TEXT("로그인 타임아웃 (%.0f초)"), LoginTimeoutSeconds);
		PlayerController->ClientReturnToMainMenuWithTextReason(FText::FromString(KickReason));
	}
}

// ════════════════════════════════════════════════════════════════════════════════
// 🔐 SwapToGameController - Controller 교체
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 역할: LoginController → GameController 교체
//
// 📌 왜 Controller를 교체하나?
//    - LoginController: 로그인 UI만 담당
//    - GameController: 실제 게임 플레이 담당 (BP_InvPlayerController)
//
// 📌 처리 흐름:
//    1. 새 GameController 스폰
//    2. Client_PrepareControllerSwap() - 로그인 UI 숨김
//    3. SwapPlayerControllers() - 안전한 교체
//    4. SpawnHeroCharacter() 호출
//
// ════════════════════════════════════════════════════════════════════════════════
void AHellunaBaseGameMode::SwapToGameController(AHellunaLoginController* LoginController, const FString& PlayerId, EHellunaHeroType SelectedHeroType)
{
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║     [BaseGameMode] SwapToGameController                    ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ PlayerId: '%s'"), *PlayerId);
	UE_LOG(LogTemp, Warning, TEXT("║ LoginController: %s"), *GetNameSafe(LoginController));

	if (!LoginController)
	{
		UE_LOG(LogTemp, Error, TEXT("║ LoginController nullptr!"));
		UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
		return;
	}

	TSubclassOf<APlayerController> GameControllerClass = LoginController->GetGameControllerClass();
	if (!GameControllerClass)
	{
		UE_LOG(LogTemp, Error, TEXT("║ GameControllerClass 미설정!"));
		UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
		SpawnHeroCharacter(LoginController);
		return;
	}

	if (AHellunaPlayerState* OldPS = LoginController->GetPlayerState<AHellunaPlayerState>())
	{
		OldPS->ClearLoginInfo();
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.Owner = this;

	FVector SpawnLocation = LoginController->GetFocalLocation();
	FRotator SpawnRotation = LoginController->GetControlRotation();

	APlayerController* NewController = GetWorld()->SpawnActor<APlayerController>(
		GameControllerClass, SpawnLocation, SpawnRotation, SpawnParams);

	if (!NewController)
	{
		UE_LOG(LogTemp, Error, TEXT("║ 새 Controller 스폰 실패!"));
		UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
		SpawnHeroCharacter(LoginController);
		return;
	}

	LoginController->Client_PrepareControllerSwap();
	SwapPlayerControllers(LoginController, NewController);

	if (AHellunaPlayerState* NewPS = NewController->GetPlayerState<AHellunaPlayerState>())
	{
		NewPS->SetLoginInfo(PlayerId);
		NewPS->SetSelectedHeroType(SelectedHeroType);

		AInv_PlayerController* InvPC = Cast<AInv_PlayerController>(NewController);
		if (IsValid(InvPC))
		{
			InvPC->OnControllerEndPlay.AddDynamic(this, &AHellunaBaseGameMode::OnInvControllerEndPlay);
			ControllerToPlayerIdMap.Add(InvPC, PlayerId);
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("║ Controller 교체 완료!"));
	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));

	FTimerHandle TimerHandle;
	GetWorldTimerManager().SetTimer(TimerHandle, [this, NewController]()
	{
		if (IsValid(NewController))
		{
			SpawnHeroCharacter(NewController);
		}
	}, 0.3f, false);
}

// ════════════════════════════════════════════════════════════════════════════════
// 🔐 SpawnHeroCharacter - 캐릭터 소환
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 처리 흐름:
//    1. PlayerState에서 선택한 캐릭터 타입 가져오기
//    2. HeroCharacterMap에서 해당 클래스 찾기
//    3. 캐릭터 스폰 및 Possess
//    4. 인벤토리 로드 (LoadAndSendInventoryToClient)
//    5. 첫 플레이어면 InitializeGame() 호출 → 게임 시작!
//
// ════════════════════════════════════════════════════════════════════════════════
void AHellunaBaseGameMode::SpawnHeroCharacter(APlayerController* PlayerController)
{
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║     [BaseGameMode] SpawnHeroCharacter                      ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ Controller: %s"), *GetNameSafe(PlayerController));

	if (!PlayerController)
	{
		UE_LOG(LogTemp, Error, TEXT("║ Controller nullptr!"));
		UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
		return;
	}

	TSubclassOf<APawn> SpawnClass = nullptr;
	int32 CharacterIndex = -1;

	if (AHellunaPlayerState* PS = PlayerController->GetPlayerState<AHellunaPlayerState>())
	{
		CharacterIndex = PS->GetSelectedCharacterIndex();
	}

	if (CharacterIndex >= 0 && HeroCharacterMap.Contains(IndexToHeroType(CharacterIndex)))
	{
		SpawnClass = HeroCharacterMap[IndexToHeroType(CharacterIndex)];
	}
	else if (HeroCharacterClass)
	{
		SpawnClass = HeroCharacterClass;
	}

	if (!SpawnClass)
	{
		UE_LOG(LogTemp, Error, TEXT("║ SpawnClass nullptr!"));
		UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
		return;
	}

	APawn* OldPawn = PlayerController->GetPawn();
	if (OldPawn)
	{
		PlayerController->UnPossess();
		OldPawn->Destroy();
	}

	FVector SpawnLocation = FVector(0.f, 0.f, 200.f);
	FRotator SpawnRotation = FRotator::ZeroRotator;

	AActor* PlayerStart = FindPlayerStart(PlayerController);
	if (PlayerStart)
	{
		SpawnLocation = PlayerStart->GetActorLocation();
		SpawnRotation = PlayerStart->GetActorRotation();
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	SpawnParams.Owner = PlayerController;

	APawn* NewPawn = GetWorld()->SpawnActor<APawn>(SpawnClass, SpawnLocation, SpawnRotation, SpawnParams);

	if (!NewPawn)
	{
		UE_LOG(LogTemp, Error, TEXT("║ HeroCharacter 스폰 실패!"));
		UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
		return;
	}

	PlayerController->Possess(NewPawn);

	AHellunaLoginController* LoginController = Cast<AHellunaLoginController>(PlayerController);
	if (LoginController)
	{
		LoginController->Client_PrepareControllerSwap();
	}

	UE_LOG(LogTemp, Warning, TEXT("║ Possess 완료!"));
	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));

	// ════════════════════════════════════════════════════════════════════════════════
	// 📌 첫 플레이어 캐릭터 소환 → 게임 초기화!
	// ════════════════════════════════════════════════════════════════════════════════
	// ⚠️ 주의: bGameInitialized는 InitializeGame() 내부에서 설정됨!
	//          여기서 미리 설정하면 자식 클래스의 InitializeGame()이 스킵됨!
	// ════════════════════════════════════════════════════════════════════════════════
	if (!bGameInitialized)
	{
		InitializeGame();  // InitializeGame() 내부에서 bGameInitialized = true 설정
	}

	FTimerHandle InventoryLoadTimer;
	GetWorldTimerManager().SetTimer(InventoryLoadTimer, [this, PlayerController]()
	{
		if (IsValid(PlayerController))
		{
			LoadAndSendInventoryToClient(PlayerController);
		}
	}, 1.0f, false);
}

bool AHellunaBaseGameMode::IsPlayerLoggedIn(const FString& PlayerId) const
{
	if (UMDF_GameInstance* GI = Cast<UMDF_GameInstance>(UGameplayStatics::GetGameInstance(GetWorld())))
	{
		return GI->IsPlayerLoggedIn(PlayerId);
	}
	return false;
}

// ════════════════════════════════════════════════════════════════════════════════
// 🔐 Logout - 플레이어 로그아웃
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 호출 시점: 플레이어 연결 끊김 시 (엔진 자동 호출)
//
// 📌 처리 흐름:
//    1. UnregisterCharacterUse() - 캐릭터 사용 해제
//    2. GameInstance.UnregisterLogin() - 접속자 목록에서 제거
//    3. 로그인 타임아웃 타이머 정리
//
// ════════════════════════════════════════════════════════════════════════════════
void AHellunaBaseGameMode::Logout(AController* Exiting)
{
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║     [BaseGameMode] Logout                                  ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ Controller: %s"), *GetNameSafe(Exiting));
	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));

	if (!Exiting)
	{
		Super::Logout(Exiting);
		return;
	}

	if (APlayerController* PC = Cast<APlayerController>(Exiting))
	{
		if (FTimerHandle* Timer = LoginTimeoutTimers.Find(PC))
		{
			GetWorldTimerManager().ClearTimer(*Timer);
			LoginTimeoutTimers.Remove(PC);
		}
	}

	AHellunaPlayerState* PS = Exiting->GetPlayerState<AHellunaPlayerState>();
	FString PlayerId;
	if (PS)
	{
		PlayerId = PS->GetPlayerUniqueId();
	}

	if (!PlayerId.IsEmpty())
	{
		APawn* Pawn = Exiting->GetPawn();
		UInv_InventoryComponent* InvComp = Pawn ? Pawn->FindComponentByClass<UInv_InventoryComponent>() : nullptr;

		if (InvComp)
		{
			TArray<FInv_SavedItemData> CollectedItems = InvComp->CollectInventoryDataForSave();
			FHellunaPlayerInventoryData SaveData;
			SaveData.LastSaveTime = FDateTime::Now();

			for (const FInv_SavedItemData& Item : CollectedItems)
			{
				FHellunaInventoryItemData DestItem;
				DestItem.ItemType = Item.ItemType;
				DestItem.StackCount = Item.StackCount;
				DestItem.GridPosition = Item.GridPosition;
				DestItem.GridCategory = Item.GridCategory;
				DestItem.EquipSlotIndex = Item.bEquipped ? Item.WeaponSlotIndex : -1;
				SaveData.Items.Add(DestItem);
			}

			if (IsValid(InventorySaveGame) && SaveData.Items.Num() > 0)
			{
				InventorySaveGame->SavePlayerInventory(PlayerId, SaveData);
				UHellunaInventorySaveGame::Save(InventorySaveGame);
			}
		}
		else
		{
			if (FHellunaPlayerInventoryData* CachedData = CachedPlayerInventoryData.Find(PlayerId))
			{
				CachedData->LastSaveTime = FDateTime::Now();
				if (IsValid(InventorySaveGame))
				{
					InventorySaveGame->SavePlayerInventory(PlayerId, *CachedData);
					UHellunaInventorySaveGame::Save(InventorySaveGame);
				}
			}
		}

		CachedPlayerInventoryData.Remove(PlayerId);

		if (UMDF_GameInstance* GI = Cast<UMDF_GameInstance>(UGameplayStatics::GetGameInstance(GetWorld())))
		{
			GI->RegisterLogout(PlayerId);
		}

		UnregisterCharacterUse(PlayerId);
	}

	Super::Logout(Exiting);
}

void AHellunaBaseGameMode::HandleSeamlessTravelPlayer(AController*& C)
{
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║     [BaseGameMode] HandleSeamlessTravelPlayer              ║"));
	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));

	FString SavedPlayerId;
	EHellunaHeroType SavedHeroType = EHellunaHeroType::None;
	bool bSavedIsLoggedIn = false;

	if (C)
	{
		if (AHellunaPlayerState* OldPS = C->GetPlayerState<AHellunaPlayerState>())
		{
			SavedPlayerId = OldPS->GetPlayerUniqueId();
			SavedHeroType = OldPS->GetSelectedHeroType();
			bSavedIsLoggedIn = OldPS->IsLoggedIn();
		}
	}

	Super::HandleSeamlessTravelPlayer(C);

	if (C && !SavedPlayerId.IsEmpty())
	{
		if (AHellunaPlayerState* NewPS = C->GetPlayerState<AHellunaPlayerState>())
		{
			NewPS->SetLoginInfo(SavedPlayerId);
			NewPS->SetSelectedHeroType(SavedHeroType);
		}

		if (bSavedIsLoggedIn)
		{
			APlayerController* TraveledPC = Cast<APlayerController>(C);
			if (TraveledPC)
			{
				FTimerHandle TimerHandle;
				GetWorldTimerManager().SetTimer(TimerHandle, [this, TraveledPC, SavedPlayerId, SavedHeroType]()
				{
					if (IsValid(TraveledPC))
					{
						AHellunaLoginController* LoginController = Cast<AHellunaLoginController>(TraveledPC);
						if (LoginController && LoginController->GetGameControllerClass())
						{
							SwapToGameController(LoginController, SavedPlayerId, SavedHeroType);
						}
						else
						{
							SpawnHeroCharacter(TraveledPC);
						}
					}
				}, 0.5f, false);
			}
		}
	}
}

// ════════════════════════════════════════════════════════════════════════════════
// 🎭 ProcessCharacterSelection - 캐릭터 선택 처리
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 호출 시점: LoginController.Server_SelectCharacter() RPC에서 호출
//
// 📌 처리 흐름:
//    1. 중복 선택 체크 (IsCharacterInUse)
//    2. PlayerState.SelectedHeroType 설정
//    3. RegisterCharacterUse() - UsedCharacterMap에 등록
//    4. SwapToGameController() 호출
//
// ════════════════════════════════════════════════════════════════════════════════
void AHellunaBaseGameMode::ProcessCharacterSelection(APlayerController* PlayerController, EHellunaHeroType HeroType)
{
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║  [BaseGameMode] ProcessCharacterSelection                  ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ HeroType: %s"), *UEnum::GetValueAsString(HeroType));
	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));

	if (!PlayerController) return;

	AHellunaLoginController* LoginController = Cast<AHellunaLoginController>(PlayerController);

	if (HeroType == EHellunaHeroType::None || !HeroCharacterMap.Contains(HeroType))
	{
		if (LoginController)
		{
			LoginController->Client_CharacterSelectionResult(false, TEXT("유효하지 않은 캐릭터입니다."));
		}
		return;
	}

	if (IsCharacterInUse(HeroType))
	{
		if (LoginController)
		{
			LoginController->Client_CharacterSelectionResult(false, TEXT("다른 플레이어가 사용 중인 캐릭터입니다."));
		}
		return;
	}

	FString PlayerId;
	AHellunaPlayerState* PS = PlayerController->GetPlayerState<AHellunaPlayerState>();
	if (PS)
	{
		PlayerId = PS->GetPlayerUniqueId();
		PS->SetSelectedHeroType(HeroType);
	}

	if (PlayerId.IsEmpty())
	{
		if (LoginController)
		{
			LoginController->Client_CharacterSelectionResult(false, TEXT("로그인 정보 오류"));
		}
		return;
	}

	RegisterCharacterUse(HeroType, PlayerId);

	if (LoginController)
	{
		LoginController->Client_CharacterSelectionResult(true, TEXT(""));
	}

	if (LoginController && LoginController->GetGameControllerClass())
	{
		FTimerHandle TimerHandle;
		GetWorldTimerManager().SetTimer(TimerHandle, [this, LoginController, PlayerId, HeroType]()
		{
			if (IsValid(LoginController))
			{
				SwapToGameController(LoginController, PlayerId, HeroType);
			}
		}, 0.3f, false);
	}
	else
	{
		SpawnHeroCharacter(PlayerController);
	}
}

// ════════════════════════════════════════════════════════════════════════════════
// 🎭 RegisterCharacterUse - 캐릭터 사용 등록
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 역할: 캐릭터 중복 선택 방지
//    - UsedCharacterMap: (캐릭터타입 → PlayerId)
//    - GameState에도 알림 → 모든 클라이언트 UI 실시간 갱신
//
// ════════════════════════════════════════════════════════════════════════════════
void AHellunaBaseGameMode::RegisterCharacterUse(EHellunaHeroType HeroType, const FString& PlayerId)
{
	if (!HasAuthority()) return;
	UnregisterCharacterUse(PlayerId);
	UsedCharacterMap.Add(HeroType, PlayerId);

	if (AHellunaDefenseGameState* GS = GetGameState<AHellunaDefenseGameState>())
	{
		GS->AddUsedCharacter(HeroType);
	}
}

void AHellunaBaseGameMode::UnregisterCharacterUse(const FString& PlayerId)
{
	if (!HasAuthority()) return;
	if (PlayerId.IsEmpty()) return;

	EHellunaHeroType FoundType = EHellunaHeroType::None;
	for (const auto& Pair : UsedCharacterMap)
	{
		if (Pair.Value == PlayerId)
		{
			FoundType = Pair.Key;
			break;
		}
	}

	if (FoundType != EHellunaHeroType::None)
	{
		UsedCharacterMap.Remove(FoundType);
		if (AHellunaDefenseGameState* GS = GetGameState<AHellunaDefenseGameState>())
		{
			GS->RemoveUsedCharacter(FoundType);
		}
	}
}

bool AHellunaBaseGameMode::IsCharacterInUse(EHellunaHeroType HeroType) const
{
	return UsedCharacterMap.Contains(HeroType);
}

TArray<bool> AHellunaBaseGameMode::GetAvailableCharacters() const
{
	TArray<bool> Result;
	for (int32 i = 0; i < 3; i++)
	{
		Result.Add(!IsCharacterInUse(IndexToHeroType(i)));
	}
	return Result;
}

TMap<EHellunaHeroType, bool> AHellunaBaseGameMode::GetAvailableCharactersMap() const
{
	TMap<EHellunaHeroType, bool> Result;
	Result.Add(EHellunaHeroType::Lui, !IsCharacterInUse(EHellunaHeroType::Lui));
	Result.Add(EHellunaHeroType::Luna, !IsCharacterInUse(EHellunaHeroType::Luna));
	Result.Add(EHellunaHeroType::Liam, !IsCharacterInUse(EHellunaHeroType::Liam));
	return Result;
}

TSubclassOf<APawn> AHellunaBaseGameMode::GetHeroCharacterClass(EHellunaHeroType HeroType) const
{
	if (HeroCharacterMap.Contains(HeroType))
	{
		return HeroCharacterMap[HeroType];
	}
	return HeroCharacterClass;
}

EHellunaHeroType AHellunaBaseGameMode::IndexToHeroType(int32 Index)
{
	if (Index < 0 || Index > 2) return EHellunaHeroType::None;
	return static_cast<EHellunaHeroType>(Index);
}

// ════════════════════════════════════════════════════════════════════════════════
// 📦 SaveAllPlayersInventory - 모든 플레이어 인벤토리 저장
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 호출 시점:
//    - 맵 이동 전 (ServerTravel)
//    - 자동저장 타이머 (5분마다)
//
// 📌 처리 흐름:
//    1. 모든 PlayerController 순회
//    2. InventoryComponent에서 아이템 데이터 수집
//    3. EquipmentComponent에서 장착 상태 추가
//    4. SaveInventoryFromCharacterEndPlay() 호출
//
// 📌 저장 위치: Saved/SaveGames/HellunaInventory.sav
//
// ════════════════════════════════════════════════════════════════════════════════
int32 AHellunaBaseGameMode::SaveAllPlayersInventory()
{
	UE_LOG(LogTemp, Warning, TEXT("[BaseGameMode] SaveAllPlayersInventory"));

	int32 SavedCount = 0;

	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (!IsValid(PC)) continue;

		AHellunaPlayerState* PS = PC->GetPlayerState<AHellunaPlayerState>();
		if (!PS || !PS->IsLoggedIn()) continue;

		FString PlayerId = PS->GetPlayerUniqueId();
		if (PlayerId.IsEmpty()) continue;

		UInv_InventoryComponent* InvComp = PC->FindComponentByClass<UInv_InventoryComponent>();
		if (!InvComp) continue;

		TArray<FInv_SavedItemData> CollectedItems = InvComp->CollectInventoryDataForSave();

		UInv_EquipmentComponent* EquipComp = PC->FindComponentByClass<UInv_EquipmentComponent>();
		if (EquipComp)
		{
			const TArray<TObjectPtr<AInv_EquipActor>>& EquippedActors = EquipComp->GetEquippedActors();
			for (const TObjectPtr<AInv_EquipActor>& EquipActor : EquippedActors)
			{
				if (EquipActor.Get())
				{
					FGameplayTag ItemType = EquipActor->GetEquipmentType();
					int32 SlotIndex = EquipActor->GetWeaponSlotIndex();
					for (FInv_SavedItemData& Item : CollectedItems)
					{
						if (Item.ItemType == ItemType && !Item.bEquipped)
						{
							Item.bEquipped = true;
							Item.WeaponSlotIndex = SlotIndex;
							break;
						}
					}
				}
			}
		}

		if (CollectedItems.Num() > 0)
		{
			SaveInventoryFromCharacterEndPlay(PlayerId, CollectedItems);
			SavedCount++;
		}
	}

	return SavedCount;
}

// ════════════════════════════════════════════════════════════════════════════════
// 📦 LoadAndSendInventoryToClient - 인벤토리 로드 후 클라이언트 전송
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 호출 시점: SpawnHeroCharacter()에서 캐릭터 소환 후
//
// 📌 처리 흐름:
//    1. InventorySaveGame에서 저장된 데이터 로드
//    2. 아이템 액터 스폰 (서버)
//    3. InventoryComponent에 아이템 추가
//    4. 장착 상태 복원
//
// ════════════════════════════════════════════════════════════════════════════════
void AHellunaBaseGameMode::LoadAndSendInventoryToClient(APlayerController* PC)
{
	UE_LOG(LogTemp, Warning, TEXT("[BaseGameMode] LoadAndSendInventoryToClient"));

	if (!HasAuthority() || !IsValid(PC)) return;

	AHellunaPlayerState* PS = PC->GetPlayerState<AHellunaPlayerState>();
	if (!IsValid(PS)) return;

	FString PlayerUniqueId = PS->GetPlayerUniqueId();
	if (PlayerUniqueId.IsEmpty()) return;

	if (!IsValid(InventorySaveGame)) return;

	FHellunaPlayerInventoryData LoadedData;
	bool bDataFound = InventorySaveGame->LoadPlayerInventory(PlayerUniqueId, LoadedData);

	if (!bDataFound || LoadedData.Items.Num() == 0) return;

	UInv_InventoryComponent* InvComp = PC->FindComponentByClass<UInv_InventoryComponent>();
	if (!IsValid(InvComp) || !IsValid(ItemTypeMappingDataTable)) return;

	for (const FHellunaInventoryItemData& ItemData : LoadedData.Items)
	{
		if (!ItemData.ItemType.IsValid()) continue;

		TSubclassOf<AActor> ActorClass = UHellunaItemTypeMapping::GetActorClassFromItemType(
			ItemTypeMappingDataTable, ItemData.ItemType);
		if (!ActorClass) continue;

		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		AActor* SpawnedActor = GetWorld()->SpawnActor<AActor>(ActorClass, FVector(0.f, 0.f, -10000.f), FRotator::ZeroRotator, SpawnParams);
		if (!IsValid(SpawnedActor)) continue;

		UInv_ItemComponent* ItemComp = SpawnedActor->FindComponentByClass<UInv_ItemComponent>();
		if (!IsValid(ItemComp))
		{
			SpawnedActor->Destroy();
			continue;
		}

		InvComp->Server_AddNewItem(ItemComp, ItemData.StackCount, 0);

		const int32 Columns = 8;
		int32 SavedGridIndex = ItemData.GridPosition.Y * Columns + ItemData.GridPosition.X;
		InvComp->SetLastEntryGridPosition(SavedGridIndex, ItemData.GridCategory);
	}

	TSet<UInv_InventoryItem*> ServerProcessedItems;
	for (const FHellunaInventoryItemData& ItemData : LoadedData.Items)
	{
		if (ItemData.EquipSlotIndex < 0) continue;

		UInv_InventoryItem* FoundItem = InvComp->FindItemByTypeExcluding(ItemData.ItemType, ServerProcessedItems);
		if (FoundItem)
		{
			InvComp->OnItemEquipped.Broadcast(FoundItem, ItemData.EquipSlotIndex);
			ServerProcessedItems.Add(FoundItem);
		}
	}

	TArray<FInv_SavedItemData> SavedItemsForClient;
	for (const FHellunaInventoryItemData& ItemData : LoadedData.Items)
	{
		FInv_SavedItemData ClientData;
		ClientData.ItemType = ItemData.ItemType;
		ClientData.StackCount = ItemData.StackCount;
		ClientData.GridPosition = ItemData.GridPosition;
		ClientData.GridCategory = ItemData.GridCategory;
		ClientData.bEquipped = (ItemData.EquipSlotIndex >= 0);
		ClientData.WeaponSlotIndex = ItemData.EquipSlotIndex;
		SavedItemsForClient.Add(ClientData);
	}

	AInv_PlayerController* InvPC = Cast<AInv_PlayerController>(PC);
	if (IsValid(InvPC))
	{
		InvPC->Client_ReceiveInventoryData(SavedItemsForClient);
	}
}

void AHellunaBaseGameMode::SaveInventoryFromCharacterEndPlay(const FString& PlayerId, const TArray<FInv_SavedItemData>& CollectedItems)
{
	if (PlayerId.IsEmpty() || CollectedItems.Num() == 0) return;

	FHellunaPlayerInventoryData SaveData;
	SaveData.LastSaveTime = FDateTime::Now();

	for (const FInv_SavedItemData& Item : CollectedItems)
	{
		FHellunaInventoryItemData DestItem;
		DestItem.ItemType = Item.ItemType;
		DestItem.StackCount = Item.StackCount;
		DestItem.GridPosition = Item.GridPosition;
		DestItem.GridCategory = Item.GridCategory;
		DestItem.EquipSlotIndex = Item.bEquipped ? Item.WeaponSlotIndex : -1;
		SaveData.Items.Add(DestItem);
	}

	if (IsValid(InventorySaveGame))
	{
		InventorySaveGame->SavePlayerInventory(PlayerId, SaveData);
		UHellunaInventorySaveGame::Save(InventorySaveGame);
	}

	CachedPlayerInventoryData.Add(PlayerId, SaveData);
}

void AHellunaBaseGameMode::OnPlayerInventoryStateReceived(
	AInv_PlayerController* PlayerController,
	const TArray<FInv_SavedItemData>& SavedItems)
{
	AHellunaPlayerState* PS = PlayerController->GetPlayerState<AHellunaPlayerState>();
	if (!IsValid(PS)) return;

	FString PlayerUniqueId = PS->GetPlayerUniqueId();
	if (PlayerUniqueId.IsEmpty()) return;

	FHellunaPlayerInventoryData PlayerData;
	PlayerData.LastSaveTime = FDateTime::Now();
	PlayerData.SaveVersion = 1;

	for (const FInv_SavedItemData& SourceItem : SavedItems)
	{
		FHellunaInventoryItemData DestItem;
		DestItem.ItemType = SourceItem.ItemType;
		DestItem.StackCount = SourceItem.StackCount;
		DestItem.GridPosition = SourceItem.GridPosition;
		DestItem.GridCategory = SourceItem.GridCategory;
		DestItem.EquipSlotIndex = SourceItem.bEquipped ? SourceItem.WeaponSlotIndex : -1;
		PlayerData.Items.Add(DestItem);
	}

	CachedPlayerInventoryData.Add(PlayerUniqueId, PlayerData);

	if (IsValid(InventorySaveGame))
	{
		InventorySaveGame->SavePlayerInventory(PlayerUniqueId, PlayerData);
		UHellunaInventorySaveGame::Save(InventorySaveGame);
	}
}

// ════════════════════════════════════════════════════════════════════════════════
// 📦 자동저장 시스템
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 동작 방식:
//    - StartAutoSaveTimer(): BeginPlay에서 타이머 시작
//    - OnAutoSaveTimer(): AutoSaveIntervalSeconds(기본 300초=5분)마다 호출
//    - RequestAllPlayersInventoryState(): 모든 플레이어에게 인벤토리 상태 요청
//    - OnPlayerInventoryStateReceived(): 응답 받으면 저장
//
// ════════════════════════════════════════════════════════════════════════════════
void AHellunaBaseGameMode::StartAutoSaveTimer()
{
	if (AutoSaveIntervalSeconds <= 0.0f) return;

	StopAutoSaveTimer();

	GetWorldTimerManager().SetTimer(
		AutoSaveTimerHandle,
		this,
		&AHellunaBaseGameMode::OnAutoSaveTimer,
		AutoSaveIntervalSeconds,
		true
	);

	UE_LOG(LogTemp, Warning, TEXT("[BaseGameMode] AutoSave Timer Started (%.0fs)"), AutoSaveIntervalSeconds);
}

void AHellunaBaseGameMode::StopAutoSaveTimer()
{
	if (AutoSaveTimerHandle.IsValid())
	{
		GetWorldTimerManager().ClearTimer(AutoSaveTimerHandle);
	}
}

void AHellunaBaseGameMode::OnAutoSaveTimer()
{
	RequestAllPlayersInventoryState();
}

void AHellunaBaseGameMode::RequestAllPlayersInventoryState()
{
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (!IsValid(PC)) continue;

		AInv_PlayerController* InvPC = Cast<AInv_PlayerController>(PC);
		if (!IsValid(InvPC)) continue;

		if (!InvPC->OnInventoryStateReceived.IsBound())
		{
			InvPC->OnInventoryStateReceived.AddDynamic(this, &AHellunaBaseGameMode::OnPlayerInventoryStateReceived);
		}

		RequestPlayerInventoryState(PC);
	}
}

void AHellunaBaseGameMode::RequestPlayerInventoryState(APlayerController* PC)
{
	if (!IsValid(PC)) return;

	AInv_PlayerController* InvPC = Cast<AInv_PlayerController>(PC);
	if (IsValid(InvPC))
	{
		InvPC->Client_RequestInventoryState();
	}
}

void AHellunaBaseGameMode::OnInvControllerEndPlay(AInv_PlayerController* PlayerController, const TArray<FInv_SavedItemData>& SavedItems)
{
	if (!IsValid(PlayerController)) return;

	FString PlayerId;
	if (FString* FoundPlayerId = ControllerToPlayerIdMap.Find(PlayerController))
	{
		PlayerId = *FoundPlayerId;
		ControllerToPlayerIdMap.Remove(PlayerController);
	}
	else
	{
		AHellunaPlayerState* PS = PlayerController->GetPlayerState<AHellunaPlayerState>();
		if (IsValid(PS))
		{
			PlayerId = PS->GetPlayerUniqueId();
		}
	}

	TArray<FInv_SavedItemData> MergedItems = SavedItems;

	int32 EquippedCount = 0;
	for (const FInv_SavedItemData& Item : MergedItems)
	{
		if (Item.bEquipped) EquippedCount++;
	}

	if (EquippedCount == 0 && !PlayerId.IsEmpty())
	{
		if (FHellunaPlayerInventoryData* CachedData = CachedPlayerInventoryData.Find(PlayerId))
		{
			for (const FHellunaInventoryItemData& CachedItem : CachedData->Items)
			{
				if (CachedItem.EquipSlotIndex >= 0)
				{
					for (FInv_SavedItemData& Item : MergedItems)
					{
						if (Item.ItemType == CachedItem.ItemType && !Item.bEquipped)
						{
							Item.bEquipped = true;
							Item.WeaponSlotIndex = CachedItem.EquipSlotIndex;
							break;
						}
					}
				}
			}
		}
	}

	if (!PlayerId.IsEmpty() && MergedItems.Num() > 0)
	{
		SaveInventoryFromCharacterEndPlay(PlayerId, MergedItems);
	}

	if (!PlayerId.IsEmpty())
	{
		AHellunaPlayerState* PS = PlayerController->GetPlayerState<AHellunaPlayerState>();
		if (IsValid(PS) && PS->IsLoggedIn())
		{
			PS->ClearLoginInfo();
		}

		if (UMDF_GameInstance* GI = Cast<UMDF_GameInstance>(UGameplayStatics::GetGameInstance(GetWorld())))
		{
			GI->RegisterLogout(PlayerId);
		}
	}
}

void AHellunaBaseGameMode::DebugTestItemTypeMapping()
{
	if (!IsValid(ItemTypeMappingDataTable))
	{
		UE_LOG(LogTemp, Error, TEXT("[BaseGameMode] ItemTypeMappingDataTable not set!"));
		return;
	}

	TArray<FString> TestTags = {
		TEXT("GameItems.Equipment.Weapons.Axe"),
		TEXT("GameItems.Consumables.Potions.Blue.Small"),
		TEXT("GameItems.Consumables.Potions.Red.Small"),
		TEXT("GameItems.Craftables.FireFernFruit"),
		TEXT("GameItems.Craftables.LuminDaisy"),
	};

	int32 SuccessCount = 0;
	for (const FString& TagString : TestTags)
	{
		FGameplayTag TestTag = FGameplayTag::RequestGameplayTag(FName(*TagString), false);
		if (TestTag.IsValid())
		{
			TSubclassOf<AActor> FoundClass = UHellunaItemTypeMapping::GetActorClassFromItemType(
				ItemTypeMappingDataTable, TestTag);
			if (FoundClass) SuccessCount++;
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("[BaseGameMode] ItemTypeMapping Test: %d/%d passed"), SuccessCount, TestTags.Num());
}

void AHellunaBaseGameMode::DebugPrintAllItemMappings()
{
	if (IsValid(ItemTypeMappingDataTable))
	{
		UHellunaItemTypeMapping::DebugPrintAllMappings(ItemTypeMappingDataTable);
	}
}

void AHellunaBaseGameMode::DebugTestInventorySaveGame()
{
	if (!IsValid(InventorySaveGame))
	{
		UE_LOG(LogTemp, Error, TEXT("[BaseGameMode] InventorySaveGame is nullptr!"));
		return;
	}

	const FString TestPlayerId = TEXT("TestPlayer_Debug");

	FHellunaPlayerInventoryData TestData;
	TestData.SaveVersion = 1;

	FHellunaInventoryItemData Item1;
	Item1.ItemType = FGameplayTag::RequestGameplayTag(FName("GameItems.Equipment.Weapons.Axe"), false);
	Item1.StackCount = 1;
	Item1.GridPosition = FIntPoint(0, 0);
	Item1.EquipSlotIndex = 0;
	TestData.Items.Add(Item1);

	InventorySaveGame->SavePlayerInventory(TestPlayerId, TestData);
	bool bSaveSuccess = UHellunaInventorySaveGame::Save(InventorySaveGame);

	FHellunaPlayerInventoryData LoadedData;
	bool bLoadSuccess = InventorySaveGame->LoadPlayerInventory(TestPlayerId, LoadedData);

	UE_LOG(LogTemp, Warning, TEXT("[BaseGameMode] SaveGame Test: Save=%s, Load=%s"),
		bSaveSuccess ? TEXT("OK") : TEXT("FAIL"),
		bLoadSuccess ? TEXT("OK") : TEXT("FAIL"));
}

void AHellunaBaseGameMode::DebugRequestSaveAllInventory()
{
	RequestAllPlayersInventoryState();
}

void AHellunaBaseGameMode::DebugForceAutoSave()
{
	OnAutoSaveTimer();
}

void AHellunaBaseGameMode::DebugTestLoadInventory()
{
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (IsValid(PC))
		{
			LoadAndSendInventoryToClient(PC);
			return;
		}
	}
}
