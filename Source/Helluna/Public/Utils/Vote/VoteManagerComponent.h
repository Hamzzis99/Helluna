// Fill out your copyright notice in the Description page of Project Settings.

/**
 * @file    Utils/Vote/VoteManagerComponent.h
 * @brief   투표 시스템의 핵심 로직을 담당하는 컴포넌트
 *
 * @details GameState에 부착되어 서버에서 투표를 관리하고, 클라이언트로 상태를 복제합니다.
 *
 *          주요 책임:
 *          - 투표 시작/종료 관리
 *          - 투표 집계 및 결과 판정
 *          - 타이머 관리 (타임아웃 처리)
 *          - 클라이언트로 상태 복제
 *          - RPC를 통한 클라이언트-서버 통신
 *
 * @usage   GameState에서 CreateDefaultSubobject로 생성:
 * @code
 *          // AMyGameState.h
 *          UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Vote")
 *          TObjectPtr<UVoteManagerComponent> VoteManager;
 *
 *          // AMyGameState.cpp
 *          AMyGameState::AMyGameState()
 *          {
 *              VoteManager = CreateDefaultSubobject<UVoteManagerComponent>(TEXT("VoteManager"));
 *          }
 * @endcode
 *
 * @flow    투표 흐름도:
 *
 *          [클라이언트]                          [서버]
 *              │                                   │
 *          MoveMapActor::Interact()                │
 *              │                                   │
 *              └──── Server RPC ──────────────────→│
 *                                                  │
 *                                    VoteManager->StartVote(Request, Handler)
 *                                                  │
 *                                    Handler->OnVoteStarting() 검증
 *                                                  │
 *                                    Multicast_NotifyVoteStarted()
 *              │                                   │
 *              ←───── Multicast RPC ──────────────┘
 *              │
 *          OnVoteStarted 델리게이트 → UI 표시
 *              │
 *          플레이어 F1/F2 입력
 *              │
 *              └──── Server_SubmitVote(bAgree) ──→│
 *                                                  │
 *                                    PlayerVotes에 기록
 *                                                  │
 *                                    CheckVoteResult()
 *                                                  │
 *                                    ┌─────────────┴─────────────┐
 *                                    │                           │
 *                              [통과 조건 충족]            [실패 조건 충족]
 *                                    │                           │
 *                        Handler->ExecuteVoteResult()   Handler->OnVoteFailed()
 *                                    │                           │
 *                                    └─────────────┬─────────────┘
 *                                                  │
 *                                    Multicast_NotifyVoteEnded()
 *              │                                   │
 *              ←───── Multicast RPC ──────────────┘
 *              │
 *          OnVoteEnded 델리게이트 → UI 숨김
 *
 * @author  [작성자]
 * @date    2026-02-05
 */

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Utils/Vote/VoteTypes.h"
#include "Utils/Vote/VoteHandler.h"
#include "VoteManagerComponent.generated.h"

// ============================================================================
// 델리게이트 선언
// ============================================================================

/**
 * @brief   투표 시작 시 브로드캐스트되는 델리게이트
 * @details UI에서 바인딩하여 투표 위젯을 표시하는 데 사용합니다.
 * @param   Request - 시작된 투표 요청 정보
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnVoteStarted, const FVoteRequest&, Request);

/**
 * @brief   투표 현황 업데이트 시 브로드캐스트되는 델리게이트
 * @details UI에서 바인딩하여 찬성/반대 현황을 갱신하는 데 사용합니다.
 * @param   Status - 현재 투표 현황 (찬성/반대/미투표 수, 남은 시간)
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnVoteUpdated, const FVoteStatus&, Status);

/**
 * @brief   투표 종료 시 브로드캐스트되는 델리게이트
 * @details UI에서 바인딩하여 투표 위젯을 숨기는 데 사용합니다.
 * @param   VoteType - 종료된 투표 종류
 * @param   bPassed  - 투표 통과 여부
 * @param   Reason   - 종료 사유 (실패/취소 시 사유 문자열)
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnVoteEnded, EVoteType, VoteType, bool, bPassed, const FString&, Reason);

// ============================================================================
// UVoteManagerComponent 클래스
// ============================================================================

/**
 * @brief   투표 시스템 관리 컴포넌트
 * @details GameState에 부착되어 멀티플레이어 투표 시스템을 관리합니다.
 *          서버에서 투표 로직을 처리하고, 클라이언트로 상태를 복제합니다.
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class HELLUNA_API UVoteManagerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVoteManagerComponent();

	// ========================================================================
	// 델리게이트 (UI 바인딩용)
	// ========================================================================

	/**
	 * @brief 투표 시작 시 호출되는 델리게이트
	 * @note  클라이언트에서 UI 표시에 사용
	 */
	UPROPERTY(BlueprintAssignable, Category = "Vote")
	FOnVoteStarted OnVoteStarted;

	/**
	 * @brief 투표 현황 업데이트 시 호출되는 델리게이트
	 * @note  클라이언트에서 UI 갱신에 사용
	 */
	UPROPERTY(BlueprintAssignable, Category = "Vote")
	FOnVoteUpdated OnVoteUpdated;

	/**
	 * @brief 투표 종료 시 호출되는 델리게이트
	 * @note  클라이언트에서 UI 숨김에 사용
	 */
	UPROPERTY(BlueprintAssignable, Category = "Vote")
	FOnVoteEnded OnVoteEnded;

	// ========================================================================
	// Public 함수 - 투표 시작
	// ========================================================================

	/**
	 * @brief   새 투표 시작 (서버 전용)
	 *
	 * @details 새로운 투표를 시작합니다. 이미 투표가 진행 중이면 실패합니다.
	 *          Handler의 OnVoteStarting()이 false를 반환해도 실패합니다.
	 *
	 * @param   Request - 투표 요청 정보 (타입, 조건, 타임아웃 등)
	 * @param   Handler - 결과 처리할 핸들러 (IVoteHandler 구현체)
	 *
	 * @return  true  - 투표가 성공적으로 시작됨
	 * @return  false - 시작 실패 (이미 진행 중, 검증 실패, 클라이언트 호출 등)
	 *
	 * @note    서버에서만 호출해야 합니다. 클라이언트에서 호출 시 false 반환
	 */
	UFUNCTION(BlueprintCallable, Category = "Vote")
	bool StartVote(const FVoteRequest& Request, TScriptInterface<IVoteHandler> Handler);

	// ========================================================================
	// Public 함수 - 투표 제출
	// ========================================================================

	/**
	 * @brief   플레이어 투표 제출 (클라이언트 → 서버)
	 *
	 * @details 지정된 플레이어의 투표를 서버에 제출합니다.
	 *          이미 투표한 플레이어가 다시 호출하면 무시됩니다 (변경 불가).
	 *
	 * @param   Voter  - 투표하는 플레이어의 PlayerState
	 * @param   bAgree - true: 찬성, false: 반대
	 *
	 * @note    VoteInputComponent에서 호출
	 * @note    Voter가 null이거나 참여자가 아니면 무시됨
	 * @note    Server RPC - 클라이언트에서 호출, 서버에서 실행
	 * @note    Reliable - 신뢰성 보장 (반드시 서버에 도달)
	 */
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Vote")
	void Server_SubmitVote(APlayerState* Voter, bool bAgree);

	// ========================================================================
	// Public 함수 - 투표 취소
	// ========================================================================

	/**
	 * @brief   진행 중인 투표 강제 취소 (서버 전용)
	 *
	 * @details 현재 진행 중인 투표를 즉시 취소합니다.
	 *          DisconnectPolicy가 CancelVote일 때 플레이어 퇴장 시 자동 호출됩니다.
	 *
	 * @param   Reason - 취소 사유 문자열
	 *
	 * @note    서버에서만 호출해야 합니다.
	 * @note    Handler의 OnVoteCancelled()가 호출됩니다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Vote")
	void CancelVote(const FString& Reason);

	// ========================================================================
	// Public 함수 - 상태 조회
	// ========================================================================

	/**
	 * @brief   현재 투표 진행 중인지 확인
	 * @return  true: 투표 진행 중, false: 투표 없음
	 */
	UFUNCTION(BlueprintPure, Category = "Vote")
	bool IsVoteInProgress() const { return bIsVoteInProgress; }

	/**
	 * @brief   현재 투표 요청 정보 반환
	 * @return  현재 진행 중인 투표의 요청 정보 (없으면 기본값)
	 */
	UFUNCTION(BlueprintPure, Category = "Vote")
	const FVoteRequest& GetCurrentRequest() const { return CurrentRequest; }

	/**
	 * @brief   현재 투표 현황 반환
	 * @return  투표 현황 (전체 인원, 찬성/반대/미투표 수, 남은 시간)
	 * @note    서버와 클라이언트 모두에서 호출 가능
	 */
	UFUNCTION(BlueprintPure, Category = "Vote")
	FVoteStatus GetCurrentStatus() const;

	/**
	 * @brief   특정 플레이어의 투표 결과 조회
	 * @param   PlayerState - 조회할 플레이어
	 * @return  해당 플레이어의 투표 결과 (NotVoted, Agree, Disagree)
	 * @note    서버에서만 정확한 값 반환. 클라이언트는 항상 NotVoted 반환
	 */
	UFUNCTION(BlueprintPure, Category = "Vote")
	EVoteResult GetPlayerVoteResult(APlayerState* PlayerState) const;

protected:
	// ========================================================================
	// 라이프사이클 오버라이드
	// ========================================================================

	/** 컴포넌트 시작 시 호출 */
	virtual void BeginPlay() override;

	/** 컴포넌트 종료 시 호출 */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** 복제할 프로퍼티 등록 */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// ========================================================================
	// 내부 로직 (서버 전용)
	// ========================================================================

	/**
	 * @brief   투표 결과 체크 및 처리
	 *
	 * @details 현재 투표 상황을 분석하여 조기 판정이 가능한지 확인합니다.
	 *          - 만장일치: 모두 찬성 또는 1명이라도 반대 시 즉시 종료
	 *          - 과반수: 과반 찬성 확정 또는 과반 반대 확정 시 즉시 종료
	 *          모든 플레이어가 투표했으면 최종 결과를 판정합니다.
	 *
	 * @note    서버에서만 호출됩니다.
	 */
	void CheckVoteResult();

	/**
	 * @brief   투표 타임아웃 처리
	 *
	 * @details 제한 시간이 만료되면 호출됩니다.
	 *          미투표자를 제외하고 현재 상태로 결과를 판정합니다.
	 *
	 * @note    서버에서만 호출됩니다. (타이머 콜백)
	 */
	void OnVoteTimeout();

	/**
	 * @brief   투표 종료 처리 (통과/실패/취소 공통)
	 *
	 * @details 투표를 종료하고 결과에 따라 Handler의 콜백을 호출합니다.
	 *          모든 클라이언트에 종료를 알립니다.
	 *
	 * @param   bPassed - true: 통과, false: 실패/취소
	 * @param   Reason  - 종료 사유 문자열
	 *
	 * @note    서버에서만 호출됩니다.
	 */
	void EndVote(bool bPassed, const FString& Reason);

	/**
	 * @brief   플레이어 퇴장 처리
	 *
	 * @details 투표 진행 중 플레이어가 퇴장하면 호출됩니다.
	 *          DisconnectPolicy에 따라 처리합니다:
	 *          - ExcludeAndContinue: 해당 플레이어 제외 후 계속
	 *          - CancelVote: 투표 취소
	 *
	 * @param   ExitingPlayer - 퇴장하는 플레이어
	 *
	 * @note    서버에서만 호출됩니다.
	 */
	void HandlePlayerDisconnect(APlayerState* ExitingPlayer);

	// ========================================================================
	// Multicast RPC (서버 → 모든 클라이언트)
	// ========================================================================

	/**
	 * @brief   투표 시작 알림
	 * @param   Request - 시작된 투표 요청 정보
	 * @note    서버에서 호출, 모든 클라이언트에서 실행
	 */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_NotifyVoteStarted(const FVoteRequest& Request);

	/**
	 * @brief   투표 현황 업데이트 알림
	 * @param   Status - 현재 투표 현황
	 * @note    서버에서 호출, 모든 클라이언트에서 실행
	 */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_NotifyVoteUpdated(const FVoteStatus& Status);

	/**
	 * @brief   투표 종료 알림
	 * @param   VoteType - 종료된 투표 종류
	 * @param   bPassed  - 통과 여부
	 * @param   Reason   - 종료 사유
	 * @note    서버에서 호출, 모든 클라이언트에서 실행
	 */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_NotifyVoteEnded(EVoteType VoteType, bool bPassed, const FString& Reason);

private:
	// ========================================================================
	// 복제 변수 (서버 → 클라이언트)
	// ========================================================================

	/**
	 * @brief 투표 진행 중 여부
	 * @note  Replicated - 모든 클라이언트에 복제됨
	 */
	UPROPERTY(Replicated)
	bool bIsVoteInProgress = false;

	/**
	 * @brief 현재 투표 요청 정보
	 * @note  Replicated - 모든 클라이언트에 복제됨
	 */
	UPROPERTY(Replicated)
	FVoteRequest CurrentRequest;

	/**
	 * @brief 남은 시간 (초)
	 * @note  Replicated - 모든 클라이언트에 복제됨
	 */
	UPROPERTY(Replicated)
	float RemainingTime = 0.0f;

	// ========================================================================
	// 서버 전용 변수
	// ========================================================================

	/**
	 * @brief 각 플레이어의 투표 결과
	 * @note  복제 안 함 - 서버에서만 관리
	 * @note  Key: PlayerState, Value: 투표 결과 (NotVoted, Agree, Disagree)
	 */
	UPROPERTY()
	TMap<TObjectPtr<APlayerState>, EVoteResult> PlayerVotes;

	/**
	 * @brief 투표 결과 처리 핸들러
	 * @note  복제 안 함 - 서버에서만 사용
	 * @note  투표 종료 시 ExecuteVoteResult() 또는 OnVoteFailed() 호출
	 */
	UPROPERTY()
	TScriptInterface<IVoteHandler> CurrentHandler;

	/**
	 * @brief 타임아웃 타이머 핸들
	 * @note  복제 안 함 - 서버에서만 사용
	 */
	FTimerHandle VoteTimerHandle;
};
