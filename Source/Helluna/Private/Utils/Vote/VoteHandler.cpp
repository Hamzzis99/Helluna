// Fill out your copyright notice in the Description page of Project Settings.

/**
 * @file    Utils/Vote/VoteHandler.cpp
 * @brief   IVoteHandler 인터페이스의 선택적 함수들에 대한 기본 구현 제공
 *
 * @details 이 파일은 IVoteHandler 인터페이스의 BlueprintNativeEvent 함수들에 대한
 *          기본 구현을 제공합니다. 구현 클래스에서 해당 함수를 오버라이드하지 않으면
 *          이 기본 구현이 사용됩니다.
 *
 *          기본 동작:
 *          - OnVoteFailed: 로그 출력 후 종료 (추가 처리 없음)
 *          - OnVoteStarting: 항상 true 반환 (모든 투표 허용)
 *          - OnVoteCancelled: 로그 출력 후 종료 (추가 처리 없음)
 *
 * @note    ExecuteVoteResult는 기본 구현을 제공하지 않습니다.
 *          반드시 구현 클래스에서 오버라이드해야 합니다.
 *
 * @author  [작성자]
 * @date    2026-02-05
 */

#include "Utils/Vote/VoteHandler.h"

/**
 * @brief   투표 실패 시 호출되는 기본 구현
 *
 * @details 이 함수는 투표가 시간 초과, 반대 다수 등의 이유로 실패했을 때
 *          VoteManager에 의해 호출됩니다.
 *
 *          기본 동작: 디버그 로그만 출력하고 종료합니다.
 *
 * @param   Request - 실패한 투표 요청 정보
 * @param   Reason  - 실패 사유 (예: "시간 초과", "반대 다수", "찬성 부족")
 *
 * @note    오버라이드 시 고려사항:
 *          - 실패 UI 표시가 필요한 경우 오버라이드
 *          - 실패 통계 수집이 필요한 경우 오버라이드
 *          - 재투표 로직이 필요한 경우 오버라이드
 */
void IVoteHandler::OnVoteFailed_Implementation(const FVoteRequest& Request, const FString& Reason)
{
	UE_LOG(LogHellunaVote, Log, TEXT("[IVoteHandler] OnVoteFailed 기본 구현 - Type: %s, Reason: %s"),
		*Request.GetVoteTypeName(), *Reason);
}

/**
 * @brief   투표 시작 시 호출되는 기본 구현
 *
 * @details 이 함수는 투표가 시작되기 직전에 VoteManager에 의해 호출됩니다.
 *          false를 반환하면 투표가 즉시 취소됩니다.
 *
 *          기본 동작: 디버그 로그를 출력하고 true를 반환합니다. (항상 허용)
 *
 * @param   Request - 시작될 투표 요청 정보
 *
 * @return  true - 항상 투표 진행 허용
 *
 * @note    오버라이드 시 고려사항:
 *          - 맵 이동 투표: 목표 맵 존재 여부 검증
 *          - 강퇴 투표: 대상 플레이어 유효성 검증
 *          - 난이도 투표: 난이도 값 범위 검증
 *          - 투표 쿨다운 체크
 *          - 동시 투표 방지 로직
 */
bool IVoteHandler::OnVoteStarting_Implementation(const FVoteRequest& Request)
{
	UE_LOG(LogHellunaVote, Log, TEXT("[IVoteHandler] OnVoteStarting 기본 구현 - Type: %s, 허용"),
		*Request.GetVoteTypeName());

	return true;
}

/**
 * @brief   투표 취소 시 호출되는 기본 구현
 *
 * @details 이 함수는 외부 요인(중도 퇴장, 연결 끊김 등)으로 인해
 *          투표가 강제 취소되었을 때 VoteManager에 의해 호출됩니다.
 *
 *          OnVoteFailed와의 차이점:
 *          - OnVoteFailed: 투표가 정상 진행되었으나 통과 조건 미충족
 *          - OnVoteCancelled: 외부 요인으로 인한 강제 종료
 *
 *          기본 동작: 디버그 로그만 출력하고 종료합니다.
 *
 * @param   Request - 취소된 투표 요청 정보
 * @param   Reason  - 취소 사유 (예: "투표 시작자 퇴장", "참여 인원 부족")
 *
 * @note    오버라이드 시 고려사항:
 *          - 리소스 정리가 필요한 경우 오버라이드
 *          - 취소 알림 UI 표시가 필요한 경우 오버라이드
 *          - 취소 로그/통계 수집이 필요한 경우 오버라이드
 */
void IVoteHandler::OnVoteCancelled_Implementation(const FVoteRequest& Request, const FString& Reason)
{
	UE_LOG(LogHellunaVote, Log, TEXT("[IVoteHandler] OnVoteCancelled 기본 구현 - Type: %s, Reason: %s"),
		*Request.GetVoteTypeName(), *Reason);
}
