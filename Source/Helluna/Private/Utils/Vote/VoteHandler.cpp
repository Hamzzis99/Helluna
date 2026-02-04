// Fill out your copyright notice in the Description page of Project Settings.

/**
 * @file    Utils/Vote/VoteHandler.cpp
 * @brief   IVoteHandler 인터페이스 구현 파일
 *
 * @details BlueprintNativeEvent 인터페이스 함수들은 UHT가 자동으로 기본 구현을 생성합니다.
 *          따라서 이 파일에서는 별도의 _Implementation 함수를 정의하지 않습니다.
 *
 *          구현 클래스(MoveMapActor 등)에서 필요한 함수만 _Implementation으로 오버라이드하세요.
 *
 *          필수 구현:
 *          - ExecuteVoteResult_Implementation: 투표 통과 시 실행할 로직
 *
 *          선택적 구현 (오버라이드하지 않으면 빈 동작):
 *          - OnVoteFailed_Implementation: 투표 실패 시 처리
 *          - OnVoteStarting_Implementation: 투표 시작 전 검증 (기본: true 반환)
 *          - OnVoteCancelled_Implementation: 투표 취소 시 처리
 *
 * @note    UE의 BlueprintNativeEvent 인터페이스에서는 cpp에서 기본 구현을 제공할 수 없습니다.
 *          UHT가 자동으로 빈 기본 구현을 생성하기 때문입니다.
 *
 * @author  [작성자]
 * @date    2026-02-05
 */

#include "Utils/Vote/VoteHandler.h"

// BlueprintNativeEvent 함수들은 UHT가 자동으로 빈 기본 구현을 생성합니다.
// 구현 클래스에서 필요한 함수만 _Implementation으로 오버라이드하세요.
//
// 예시 (MoveMapActor.cpp):
// void AMoveMapActor::ExecuteVoteResult_Implementation(const FVoteRequest& Request)
// {
//     // 투표 통과 시 맵 이동 실행
// }
