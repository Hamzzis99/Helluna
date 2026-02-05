// Fill out your copyright notice in the Description page of Project Settings.

/**
 * @file    Utils/Vote/VoteManagerComponent.cpp
 * @brief   VoteManagerComponent의 구현 파일
 *
 * @details 모든 투표 로직, RPC, 타이머 처리를 담당합니다.
 *
 *          주요 함수:
 *          - StartVote(): 투표 시작 (서버)
 *          - ReceiveVote(): 투표 수신 (서버 전용, HeroController에서 호출)
 *          - CheckVoteResult(): 투표 결과 판정 (서버)
 *          - EndVote(): 투표 종료 처리 (서버)
 *          - Multicast_*(): 클라이언트 알림 (서버→클라 RPC)
 *
 * @author  [작성자]
 * @date    2026-02-05
 */

#include "Utils/Vote/VoteManagerComponent.h"
#include "Net/UnrealNetwork.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "Engine/World.h"
#include "TimerManager.h"

// 로그 카테고리 정의
DEFINE_LOG_CATEGORY(LogHellunaVote);

// ============================================================================
// 생성자
// ============================================================================

UVoteManagerComponent::UVoteManagerComponent()
{
	// 네트워크 복제 활성화
	SetIsReplicatedByDefault(true);

	// 자동 활성화
	bAutoActivate = true;

	// Tick 활성화 - RemainingTime 갱신용 (투표 시작 시에만 활성화)
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;

	UE_LOG(LogHellunaVote, Log, TEXT("[VoteManager] 생성자 호출 - 컴포넌트 생성됨"));
}

// ============================================================================
// 라이프사이클
// ============================================================================

void UVoteManagerComponent::BeginPlay()
{
	Super::BeginPlay();

	const bool bIsServer = GetOwner()->HasAuthority();
	UE_LOG(LogHellunaVote, Log, TEXT("[VoteManager] BeginPlay - %s"),
		bIsServer ? TEXT("서버") : TEXT("클라이언트"));

	// 서버에서만 플레이어 퇴장 감지 설정
	if (bIsServer)
	{
		if (AGameStateBase* GameState = GetWorld()->GetGameState())
		{
			// PlayerArray 변경 감지는 GameMode에서 처리하는 것이 일반적
			// 여기서는 별도의 델리게이트 바인딩이 필요한 경우 추가
			UE_LOG(LogHellunaVote, Log, TEXT("[VoteManager] 서버 초기화 완료 - GameState: %s"),
				*GameState->GetName());
		}
	}
}

void UVoteManagerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UE_LOG(LogHellunaVote, Log, TEXT("[VoteManager] EndPlay - Reason: %d"), static_cast<int32>(EndPlayReason));

	// 타이머 정리
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(VoteTimerHandle);
	}

	Super::EndPlay(EndPlayReason);
}

void UVoteManagerComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// 서버에서만 시간 갱신
	if (!GetOwner()->HasAuthority() || !bIsVoteInProgress)
	{
		return;
	}

	// 남은 시간 갱신
	RemainingTime = FMath::Max(0.0f, RemainingTime - DeltaTime);

	// 1초 간격으로 클라이언트에 현황 업데이트
	TimeSinceLastUpdate += DeltaTime;
	if (TimeSinceLastUpdate >= UpdateInterval)
	{
		TimeSinceLastUpdate = 0.0f;
		Multicast_NotifyVoteUpdated(GetCurrentStatus());
	}
}

void UVoteManagerComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UVoteManagerComponent, bIsVoteInProgress);
	DOREPLIFETIME(UVoteManagerComponent, CurrentRequest);
	DOREPLIFETIME(UVoteManagerComponent, RemainingTime);
}

// ============================================================================
// 투표 시작
// ============================================================================

bool UVoteManagerComponent::StartVote(const FVoteRequest& Request, TScriptInterface<IVoteHandler> Handler)
{
	UE_LOG(LogHellunaVote, Log, TEXT("[VoteManager] StartVote 진입 - %s"), *Request.ToString());

	// 서버 권한 체크
	if (!GetOwner()->HasAuthority())
	{
		UE_LOG(LogHellunaVote, Warning, TEXT("[VoteManager] StartVote 실패 - 서버 권한 없음"));
		return false;
	}

	// 이미 진행 중인 투표 체크
	if (bIsVoteInProgress)
	{
		UE_LOG(LogHellunaVote, Warning, TEXT("[VoteManager] StartVote 실패 - 이미 투표 진행 중"));
		return false;
	}

	// 요청 유효성 체크
	if (!Request.IsValid())
	{
		UE_LOG(LogHellunaVote, Warning, TEXT("[VoteManager] StartVote 실패 - 유효하지 않은 Request"));
		return false;
	}

	// 핸들러 유효성 체크
	if (!Handler.GetObject())
	{
		UE_LOG(LogHellunaVote, Warning, TEXT("[VoteManager] StartVote 실패 - Handler가 null"));
		return false;
	}

	// 핸들러의 시작 전 검증
	UE_LOG(LogHellunaVote, Log, TEXT("[VoteManager] Handler->OnVoteStarting() 호출 - Handler: %s"),
		*Handler.GetObject()->GetName());
	if (!IVoteHandler::Execute_OnVoteStarting(Handler.GetObject(), Request))
	{
		UE_LOG(LogHellunaVote, Warning,
			TEXT("[VoteManager] StartVote 실패 - Handler->OnVoteStarting() 반환값 false. ")
			TEXT("Handler 클래스에서 OnVoteStarting_Implementation을 오버라이드하여 true를 반환하는지 확인하세요."));
		return false;
	}

	// 상태 설정
	CurrentRequest = Request;
	CurrentHandler = Handler;
	bIsVoteInProgress = true;
	RemainingTime = Request.Timeout;

	// PlayerVotes 초기화 - 현재 접속 플레이어들을 NotVoted로 설정
	PlayerVotes.Empty();
	if (AGameStateBase* GameState = GetWorld()->GetGameState())
	{
		for (APlayerState* PS : GameState->PlayerArray)
		{
			if (PS && PS->IsValidLowLevel())
			{
				PlayerVotes.Add(PS, EVoteResult::NotVoted);
				UE_LOG(LogHellunaVote, Log, TEXT("[VoteManager] 투표 참여자 등록: %s"), *PS->GetPlayerName());
			}
		}
	}

	// 시작자 자동 찬성 처리
	if (Request.Initiator.IsValid())
	{
		APlayerState* InitiatorPS = Request.Initiator.Get();
		if (PlayerVotes.Contains(InitiatorPS))
		{
			PlayerVotes[InitiatorPS] = EVoteResult::Agree;
			UE_LOG(LogHellunaVote, Log, TEXT("[VoteManager] 시작자 자동 찬성: %s"), *InitiatorPS->GetPlayerName());
		}
	}

	// 타임아웃 타이머 시작
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			VoteTimerHandle,
			this,
			&UVoteManagerComponent::OnVoteTimeout,
			Request.Timeout,
			false
		);
		UE_LOG(LogHellunaVote, Log, TEXT("[VoteManager] 타이머 시작 - %.1f초"), Request.Timeout);
	}

	// Tick 활성화 (RemainingTime 갱신 시작)
	SetComponentTickEnabled(true);
	TimeSinceLastUpdate = 0.0f;

	// 모든 클라이언트에 투표 시작 알림
	Multicast_NotifyVoteStarted(Request);

	// [수정] 초기 투표 현황 전달 (시작자 자동 찬성 1표 반영)
	Multicast_NotifyVoteUpdated(GetCurrentStatus());

	UE_LOG(LogHellunaVote, Log, TEXT("[VoteManager] StartVote 완료 - 투표 시작됨, 참여자: %d명"), PlayerVotes.Num());

	// 시작자 혼자인 경우 즉시 결과 체크
	CheckVoteResult();

	return true;
}

// ============================================================================
// 투표 제출
// ============================================================================

void UVoteManagerComponent::ReceiveVote(APlayerState* Voter, bool bAgree)
{
	UE_LOG(LogHellunaVote, Log, TEXT("[VoteManager] ReceiveVote 진입 - Voter: %s, bAgree: %s"),
		Voter ? *Voter->GetPlayerName() : TEXT("null"),
		bAgree ? TEXT("찬성") : TEXT("반대"));

	// 투표 진행 중 체크
	if (!bIsVoteInProgress)
	{
		UE_LOG(LogHellunaVote, Warning, TEXT("[VoteManager] ReceiveVote 실패 - 진행 중인 투표 없음"));
		return;
	}

	// Voter 유효성 체크
	if (!Voter)
	{
		UE_LOG(LogHellunaVote, Warning, TEXT("[VoteManager] ReceiveVote 실패 - Voter가 null"));
		return;
	}

	// 투표 참여자인지 체크
	if (!PlayerVotes.Contains(Voter))
	{
		UE_LOG(LogHellunaVote, Warning, TEXT("[VoteManager] ReceiveVote 실패 - %s는 투표 참여자 아님"),
			*Voter->GetPlayerName());
		return;
	}

	// 이미 투표했는지 체크
	if (PlayerVotes[Voter] != EVoteResult::NotVoted)
	{
		UE_LOG(LogHellunaVote, Warning, TEXT("[VoteManager] ReceiveVote 무시 - %s 이미 투표함"),
			*Voter->GetPlayerName());
		return;
	}

	// 투표 기록
	PlayerVotes[Voter] = bAgree ? EVoteResult::Agree : EVoteResult::Disagree;
	UE_LOG(LogHellunaVote, Log, TEXT("[VoteManager] 투표 기록됨 - %s: %s"),
		*Voter->GetPlayerName(),
		bAgree ? TEXT("찬성") : TEXT("반대"));

	// 현황 업데이트 브로드캐스트
	Multicast_NotifyVoteUpdated(GetCurrentStatus());

	// 결과 체크
	CheckVoteResult();
}

// ============================================================================
// 투표 취소
// ============================================================================

void UVoteManagerComponent::CancelVote(const FString& Reason)
{
	UE_LOG(LogHellunaVote, Log, TEXT("[VoteManager] CancelVote 진입 - Reason: %s"), *Reason);

	// 서버 권한 체크
	if (!GetOwner()->HasAuthority())
	{
		UE_LOG(LogHellunaVote, Warning, TEXT("[VoteManager] CancelVote 실패 - 서버 권한 없음"));
		return;
	}

	// 진행 중인 투표 체크
	if (!bIsVoteInProgress)
	{
		UE_LOG(LogHellunaVote, Warning, TEXT("[VoteManager] CancelVote 실패 - 진행 중인 투표 없음"));
		return;
	}

	// Handler에 취소 알림
	if (CurrentHandler.GetObject())
	{
		IVoteHandler::Execute_OnVoteCancelled(CurrentHandler.GetObject(), CurrentRequest, Reason);
	}

	// 투표 종료 처리
	EndVote(false, Reason);
}

// ============================================================================
// 상태 조회
// ============================================================================

FVoteStatus UVoteManagerComponent::GetCurrentStatus() const
{
	FVoteStatus Status;
	Status.RemainingTime = RemainingTime;

	// PlayerVotes 순회하며 카운트
	for (const auto& Pair : PlayerVotes)
	{
		Status.TotalPlayers++;

		switch (Pair.Value)
		{
		case EVoteResult::Agree:
			Status.AgreeCount++;
			break;
		case EVoteResult::Disagree:
			Status.DisagreeCount++;
			break;
		case EVoteResult::NotVoted:
		default:
			Status.NotVotedCount++;
			break;
		}
	}

	return Status;
}

EVoteResult UVoteManagerComponent::GetPlayerVoteResult(APlayerState* PlayerState) const
{
	if (!PlayerState)
	{
		return EVoteResult::NotVoted;
	}

	const EVoteResult* Result = PlayerVotes.Find(PlayerState);
	return Result ? *Result : EVoteResult::NotVoted;
}

// ============================================================================
// 내부 로직 - 결과 체크
// ============================================================================

void UVoteManagerComponent::CheckVoteResult()
{
	UE_LOG(LogHellunaVote, Log, TEXT("[VoteManager] CheckVoteResult 진입"));

	if (!bIsVoteInProgress)
	{
		return;
	}

	// 현재 상태 계산
	const FVoteStatus Status = GetCurrentStatus();
	UE_LOG(LogHellunaVote, Log, TEXT("[VoteManager] 현재 상태 - %s"), *Status.ToString());

	const int32 TotalPlayers = Status.TotalPlayers;
	const int32 AgreeCount = Status.AgreeCount;
	const int32 DisagreeCount = Status.DisagreeCount;
	const int32 VotedCount = AgreeCount + DisagreeCount;

	// 참여자가 없으면 취소
	if (TotalPlayers == 0)
	{
		UE_LOG(LogHellunaVote, Warning, TEXT("[VoteManager] 투표 참여자 없음 - 취소"));
		EndVote(false, TEXT("참여자 없음"));
		return;
	}

	// 만장일치 조건 체크
	if (CurrentRequest.Condition == EVoteCondition::Unanimous)
	{
		// 1명이라도 반대하면 즉시 실패
		if (DisagreeCount > 0)
		{
			UE_LOG(LogHellunaVote, Log, TEXT("[VoteManager] 만장일치 실패 - 반대 투표 발생"));
			EndVote(false, TEXT("반대 투표 발생"));
			return;
		}

		// 모두 찬성하면 통과
		if (AgreeCount == TotalPlayers)
		{
			UE_LOG(LogHellunaVote, Log, TEXT("[VoteManager] 만장일치 통과"));
			EndVote(true, TEXT("만장일치 통과"));
			return;
		}
	}
	// 과반수 조건 체크
	else if (CurrentRequest.Condition == EVoteCondition::Majority)
	{
		const int32 MajorityThreshold = (TotalPlayers / 2) + 1;

		// 과반 찬성 확정
		if (AgreeCount >= MajorityThreshold)
		{
			UE_LOG(LogHellunaVote, Log, TEXT("[VoteManager] 과반수 찬성 확정 - %d/%d"),
				AgreeCount, TotalPlayers);
			EndVote(true, TEXT("과반수 찬성"));
			return;
		}

		// 과반 반대 확정 (남은 인원이 모두 찬성해도 과반 불가능)
		const int32 RemainingVotes = TotalPlayers - VotedCount;
		if (AgreeCount + RemainingVotes < MajorityThreshold)
		{
			UE_LOG(LogHellunaVote, Log, TEXT("[VoteManager] 과반수 반대 확정 - 찬성 가능 최대: %d, 필요: %d"),
				AgreeCount + RemainingVotes, MajorityThreshold);
			EndVote(false, TEXT("과반수 반대"));
			return;
		}
	}

	// 모두 투표 완료 시 최종 판정
	if (VotedCount == TotalPlayers)
	{
		UE_LOG(LogHellunaVote, Log, TEXT("[VoteManager] 모든 투표 완료 - 최종 판정"));

		if (CurrentRequest.Condition == EVoteCondition::Unanimous)
		{
			// 여기까지 왔으면 모두 찬성 (반대 시 위에서 종료됨)
			EndVote(true, TEXT("만장일치 통과"));
		}
		else
		{
			// 과반수: 찬성이 더 많으면 통과
			const bool bPassed = AgreeCount > DisagreeCount;
			EndVote(bPassed, bPassed ? TEXT("과반수 찬성") : TEXT("과반수 반대"));
		}
	}
}

// ============================================================================
// 내부 로직 - 타임아웃
// ============================================================================

void UVoteManagerComponent::OnVoteTimeout()
{
	UE_LOG(LogHellunaVote, Log, TEXT("[VoteManager] OnVoteTimeout - 제한 시간 만료"));

	if (!bIsVoteInProgress)
	{
		return;
	}

	const FVoteStatus Status = GetCurrentStatus();
	const int32 VotedCount = Status.AgreeCount + Status.DisagreeCount;

	// 만장일치 조건
	if (CurrentRequest.Condition == EVoteCondition::Unanimous)
	{
		// 미투표자가 있으면 실패 (만장일치 불가)
		if (Status.NotVotedCount > 0)
		{
			UE_LOG(LogHellunaVote, Log, TEXT("[VoteManager] 타임아웃 - 만장일치 실패 (미투표자 %d명)"),
				Status.NotVotedCount);
			EndVote(false, TEXT("시간 초과 - 만장일치 불가"));
			return;
		}

		// 모두 찬성했으면 통과
		if (Status.DisagreeCount == 0)
		{
			EndVote(true, TEXT("만장일치 통과"));
		}
		else
		{
			EndVote(false, TEXT("반대 투표 발생"));
		}
	}
	// 과반수 조건
	else
	{
		// 투표자 중 과반 체크 (미투표자 제외)
		if (VotedCount == 0)
		{
			UE_LOG(LogHellunaVote, Log, TEXT("[VoteManager] 타임아웃 - 투표자 없음"));
			EndVote(false, TEXT("시간 초과 - 투표자 없음"));
			return;
		}

		const int32 MajorityThreshold = (VotedCount / 2) + 1;
		const bool bPassed = Status.AgreeCount >= MajorityThreshold;

		UE_LOG(LogHellunaVote, Log, TEXT("[VoteManager] 타임아웃 - 투표자 중 과반 체크: 찬성 %d / 투표자 %d, 필요 %d"),
			Status.AgreeCount, VotedCount, MajorityThreshold);

		EndVote(bPassed, bPassed ? TEXT("시간 초과 - 과반수 찬성") : TEXT("시간 초과 - 찬성 부족"));
	}
}

// ============================================================================
// 내부 로직 - 투표 종료
// ============================================================================

void UVoteManagerComponent::EndVote(bool bPassed, const FString& Reason)
{
	UE_LOG(LogHellunaVote, Log, TEXT("[VoteManager] EndVote 진입 - bPassed: %s, Reason: %s"),
		bPassed ? TEXT("true") : TEXT("false"), *Reason);

	// 타이머 정리
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(VoteTimerHandle);
	}

	// Handler 콜백 호출
	if (CurrentHandler.GetObject())
	{
		if (bPassed)
		{
			// 투표 통과: 2초 딜레이 후 ExecuteVoteResult 호출
			UE_LOG(LogHellunaVote, Log, TEXT("[VoteManager] 투표 통과 - 2초 후 결과 실행 예정"));

			// 저장할 값 (Multicast 전에 저장)
			const EVoteType EndedVoteType = CurrentRequest.VoteType;

			// 모든 클라이언트에 종료 알림 (딜레이 전에 먼저 알림)
			Multicast_NotifyVoteEnded(EndedVoteType, bPassed, Reason);

			// Tick 비활성화
			SetComponentTickEnabled(false);
			TimeSinceLastUpdate = 0.0f;

			// 투표 진행 상태만 해제 (Handler와 Request는 딜레이 후 사용해야 하므로 유지)
			bIsVoteInProgress = false;
			PlayerVotes.Empty();
			RemainingTime = 0.0f;

			// 2초 딜레이 타이머 설정
			if (UWorld* World = GetWorld())
			{
				World->GetTimerManager().SetTimer(
					VoteResultDelayTimerHandle,
					this,
					&UVoteManagerComponent::ExecuteVoteResultAfterDelay,
					2.0f,
					false
				);
			}

			UE_LOG(LogHellunaVote, Log, TEXT("[VoteManager] EndVote 완료 - 투표 종료됨 (결과 실행 대기 중)"));
			return;
		}
		else
		{
			UE_LOG(LogHellunaVote, Log, TEXT("[VoteManager] Handler->OnVoteFailed 호출"));
			IVoteHandler::Execute_OnVoteFailed(CurrentHandler.GetObject(), CurrentRequest, Reason);
		}
	}

	// 저장할 값 (Multicast 전에 저장)
	const EVoteType EndedVoteType = CurrentRequest.VoteType;

	// 모든 클라이언트에 종료 알림
	Multicast_NotifyVoteEnded(EndedVoteType, bPassed, Reason);

	// Tick 비활성화
	SetComponentTickEnabled(false);
	TimeSinceLastUpdate = 0.0f;

	// 상태 초기화
	bIsVoteInProgress = false;
	CurrentRequest = FVoteRequest();
	CurrentHandler = nullptr;
	PlayerVotes.Empty();
	RemainingTime = 0.0f;

	UE_LOG(LogHellunaVote, Log, TEXT("[VoteManager] EndVote 완료 - 투표 종료됨"));
}

// ============================================================================
// 내부 로직 - 투표 통과 후 딜레이 실행
// ============================================================================

void UVoteManagerComponent::ExecuteVoteResultAfterDelay()
{
	UE_LOG(LogHellunaVote, Log, TEXT("[VoteManager] ExecuteVoteResultAfterDelay - 2초 경과, 결과 실행"));

	if (CurrentHandler.GetObject())
	{
		IVoteHandler::Execute_ExecuteVoteResult(CurrentHandler.GetObject(), CurrentRequest);
	}
	else
	{
		UE_LOG(LogHellunaVote, Warning, TEXT("[VoteManager] ExecuteVoteResultAfterDelay - Handler가 유효하지 않음"));
	}

	// 최종 상태 초기화
	CurrentRequest = FVoteRequest();
	CurrentHandler = nullptr;

	UE_LOG(LogHellunaVote, Log, TEXT("[VoteManager] 결과 실행 완료 - 상태 초기화됨"));
}

// ============================================================================
// 내부 로직 - 플레이어 퇴장 처리
// ============================================================================

void UVoteManagerComponent::HandlePlayerDisconnect(APlayerState* ExitingPlayer)
{
	if (!ExitingPlayer)
	{
		return;
	}

	UE_LOG(LogHellunaVote, Log, TEXT("[VoteManager] HandlePlayerDisconnect 진입 - Player: %s"),
		*ExitingPlayer->GetPlayerName());

	// 투표 진행 중 아니면 무시
	if (!bIsVoteInProgress)
	{
		return;
	}

	// 해당 플레이어가 투표 참여자인지 확인
	if (!PlayerVotes.Contains(ExitingPlayer))
	{
		UE_LOG(LogHellunaVote, Log, TEXT("[VoteManager] 퇴장 플레이어가 투표 참여자 아님"));
		return;
	}

	// DisconnectPolicy에 따라 처리
	switch (CurrentRequest.DisconnectPolicy)
	{
	case EVoteDisconnectPolicy::CancelVote:
		UE_LOG(LogHellunaVote, Log, TEXT("[VoteManager] DisconnectPolicy: CancelVote - 투표 취소"));
		CancelVote(FString::Printf(TEXT("플레이어 퇴장: %s"), *ExitingPlayer->GetPlayerName()));
		break;

	case EVoteDisconnectPolicy::ExcludeAndContinue:
		UE_LOG(LogHellunaVote, Log, TEXT("[VoteManager] DisconnectPolicy: ExcludeAndContinue - 플레이어 제외 후 계속"));
		PlayerVotes.Remove(ExitingPlayer);

		// 남은 참여자가 없으면 취소
		if (PlayerVotes.Num() == 0)
		{
			CancelVote(TEXT("참여자 없음"));
		}
		else
		{
			// 현황 업데이트 및 결과 재체크
			Multicast_NotifyVoteUpdated(GetCurrentStatus());
			CheckVoteResult();
		}
		break;

	default:
		break;
	}
}

// ============================================================================
// Multicast RPC 구현
// ============================================================================

void UVoteManagerComponent::Multicast_NotifyVoteStarted_Implementation(const FVoteRequest& Request)
{
	UE_LOG(LogHellunaVote, Log, TEXT("[VoteManager] Multicast_NotifyVoteStarted 수신 - %s"),
		*Request.GetVoteTypeName());

	// 델리게이트 브로드캐스트 (UI에서 수신)
	OnVoteStarted.Broadcast(Request);
}

void UVoteManagerComponent::Multicast_NotifyVoteUpdated_Implementation(const FVoteStatus& Status)
{
	UE_LOG(LogHellunaVote, Log, TEXT("[VoteManager] Multicast_NotifyVoteUpdated 수신 - %s"),
		*Status.ToString());

	// 델리게이트 브로드캐스트 (UI에서 수신)
	OnVoteUpdated.Broadcast(Status);
}

void UVoteManagerComponent::Multicast_NotifyVoteEnded_Implementation(EVoteType VoteType, bool bPassed, const FString& Reason)
{
	UE_LOG(LogHellunaVote, Log, TEXT("[VoteManager] Multicast_NotifyVoteEnded 수신 - Type: %d, Passed: %s, Reason: %s"),
		static_cast<int32>(VoteType),
		bPassed ? TEXT("true") : TEXT("false"),
		*Reason);

	// 델리게이트 브로드캐스트 (UI에서 수신)
	OnVoteEnded.Broadcast(VoteType, bPassed, Reason);
}
