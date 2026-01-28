// Fill out your copyright notice in the Description page of Project Settings.
// 
// ============================================
// 📌 HellunaDefenseGameMode
// 📌 작성자: Gihyeon
// 📌 작성일: 2025-01-23
// 📌 수정일: 2025-01-28 (Phase B - 로그인 로직 이전)
// ============================================

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
#include "GameFramework/SpectatorPawn.h"

// ============================================
// 📌 [Phase B] Inv_PlayerController include
// Client RPC 호출을 위해 필요
// ============================================
#include "Player/Inv_PlayerController.h"

#include "debughelper.h"

AHellunaDefenseGameMode::AHellunaDefenseGameMode()
{
	PrimaryActorTick.bCanEverTick = false;
	bUseSeamlessTravel = true;

	// ============================================
	// 📌 [Phase B] PlayerState 클래스 설정
	// ============================================
	PlayerStateClass = AHellunaPlayerState::StaticClass();

	// ============================================
	// 📌 [Phase B] DefaultPawn 설정
	// 로그인 전에는 SpectatorPawn (관전자) 상태
	// 로그인 성공 후 HeroCharacter로 교체됨
	// ============================================
	DefaultPawnClass = ASpectatorPawn::StaticClass();
}

void AHellunaDefenseGameMode::BeginPlay()
{
	Super::BeginPlay();

	if (!HasAuthority())
		return;

	// ============================================
	// 📌 [Phase B] 계정 데이터 로드
	// ============================================
	AccountSaveGame = UHellunaAccountSaveGame::LoadOrCreate();

	if (AccountSaveGame)
	{
		UE_LOG(LogTemp, Log, TEXT("[DefenseGameMode] BeginPlay: 계정 데이터 로드 완료 (계정 %d개)"), AccountSaveGame->GetAccountCount());
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[DefenseGameMode] BeginPlay: 계정 데이터 로드 실패!"));
	}

	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("========================================"));
	UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] Phase B: 로그인 + 게임 통합 모드"));
	UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] PlayerStateClass: %s"), PlayerStateClass ? *PlayerStateClass->GetName() : TEXT("nullptr"));
	UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] DefaultPawnClass: %s"), DefaultPawnClass ? *DefaultPawnClass->GetName() : TEXT("nullptr"));
	UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] HeroCharacterClass: %s"), HeroCharacterClass ? *HeroCharacterClass->GetName() : TEXT("미설정! BP에서 설정 필요"));
	UE_LOG(LogTemp, Warning, TEXT("========================================"));
	UE_LOG(LogTemp, Warning, TEXT(""));

	CacheBossSpawnPoints();
	CacheMonsterSpawnPoints();
	EnterDay();
}

// ============================================
// 📌 [Phase B] PostLogin - 플레이어 접속 처리
// ============================================
void AHellunaDefenseGameMode::PostLogin(APlayerController* NewPlayer)
{
	/**
	 * ============================================
	 * 📌 [Phase B] 플레이어 접속 흐름
	 * 
	 * 1. PostLogin 호출됨
	 * 2. DefaultPawn (SpectatorPawn) 상태로 시작
	 * 3. 이미 로그인 된 상태인지 확인 (SeamlessTravel에서 온 경우)
	 *    - 로그인 됨 → 바로 HeroCharacter 소환
	 *    - 로그인 안 됨 → 로그인 UI 표시 요청, 타임아웃 타이머 시작
	 * 4. 클라이언트에서 로그인 버튼 클릭
	 * 5. Server RPC로 ProcessLogin() 호출
	 * 6. 로그인 성공 → OnLoginSuccess() → SpawnHeroCharacter()
	 * 
	 * [TODO: 캐릭터 선택창 구현 시]
	 * 6. 로그인 성공 → 캐릭터 선택 UI 표시
	 * 7. 캐릭터 선택 완료 → SpawnHeroCharacter(SelectedClass)
	 * ============================================
	 */

	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("========================================"));
	UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] ★★ PostLogin 호출됨!"));
	UE_LOG(LogTemp, Warning, TEXT("========================================"));
	
	if (!NewPlayer)
	{
		UE_LOG(LogTemp, Error, TEXT("[DefenseGameMode] PostLogin: NewPlayer가 nullptr!"));
		Super::PostLogin(NewPlayer);
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] PostLogin - Controller: %s"), *GetNameSafe(NewPlayer));

	// PlayerState 확인
	AHellunaPlayerState* PS = NewPlayer->GetPlayerState<AHellunaPlayerState>();
	
	if (PS)
	{
		FString PlayerId = PS->GetPlayerUniqueId();
		bool bIsLoggedIn = PS->IsLoggedIn();
		
		UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] PostLogin - PlayerState: %s"), *GetNameSafe(PS));
		UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] PostLogin - PlayerId: '%s'"), *PlayerId);
		UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] PostLogin - bIsLoggedIn: %s"), bIsLoggedIn ? TEXT("TRUE") : TEXT("FALSE"));

		if (bIsLoggedIn && !PlayerId.IsEmpty())
		{
			// ============================================
			// ✅ 이미 로그인 된 상태 (SeamlessTravel에서 온 경우)
			// → 바로 HeroCharacter 소환!
			// ============================================
			UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] PostLogin - ✅ 이미 로그인됨! '%s' → HeroCharacter 소환"), *PlayerId);
			
			// 약간의 딜레이 후 캐릭터 소환 (네트워크 안정화)
			FTimerHandle SpawnTimer;
			GetWorldTimerManager().SetTimer(SpawnTimer, [this, NewPlayer]()
			{
				SpawnHeroCharacter(NewPlayer);
			}, 0.5f, false);
		}
		else
		{
			// ============================================
			// ⏳ 로그인 안 된 상태
			// → 로그인 UI 표시 요청 + 타임아웃 타이머 시작
			// ============================================
			UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] PostLogin - ⏳ 로그인 필요!"));
			UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] PostLogin - 로그인 UI 표시 요청 (타임아웃: %.0f초)"), LoginTimeoutSeconds);

			// ============================================
			// 📌 [TODO] 클라이언트에 로그인 UI 표시 요청
			// 
			// 현재: Inv_PlayerController에 Client RPC 추가 필요
			// 예: NewPlayer->Client_ShowLoginUI();
			// 
			// 또는 Blueprint에서 처리:
			// 1. PlayerController BeginPlay에서 로그인 여부 체크
			// 2. 로그인 안 됐으면 로그인 위젯 표시
			// ============================================

			// 타임아웃 타이머 시작
			FTimerHandle& TimeoutTimer = LoginTimeoutTimers.FindOrAdd(NewPlayer);
			GetWorldTimerManager().SetTimer(TimeoutTimer, [this, NewPlayer]()
			{
				OnLoginTimeout(NewPlayer);
			}, LoginTimeoutSeconds, false);

			UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] PostLogin - ⏰ 로그인 타임아웃 타이머 시작 (%.0f초)"), LoginTimeoutSeconds);
		}
	}
	else
	{
		APlayerState* RawPS = NewPlayer->GetPlayerState<APlayerState>();
		UE_LOG(LogTemp, Error, TEXT("[DefenseGameMode] PostLogin - ❌ HellunaPlayerState 아님!"));
		UE_LOG(LogTemp, Error, TEXT("[DefenseGameMode] PostLogin - RawPS: %s (Class: %s)"),
			RawPS ? *GetNameSafe(RawPS) : TEXT("nullptr"),
			RawPS ? *RawPS->GetClass()->GetName() : TEXT("N/A"));
	}

	UE_LOG(LogTemp, Warning, TEXT("========================================"));
	UE_LOG(LogTemp, Warning, TEXT(""));

	Super::PostLogin(NewPlayer);
}

// ============================================
// 📌 [Phase B] ProcessLogin - 로그인 요청 처리
// ============================================
void AHellunaDefenseGameMode::ProcessLogin(APlayerController* PlayerController, const FString& PlayerId, const FString& Password)
{
	/**
	 * ============================================
	 * 📌 로그인 처리 흐름
	 * 
	 * 1. 서버 권한 확인
	 * 2. 동시 접속 체크 (같은 ID 이미 접속 중?)
	 * 3. 계정 존재 여부 확인
	 *    - 있으면: 비밀번호 검증
	 *    - 없으면: 새 계정 생성
	 * 4. 로그인 성공/실패 처리
	 * ============================================
	 */

	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("========================================"));
	UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] ★ ProcessLogin 호출됨!"));
	UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] PlayerId: '%s'"), *PlayerId);
	UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] Controller: %s"), *GetNameSafe(PlayerController));
	UE_LOG(LogTemp, Warning, TEXT("========================================"));

	if (!HasAuthority())
	{
		UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] ProcessLogin: 서버에서만 호출 가능!"));
		return;
	}

	if (!PlayerController)
	{
		UE_LOG(LogTemp, Error, TEXT("[DefenseGameMode] ProcessLogin: PlayerController가 nullptr!"));
		return;
	}

	// ============================================
	// 📌 1단계: 동시 접속 체크
	// ============================================
	if (IsPlayerLoggedIn(PlayerId))
	{
		UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] ProcessLogin - ❌ 동시 접속 거부! '%s' 이미 접속 중"), *PlayerId);
		OnLoginFailed(PlayerController, TEXT("이미 접속 중인 계정입니다."));
		return;
	}

	// ============================================
	// 📌 2단계: 계정 데이터 확인
	// ============================================
	if (!AccountSaveGame)
	{
		UE_LOG(LogTemp, Error, TEXT("[DefenseGameMode] ProcessLogin - ❌ AccountSaveGame이 nullptr!"));
		OnLoginFailed(PlayerController, TEXT("서버 오류: 계정 데이터를 불러올 수 없습니다."));
		return;
	}

	// ============================================
	// 📌 3단계: 계정 존재 여부 확인 및 검증
	// ============================================
	if (AccountSaveGame->HasAccount(PlayerId))
	{
		// 기존 계정 → 비밀번호 검증
		if (AccountSaveGame->ValidatePassword(PlayerId, Password))
		{
			UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] ProcessLogin - ✅ 비밀번호 일치!"));
			OnLoginSuccess(PlayerController, PlayerId);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] ProcessLogin - ❌ 비밀번호 불일치!"));
			OnLoginFailed(PlayerController, TEXT("아이디가 이미 존재합니다. 비밀번호를 확인해주세요."));
		}
	}
	else
	{
		// 새 계정 → 생성
		if (AccountSaveGame->CreateAccount(PlayerId, Password))
		{
			UHellunaAccountSaveGame::Save(AccountSaveGame);
			UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] ProcessLogin - ✅ 새 계정 생성됨! '%s'"), *PlayerId);
			OnLoginSuccess(PlayerController, PlayerId);
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("[DefenseGameMode] ProcessLogin - ❌ 계정 생성 실패!"));
			OnLoginFailed(PlayerController, TEXT("계정 생성에 실패했습니다."));
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("========================================"));
	UE_LOG(LogTemp, Warning, TEXT(""));
}

// ============================================
// 📌 [Phase B] OnLoginSuccess - 로그인 성공 처리
// ============================================
void AHellunaDefenseGameMode::OnLoginSuccess(APlayerController* PlayerController, const FString& PlayerId)
{
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║         [DefenseGameMode] OnLoginSuccess                   ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ PlayerId: '%s'"), *PlayerId);
	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));

	// ============================================
	// 📌 1. 타임아웃 타이머 취소
	// ============================================
	if (FTimerHandle* TimerHandle = LoginTimeoutTimers.Find(PlayerController))
	{
		GetWorldTimerManager().ClearTimer(*TimerHandle);
		LoginTimeoutTimers.Remove(PlayerController);
		UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] ✅ 타임아웃 타이머 취소됨"));
	}

	// ============================================
	// 📌 2. GameInstance에 로그인 등록
	// ============================================
	if (UMDF_GameInstance* GI = Cast<UMDF_GameInstance>(UGameplayStatics::GetGameInstance(GetWorld())))
	{
		GI->RegisterLogin(PlayerId);
		UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] ✅ GameInstance에 등록됨"));
	}

	// ============================================
	// 📌 3. PlayerState에 로그인 정보 저장
	// ============================================
	if (AHellunaPlayerState* PS = PlayerController->GetPlayerState<AHellunaPlayerState>())
	{
		PS->SetLoginInfo(PlayerId);
		UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] ✅ PlayerState에 저장됨"));
	}

	// ============================================
	// 📌 4. 클라이언트에 성공 알림 (Client RPC)
	// ============================================
	if (AInv_PlayerController* InvPC = Cast<AInv_PlayerController>(PlayerController))
	{
		InvPC->Client_LoginResult(true, TEXT(""));
		UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] ✅ Client_LoginResult(true) 호출됨"));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] ⚠️ Inv_PlayerController 아님 - Client RPC 스킵"));
	}

	// ============================================
	// 📌 5. HeroCharacter 소환
	// 
	// [TODO: 캐릭터 선택창 구현 시]
	// 여기서 바로 SpawnHeroCharacter를 호출하지 않고,
	// 캐릭터 선택 UI를 표시한 후
	// 선택 완료 시 SpawnHeroCharacter 호출
	// ============================================
	UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] → HeroCharacter 소환 시작..."));
	UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] [TODO] 캐릭터 선택창 구현 시 여기서 UI 표시!"));
	
	SpawnHeroCharacter(PlayerController);

	UE_LOG(LogTemp, Warning, TEXT(""));
}

// ============================================
// 📌 [Phase B] OnLoginFailed - 로그인 실패 처리
// ============================================
void AHellunaDefenseGameMode::OnLoginFailed(APlayerController* PlayerController, const FString& ErrorMessage)
{
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║         [DefenseGameMode] OnLoginFailed                    ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ ErrorMessage: '%s'"), *ErrorMessage);
	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));

	// ============================================
	// 📌 클라이언트에 실패 알림 (Client RPC)
	// ============================================
	if (AInv_PlayerController* InvPC = Cast<AInv_PlayerController>(PlayerController))
	{
		InvPC->Client_LoginResult(false, ErrorMessage);
		UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] ✅ Client_LoginResult(false) 호출됨"));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] ⚠️ Inv_PlayerController 아님 - Client RPC 스킵"));
	}

	UE_LOG(LogTemp, Warning, TEXT(""));
}

// ============================================
// 📌 [Phase B] OnLoginTimeout - 로그인 타임아웃 처리
// ============================================
void AHellunaDefenseGameMode::OnLoginTimeout(APlayerController* PlayerController)
{
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("========================================"));
	UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] ⏰ OnLoginTimeout!"));
	UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] 킥 대상: %s"), *GetNameSafe(PlayerController));
	UE_LOG(LogTemp, Warning, TEXT("========================================"));

	if (!PlayerController)
		return;

	// 타이머 정리
	LoginTimeoutTimers.Remove(PlayerController);

	// 킥 처리
	if (PlayerController->GetNetConnection())
	{
		FString KickReason = FString::Printf(TEXT("로그인 타임아웃 (%.0f초). 다시 접속해주세요."), LoginTimeoutSeconds);
		UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] 킥 실행! 사유: %s"), *KickReason);
		
		PlayerController->ClientReturnToMainMenuWithTextReason(FText::FromString(KickReason));
	}

	UE_LOG(LogTemp, Warning, TEXT("========================================"));
	UE_LOG(LogTemp, Warning, TEXT(""));
}

// ============================================
// 📌 [Phase B] SpawnHeroCharacter - 히어로 캐릭터 소환
// ============================================
void AHellunaDefenseGameMode::SpawnHeroCharacter(APlayerController* PlayerController)
{
	/**
	 * ============================================
	 * 📌 [Phase B] 히어로 캐릭터 소환 흐름
	 * 
	 * 1. HeroCharacterClass 유효성 체크
	 * 2. 스폰 위치 결정 (PlayerStart 또는 기본 위치)
	 * 3. HeroCharacter 스폰
	 * 4. 기존 Pawn (SpectatorPawn) 제거
	 * 5. 새 HeroCharacter로 Possess
	 * 
	 * [TODO: 캐릭터 선택창 구현 시]
	 * 이 함수에 TSubclassOf<APawn> SelectedCharacterClass 파라미터 추가
	 * 
	 * void SpawnHeroCharacter(APlayerController* PlayerController, 
	 *                         TSubclassOf<APawn> SelectedCharacterClass);
	 * 
	 * 그리고 HeroCharacterClass 대신 SelectedCharacterClass 사용
	 * ============================================
	 */

	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("========================================"));
	UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] ★ SpawnHeroCharacter 호출됨!"));
	UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] Controller: %s"), *GetNameSafe(PlayerController));
	UE_LOG(LogTemp, Warning, TEXT("========================================"));

	if (!PlayerController)
	{
		UE_LOG(LogTemp, Error, TEXT("[DefenseGameMode] SpawnHeroCharacter - PlayerController가 nullptr!"));
		return;
	}

	// ============================================
	// 📌 1. HeroCharacterClass 체크
	// ============================================
	if (!HeroCharacterClass)
	{
		UE_LOG(LogTemp, Error, TEXT("[DefenseGameMode] SpawnHeroCharacter - ❌ HeroCharacterClass가 설정되지 않았습니다!"));
		UE_LOG(LogTemp, Error, TEXT("[DefenseGameMode] → BP_HellunaDefenseGameMode에서 'Hero Character Class'를 설정해주세요!"));
		
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 10.f, FColor::Red,
				TEXT("❌ HeroCharacterClass가 설정되지 않았습니다! GameMode BP에서 설정 필요"));
		}
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode]   - HeroCharacterClass: %s"), *HeroCharacterClass->GetName());

	// ============================================
	// 📌 2. 스폰 위치 결정
	// ============================================
	FVector SpawnLocation = FVector::ZeroVector;
	FRotator SpawnRotation = FRotator::ZeroRotator;

	// PlayerStart 찾기
	AActor* PlayerStart = FindPlayerStart(PlayerController);
	if (PlayerStart)
	{
		SpawnLocation = PlayerStart->GetActorLocation();
		SpawnRotation = PlayerStart->GetActorRotation();
		UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode]   - 스폰 위치: PlayerStart (%s)"), *SpawnLocation.ToString());
	}
	else
	{
		// PlayerStart 없으면 기본 위치
		SpawnLocation = FVector(0.f, 0.f, 200.f);
		UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode]   - 스폰 위치: 기본 위치 (%s)"), *SpawnLocation.ToString());
	}

	// ============================================
	// 📌 3. HeroCharacter 스폰
	// ============================================
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	SpawnParams.Owner = PlayerController;

	APawn* NewPawn = GetWorld()->SpawnActor<APawn>(HeroCharacterClass, SpawnLocation, SpawnRotation, SpawnParams);

	if (!NewPawn)
	{
		UE_LOG(LogTemp, Error, TEXT("[DefenseGameMode] SpawnHeroCharacter - ❌ HeroCharacter 스폰 실패!"));
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode]   - ✅ HeroCharacter 스폰 성공: %s"), *GetNameSafe(NewPawn));

	// ============================================
	// 📌 4. 기존 Pawn 제거
	// ============================================
	APawn* OldPawn = PlayerController->GetPawn();
	if (OldPawn)
	{
		UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode]   - 기존 Pawn 제거: %s"), *GetNameSafe(OldPawn));
		OldPawn->Destroy();
	}

	// ============================================
	// 📌 5. 새 HeroCharacter로 Possess
	// ============================================
	PlayerController->Possess(NewPawn);
	UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode]   - ✅ Possess 완료!"));

	UE_LOG(LogTemp, Warning, TEXT("========================================"));
	UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] ✅ HeroCharacter 소환 완료!"));
	UE_LOG(LogTemp, Warning, TEXT("========================================"));
	UE_LOG(LogTemp, Warning, TEXT(""));
}

// ============================================
// 📌 IsPlayerLoggedIn - 동시 접속 여부 확인
// ============================================
bool AHellunaDefenseGameMode::IsPlayerLoggedIn(const FString& PlayerId) const
{
	if (UMDF_GameInstance* GI = Cast<UMDF_GameInstance>(UGameplayStatics::GetGameInstance(GetWorld())))
	{
		return GI->IsPlayerLoggedIn(PlayerId);
	}
	return false;
}

// ============================================
// 📌 Logout - 로그아웃 처리
// ============================================
void AHellunaDefenseGameMode::Logout(AController* Exiting)
{
	UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] ★★★ Logout 호출됨! Exiting: %s"), Exiting ? *GetNameSafe(Exiting) : TEXT("nullptr"));
	
	if (Exiting)
	{
		// 타임아웃 타이머 정리
		if (APlayerController* PC = Cast<APlayerController>(Exiting))
		{
			if (FTimerHandle* TimerHandle = LoginTimeoutTimers.Find(PC))
			{
				GetWorldTimerManager().ClearTimer(*TimerHandle);
				LoginTimeoutTimers.Remove(PC);
			}
		}

		// PlayerState에서 로그인 정보 가져오기
		APlayerState* RawPS = Exiting->GetPlayerState<APlayerState>();
		UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] RawPlayerState: %s (Class: %s)"), 
			RawPS ? *GetNameSafe(RawPS) : TEXT("nullptr"),
			RawPS ? *RawPS->GetClass()->GetName() : TEXT("N/A"));

		if (AHellunaPlayerState* PS = Exiting->GetPlayerState<AHellunaPlayerState>())
		{
			FString PlayerId = PS->GetPlayerUniqueId();
			bool bIsLoggedIn = PS->IsLoggedIn();
			
			UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] HellunaPlayerState 찾음!"));
			UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode]   - PlayerId: '%s'"), *PlayerId);
			UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode]   - bIsLoggedIn: %s"), bIsLoggedIn ? TEXT("TRUE") : TEXT("FALSE"));
			
			if (!PlayerId.IsEmpty())
			{
				if (UMDF_GameInstance* GI = Cast<UMDF_GameInstance>(UGameplayStatics::GetGameInstance(GetWorld())))
				{
					GI->RegisterLogout(PlayerId);
					UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] ✅ 로그아웃 완료 - ID: %s"), *PlayerId);
				}
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] ⚠️ PlayerId가 비어있음 (로그인 안 한 상태로 종료)"));
			}
		}
	}

	Super::Logout(Exiting);
}

// ============================================
// 📌 HandleSeamlessTravelPlayer - SeamlessTravel 처리
// ============================================
void AHellunaDefenseGameMode::HandleSeamlessTravelPlayer(AController*& C)
{
	UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] ★ HandleSeamlessTravelPlayer 호출됨!"));
	
	// 기존 로그인 정보 저장
	FString SavedPlayerId;
	bool bSavedIsLoggedIn = false;
	
	if (C)
	{
		UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode]   (전) Controller: %s"), *GetNameSafe(C));
		
		if (AHellunaPlayerState* OldPS = C->GetPlayerState<AHellunaPlayerState>())
		{
			SavedPlayerId = OldPS->GetPlayerUniqueId();
			bSavedIsLoggedIn = OldPS->IsLoggedIn();
			
			UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode]   (전) PlayerState: %s"), *GetNameSafe(OldPS));
			UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode]   (저장) PlayerId: '%s', bIsLoggedIn: %s"), 
				*SavedPlayerId, bSavedIsLoggedIn ? TEXT("TRUE") : TEXT("FALSE"));
		}
	}

	// 부모 클래스 호출
	Super::HandleSeamlessTravelPlayer(C);
	
	// 새 PlayerState에 로그인 정보 복원
	UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode]   (후) Controller: %s"), C ? *GetNameSafe(C) : TEXT("nullptr"));
	
	if (C && !SavedPlayerId.IsEmpty())
	{
		if (AHellunaPlayerState* NewPS = C->GetPlayerState<AHellunaPlayerState>())
		{
			UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode]   (후) PlayerState: %s"), *GetNameSafe(NewPS));
			NewPS->SetLoginInfo(SavedPlayerId);
			UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode]   ✅ '%s' → %s에 복원 완료!"), *SavedPlayerId, *GetNameSafe(NewPS));
		}
	}
}

// ============================================
// 📌 기존 기능들 (보스, 몬스터, 낮/밤 등)
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
	if (!HasAuthority())
		return;

	if (!TestMonsterClass)
	{
		Debug::Print(TEXT("[Defense] TestMonsterClass is null"), FColor::Red);
		return;
	}

	if (MonsterSpawnPoints.IsEmpty())
	{
		Debug::Print(TEXT("[Defense] No MonsterSpawn TargetPoints (Tag=MonsterSpawn)"), FColor::Red);
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
	if (!HasAuthority())
		return;
	GetWorld()->ServerTravel(TEXT("/Game/Minwoo/MinwooTestMap?listen"));
}

void AHellunaDefenseGameMode::SetBossReady(bool bReady)
{
	if (!HasAuthority() || bBossReady == bReady)
		return;

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
	if (!HasAuthority())
		return;

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
	if (!HasAuthority() || !Monster)
		return;

	AHellunaDefenseGameState* GS = GetGameState<AHellunaDefenseGameState>();
	if (!GS || GS->GetPhase() != EDefensePhase::Night)
		return;

	if (AliveMonsters.Contains(Monster))
		return;

	AliveMonsters.Add(Monster);
	GS->SetAliveMonsterCount(AliveMonsters.Num());

	Debug::Print(FString::Printf(TEXT("[Defense] Register Monster: %s | Alive=%d"),
		*GetNameSafe(Monster), AliveMonsters.Num()));
}

void AHellunaDefenseGameMode::NotifyMonsterDied(AActor* DeadMonster)
{
	if (!HasAuthority() || !DeadMonster)
		return;

	AHellunaDefenseGameState* GS = GetGameState<AHellunaDefenseGameState>();
	if (!GS) return;

	AliveMonsters.Remove(TWeakObjectPtr<AActor>(DeadMonster));
	GS->SetAliveMonsterCount(AliveMonsters.Num());

	Debug::Print(FString::Printf(TEXT("[Defense] Monster Died: %s | Alive=%d"),
		*GetNameSafe(DeadMonster), AliveMonsters.Num()));

	if (AliveMonsters.Num() <= 0)
	{
		GetWorldTimerManager().ClearTimer(TimerHandle_ToDay);
		GetWorldTimerManager().SetTimer(TimerHandle_ToDay, this, &ThisClass::EnterDay, TestNightFailToDayDelay, false);
	}
}
