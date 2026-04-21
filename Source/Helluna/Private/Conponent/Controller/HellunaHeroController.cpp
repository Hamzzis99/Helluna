// Fill out your copyright notice in the Description page of Project Settings.


#include "Controller/HellunaHeroController.h"
#include "Utils/Vote/VoteManagerComponent.h"
#include "Utils/Vote/VoteTypes.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "UI/Vote/VoteWidget.h"
#include "GameMode/Widget/HellunaGameResultWidget.h"
#include "GameMode/HellunaDefenseGameMode.h"  // EHellunaGameEndReason, EndGame()
#include "Blueprint/UserWidget.h"
#include "TimerManager.h"

// [Loading Barrier]
#include "Loading/HellunaLoadingHUDWidget.h"
#include "Loading/HellunaLoadingShipActor.h"
#include "MoviePlayer.h"
#include "Camera/CameraActor.h"
#include "Camera/PlayerCameraManager.h"
#include "Kismet/GameplayStatics.h"
#include "WorldPartition/WorldPartitionSubsystem.h"
#include "Engine/AssetManager.h"

// [Phase 10] 채팅 시스템
#include "Chat/HellunaChatTypes.h"
#include "Chat/HellunaChatWidget.h"
#include "GameMode/HellunaDefenseGameState.h"
#include "Player/HellunaPlayerState.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "MDF_Function/MDF_Instance/MDF_GameInstance.h"
#include "Settings/Widget/HellunaGraphicsSettingsWidget.h"
#include "UI/PauseMenu/HellunaPauseMenuWidget.h"
#include "UI/HUD/HellunaDebugHUDWidget.h"

// [Puzzle] 퍼즐 시스템
#include "Puzzle/PuzzleCubeActor.h"
#include "Puzzle/PuzzleGridWidget.h"
#include "EngineUtils.h" // TActorIterator
#include "Components/PostProcessComponent.h"

// [BossEvent] 보스 조우 시스템
#include "BossEvent/BossEncounterCube.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"

// [DebugHUD] 디버그 HUD 시스템
#include "UI/HUD/HellunaDebugHUDWidget.h"

// [WorldMap] 풀스크린 월드맵 + 핑 시스템
#include "UI/WorldMap/HellunaWorldMapWidget.h"

// [PauseMenu] 위젯 애니메이션 재생
// WidgetBlueprintGeneratedClass는 PauseMenuWidget 내부로 이동



AHellunaHeroController::AHellunaHeroController()
{
	PrimaryActorTick.bCanEverTick = true;
	HeroTeamID = FGenericTeamId(0);
}

FGenericTeamId AHellunaHeroController::GetGenericTeamId() const
{
	return HeroTeamID;
}

// ============================================================================
// 라이프사이클
// ============================================================================

void AHellunaHeroController::SetupInputComponent()
{
	Super::SetupInputComponent();
}

void AHellunaHeroController::BeginPlay()
{
	Super::BeginPlay();

	UE_LOG(LogHellunaVote, Log, TEXT("[HellunaHeroController] BeginPlay - %s"),
		IsLocalController() ? TEXT("로컬") : TEXT("서버"));

	// 로컬 플레이어만 투표 위젯 생성
	if (IsLocalController() && VoteWidgetClass)
	{
		// GameState 복제 대기 후 위젯 초기화 (0.5초 딜레이)
		GetWorldTimerManager().SetTimer(
			VoteWidgetInitTimerHandle,
			this,
			&AHellunaHeroController::InitializeVoteWidget,
			0.5f,
			false
		);

		UE_LOG(LogHellunaVote, Log, TEXT("[HellunaHeroController] 투표 위젯 초기화 타이머 설정 (0.5초)"));
	}

	// [Puzzle] 퍼즐 입력 바인딩 (로컬 플레이어만, GameState 불필요)
	if (IsLocalController() && PuzzleInteractAction && PuzzleMappingContext)
	{
		if (ULocalPlayer* PuzzleLP = GetLocalPlayer())
		{
			if (UEnhancedInputLocalPlayerSubsystem* PuzzleSub = PuzzleLP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
			{
				PuzzleSub->AddMappingContext(PuzzleMappingContext, 10);
				UE_LOG(LogTemp, Warning, TEXT("[PuzzleInput] PuzzleMappingContext 추가 완료 (priority=10)"));
			}
		}

		if (UEnhancedInputComponent* PuzzleEIC = Cast<UEnhancedInputComponent>(InputComponent))
		{
			PuzzleEIC->BindAction(PuzzleInteractAction, ETriggerEvent::Triggered, this, &AHellunaHeroController::OnPuzzleInteractInput);

			// F키 홀드 상태 추적 — Ongoing은 명시적 트리거 없으면 발동 안 함, Started 사용
			PuzzleEIC->BindAction(PuzzleInteractAction, ETriggerEvent::Started, this, &AHellunaHeroController::OnPuzzleInteractOngoing);
			PuzzleEIC->BindAction(PuzzleInteractAction, ETriggerEvent::Completed, this, &AHellunaHeroController::OnPuzzleInteractReleased);
			PuzzleEIC->BindAction(PuzzleInteractAction, ETriggerEvent::Canceled, this, &AHellunaHeroController::OnPuzzleInteractReleased);

			if (PuzzleUpAction)
			{
				PuzzleEIC->BindAction(PuzzleUpAction, ETriggerEvent::Started, this, &AHellunaHeroController::OnPuzzleUpInput);
			}
			if (PuzzleDownAction)
			{
				PuzzleEIC->BindAction(PuzzleDownAction, ETriggerEvent::Started, this, &AHellunaHeroController::OnPuzzleDownInput);
			}
			if (PuzzleLeftAction)
			{
				PuzzleEIC->BindAction(PuzzleLeftAction, ETriggerEvent::Started, this, &AHellunaHeroController::OnPuzzleLeftInput);
			}
			if (PuzzleRightAction)
			{
				PuzzleEIC->BindAction(PuzzleRightAction, ETriggerEvent::Started, this, &AHellunaHeroController::OnPuzzleRightInput);
			}
			if (PuzzleRotateAction)
			{
				PuzzleEIC->BindAction(PuzzleRotateAction, ETriggerEvent::Started, this, &AHellunaHeroController::OnPuzzleRotateInput);
			}
			if (PuzzleExitAction)
			{
				PuzzleEIC->BindAction(PuzzleExitAction, ETriggerEvent::Started, this, &AHellunaHeroController::OnPuzzleExitInput);
			}

			UE_LOG(LogTemp, Log, TEXT("[HeroController] PuzzleInteractAction + Navigate/Rotate/Exit 바인딩 완료"));
		}
	}

	// [HackMode] PostProcess 지연 초기화 (Pawn 스폰 대기)
	if (IsLocalController())
	{
		FTimerHandle PostProcessInitHandle;
		GetWorldTimerManager().SetTimer(PostProcessInitHandle, [this]()
		{
			APawn* MyPawn = GetPawn();
			if (!IsValid(MyPawn)) { return; }
			if (DesaturationPostProcess) { return; } // 이미 생성됨

			DesaturationPostProcess = NewObject<UPostProcessComponent>(MyPawn);
			if (DesaturationPostProcess)
			{
				DesaturationPostProcess->RegisterComponent();
				DesaturationPostProcess->AttachToComponent(
					MyPawn->GetRootComponent(),
					FAttachmentTransformRules::KeepRelativeTransform);
				DesaturationPostProcess->bUnbound = true;
				DesaturationPostProcess->Priority = 10.f;

				FPostProcessSettings& Settings = DesaturationPostProcess->Settings;
				Settings.bOverride_ColorSaturation = true;
				Settings.ColorSaturation = FVector4(1.f, 1.f, 1.f, 1.f);
				Settings.bOverride_ColorContrast = true;
				Settings.ColorContrast = FVector4(1.f, 1.f, 1.f, 1.f);
				Settings.bOverride_VignetteIntensity = true;
				Settings.VignetteIntensity = 0.f;

				UE_LOG(LogTemp, Log, TEXT("[HackMode] PostProcessComponent created"));
			}
		}, 0.5f, false);
	}

	// [Phase 10] 채팅 위젯 초기화 (로컬 플레이어만)
	if (IsLocalController() && ChatWidgetClass)
	{
		GetWorldTimerManager().SetTimer(
			ChatWidgetInitTimerHandle,
			this,
			&AHellunaHeroController::InitializeChatWidget,
			0.5f,
			false
		);

		UE_LOG(LogHellunaChat, Log, TEXT("[HellunaHeroController] 채팅 위젯 초기화 타이머 설정 (0.5초)"));
	}

	// ── [PauseMenu] ESC/U 키 바인딩 (Enhanced Input) ──
	if (IsLocalController() && PauseMenuToggleAction && PauseMenuMappingContext)
	{
		if (ULocalPlayer* PauseLP = GetLocalPlayer())
		{
			if (UEnhancedInputLocalPlayerSubsystem* PauseSub = PauseLP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
			{
				PauseSub->AddMappingContext(PauseMenuMappingContext, 10);
				UE_LOG(LogTemp, Log, TEXT("[PauseMenu] PauseMenuMappingContext 추가 완료 (priority=10)"));
			}
		}

		if (UEnhancedInputComponent* PauseEIC = Cast<UEnhancedInputComponent>(InputComponent))
		{
			PauseEIC->BindAction(PauseMenuToggleAction, ETriggerEvent::Started, this, &AHellunaHeroController::OnPauseMenuToggleInput);
			UE_LOG(LogTemp, Log, TEXT("[PauseMenu] PauseMenuToggleAction 바인딩 완료 (ESC+U)"));
		}
	}

	// ── [WorldMap] M키 월드맵 토글 바인딩 (로컬 플레이어만) ──
	if (IsLocalController() && ToggleMapAction && WorldMapMappingContext)
	{
		if (ULocalPlayer* MapLP = GetLocalPlayer())
		{
			if (UEnhancedInputLocalPlayerSubsystem* MapSub = MapLP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
			{
				MapSub->AddMappingContext(WorldMapMappingContext, 10);
				UE_LOG(LogTemp, Log, TEXT("[WorldMap] WorldMapMappingContext 추가 완료 (priority=10)"));
			}
		}

		if (UEnhancedInputComponent* MapEIC = Cast<UEnhancedInputComponent>(InputComponent))
		{
			MapEIC->BindAction(ToggleMapAction, ETriggerEvent::Started, this, &AHellunaHeroController::OnToggleWorldMapInput);
			UE_LOG(LogTemp, Log, TEXT("[WorldMap] ToggleMapAction 바인딩 완료 (M키)"));
		}
	}

	// ── [DebugHUD] 디버그 HUD 생성 + F5 입력 바인딩 (로컬 플레이어만) ──
#if !UE_BUILD_SHIPPING
	if (IsLocalController())
	{
		CreateDebugHUD();

		// F5 키 바인딩 (DebugHUDToggleAction이 BP에서 설정된 경우)
		if (DebugHUDToggleAction)
		{
			if (UEnhancedInputComponent* DebugEIC = Cast<UEnhancedInputComponent>(InputComponent))
			{
				DebugEIC->BindAction(DebugHUDToggleAction, ETriggerEvent::Started, this, &AHellunaHeroController::OnDebugHUDToggle);
				UE_LOG(LogTemp, Log, TEXT("[DebugHUD] F5 토글 입력 바인딩 완료"));
			}
		}
	}
#endif
}

// ============================================================================
// Tick — Desaturation Lerp
// ============================================================================

void AHellunaHeroController::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	TickDesaturation(DeltaTime);
	TickColorReveal(DeltaTime);
}

// ============================================================================
// C5+B7: EndPlay — 타이머 정리 + 채팅 델리게이트 해제
// ============================================================================

void AHellunaHeroController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// C5: 타이머 핸들 정리 (파괴 후 콜백 방지)
	GetWorldTimerManager().ClearTimer(VoteWidgetInitTimerHandle);
	GetWorldTimerManager().ClearTimer(ChatWidgetInitTimerHandle);

	// [Puzzle] 퍼즐 위젯 정리
	if (IsValid(ActivePuzzleWidget))
	{
		ActivePuzzleWidget->RemoveFromParent();
		ActivePuzzleWidget = nullptr;
	}
	bInPuzzleMode = false;

	// [WorldMap] 월드맵 위젯 정리
	if (IsValid(WorldMapWidgetInstance))
	{
		WorldMapWidgetInstance->RemoveFromParent();
		WorldMapWidgetInstance = nullptr;
	}

	// [HackMode] Saturation 즉시 복원
	CurrentSaturation = 1.f;
	TargetSaturation = 1.f;
	bInHackMode = false;

	// B7: 채팅 델리게이트 언바인딩 (GameState가 파괴된 위젯 참조 방지)
	if (IsValid(ChatWidgetInstance))
	{
		if (UWorld* World = GetWorld())
		{
			if (AHellunaDefenseGameState* DefenseGS = World->GetGameState<AHellunaDefenseGameState>())
			{
				DefenseGS->OnChatMessageReceived.RemoveDynamic(ChatWidgetInstance, &UHellunaChatWidget::OnReceiveChatMessage);
			}
		}
		ChatWidgetInstance->OnChatMessageSubmitted.RemoveDynamic(this, &AHellunaHeroController::OnChatMessageSubmitted);
	}

	Super::EndPlay(EndPlayReason);
}

// ============================================================================
// [투표 시스템] 위젯 자동 초기화
// ============================================================================

void AHellunaHeroController::InitializeVoteWidget()
{
	// [Fix26] 재시도 횟수 제한 (무한 루프 방지, ChatWidget과 동일 패턴)
	constexpr int32 MaxVoteWidgetInitRetries = 20;
	if (VoteWidgetInitRetryCount >= MaxVoteWidgetInitRetries)
	{
		UE_LOG(LogHellunaVote, Error, TEXT("[HellunaHeroController] 투표 위젯 초기화 최대 재시도(%d) 초과 — 중단"), MaxVoteWidgetInitRetries);
		return;
	}

	UE_LOG(LogHellunaVote, Log, TEXT("[HellunaHeroController] InitializeVoteWidget 진입 (시도 %d/%d)"), VoteWidgetInitRetryCount + 1, MaxVoteWidgetInitRetries);

	// 1. 위젯이 아직 없으면 생성 (재시도 시 중복 생성 방지)
	if (!VoteWidgetInstance)
	{
		VoteWidgetInstance = CreateWidget<UVoteWidget>(this, VoteWidgetClass);
		if (!VoteWidgetInstance)
		{
			UE_LOG(LogHellunaVote, Error, TEXT("[HellunaHeroController] 투표 위젯 생성 실패!"));
			return;
		}

		// 뷰포트에 추가
		VoteWidgetInstance->AddToViewport();
		UE_LOG(LogHellunaVote, Log, TEXT("[HellunaHeroController] 투표 위젯 생성 및 Viewport 추가 완료"));
	}

	// 2. GameState에서 VoteManager 가져오기
	// [Fix26] GetWorld() null 체크
	UWorld* VoteWorld = GetWorld();
	if (!VoteWorld) return;
	AGameStateBase* GameState = VoteWorld->GetGameState();
	if (!GameState)
	{
		UE_LOG(LogHellunaVote, Warning, TEXT("[HellunaHeroController] GameState가 아직 없음 - 재시도"));
		++VoteWidgetInitRetryCount; // [Fix26]
		GetWorldTimerManager().SetTimer(
			VoteWidgetInitTimerHandle,
			this,
			&AHellunaHeroController::InitializeVoteWidget,
			0.5f,
			false
		);
		return;
	}

	UVoteManagerComponent* VoteManager = GameState->FindComponentByClass<UVoteManagerComponent>();
	if (!VoteManager)
	{
		UE_LOG(LogHellunaVote, Warning, TEXT("[HellunaHeroController] VoteManager가 아직 없음 - 재시도"));
		++VoteWidgetInitRetryCount; // [Fix26]
		GetWorldTimerManager().SetTimer(
			VoteWidgetInitTimerHandle,
			this,
			&AHellunaHeroController::InitializeVoteWidget,
			0.5f,
			false
		);
		return;
	}

	// 4. 위젯에 VoteManager 바인딩
	VoteWidgetInstance->InitializeVoteWidget(VoteManager);

	UE_LOG(LogHellunaVote, Log, TEXT("[HellunaHeroController] 투표 위젯 초기화 완료!"));
}

// ============================================================================
// [투표 시스템] Server RPC - 클라이언트 → 서버
// ============================================================================

// ============================================================================
// [Step3] Server_SubmitVote Validate
// bool 파라미터는 범위가 제한적이므로 항상 true 반환
// WithValidation 자체가 UE 네트워크 스택의 기본 무결성 검증을 활성화함
// ============================================================================
bool AHellunaHeroController::Server_SubmitVote_Validate(bool bAgree)
{
	return true;
}

void AHellunaHeroController::Server_SubmitVote_Implementation(bool bAgree)
{
	// 1. PlayerState 가져오기 (서버에서 실행되므로 항상 유효)
	APlayerState* VoterPS = GetPlayerState<APlayerState>();
	if (!VoterPS)
	{
		UE_LOG(LogHellunaVote, Warning, TEXT("[HeroController] Server_SubmitVote - PlayerState가 null"));
		return;
	}

	// 2. GameState에서 VoteManagerComponent 가져오기
	// [Fix26] GetWorld() null 체크
	UWorld* SubmitWorld = GetWorld();
	if (!SubmitWorld) return;
	AGameStateBase* GameState = SubmitWorld->GetGameState();
	if (!GameState)
	{
		UE_LOG(LogHellunaVote, Warning, TEXT("[HeroController] Server_SubmitVote - GameState가 null"));
		return;
	}

	UVoteManagerComponent* VoteManager = GameState->FindComponentByClass<UVoteManagerComponent>();
	if (!VoteManager)
	{
		UE_LOG(LogHellunaVote, Warning, TEXT("[HeroController] Server_SubmitVote - VoteManager가 null"));
		return;
	}

	// 3. 일반 함수 호출 (서버에서 서버로, NetConnection 문제 없음)
	UE_LOG(LogHellunaVote, Log, TEXT("[HeroController] Server_SubmitVote → VoteManager->ReceiveVote(%s, %s)"),
		*VoterPS->GetPlayerName(), bAgree ? TEXT("찬성") : TEXT("반대"));
	VoteManager->ReceiveVote(VoterPS, bAgree);
}

// ============================================================================
// [디버그] 치트 RPC — 클라이언트 → 서버 강제 게임 종료
// ============================================================================

void AHellunaHeroController::Server_CheatEndGame_Implementation(uint8 ReasonIndex)
{
#if UE_BUILD_SHIPPING
	// 프로덕션(Shipping) 빌드에서는 치트 명령 무효화
	UE_LOG(LogHelluna, Warning, TEXT("[HeroController] Server_CheatEndGame: Shipping 빌드에서 치트 차단"));
	return;
#endif

	if (!HasAuthority())
	{
		return;
	}

	AHellunaDefenseGameMode* DefenseGM = Cast<AHellunaDefenseGameMode>(GetWorld()->GetAuthGameMode());
	if (!IsValid(DefenseGM))
	{
		UE_LOG(LogTemp, Warning, TEXT("[HeroController] Server_CheatEndGame: DefenseGameMode가 아님 — 무시"));
		return;
	}

	// ReasonIndex → EHellunaGameEndReason 변환
	EHellunaGameEndReason Reason;
	switch (ReasonIndex)
	{
	case 0:  Reason = EHellunaGameEndReason::Escaped;        break;
	case 1:  Reason = EHellunaGameEndReason::AllDead;         break;
	case 2:  Reason = EHellunaGameEndReason::ServerShutdown;  break;
	default: Reason = EHellunaGameEndReason::Escaped;         break;
	}

	UE_LOG(LogTemp, Warning, TEXT("[HeroController] === 치트: EndGame(%d) 호출 ==="), ReasonIndex);
	DefenseGM->EndGame(Reason);
}

// ============================================================================
// [Phase 7] 게임 결과 UI — Client RPC
// ============================================================================

void AHellunaHeroController::Client_ShowGameResult_Implementation(
	const TArray<FInv_SavedItemData>& ResultItems,
	bool bSurvived,
	const FString& Reason,
	const FString& LobbyURL)
{
	UE_LOG(LogTemp, Log, TEXT("[HeroController] Client_ShowGameResult | Survived=%s Items=%d Reason=%s"),
		bSurvived ? TEXT("Yes") : TEXT("No"), ResultItems.Num(), *Reason);

	// [Phase 12f] LobbyURL이 비어있으면 GameInstance에서 동적 구성
	FString ResolvedLobbyURL = LobbyURL;
	if (ResolvedLobbyURL.IsEmpty())
	{
		UMDF_GameInstance* GI = Cast<UMDF_GameInstance>(GetGameInstance());
		if (GI && !GI->ConnectedServerIP.IsEmpty())
		{
			ResolvedLobbyURL = FString::Printf(TEXT("%s:%d"), *GI->ConnectedServerIP, GI->LobbyServerPort);
			UE_LOG(LogTemp, Log, TEXT("[HeroController] LobbyURL 동적 구성: %s"), *ResolvedLobbyURL);
		}
	}

	if (!GameResultWidgetClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("[HeroController] GameResultWidgetClass가 설정되지 않음! BP에서 설정 필요"));
		// 위젯 없으면 바로 로비로 이동
		if (!ResolvedLobbyURL.IsEmpty())
		{
			ClientTravel(ResolvedLobbyURL, TRAVEL_Absolute);
		}
		return;
	}

	// 기존 결과 위젯 중복 생성 방지
	if (GameResultWidgetInstance)
	{
		GameResultWidgetInstance->RemoveFromParent();
		GameResultWidgetInstance = nullptr;
	}

	GameResultWidgetInstance = CreateWidget<UHellunaGameResultWidget>(this, GameResultWidgetClass);
	if (!GameResultWidgetInstance)
	{
		UE_LOG(LogTemp, Error, TEXT("[HeroController] 결과 위젯 생성 실패!"));
		if (!ResolvedLobbyURL.IsEmpty())
		{
			ClientTravel(ResolvedLobbyURL, TRAVEL_Absolute);
		}
		return;
	}

	// 로비 URL 설정 (위젯에도 동적 구성 폴백 있음)
	GameResultWidgetInstance->LobbyURL = ResolvedLobbyURL;

	// 결과 데이터 설정 (내부에서 OnResultDataSet BP 이벤트 호출)
	GameResultWidgetInstance->SetResultData(ResultItems, bSurvived, Reason);

	// 뷰포트에 추가
	GameResultWidgetInstance->AddToViewport(100);  // 높은 ZOrder로 최상위 표시

	// 마우스 커서 표시 (결과 화면에서 버튼 클릭 가능하도록)
	SetShowMouseCursor(true);
	SetInputMode(FInputModeUIOnly());
}

// ============================================================================
// [Phase 10] 채팅 시스템
// ============================================================================

void AHellunaHeroController::InitializeChatWidget()
{
	UE_LOG(LogHellunaChat, Log, TEXT("[HeroController] InitializeChatWidget 진입"));

	// 1. 위젯이 아직 없으면 생성
	if (!ChatWidgetInstance)
	{
		ChatWidgetInstance = CreateWidget<UHellunaChatWidget>(this, ChatWidgetClass);
		if (!ChatWidgetInstance)
		{
			UE_LOG(LogHellunaChat, Error, TEXT("[HeroController] 채팅 위젯 생성 실패!"));
			return;
		}

		// 뷰포트에 추가 (낮은 ZOrder — 다른 UI보다 뒤에)
		ChatWidgetInstance->AddToViewport(0);
		UE_LOG(LogHellunaChat, Log, TEXT("[HeroController] 채팅 위젯 생성 및 Viewport 추가 완료"));
	}

	// 2. GameState 대기
	UWorld* World = GetWorld(); // C6: GetWorld() null 체크
	if (!World) return;
	AHellunaDefenseGameState* DefenseGS = World->GetGameState<AHellunaDefenseGameState>();
	if (!DefenseGS)
	{
		// U30: 무한 재시도 방지
		++ChatWidgetInitRetryCount;
		if (ChatWidgetInitRetryCount >= MaxChatWidgetInitRetries)
		{
			UE_LOG(LogHellunaChat, Error, TEXT("[HeroController] GameState %d회 재시도 실패 — 채팅 초기화 중단"), MaxChatWidgetInitRetries);
			return;
		}
		UE_LOG(LogHellunaChat, Warning, TEXT("[HeroController] GameState가 아직 없음 — 채팅 위젯 재시도 (%d/%d)"), ChatWidgetInitRetryCount, MaxChatWidgetInitRetries);
		GetWorldTimerManager().SetTimer(
			ChatWidgetInitTimerHandle,
			this,
			&AHellunaHeroController::InitializeChatWidget,
			0.5f,
			false
		);
		return;
	}

	// 3. GameState의 채팅 메시지 델리게이트 바인딩
	// [Fix26] AddDynamic → AddUniqueDynamic (재초기화 시 이중 바인딩 방지)
	DefenseGS->OnChatMessageReceived.AddUniqueDynamic(ChatWidgetInstance, &UHellunaChatWidget::OnReceiveChatMessage);

	// 4. 위젯의 메시지 제출 델리게이트 바인딩
	ChatWidgetInstance->OnChatMessageSubmitted.AddUniqueDynamic(this, &AHellunaHeroController::OnChatMessageSubmitted);

	// 5. Enhanced Input 바인딩 — 채팅 토글 (Enter 키)
	if (ChatToggleAction && ChatMappingContext)
	{
		if (ULocalPlayer* LocalPlayer = GetLocalPlayer())
		{
			if (UEnhancedInputLocalPlayerSubsystem* Subsystem = LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
			{
				// 채팅 IMC를 항상 활성화
				Subsystem->AddMappingContext(ChatMappingContext, 10);
				UE_LOG(LogHellunaChat, Log, TEXT("[HeroController] ChatMappingContext 추가 완료"));
			}
		}

		if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(InputComponent))
		{
			EIC->BindAction(ChatToggleAction, ETriggerEvent::Started, this, &AHellunaHeroController::OnChatToggleInput);
			UE_LOG(LogHellunaChat, Log, TEXT("[HeroController] ChatToggleAction 바인딩 완료"));
		}
	}
	else
	{
		UE_LOG(LogHellunaChat, Warning, TEXT("[HeroController] ChatToggleAction 또는 ChatMappingContext가 설정되지 않음 — BP에서 설정 필요"));
	}

	UE_LOG(LogHellunaChat, Log, TEXT("[HeroController] 채팅 위젯 초기화 완료!"));
}

void AHellunaHeroController::OnChatToggleInput(const FInputActionValue& Value)
{
	// 퍼즐 모드 중에는 채팅 차단
	if (bInPuzzleMode)
	{
		return;
	}

	UE_LOG(LogHellunaChat, Warning, TEXT("[HeroController] OnChatToggleInput 호출됨!"));

	// W6: 채팅 입력 활성 상태에서 Enter는 TextBox의 OnTextCommitted가 처리
	// Enhanced Input과 TextBox 양쪽에서 Enter가 동시 처리되는 충돌 방지
	if (IsValid(ChatWidgetInstance) && ChatWidgetInstance->IsChatInputActive())
	{
		return;
	}
	ToggleChatInput();
}

void AHellunaHeroController::ToggleChatInput()
{
	if (!IsValid(ChatWidgetInstance)) return;

	ChatWidgetInstance->ToggleChatInput();
}

void AHellunaHeroController::OnChatMessageSubmitted(const FString& Message)
{
	// 클라이언트에서 위젯이 메시지 제출 → Server RPC 호출
	if (!Message.IsEmpty() && Message.Len() <= 200)
	{
		Server_SendChatMessage(Message);
	}
}

bool AHellunaHeroController::Server_SendChatMessage_Validate(const FString& Message)
{
	// Fix18 준수: FString 값 타입만 검증, UObject 포인터 검증 금지
	return Message.Len() > 0 && Message.Len() <= 200;
}

void AHellunaHeroController::Server_SendChatMessage_Implementation(const FString& Message)
{
	// 스팸 방지: 0.5초 쿨다운
	const double CurrentTime = FPlatformTime::Seconds();
	if (CurrentTime - LastChatMessageTime < ChatCooldownSeconds)
	{
		UE_LOG(LogHellunaChat, Warning, TEXT("[HeroController] 채팅 쿨다운 — 메시지 무시 | Player=%s"),
			*GetNameSafe(this));
		return;
	}
	LastChatMessageTime = CurrentTime;

	// 발신자 이름 가져오기 (PlayerState의 PlayerUniqueId)
	FString SenderName;
	if (AHellunaPlayerState* HellunaPS = GetPlayerState<AHellunaPlayerState>())
	{
		SenderName = HellunaPS->GetPlayerUniqueId();
	}
	if (SenderName.IsEmpty())
	{
		SenderName = TEXT("Unknown");
	}

	// GameState를 통해 모든 클라이언트에 브로드캐스트
	UWorld* SendWorld = GetWorld(); // C6: GetWorld() null 체크
	if (!SendWorld) return;
	AHellunaDefenseGameState* DefenseGS = SendWorld->GetGameState<AHellunaDefenseGameState>();
	if (DefenseGS)
	{
		DefenseGS->BroadcastChatMessage(SenderName, Message, EChatMessageType::Player);
	}
	else
	{
		UE_LOG(LogHellunaChat, Warning, TEXT("[HeroController] Server_SendChatMessage: GameState가 null!"));
	}
}

// ============================================================================
// [Puzzle] 퍼즐 시스템
// ============================================================================

void AHellunaHeroController::TryEnterPuzzleFromCube(APuzzleCubeActor* Cube)
{
	if (bInPuzzleMode || !IsValid(Cube)) return;

	LocalTargetPuzzleCube = Cube;

	UE_LOG(LogTemp, Warning,
		TEXT("[PuzzleController] TryEnterPuzzleFromCube: Cube=%s"),
		*GetNameSafe(Cube));

	Server_PuzzleTryEnter();
}

void AHellunaHeroController::OnPuzzleInteractInput(const FInputActionValue& Value)
{
	// [Fix] Started가 놓쳐도 Triggered 매 틱에서 홀드 상태 보장
	bHoldingPuzzleInteract = true;

	if (bInPuzzleMode)
	{
		return;
	}

	// 클라이언트 측: 근처 퍼즐 큐브 확인
	APawn* MyPawn = GetPawn();
	if (!IsValid(MyPawn))
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	APuzzleCubeActor* NearestCube = nullptr;
	float NearestDist = FLT_MAX;

	for (TActorIterator<APuzzleCubeActor> It(World); It; ++It)
	{
		APuzzleCubeActor* Cube = *It;
		if (!IsValid(Cube))
		{
			continue;
		}

		const float Dist = FVector::Dist(MyPawn->GetActorLocation(), Cube->GetActorLocation());
		if (Dist < Cube->GetInteractionRadius() && Dist < NearestDist)
		{
			NearestDist = Dist;
			NearestCube = Cube;
		}
	}

	if (!NearestCube)
	{
		return;
	}

	LocalTargetPuzzleCube = NearestCube;

	// [Guard] IMC Hold 트리거가 직렬화에서 풀릴 수 있으므로
	// C++에서 직접 홀드 프로그레스 완료를 체크 (1.5초)
	if (NearestCube->GetLocalHoldProgress() < 0.95f)
	{
		UE_LOG(LogTemp, Log, TEXT("[PuzzleController] Hold not complete yet (%.2f), ignoring"), NearestCube->GetLocalHoldProgress());
		return;
	}

	// 로그 #12
	UE_LOG(LogTemp, Warning,
		TEXT("[PuzzleController] TryEnterPuzzle: Cube=%s, Distance=%.1f"),
		*GetNameSafe(NearestCube), NearestDist);

	
	Server_PuzzleTryEnter();
}

// --- ExitPuzzle ---

void AHellunaHeroController::ExitPuzzle()
{
	if (!bInPuzzleMode) { return; }

	UE_LOG(LogTemp, Warning, TEXT("[PuzzleController] ExitPuzzle"));

	// 퇴장 애니메이션 재생 → 0.5초 후 실제 정리
	if (IsValid(ActivePuzzleWidget))
	{
		ActivePuzzleWidget->PlayCloseAnimation();

		// 애니메이션 완료 대기 후 정리 (SetTimerForNextTick 아닌 0.5초 딜레이)
		FTimerHandle CleanupHandle;
		GetWorldTimerManager().SetTimer(CleanupHandle, [this]()
		{
			if (IsValid(ActivePuzzleWidget))
			{
				ActivePuzzleWidget->RemoveFromParent();
				ActivePuzzleWidget = nullptr;
			}

			bInPuzzleMode = false;

			// 퍼즐 모드 전용 IMC 제거 (방향/회전/나가기 키 해제)
			if (ULocalPlayer* LP = GetLocalPlayer())
			{
				if (UEnhancedInputLocalPlayerSubsystem* Sub = LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
				{
					if (PuzzleModeMappingContext)
					{
						Sub->RemoveMappingContext(PuzzleModeMappingContext);
					}
					UE_LOG(LogTemp, Warning, TEXT("[PuzzleInput] PuzzleModeMappingContext removed (puzzle exit)"));
				}
			}

			SetInputMode(FInputModeGameOnly());

			// 해킹 모드 종료 (컬러 복원)
			SetDesaturation(1.f, 1.0f);
			bInHackMode = false;

			Server_PuzzleExit();
		}, 0.5f, false);
	}
	else
	{
		// 위젯 없으면 즉시 정리
		bInPuzzleMode = false;

		// 해킹 모드 종료
		SetDesaturation(1.f, 1.0f);
		bInHackMode = false;

		if (ULocalPlayer* LP = GetLocalPlayer())
		{
			if (UEnhancedInputLocalPlayerSubsystem* Sub = LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
			{
				if (PuzzleModeMappingContext)
				{
					Sub->RemoveMappingContext(PuzzleModeMappingContext);
				}
			}
		}

		SetInputMode(FInputModeGameOnly());

		Server_PuzzleExit();
	}
}

// --- RequestPuzzleRotateCell ---

void AHellunaHeroController::RequestPuzzleRotateCell(int32 CellIndex)
{
	Server_PuzzleRotateCell(CellIndex);
}

// --- Server_PuzzleTryEnter ---

bool AHellunaHeroController::Server_PuzzleTryEnter_Validate()
{
	return true;
}

void AHellunaHeroController::Server_PuzzleTryEnter_Implementation()
{
	APawn* MyPawn = GetPawn();
	if (!IsValid(MyPawn))
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// 서버 측 근처 퍼즐 큐브 탐색 (보안 검증)
	APuzzleCubeActor* NearestCube = nullptr;
	float NearestDist = FLT_MAX;

	for (TActorIterator<APuzzleCubeActor> It(World); It; ++It)
	{
		APuzzleCubeActor* Cube = *It;
		if (!IsValid(Cube))
		{
			continue;
		}

		const float Dist = FVector::Dist(MyPawn->GetActorLocation(), Cube->GetActorLocation());
		if (Dist < Cube->GetInteractionRadius() && Dist < NearestDist)
		{
			NearestDist = Dist;
			NearestCube = Cube;
		}
	}

	if (!NearestCube)
	{
		UE_LOG(LogTemp, Warning, TEXT("[PuzzleController] Server_PuzzleTryEnter: 근처에 PuzzleCube 없음"));
		return;
	}

	if (NearestCube->TryEnterPuzzle(this))
	{
		ServerPuzzleCube = NearestCube;
		Client_PuzzleEntered();
	}
}

// --- Server_PuzzleRotateCell ---

bool AHellunaHeroController::Server_PuzzleRotateCell_Validate(int32 CellIndex)
{
	return CellIndex >= 0 && CellIndex < 16;
}

void AHellunaHeroController::Server_PuzzleRotateCell_Implementation(int32 CellIndex)
{
	if (!ServerPuzzleCube.IsValid())
	{
		return;
	}

	ServerPuzzleCube->RotateCell(CellIndex);
}

// --- Server_PuzzleExit ---

bool AHellunaHeroController::Server_PuzzleExit_Validate()
{
	return true;
}

void AHellunaHeroController::Server_PuzzleExit_Implementation()
{
	if (ServerPuzzleCube.IsValid())
	{
		ServerPuzzleCube->ExitPuzzle(this);
		ServerPuzzleCube = nullptr;
	}
}

// --- Client_PuzzleEntered ---

void AHellunaHeroController::Client_PuzzleEntered_Implementation()
{
	if (bInPuzzleMode)
	{
		return;
	}

	if (!PuzzleGridWidgetClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("[PuzzleController] PuzzleGridWidgetClass가 설정되지 않음 — BP에서 설정 필요"));
		return;
	}

	if (!LocalTargetPuzzleCube.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("[PuzzleController] LocalTargetPuzzleCube가 null"));
		return;
	}

	// 기존 위젯 제거
	if (IsValid(ActivePuzzleWidget))
	{
		ActivePuzzleWidget->RemoveFromParent();
		ActivePuzzleWidget = nullptr;
	}

	// 위젯 생성
	ActivePuzzleWidget = CreateWidget<UPuzzleGridWidget>(this, PuzzleGridWidgetClass);
	if (!IsValid(ActivePuzzleWidget))
	{
		UE_LOG(LogTemp, Error, TEXT("[PuzzleController] 퍼즐 위젯 생성 실패!"));
		return;
	}

	ActivePuzzleWidget->AddToViewport(50);
	ActivePuzzleWidget->InitGrid(LocalTargetPuzzleCube.Get());

	bInPuzzleMode = true;

	// 퍼즐 모드 전용 IMC 추가 (방향/회전/나가기 키, priority=100으로 일반 키 우선 소비)
	if (ULocalPlayer* LP = GetLocalPlayer())
	{
		if (UEnhancedInputLocalPlayerSubsystem* Sub = LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
		{
			if (PuzzleModeMappingContext)
			{
				Sub->AddMappingContext(PuzzleModeMappingContext, 100);
			}
			UE_LOG(LogTemp, Warning, TEXT("[PuzzleInput] PuzzleModeMappingContext added (priority=100, puzzle mode)"));
		}
	}

	// Enhanced Input이 처리하므로 위젯 포커스 불필요
	// GameOnly 모드: 마우스 클릭 후에도 Enhanced Input이 방향키를 받음
	SetInputMode(FInputModeGameOnly());

	bInHackMode = true;

	UE_LOG(LogTemp, Log, TEXT("[PuzzleController] 퍼즐 모드 진입 완료"));
}

// --- Client_PuzzleForceExit ---

void AHellunaHeroController::Client_PuzzleForceExit_Implementation()
{
	UE_LOG(LogTemp, Warning, TEXT("[PuzzleController] Client_PuzzleForceExit: 서버에 의한 강제 퇴출"));

	if (IsValid(ActivePuzzleWidget))
	{
		ActivePuzzleWidget->RemoveFromParent();
		ActivePuzzleWidget = nullptr;
	}

	bInPuzzleMode = false;

	// 퍼즐 모드 전용 IMC 제거
	if (ULocalPlayer* LP = GetLocalPlayer())
	{
		if (UEnhancedInputLocalPlayerSubsystem* Sub = LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
		{
			if (PuzzleModeMappingContext)
			{
				Sub->RemoveMappingContext(PuzzleModeMappingContext);
			}
		}
	}

	SetInputMode(FInputModeGameOnly());
}

// --- Enhanced Input: 퍼즐 방향키 (개별 Boolean) ---

void AHellunaHeroController::OnPuzzleUpInput(const FInputActionValue& Value)
{
	if (!bInPuzzleMode || !IsValid(ActivePuzzleWidget)) { return; }
	UE_LOG(LogTemp, Warning, TEXT("[PuzzleInput] Up"));
	ActivePuzzleWidget->MoveSelection(FIntPoint(-1, 0));
}

void AHellunaHeroController::OnPuzzleDownInput(const FInputActionValue& Value)
{
	if (!bInPuzzleMode || !IsValid(ActivePuzzleWidget)) { return; }
	UE_LOG(LogTemp, Warning, TEXT("[PuzzleInput] Down"));
	ActivePuzzleWidget->MoveSelection(FIntPoint(1, 0));
}

void AHellunaHeroController::OnPuzzleLeftInput(const FInputActionValue& Value)
{
	if (!bInPuzzleMode || !IsValid(ActivePuzzleWidget)) { return; }
	UE_LOG(LogTemp, Warning, TEXT("[PuzzleInput] Left"));
	ActivePuzzleWidget->MoveSelection(FIntPoint(0, -1));
}

void AHellunaHeroController::OnPuzzleRightInput(const FInputActionValue& Value)
{
	if (!bInPuzzleMode || !IsValid(ActivePuzzleWidget)) { return; }
	UE_LOG(LogTemp, Warning, TEXT("[PuzzleInput] Right"));
	ActivePuzzleWidget->MoveSelection(FIntPoint(0, 1));
}

// --- Enhanced Input: 퍼즐 회전 ---

void AHellunaHeroController::OnPuzzleRotateInput(const FInputActionValue& Value)
{
	if (!bInPuzzleMode || !IsValid(ActivePuzzleWidget))
	{
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("[PuzzleInput] OnPuzzleRotateInput: Rotating cell (%d,%d)"),
		ActivePuzzleWidget->GetSelectedRow(), ActivePuzzleWidget->GetSelectedCol());

	ActivePuzzleWidget->RotateSelectedCell();
}

// --- Enhanced Input: 퍼즐 나가기 ---

void AHellunaHeroController::OnPuzzleExitInput(const FInputActionValue& Value)
{
	if (!bInPuzzleMode)
	{
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("[PuzzleInput] OnPuzzleExitInput: Exiting puzzle"));
	ExitPuzzle();
}

void AHellunaHeroController::OnPuzzleInteractOngoing(const FInputActionValue& Value)
{
	bHoldingPuzzleInteract = true;
}

void AHellunaHeroController::OnPuzzleInteractReleased(const FInputActionValue& Value)
{
	bHoldingPuzzleInteract = false;
}

// ============================================================================
// 해킹 모드 — PostProcess Desaturation
// ============================================================================

void AHellunaHeroController::SetDesaturation(float InTargetValue, float Duration)
{
	TargetSaturation = FMath::Clamp(InTargetValue, 0.f, 1.f);

	if (Duration <= 0.f)
	{
		CurrentSaturation = TargetSaturation;
		SaturationLerpSpeed = 0.f;
	}
	else
	{
		SaturationLerpSpeed = FMath::Abs(TargetSaturation - CurrentSaturation) / Duration;
	}

	UE_LOG(LogTemp, Warning, TEXT("[HackMode] SetDesaturation: target=%.2f, duration=%.1fs, current=%.2f"),
		TargetSaturation, Duration, CurrentSaturation);
}

void AHellunaHeroController::SetDesaturationByProgress(float HoldProgress)
{
	// F 홀드 프로그레스(0~1)를 Saturation(1~0)으로 매핑
	CurrentSaturation = 1.f - FMath::Clamp(HoldProgress, 0.f, 1.f);
	TargetSaturation = CurrentSaturation;
	SaturationLerpSpeed = 0.f; // 즉시 적용

	if (DesaturationPostProcess)
	{
		FPostProcessSettings& S = DesaturationPostProcess->Settings;
		S.ColorSaturation = FVector4(CurrentSaturation, CurrentSaturation, CurrentSaturation, 1.f);

		const float ContrastBoost = 1.f + (1.f - CurrentSaturation) * 0.2f;
		S.ColorContrast = FVector4(ContrastBoost, ContrastBoost, ContrastBoost, 1.f);

		S.VignetteIntensity = (1.f - CurrentSaturation) * 0.6f;
	}
}

void AHellunaHeroController::TickDesaturation(float DeltaTime)
{
	if (!DesaturationPostProcess) { return; }
	// 색채의 개방 연출 중에는 TickColorReveal이 PostProcess를 직접 제어
	if (bPlayingColorReveal) { return; }
	if (FMath::IsNearlyEqual(CurrentSaturation, TargetSaturation, 0.001f))
	{
		CurrentSaturation = TargetSaturation;
		return;
	}

	// Lerp
	if (CurrentSaturation < TargetSaturation)
	{
		CurrentSaturation = FMath::Min(CurrentSaturation + SaturationLerpSpeed * DeltaTime, TargetSaturation);
	}
	else
	{
		CurrentSaturation = FMath::Max(CurrentSaturation - SaturationLerpSpeed * DeltaTime, TargetSaturation);
	}

	FPostProcessSettings& S = DesaturationPostProcess->Settings;
	S.ColorSaturation = FVector4(CurrentSaturation, CurrentSaturation, CurrentSaturation, 1.f);

	const float ContrastBoost = 1.f + (1.f - CurrentSaturation) * 0.2f;
	S.ColorContrast = FVector4(ContrastBoost, ContrastBoost, ContrastBoost, 1.f);

	S.VignetteIntensity = (1.f - CurrentSaturation) * 0.6f;
}

// ============================================================================
// 색채의 개방 — 순백 섬광 → 페이드아웃 → 흑백에서 컬러 복원
// ============================================================================

void AHellunaHeroController::PlayColorReveal()
{
	if (!DesaturationPostProcess) { return; }

	// E키 퇴출 시 bInHackMode가 이미 false → 플래시 스킵, 기존 SetDesaturation으로 처리됨
	if (!bInHackMode)
	{
		UE_LOG(LogTemp, Log, TEXT("[HackMode] PlayColorReveal skipped — not in hack mode (E key exit path)"));
		return;
	}

	bPlayingColorReveal = true;
	ColorRevealProgress = 0.f;
	bColorRevealVFXSpawned = false;

	// 즉시 섬광 피크
	FPostProcessSettings& S = DesaturationPostProcess->Settings;
	S.bOverride_SceneColorTint = true;
	S.SceneColorTint = FLinearColor(6.f, 6.f, 6.f, 1.f);
	S.VignetteIntensity = 0.f;
	// Saturation은 아직 흑백 유지
	S.ColorSaturation = FVector4(0.f, 0.f, 0.f, 1.f);

	UE_LOG(LogTemp, Warning, TEXT("[HackMode] PlayColorReveal — flash!"));
}

void AHellunaHeroController::TickColorReveal(float DeltaTime)
{
	if (!bPlayingColorReveal) { return; }
	if (!DesaturationPostProcess) { return; }

	ColorRevealProgress += DeltaTime;
	FPostProcessSettings& S = DesaturationPostProcess->Settings;

	if (ColorRevealProgress <= 0.3f)
	{
		// [0~0.3초] 섬광 유지
		S.SceneColorTint = FLinearColor(6.f, 6.f, 6.f, 1.f);
		S.ColorSaturation = FVector4(0.f, 0.f, 0.f, 1.f); // 아직 흑백
	}
	else if (ColorRevealProgress <= 1.8f)
	{
		// [0.3~1.8초] 빛 페이드아웃 + 컬러 복원 (동시 진행)
		const float Phase = (ColorRevealProgress - 0.3f) / 1.5f; // 0→1
		const float EasePhase = Phase * Phase * (3.f - 2.f * Phase); // smoothstep

		// 빛: 6 → 1 (페이드아웃)
		const float Tint = FMath::Lerp(6.f, 1.f, EasePhase);
		S.SceneColorTint = FLinearColor(Tint, Tint, Tint, 1.f);

		// Niagara VFX spawn (1 time only, at fade-out start)
		if (!bColorRevealVFXSpawned && ColorRevealVFX)
		{
			bColorRevealVFXSpawned = true;
			if (APawn* MyPawn = GetPawn())
			{
				UNiagaraFunctionLibrary::SpawnSystemAtLocation(
					GetWorld(), ColorRevealVFX,
					MyPawn->GetActorLocation() + FVector(0.f, 0.f, 100.f),
					MyPawn->GetActorRotation());
				UE_LOG(LogTemp, Warning, TEXT("[HackMode] ColorReveal VFX spawned (during fade-out)"));
			}
		}

		// 흑백 → 컬러 (빛 뒤에서 드러남)
		const float Sat = FMath::Lerp(0.f, 1.f, EasePhase);
		S.ColorSaturation = FVector4(Sat, Sat, Sat, 1.f);

		// 콘트라스트 정상화
		const float Con = FMath::Lerp(1.2f, 1.f, EasePhase);
		S.ColorContrast = FVector4(Con, Con, Con, 1.f);

		CurrentSaturation = Sat;
	}
	else
	{
		// [1.8초+] 완료
		FinishColorReveal();
	}
}

void AHellunaHeroController::FinishColorReveal()
{
	bPlayingColorReveal = false;

	if (DesaturationPostProcess)
	{
		FPostProcessSettings& S = DesaturationPostProcess->Settings;
		S.SceneColorTint = FLinearColor(1.f, 1.f, 1.f, 1.f);
		S.bOverride_SceneColorTint = false;
		S.ColorSaturation = FVector4(1.f, 1.f, 1.f, 1.f);
		S.ColorContrast = FVector4(1.f, 1.f, 1.f, 1.f);
		S.VignetteIntensity = 0.f;
	}

	CurrentSaturation = 1.f;
	TargetSaturation = 1.f;
	SaturationLerpSpeed = 0.f;
	bInHackMode = false;

	UE_LOG(LogTemp, Warning, TEXT("[HackMode] ColorReveal finished — full color restored"));
}

// =========================================================================================
// [BossEvent] Server_BossEncounterActivate
// =========================================================================================

bool AHellunaHeroController::Server_BossEncounterActivate_Validate()
{
	return true;
}

void AHellunaHeroController::Server_BossEncounterActivate_Implementation()
{
	APawn* MyPawn = GetPawn();
	if (!IsValid(MyPawn))
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// 서버 측 근처 BossEncounterCube 탐색 (보안 검증)
	ABossEncounterCube* NearestCube = nullptr;
	float NearestDist = FLT_MAX;

	for (TActorIterator<ABossEncounterCube> It(World); It; ++It)
	{
		ABossEncounterCube* Cube = *It;
		if (!IsValid(Cube))
		{
			continue;
		}

		const float Dist = FVector::Dist(MyPawn->GetActorLocation(), Cube->GetActorLocation());
		if (Dist < Cube->GetInteractionRadius() && Dist < NearestDist)
		{
			NearestDist = Dist;
			NearestCube = Cube;
		}
	}

	if (!NearestCube)
	{
		UE_LOG(LogTemp, Warning, TEXT("[BossEvent] Server_BossEncounterActivate: 근처에 BossEncounterCube 없음"));
		return;
	}

	if (NearestCube->TryActivate(this))
	{
		UE_LOG(LogTemp, Warning, TEXT("[BossEvent] BossEncounterCube activated by %s"), *GetName());
		// TODO: Step 3+ — 보스 스폰 매니저 호출, 이벤트 시작
	}
}

// =========================================================================================
// [DebugHUD] 디버그 HUD 생성 및 F5 토글
// =========================================================================================

void AHellunaHeroController::CreateDebugHUD()
{
#if UE_BUILD_SHIPPING
	return; // Shipping 빌드에서는 생성하지 않음
#else
	if (!IsValid(DebugHUDWidgetClass))
	{
		UE_LOG(LogTemp, Warning, TEXT("[DebugHUD] DebugHUDWidgetClass 미설정 — BP_HellunaHeroController에서 지정 필요"));
		return;
	}

	if (IsValid(DebugHUDInstance))
	{
		return; // 이미 생성됨
	}

	DebugHUDInstance = CreateWidget<UHellunaDebugHUDWidget>(this, DebugHUDWidgetClass);
	if (IsValid(DebugHUDInstance))
	{
		DebugHUDInstance->AddToViewport(999); // 최상위 Z-Order
		UE_LOG(LogTemp, Log, TEXT("[DebugHUD] 디버그 HUD 생성 완료 (ZOrder=999)"));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[DebugHUD] 디버그 HUD 생성 실패"));
	}
#endif
}

void AHellunaHeroController::OnDebugHUDToggle(const FInputActionValue& Value)
{
	if (IsValid(DebugHUDInstance))
	{
		DebugHUDInstance->ToggleVisibility();
	}
}

// =========================================================================================
// [PauseMenu] Enhanced Input 핸들러
// =========================================================================================

void AHellunaHeroController::OnPauseMenuToggleInput(const FInputActionValue& Value)
{
	TogglePauseMenu();
}

// =========================================================================================
// [PauseMenu] 일시정지 메뉴 위젯 토글
// =========================================================================================

void AHellunaHeroController::TogglePauseMenu()
{
	// 서버 측 PlayerController에서는 위젯 생성 불가 — 로컬만 허용
	if (!IsLocalController()) return;

	// 이미 열려 있으면 즉시 닫기
	if (IsValid(PauseMenuInstance))
	{
		PauseMenuInstance->RemoveFromParent();
		PauseMenuInstance = nullptr;
		SetShowMouseCursor(false);
		SetInputMode(FInputModeGameOnly());
		UE_LOG(LogTemp, Log, TEXT("[PauseMenu] 위젯 닫기"));
		return;
	}

	// 위젯 클래스 미설정 시 경고
	if (!IsValid(PauseMenuWidgetClass))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[PauseMenu] PauseMenuWidgetClass 미설정 — BP_HellunaHeroController에서 지정 필요"));
		return;
	}

	// 생성 및 표시 (PlayerController 소유 — GetOwningPlayer() 정상 동작)
	PauseMenuInstance = CreateWidget<UHellunaPauseMenuWidget>(this, PauseMenuWidgetClass);
	if (IsValid(PauseMenuInstance))
	{
		PauseMenuInstance->AddToViewport(200);

		// SlideIn 애니메이션 재생
		PauseMenuInstance->PlayOpenAnimation();

		// 마우스 커서 표시 + UI 입력 허용 (게임 입력도 유지)
		SetShowMouseCursor(true);
		FInputModeGameAndUI InputMode;
		InputMode.SetWidgetToFocus(PauseMenuInstance->TakeWidget());
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		SetInputMode(InputMode);

		UE_LOG(LogTemp, Log, TEXT("[PauseMenu] 위젯 열기 — 클래스: %s"),
			*PauseMenuInstance->GetClass()->GetName());
	}
}

void AHellunaHeroController::ClearPauseMenuInstance()
{
	PauseMenuInstance = nullptr;
	SetShowMouseCursor(false);
	SetInputMode(FInputModeGameOnly());
	UE_LOG(LogTemp, Log, TEXT("[PauseMenu] 인스턴스 해제 완료"));
}

void AHellunaHeroController::ClearPauseMenuInstanceOnly()
{
	PauseMenuInstance = nullptr;
	UE_LOG(LogTemp, Log, TEXT("[PauseMenu] 인스턴스 레퍼런스만 해제 (커서/입력 유지)"));
}

// =========================================================================================
// [GraphicsSettings] 그래픽 설정 위젯 토글
// =========================================================================================

void AHellunaHeroController::ToggleGraphicsSettings()
{
	// 이미 열려 있으면 닫기
	if (IsValid(GraphicsSettingsInstance))
	{
		GraphicsSettingsInstance->RemoveFromParent();
		GraphicsSettingsInstance = nullptr;

		// 마우스 커서 숨기기 + 게임 입력 복귀
		SetShowMouseCursor(false);
		SetInputMode(FInputModeGameOnly());

		UE_LOG(LogTemp, Log, TEXT("[GraphicsSettings] 위젯 닫기"));
		return;
	}

	// 위젯 클래스 미설정 시 경고
	if (!IsValid(GraphicsSettingsWidgetClass))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[GraphicsSettings] GraphicsSettingsWidgetClass 미설정 — BP_HellunaHeroController에서 지정 필요"));
		return;
	}

	// 생성 및 표시 — GetGameInstance() 사용 (데디케이티드 서버 구조에서 LocalPlayerController 검증 우회)
	UGameInstance* GI = GetGameInstance();
	if (!IsValid(GI)) return;

	GraphicsSettingsInstance = CreateWidget<UHellunaGraphicsSettingsWidget>(GI, GraphicsSettingsWidgetClass);
	if (IsValid(GraphicsSettingsInstance))
	{
		GraphicsSettingsInstance->AddToViewport(100);

		// 마우스 커서 표시 + UI 입력 허용 (게임 입력도 유지)
		SetShowMouseCursor(true);
		FInputModeGameAndUI InputMode;
		InputMode.SetWidgetToFocus(GraphicsSettingsInstance->TakeWidget());
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		SetInputMode(InputMode);

		UE_LOG(LogTemp, Log, TEXT("[GraphicsSettings] 위젯 열기"));
	}
}

// ============================================================================
// [WorldMap] M키 월드맵 토글
// ============================================================================

void AHellunaHeroController::OnToggleWorldMapInput(const FInputActionValue& Value)
{
	UE_LOG(LogTemp, Warning, TEXT("[WorldMap] OnToggleWorldMapInput 호출됨 IsLocal=%d"), IsLocalController() ? 1 : 0);

	if (!IsLocalController())
	{
		return;
	}

	if (!WorldMapWidgetInstance && WorldMapWidgetClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("[WorldMap] 최초 위젯 생성: %s"), *WorldMapWidgetClass->GetName());
		WorldMapWidgetInstance = CreateWidget<UHellunaWorldMapWidget>(this, WorldMapWidgetClass);
		if (WorldMapWidgetInstance)
		{
			WorldMapWidgetInstance->AddToViewport(100);
			UE_LOG(LogTemp, Warning, TEXT("[WorldMap] 위젯 생성+AddToViewport 완료"));
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("[WorldMap] CreateWidget 실패!"));
		}
	}
	else if (!WorldMapWidgetClass)
	{
		UE_LOG(LogTemp, Error, TEXT("[WorldMap] WorldMapWidgetClass가 NULL — BP_HellunaHeroController에 할당 필요"));
	}

	if (!WorldMapWidgetInstance)
	{
		UE_LOG(LogTemp, Error, TEXT("[WorldMap] WorldMapWidgetInstance NULL — return"));
		return;
	}

	if (WorldMapWidgetInstance->IsMapOpen())
	{
		UE_LOG(LogTemp, Warning, TEXT("[WorldMap] CloseMap 호출"));
		WorldMapWidgetInstance->CloseMap();
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[WorldMap] OpenMap 호출"));
		WorldMapWidgetInstance->OpenMap();
	}
}

// ============================================================================
// [WorldMap] 핑 설정/해제 (서버 권위 복제, 팀 공유, 0.1초 쿨다운)
// ============================================================================

void AHellunaHeroController::Server_SetWorldPing_Implementation(const FVector& WorldLocation)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const double Now = World->GetTimeSeconds();
	if (Now - LastPingServerTime < PingCooldownSeconds)
	{
		return; // 0.1초 쿨다운 — 드롭
	}
	LastPingServerTime = Now;

	AHellunaPlayerState* HPS = GetPlayerState<AHellunaPlayerState>();
	if (!HPS)
	{
		return;
	}
	HPS->Server_AuthoritativeSetPing(WorldLocation);
}

void AHellunaHeroController::Server_ClearWorldPing_Implementation()
{
	AHellunaPlayerState* HPS = GetPlayerState<AHellunaPlayerState>();
	if (!HPS)
	{
		return;
	}
	HPS->Server_AuthoritativeClearPing();
}

// ════════════════════════════════════════════════════════════════════════════════
// [Loading Barrier] 클라이언트 RPC + Ready 조건 폴링 (Reedme/loading/04, 08)
// ════════════════════════════════════════════════════════════════════════════════

bool AHellunaHeroController::TryActivateLoadingCamera()
{
	if (!IsLocalController())
	{
		return false;
	}

	ACameraActor* Cam = LoadingCameraActor.Get();
	if (!Cam)
	{
		UWorld* World = GetWorld();
		if (!World) { return false; }

		TArray<AActor*> Found;
		UGameplayStatics::GetAllActorsOfClassWithTag(World, ACameraActor::StaticClass(), LoadingCameraTag, Found);
		for (AActor* A : Found)
		{
			if (ACameraActor* C = Cast<ACameraActor>(A))
			{
				Cam = C;
				break;
			}
		}
		LoadingCameraActor = Cam;
	}

	if (!Cam)
	{
		UE_LOG(LogHelluna, Warning, TEXT("[Barrier] 관람 카메라를 찾지 못함 (Tag=%s)"), *LoadingCameraTag.ToString());
		return false;
	}

	SetViewTargetWithBlend(Cam, 0.f);
	return true;
}

void AHellunaHeroController::Client_EnterLoadingScene_Implementation(const TArray<FString>& ExpectedIds, int32 PartyId)
{
	UWorld* World = GetWorld();
	UE_LOG(LogHelluna, Warning,
		TEXT("[LoadingDbg][EnterScene] ENTER | PC=%s | IsLocal=%d | World=%p | ViewportClient=%p | Expected=%d | PartyId=%d | bInLoadingScene(prev)=%d | LoadingHUDInst(prev)=%p"),
		*GetNameSafe(this), IsLocalController() ? 1 : 0,
		World, World ? World->GetGameViewport() : nullptr,
		ExpectedIds.Num(), PartyId,
		bInLoadingScene ? 1 : 0, LoadingHUDWidgetInstance.Get());

	if (!IsLocalController())
	{
		UE_LOG(LogHelluna, Warning, TEXT("[LoadingDbg][EnterScene] EARLY RETURN — !IsLocalController"));
		return;
	}

	// 만약 잔존 인스턴스가 있다면 안전 정리 (재호출 시 원본 방치 방지)
	if (LoadingHUDWidgetInstance)
	{
		UE_LOG(LogHelluna, Warning,
			TEXT("[LoadingDbg][EnterScene] 잔존 LoadingHUDWidgetInstance 감지(%p) — 강제 RemoveFromParent + null"),
			LoadingHUDWidgetInstance.Get());
		LoadingHUDWidgetInstance->SetVisibility(ESlateVisibility::Collapsed);
		LoadingHUDWidgetInstance->RemoveFromParent();
		LoadingHUDWidgetInstance = nullptr;
	}

	bInLoadingScene = true;
	bMyReadyReported = false;
	CachedExpectedIds = ExpectedIds;
	CachedBarrierPartyId = PartyId;
	PollLogTickCounter = 0;

	TryActivateLoadingCamera();

	// §13 v2.1 §3.3.4 Phase 2c — A→C 핸드오프: Ship 위상/Amp 복원
	UMDF_GameInstance* GI = GetGameInstance() ? Cast<UMDF_GameInstance>(GetGameInstance()) : nullptr;
	const bool bHasHandoff = (GI && GI->bHasSavedShipPose);
	if (bHasHandoff && World)
	{
		TArray<AActor*> FoundShips;
		UGameplayStatics::GetAllActorsOfClassWithTag(World, AHellunaLoadingShipActor::StaticClass(),
			LoadingShipTag, FoundShips);
		for (AActor* A : FoundShips)
		{
			if (AHellunaLoadingShipActor* Ship = Cast<AHellunaLoadingShipActor>(A))
			{
				Ship->ApplyPoseSnapshot(GI->SavedShipTimeAccum, GI->SavedShipAmpScale);
				UE_LOG(LogHelluna, Warning,
					TEXT("[LoadingDbg][HeroPC][C] Ship ApplyPoseSnapshot | TimeAccum=%.3f | AmpScale=%.3f"),
					GI->SavedShipTimeAccum, GI->SavedShipAmpScale);
				break;
			}
		}
		UE_LOG(LogHelluna, Warning,
			TEXT("[LoadingDbg][HeroPC][C] Ship 핸드오프 적용 시도 | Found=%d"), FoundShips.Num());
	}
	else
	{
		UE_LOG(LogHelluna, Warning,
			TEXT("[LoadingDbg][HeroPC][C] Ship 핸드오프 없음 (콜드 부트 또는 GI null) | GI=%p | bHasSavedShipPose=%d"),
			GI, GI ? (GI->bHasSavedShipPose ? 1 : 0) : 0);
	}

	SetIgnoreMoveInput(true);
	SetIgnoreLookInput(true);

	if (LoadingHUDWidgetClass && !LoadingHUDWidgetInstance)
	{
		LoadingHUDWidgetInstance = CreateWidget<UHellunaLoadingHUDWidget>(this, LoadingHUDWidgetClass);
		UE_LOG(LogHelluna, Warning,
			TEXT("[LoadingDbg][EnterScene] CreateWidget 결과 → Inst=%p | Class=%s"),
			LoadingHUDWidgetInstance.Get(),
			*GetNameSafe(LoadingHUDWidgetClass.Get()));
		if (LoadingHUDWidgetInstance)
		{
			LoadingHUDWidgetInstance->AddToViewport(100);
			UE_LOG(LogHelluna, Warning,
				TEXT("[LoadingDbg][EnterScene] AddToViewport(100) | IsInViewport=%d"),
				LoadingHUDWidgetInstance->IsInViewport() ? 1 : 0);
		}
	}
	else
	{
		UE_LOG(LogHelluna, Warning,
			TEXT("[LoadingDbg][EnterScene] CreateWidget 스킵 | Class=%s | InstStill=%p"),
			*GetNameSafe(LoadingHUDWidgetClass.Get()), LoadingHUDWidgetInstance.Get());
	}

	FString LocalPlayerId;
	if (AHellunaPlayerState* HPS = GetPlayerState<AHellunaPlayerState>())
	{
		LocalPlayerId = HPS->GetPlayerUniqueId();
	}
	if (LoadingHUDWidgetInstance)
	{
		LoadingHUDWidgetInstance->InitializeHUD(ExpectedIds, LocalPlayerId, PartyId);

		// §13 v2.1 §3.3.4 Phase 2c — HUD 초기 상태: 게임 단계 + (핸드오프 시) Saved Progress 이어받기
		LoadingHUDWidgetInstance->SetIsLobbyStage(false);
		if (bHasHandoff)
		{
			LoadingHUDWidgetInstance->SetInitialFakeProgress(GI->SavedFakeProgress);
			UE_LOG(LogHelluna, Warning,
				TEXT("[LoadingDbg][HeroPC][C] HUD 핸드오프 | SetInitialFakeProgress=%.3f"),
				GI->SavedFakeProgress);
		}
	}

	if (World)
	{
		World->GetTimerManager().SetTimer(ReadyPollTimerHandle, this,
			&AHellunaHeroController::PollReadyConditions,
			FMath::Max(ReadyPollInterval, 0.05f), true);

		// SoftTimeout 자가 보고 안전망 (WP hang 등 방어)
		const float SoftTimeout = FMath::Max(ClientSoftTimeoutSeconds, 1.0f);
		World->GetTimerManager().SetTimer(SoftTimeoutTimerHandle, this,
			&AHellunaHeroController::OnSoftTimeoutFired,
			SoftTimeout, false);

		UE_LOG(LogHelluna, Warning,
			TEXT("[LoadingDbg][EnterScene] 타이머 시작 | PollInterval=%.2fs | SoftTimeout=%.2fs"),
			FMath::Max(ReadyPollInterval, 0.05f), SoftTimeout);

		// §13 v2.1 §3.3.4 Phase 2f — 0.02s 후 MoviePlayer StopMovie (Slate Snapshot ↔ UMG HUD 크로스페이드)
		if (bHasHandoff)
		{
			FTimerHandle StopMovieHandle;
			World->GetTimerManager().SetTimer(StopMovieHandle,
				FTimerDelegate::CreateLambda([]()
				{
					if (GetMoviePlayer() && GetMoviePlayer()->IsMovieCurrentlyPlaying())
					{
						GetMoviePlayer()->StopMovie();
						UE_LOG(LogHelluna, Warning, TEXT("[LoadingDbg][HeroPC][C] MoviePlayer StopMovie 호출 (크로스페이드)"));
					}
					else
					{
						UE_LOG(LogHelluna, Warning, TEXT("[LoadingDbg][HeroPC][C] StopMovie 스킵 — MoviePlayer 미재생"));
					}
				}),
				0.02f, false);

			// §13 v2.1 §3.3.4 Phase 2g — 0.17s 후 Saved* 정리 (스냅샷 텍스처 GC)
			TWeakObjectPtr<UMDF_GameInstance> WeakGI = GI;
			FTimerHandle ClearHandoffHandle;
			World->GetTimerManager().SetTimer(ClearHandoffHandle,
				FTimerDelegate::CreateLambda([WeakGI]()
				{
					if (UMDF_GameInstance* PinnedGI = WeakGI.Get())
					{
						PinnedGI->LoadingSnapshotTexture = nullptr;
						PinnedGI->ClearLoadingHandoffState();
						UE_LOG(LogHelluna, Warning, TEXT("[LoadingDbg][HeroPC][C] Snapshot/Handoff 상태 해제"));
					}
				}),
				0.17f, false);
		}
	}

	UE_LOG(LogHelluna, Warning,
		TEXT("[LoadingDbg][EnterScene] EXIT | LoadingHUDInst(post)=%p | LocalId=%s | bHasHandoff=%d"),
		LoadingHUDWidgetInstance.Get(), *LocalPlayerId, bHasHandoff ? 1 : 0);
}

bool AHellunaHeroController::IsClientReadyForBarrier() const
{
	// (옵션 A 적용) WP IsStreamingCompleted + StreamingLevels Pending 검사 제거
	// 사유: MainMap WP에서 영원히 false → 60s HardTimeout 강제 진입. PlayerState/HUD/ViewTarget 3종으로 충분.
	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogHelluna, Warning, TEXT("[LoadingDbg][Ready?] FALSE | reason=World null"));
		return false;
	}

	// (1) 로컬 PlayerState Replicate 완료
	AHellunaPlayerState* HPS = GetPlayerState<AHellunaPlayerState>();
	if (!HPS || HPS->GetPlayerUniqueId().IsEmpty())
	{
		// 빈번 호출이라 throttle: 매 4번째 false에서만 로그
		if ((PollLogTickCounter % 4) == 0)
		{
			UE_LOG(LogHelluna, Warning,
				TEXT("[LoadingDbg][Ready?] FALSE | reason=PlayerState | HPS=%p | Id=%s"),
				HPS, HPS ? *HPS->GetPlayerUniqueId() : TEXT(""));
		}
		return false;
	}

	// (2) HUD 표시 확인
	if (!LoadingHUDWidgetInstance)
	{
		if ((PollLogTickCounter % 4) == 0)
		{
			UE_LOG(LogHelluna, Warning, TEXT("[LoadingDbg][Ready?] FALSE | reason=LoadingHUDInst null"));
		}
		return false;
	}

	// (3) ViewTarget 이 관람 카메라인지 (선택적 — 카메라 미배치 환경에선 자동 통과)
	if (APlayerCameraManager* CamMgr = PlayerCameraManager)
	{
		if (LoadingCameraActor.IsValid() && CamMgr->GetViewTarget() != LoadingCameraActor.Get())
		{
			if ((PollLogTickCounter % 4) == 0)
			{
				UE_LOG(LogHelluna, Warning,
					TEXT("[LoadingDbg][Ready?] FALSE | reason=ViewTarget | VT=%s | LoadingCam=%s"),
					*GetNameSafe(CamMgr->GetViewTarget()),
					*GetNameSafe(LoadingCameraActor.Get()));
			}
			return false;
		}
	}

	UE_LOG(LogHelluna, Warning, TEXT("[LoadingDbg][Ready?] TRUE — 모든 조건 충족"));
	return true;
}

void AHellunaHeroController::PollReadyConditions()
{
	++PollLogTickCounter;

	if (!bInLoadingScene || bMyReadyReported)
	{
		UE_LOG(LogHelluna, Warning,
			TEXT("[LoadingDbg][Poll] STOP | bInLoadingScene=%d | bMyReadyReported=%d"),
			bInLoadingScene ? 1 : 0, bMyReadyReported ? 1 : 0);
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(ReadyPollTimerHandle);
		}
		return;
	}

	if (!IsClientReadyForBarrier())
	{
		return;
	}

	bMyReadyReported = true;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ReadyPollTimerHandle);
		World->GetTimerManager().ClearTimer(SoftTimeoutTimerHandle);
	}

	if (LoadingHUDWidgetInstance)
	{
		LoadingHUDWidgetInstance->UpdateMyProgress(1.f);
		LoadingHUDWidgetInstance->TransitionToPhase2();
	}

	UE_LOG(LogHelluna, Warning,
		TEXT("[LoadingDbg][Poll] Ready 조건 충족 → Server_ReportClientReady (정상 경로) | TickCount=%d"),
		PollLogTickCounter);
	Server_ReportClientReady();
}

// ============================================
// SoftTimeout — N초 안에 Ready 못 보내면 강제 자가 보고 (WP hang 등 안전망)
// ============================================
void AHellunaHeroController::OnSoftTimeoutFired()
{
	UE_LOG(LogHelluna, Warning,
		TEXT("[LoadingDbg][SoftTimeout] FIRED | bInLoadingScene=%d | bMyReadyReported=%d | TickCount=%d"),
		bInLoadingScene ? 1 : 0, bMyReadyReported ? 1 : 0, PollLogTickCounter);

	if (!bInLoadingScene || bMyReadyReported)
	{
		UE_LOG(LogHelluna, Warning, TEXT("[LoadingDbg][SoftTimeout] 무시 — 이미 종료/보고됨"));
		return;
	}

	bMyReadyReported = true;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ReadyPollTimerHandle);
		World->GetTimerManager().ClearTimer(SoftTimeoutTimerHandle);
	}

	if (LoadingHUDWidgetInstance)
	{
		LoadingHUDWidgetInstance->UpdateMyProgress(1.f);
		LoadingHUDWidgetInstance->TransitionToPhase2();
	}

	UE_LOG(LogHelluna, Warning, TEXT("[LoadingDbg][SoftTimeout] 강제 Server_ReportClientReady 호출"));
	Server_ReportClientReady();
}

bool AHellunaHeroController::Server_ReportClientReady_Validate()
{
	return true;
}

void AHellunaHeroController::Server_ReportClientReady_Implementation()
{
	AHellunaPlayerState* PS = GetPlayerState<AHellunaPlayerState>();
	if (!PS) { return; }

	const FString PlayerId = PS->GetPlayerUniqueId();
	if (PlayerId.IsEmpty()) { return; }

	AHellunaDefenseGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<AHellunaDefenseGameMode>() : nullptr;
	if (!GM) { return; }

	GM->OnClientReportedReady(this, PlayerId);
}

void AHellunaHeroController::Client_DeliverPartyStatus_Implementation(const TArray<FHellunaReadyInfo>& Snapshot)
{
	if (!IsLocalController() || !bMyReadyReported)
	{
		return;
	}
	if (!LoadingHUDWidgetInstance)
	{
		return;
	}
	for (const FHellunaReadyInfo& Info : Snapshot)
	{
		LoadingHUDWidgetInstance->UpdateSlot(Info.PlayerId, Info.bReady);
	}
}

void AHellunaHeroController::Client_UpdateReadyStatus_Implementation(const FString& PlayerId, bool bReady)
{
	if (!IsLocalController() || !bMyReadyReported)
	{
		return;
	}
	if (LoadingHUDWidgetInstance)
	{
		LoadingHUDWidgetInstance->UpdateSlot(PlayerId, bReady);
	}
}

void AHellunaHeroController::Client_FadeToGame_Implementation()
{
	UWorld* World = GetWorld();
	UE_LOG(LogHelluna, Warning,
		TEXT("[LoadingDbg][Fade] ENTER | PC=%s | IsLocal=%d | World=%p | Viewport=%p | LoadingHUDInst=%p | Pawn=%s"),
		*GetNameSafe(this), IsLocalController() ? 1 : 0,
		World, World ? World->GetGameViewport() : nullptr,
		LoadingHUDWidgetInstance.Get(),
		*GetNameSafe(GetPawn()));

	if (!IsLocalController())
	{
		UE_LOG(LogHelluna, Warning, TEXT("[LoadingDbg][Fade] EARLY RETURN — !IsLocalController"));
		return;
	}

	// (0) 로딩 씬 우주선 감쇠 시작 — 탑승감에서 해방되는 모션 (이즈아웃 큐빅)
	if (World)
	{
		TArray<AActor*> FoundShips;
		UGameplayStatics::GetAllActorsOfClassWithTag(World, AHellunaLoadingShipActor::StaticClass(),
			LoadingShipTag, FoundShips);
		for (AActor* A : FoundShips)
		{
			if (AHellunaLoadingShipActor* Ship = Cast<AHellunaLoadingShipActor>(A))
			{
				Ship->BeginDampenForRelease();
			}
		}
		UE_LOG(LogHelluna, Warning, TEXT("[LoadingDbg][Fade] Ship 감쇠 → 발견 %d개"), FoundShips.Num());
	}

	// (1) UMG 페이드 아웃 (BP WidgetAnimation — HUD가 자체 연출)
	//     + 즉시 Visibility=Collapsed 로 시각 숨김 (Slate 잔존 안전망)
	if (LoadingHUDWidgetInstance)
	{
		UE_LOG(LogHelluna, Warning,
			TEXT("[LoadingDbg][Fade] HUD PlayFadeOutAnimation + SetVisibility(Collapsed) | Inst=%p | IsInViewport(prev)=%d"),
			LoadingHUDWidgetInstance.Get(),
			LoadingHUDWidgetInstance->IsInViewport() ? 1 : 0);
		LoadingHUDWidgetInstance->PlayFadeOutAnimation();
		LoadingHUDWidgetInstance->SetVisibility(ESlateVisibility::Collapsed);
	}
	else
	{
		UE_LOG(LogHelluna, Warning, TEXT("[LoadingDbg][Fade] HUD null — Skip PlayFadeOutAnimation"));
	}

	// (2) PlayerCameraManager 검정 페이드 (0.6초 페이드 인 → 검정 홀드 → 페이드 아웃 0.9초)
	if (APlayerCameraManager* CamMgr = PlayerCameraManager)
	{
		CamMgr->StartCameraFade(0.f, 1.f, 0.6f, FLinearColor::Black, /*bShouldFadeAudio=*/false, /*bHoldWhenFinished=*/true);
		UE_LOG(LogHelluna, Warning, TEXT("[LoadingDbg][Fade] CameraFade 0→1 (0.6s) 시작"));
	}
	else
	{
		UE_LOG(LogHelluna, Warning, TEXT("[LoadingDbg][Fade] PlayerCameraManager null!"));
	}

	// (3) 1.2초 후 Pawn 으로 ViewTarget 블렌드 + 2.1초 후 페이드 아웃 + 3.0초 후 Cleanup
	if (!World)
	{
		UE_LOG(LogHelluna, Warning, TEXT("[LoadingDbg][Fade] World null — 타이머 등록 불가, Cleanup 즉시 호출"));
		LeaveLoadingScene();
		SetIgnoreMoveInput(false);
		SetIgnoreLookInput(false);
		return;
	}

	FTimerHandle ViewTargetHandle;
	World->GetTimerManager().SetTimer(ViewTargetHandle, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		UE_LOG(LogHelluna, Warning,
			TEXT("[LoadingDbg][Fade] T+1.2s ViewTargetBlend → Pawn=%s"),
			*GetNameSafe(GetPawn()));
		if (APawn* MyPawn = GetPawn())
		{
			SetViewTargetWithBlend(MyPawn, 0.5f, VTBlend_Cubic);
		}
	}), 1.2f, false);

	FTimerHandle FadeOutHandle;
	World->GetTimerManager().SetTimer(FadeOutHandle, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		UE_LOG(LogHelluna, Warning, TEXT("[LoadingDbg][Fade] T+2.1s CameraFade 1→0 (0.9s) 시작"));
		if (APlayerCameraManager* CamMgr = PlayerCameraManager)
		{
			CamMgr->StartCameraFade(1.f, 0.f, 0.9f, FLinearColor::Black, false, false);
		}
	}), 2.1f, false);

	FTimerHandle CleanupHandle;
	World->GetTimerManager().SetTimer(CleanupHandle, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		UE_LOG(LogHelluna, Warning,
			TEXT("[LoadingDbg][Fade] T+3.0s Cleanup 진입 | LoadingHUDInst=%p"),
			LoadingHUDWidgetInstance.Get());
		LeaveLoadingScene();
		SetIgnoreMoveInput(false);
		SetIgnoreLookInput(false);
		UE_LOG(LogHelluna, Warning, TEXT("[LoadingDbg][Fade] T+3.0s Cleanup 완료 — 입력 해제 + HUD 정리"));
	}), 3.0f, false);

	UE_LOG(LogHelluna, Warning, TEXT("[LoadingDbg][Fade] 모든 타이머 등록 완료 (1.2s/2.1s/3.0s)"));
}

void AHellunaHeroController::LeaveLoadingScene()
{
	UWorld* World = GetWorld();
	UGameViewportClient* VC = World ? World->GetGameViewport() : nullptr;
	UE_LOG(LogHelluna, Warning,
		TEXT("[LoadingDbg][Leave] ENTER | PC=%s | World=%p | Viewport=%p | LoadingHUDInst=%p | bInLoadingScene(prev)=%d"),
		*GetNameSafe(this), World, VC, LoadingHUDWidgetInstance.Get(), bInLoadingScene ? 1 : 0);

	bInLoadingScene = false;
	if (World)
	{
		World->GetTimerManager().ClearTimer(ReadyPollTimerHandle);
		World->GetTimerManager().ClearTimer(SoftTimeoutTimerHandle);
	}
	if (LoadingHUDWidgetInstance)
	{
		const bool bWasInViewportBefore = LoadingHUDWidgetInstance->IsInViewport();

		// (1) 즉시 시각 숨김 — Slate 측 RemoveFromParent 가 mismatch 등으로 실패해도 보이지 않게
		LoadingHUDWidgetInstance->SetVisibility(ESlateVisibility::Collapsed);

		// (2) 표준 RemoveFromParent
		LoadingHUDWidgetInstance->RemoveFromParent();
		const bool bIsInViewportAfter = LoadingHUDWidgetInstance->IsInViewport();

		UE_LOG(LogHelluna, Warning,
			TEXT("[LoadingDbg][Leave] RemoveFromParent | Inst=%p | IsInViewport(before/after)=%d/%d"),
			LoadingHUDWidgetInstance.Get(), bWasInViewportBefore ? 1 : 0, bIsInViewportAfter ? 1 : 0);

		// (3) 다음 틱에 한 번 더 RemoveFromParent 재시도 — viewport 안정화 후
		TWeakObjectPtr<UHellunaLoadingHUDWidget> WeakHUD = LoadingHUDWidgetInstance;
		if (World)
		{
			World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [WeakHUD]()
			{
				UHellunaLoadingHUDWidget* HUD = WeakHUD.Get();
				UE_LOG(LogHelluna, Warning,
					TEXT("[LoadingDbg][Leave] NextTick 재시도 | HUD=%p | IsInViewport(prev)=%d"),
					HUD, (HUD && HUD->IsInViewport()) ? 1 : 0);
				if (HUD)
				{
					HUD->SetVisibility(ESlateVisibility::Collapsed);
					HUD->RemoveFromParent();
					UE_LOG(LogHelluna, Warning,
						TEXT("[LoadingDbg][Leave] NextTick 후 IsInViewport=%d"),
						HUD->IsInViewport() ? 1 : 0);
				}
			}));
		}

		LoadingHUDWidgetInstance = nullptr;
	}
	else
	{
		UE_LOG(LogHelluna, Warning, TEXT("[LoadingDbg][Leave] LoadingHUDInst already null"));
	}
	LoadingCameraActor.Reset();

	// [§13 v2.1+ X-3] L_LoadingShipScene 서브레벨 언로드 — 메모리 회수
	// v1엔 서브레벨이 없어 호출 없었으나, v2.1에서 공용 서브레벨 도입으로 명시적 언로드 필요.
	// Client_FadeToGame 3.0s 뒤 LeaveLoadingScene 호출 → 페이드 검정 중 병렬 언로드.
	if (World)
	{
		FLatentActionInfo UnloadLatent;
		UnloadLatent.CallbackTarget = this;
		UnloadLatent.ExecutionFunction = FName(TEXT("OnLoadingShipSceneUnloaded"));
		UnloadLatent.Linkage = 0;
		UnloadLatent.UUID = GetUniqueID() + 100;  // Lobby 쪽 Background UUID와 충돌 방지
		UGameplayStatics::UnloadStreamLevel(this, FName(TEXT("L_LoadingShipScene")), UnloadLatent, false);
		UE_LOG(LogHelluna, Warning,
			TEXT("[LoadingDbg][Leave] UnloadStreamLevel(L_LoadingShipScene) 요청 | UUID=%d"),
			UnloadLatent.UUID);
	}

	UE_LOG(LogHelluna, Warning, TEXT("[LoadingDbg][Leave] EXIT"));
}

// [§13 v2.1+ X-3] L_LoadingShipScene 언로드 완료 콜백
void AHellunaHeroController::OnLoadingShipSceneUnloaded()
{
	UE_LOG(LogHelluna, Warning, TEXT("[LoadingDbg][Leave] L_LoadingShipScene 언로드 완료"));
}
