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
