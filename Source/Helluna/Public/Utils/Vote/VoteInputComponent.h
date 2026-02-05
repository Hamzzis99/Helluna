// Fill out your copyright notice in the Description page of Project Settings.

/**
 * @file    Utils/Vote/VoteInputComponent.h
 * @brief   투표 입력(찬성/반대)을 처리하는 컴포넌트
 *
 * @details PlayerController에 부착하여 Enhanced Input으로 투표 입력을 처리합니다.
 *          투표가 시작되면 자동으로 입력 매핑이 활성화되고, 종료 시 비활성화됩니다.
 *
 * @usage   사용 방법:
 *          1) BP에서 IA_VoteAgree, IA_VoteDisagree, IMC_Vote 에셋 설정
 *          2) PlayerController에 컴포넌트 추가
 *          3) 투표 시작 시 자동으로 IMC 활성화, 종료 시 비활성화
 *
 * @flow    입력 흐름:
 *          F1 → IA_VoteAgree → OnVoteAgreeInput() → VoteManager->Server_SubmitVote(MyPS, true)
 *          F2 → IA_VoteDisagree → OnVoteDisagreeInput() → VoteManager->Server_SubmitVote(MyPS, false)
 *
 * @note    Content/Input/ 폴더에 다음 에셋 필요:
 *          - IA_VoteAgree.uasset (InputAction)
 *          - IA_VoteDisagree.uasset (InputAction)
 *          - IMC_Vote.uasset (InputMappingContext) - F1, F2 바인딩
 *
 * @author  [작성자]
 * @date    2026-02-05
 */

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "VoteInputComponent.generated.h"

// Forward Declarations
class UInputAction;
class UInputMappingContext;
class UVoteManagerComponent;
class UEnhancedInputLocalPlayerSubsystem;
struct FInputActionValue;

/**
 * @brief   투표 입력 처리 컴포넌트
 * @details PlayerController에 부착하여 F1(찬성)/F2(반대) 입력을 처리합니다.
 *          VoteManager의 델리게이트에 바인딩하여 투표 시작/종료에 맞춰
 *          자동으로 입력 매핑 컨텍스트를 활성화/비활성화합니다.
 */
UCLASS(Blueprintable, ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class HELLUNA_API UVoteInputComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVoteInputComponent();

	// ========================================================================
	// 입력 설정 (BP에서 설정)
	// ========================================================================

	/**
	 * @brief 찬성 입력 액션 (예: F1)
	 * @note  BP에서 IA_VoteAgree 에셋 지정
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Vote|Input", meta = (DisplayName = "Vote Agree Action (투표 동의 액션)"))
	TObjectPtr<UInputAction> VoteAgreeAction;

	/**
	 * @brief 반대 입력 액션 (예: F2)
	 * @note  BP에서 IA_VoteDisagree 에셋 지정
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Vote|Input", meta = (DisplayName = "Vote Disagree Action (투표 반대 액션)"))
	TObjectPtr<UInputAction> VoteDisagreeAction;

	/**
	 * @brief 투표 입력 매핑 컨텍스트
	 * @note  BP에서 IMC_Vote 에셋 지정
	 * @note  F1 → VoteAgreeAction, F2 → VoteDisagreeAction 매핑 포함
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Vote|Input", meta = (DisplayName = "Vote Mapping Context (투표 입력 매핑)"))
	TObjectPtr<UInputMappingContext> VoteMappingContext;

	/**
	 * @brief IMC 우선순위
	 * @note  다른 입력 컨텍스트와의 우선순위 설정
	 * @note  높을수록 우선 처리
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Vote|Input", meta = (DisplayName = "Mapping Context Priority (매핑 우선순위)"))
	int32 MappingContextPriority = 1;

	// ========================================================================
	// Public 함수
	// ========================================================================

	/**
	 * @brief   투표 입력 활성화
	 *
	 * @details 투표 시작 시 호출됩니다.
	 *          IMC를 Enhanced Input Subsystem에 추가하여 투표 키 입력을 활성화합니다.
	 *
	 * @note    VoteManager의 OnVoteStarted 델리게이트에 바인딩됨
	 */
	UFUNCTION(BlueprintCallable, Category = "Vote")
	void EnableVoteInput();

	/**
	 * @brief   투표 입력 비활성화
	 *
	 * @details 투표 종료 시 호출됩니다.
	 *          IMC를 Enhanced Input Subsystem에서 제거하여 투표 키 입력을 비활성화합니다.
	 *
	 * @note    VoteManager의 OnVoteEnded 델리게이트에 바인딩됨
	 */
	UFUNCTION(BlueprintCallable, Category = "Vote")
	void DisableVoteInput();

	/**
	 * @brief   투표 입력 활성화 여부
	 * @return  true: 입력 활성화됨, false: 입력 비활성화됨
	 */
	UFUNCTION(BlueprintPure, Category = "Vote")
	bool IsVoteInputEnabled() const { return bVoteInputEnabled; }

protected:
	// ========================================================================
	// 라이프사이클 오버라이드
	// ========================================================================

	/** 컴포넌트 시작 시 호출 */
	virtual void BeginPlay() override;

	/** 컴포넌트 종료 시 호출 */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	// ========================================================================
	// 입력 핸들러
	// ========================================================================

	/**
	 * @brief 찬성 입력 처리
	 * @param Value - 입력 값 (미사용, 버튼이므로)
	 */
	void OnVoteAgreeInput(const FInputActionValue& Value);

	/**
	 * @brief 반대 입력 처리
	 * @param Value - 입력 값 (미사용, 버튼이므로)
	 */
	void OnVoteDisagreeInput(const FInputActionValue& Value);

	/**
	 * @brief   실제 투표 제출 (공통)
	 * @param   bAgree - true: 찬성, false: 반대
	 * @details VoteManager->Server_SubmitVote()를 호출합니다.
	 */
	void SubmitVote(bool bAgree);

	// ========================================================================
	// 델리게이트 핸들러 (VoteManager 이벤트 수신)
	// ========================================================================

	/**
	 * @brief 투표 시작 시 호출 (델리게이트 핸들러)
	 * @param Request - 시작된 투표 요청 정보
	 */
	UFUNCTION()
	void OnVoteStarted(const FVoteRequest& Request);

	/**
	 * @brief 투표 종료 시 호출 (델리게이트 핸들러)
	 * @param VoteType - 종료된 투표 종류
	 * @param bPassed - 통과 여부
	 * @param Reason - 종료 사유
	 */
	UFUNCTION()
	void OnVoteEnded(EVoteType VoteType, bool bPassed, const FString& Reason);

	// ========================================================================
	// 헬퍼 함수
	// ========================================================================

	/**
	 * @brief   VoteManager 캐시/검색
	 * @return  GameState에 부착된 VoteManagerComponent
	 * @note    캐시된 값이 유효하면 캐시 반환, 아니면 검색 후 캐시
	 */
	UVoteManagerComponent* GetVoteManager() const;

	/**
	 * @brief   로컬 PlayerState 가져오기
	 * @return  이 컴포넌트 소유 PlayerController의 PlayerState
	 */
	APlayerState* GetLocalPlayerState() const;

	/**
	 * @brief   Enhanced Input Subsystem 가져오기
	 * @return  로컬 플레이어의 EnhancedInputLocalPlayerSubsystem
	 */
	UEnhancedInputLocalPlayerSubsystem* GetEnhancedInputSubsystem() const;

	// ========================================================================
	// 내부 상태
	// ========================================================================

	/** 입력 활성화 여부 */
	bool bVoteInputEnabled = false;

	/** VoteManager 캐시 (WeakPtr) */
	mutable TWeakObjectPtr<UVoteManagerComponent> CachedVoteManager;
};
