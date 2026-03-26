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

// [Puzzle] 퍼즐 시스템
#include "Puzzle/PuzzleCubeActor.h"
#include "Puzzle/PuzzleGridWidget.h"
#include "EngineUtils.h" // TActorIterator
#include "Components/PostProcessComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"



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
