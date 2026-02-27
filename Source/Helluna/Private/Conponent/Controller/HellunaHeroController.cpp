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



AHellunaHeroController::AHellunaHeroController()
{
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
// C5+B7: EndPlay — 타이머 정리 + 채팅 델리게이트 해제
// ============================================================================

void AHellunaHeroController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// C5: 타이머 핸들 정리 (파괴 후 콜백 방지)
	GetWorldTimerManager().ClearTimer(VoteWidgetInitTimerHandle);
	GetWorldTimerManager().ClearTimer(ChatWidgetInitTimerHandle);

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
	UE_LOG(LogHellunaVote, Log, TEXT("[HellunaHeroController] InitializeVoteWidget 진입"));

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
	AGameStateBase* GameState = GetWorld()->GetGameState();
	if (!GameState)
	{
		UE_LOG(LogHellunaVote, Warning, TEXT("[HellunaHeroController] GameState가 아직 없음 - 재시도"));
		// GameState가 아직 복제 안 됐으면 0.5초 후 재시도
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
	AGameStateBase* GameState = GetWorld()->GetGameState();
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

	if (!GameResultWidgetClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("[HeroController] GameResultWidgetClass가 설정되지 않음! BP에서 설정 필요"));
		// 위젯 없으면 바로 로비로 이동
		if (!LobbyURL.IsEmpty())
		{
			ClientTravel(LobbyURL, TRAVEL_Absolute);
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
		if (!LobbyURL.IsEmpty())
		{
			ClientTravel(LobbyURL, TRAVEL_Absolute);
		}
		return;
	}

	// 로비 URL 설정
	GameResultWidgetInstance->LobbyURL = LobbyURL;

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
	DefenseGS->OnChatMessageReceived.AddDynamic(ChatWidgetInstance, &UHellunaChatWidget::OnReceiveChatMessage);

	// 4. 위젯의 메시지 제출 델리게이트 바인딩
	ChatWidgetInstance->OnChatMessageSubmitted.AddDynamic(this, &AHellunaHeroController::OnChatMessageSubmitted);

	// 5. Enhanced Input 바인딩 — 채팅 토글 (Enter 키)
	if (ChatToggleAction && ChatMappingContext)
	{
		if (ULocalPlayer* LocalPlayer = GetLocalPlayer())
		{
			if (UEnhancedInputLocalPlayerSubsystem* Subsystem = LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
			{
				// 채팅 IMC를 항상 활성화
				Subsystem->AddMappingContext(ChatMappingContext, 0);
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
