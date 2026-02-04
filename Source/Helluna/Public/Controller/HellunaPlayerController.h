// Fill out your copyright notice in the Description page of Project Settings.

/**
 * @file    Controller/HellunaPlayerController.h
 * @brief   Helluna 게임 전용 PlayerController
 *
 * @details AInv_PlayerController를 상속받아 Helluna 게임 전용 기능을 추가합니다.
 *          인벤토리 기능은 부모 클래스에서 처리하고, 여기서는 게임 전용 기능만 담당합니다.
 *
 *          포함 기능:
 *          - 투표 시스템 입력 처리 (VoteInputComponent)
 *          - (추후) 채팅, 기타 Helluna 전용 기능
 *
 * @usage   1. 이 클래스를 생성
 *          2. 에디터에서 BP_Inv_PlayerController의 부모를 AHellunaPlayerController로 변경
 *          3. GameMode의 PlayerControllerClass 확인
 *
 * @author  [작성자]
 * @date    2026-02-05
 */

#pragma once

#include "CoreMinimal.h"
#include "Player/Inv_PlayerController.h"
#include "HellunaPlayerController.generated.h"

class UVoteInputComponent;

/**
 * @brief   Helluna 게임 전용 PlayerController
 * @details AInv_PlayerController의 인벤토리 기능을 유지하면서
 *          투표 시스템 등 Helluna 전용 기능을 추가합니다.
 */
UCLASS()
class HELLUNA_API AHellunaPlayerController : public AInv_PlayerController
{
	GENERATED_BODY()

public:
	AHellunaPlayerController();

	// =========================================================================================
	// [투표 시스템] (김기현)
	// =========================================================================================

	/**
	 * @brief 투표 입력 처리 컴포넌트
	 * @note  F1: 찬성, F2: 반대
	 * @note  투표 시작 시 자동으로 입력 활성화, 종료 시 비활성화
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Vote")
	TObjectPtr<UVoteInputComponent> VoteInputComponent;

protected:
	virtual void BeginPlay() override;
};
