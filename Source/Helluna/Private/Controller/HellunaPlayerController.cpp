// Fill out your copyright notice in the Description page of Project Settings.

/**
 * @file    Controller/HellunaPlayerController.cpp
 * @brief   AHellunaPlayerController 구현
 *
 * @details Helluna 게임 전용 PlayerController의 구현 파일입니다.
 *          투표 시스템 컴포넌트 생성 및 초기화를 담당합니다.
 *
 * @author  [작성자]
 * @date    2026-02-05
 */

#include "Controller/HellunaPlayerController.h"
#include "Utils/Vote/VoteInputComponent.h"

// ============================================================================
// 생성자
// ============================================================================

AHellunaPlayerController::AHellunaPlayerController()
{
	// =========================================================================================
	// [투표 시스템] VoteInputComponent 생성 (김기현)
	// =========================================================================================
	VoteInputComponent = CreateDefaultSubobject<UVoteInputComponent>(TEXT("VoteInputComponent"));

	UE_LOG(LogTemp, Log, TEXT("[HellunaPlayerController] 생성자 - VoteInputComponent 생성됨"));
}

// ============================================================================
// 라이프사이클
// ============================================================================

void AHellunaPlayerController::BeginPlay()
{
	Super::BeginPlay();

	UE_LOG(LogTemp, Log, TEXT("[HellunaPlayerController] BeginPlay - %s"),
		IsLocalController() ? TEXT("로컬") : TEXT("서버"));
}
