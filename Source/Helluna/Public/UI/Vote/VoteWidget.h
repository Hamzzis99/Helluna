// Fill out your copyright notice in the Description page of Project Settings.

/**
 * @file    UI/Vote/VoteWidget.h
 * @brief   투표 UI 위젯 베이스 클래스
 *
 * @details VoteManager의 델리게이트에 바인딩하여 투표 상태를 표시합니다.
 *          Left 4 Dead 스타일의 간단하고 직관적인 UI를 제공합니다.
 *
 *          표시 정보:
 *          - 투표 종류 (맵 이동, 강퇴, 난이도)
 *          - 투표 대상 (맵 이름, 플레이어 등)
 *          - 찬성/반대 카운트
 *          - 남은 시간
 *          - 입력 안내 (F1 찬성, F2 반대)
 *
 * @usage   1. 이 클래스를 상속받는 WBP_VoteWidget 생성
 *          2. BP에서 UI 요소 배치 및 바인딩
 *          3. HUD에서 WBP_VoteWidget 인스턴스 생성/관리
 *
 * @author  [작성자]
 * @date    2026-02-05
 */

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Utils/Vote/VoteTypes.h"
#include "VoteWidget.generated.h"

class UTextBlock;
class UProgressBar;
class UVoteManagerComponent;
class UVoteResultWidget;

/**
 * @brief   투표 UI 위젯 베이스 클래스
 * @details VoteManager 델리게이트에 바인딩하여 투표 상태를 표시합니다.
 *          BP에서 상속받아 UI 요소를 배치하고 바인딩합니다.
 */
UCLASS()
class HELLUNA_API UVoteWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/**
	 * @brief   [Init] 위젯 초기화 및 VoteManager 바인딩
	 * @param   VoteManager - 바인딩할 VoteManagerComponent
	 * @note    HUD 또는 PlayerController에서 위젯 생성 후 호출
	 */
	UFUNCTION(BlueprintCallable, Category = "Vote|UI")
	void InitializeVoteWidget(UVoteManagerComponent* VoteManager);

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	// ========================================================================
	// UI 업데이트 함수 [Callback] (BP에서 오버라이드 가능)
	// ========================================================================

	/**
	 * @brief   [Callback] 투표 시작 시 호출 - UI 표시
	 * @param   Request - 투표 요청 정보
	 * @note    BP에서 오버라이드하여 애니메이션 등 추가 가능
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "Vote|UI")
	void OnVoteStarted(const FVoteRequest& Request);

	/**
	 * @brief   [Callback] 투표 현황 업데이트 시 호출
	 * @param   Status - 현재 투표 현황 (찬성/반대 수, 남은 시간 등)
	 * @note    BP에서 오버라이드하여 커스텀 UI 갱신 가능
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "Vote|UI")
	void OnVoteUpdated(const FVoteStatus& Status);

	/**
	 * @brief   [Callback] 투표 종료 시 호출 - UI 숨김
	 * @param   VoteType - 종료된 투표 종류
	 * @param   bPassed  - 통과 여부
	 * @param   Reason   - 종료 사유
	 * @note    BP에서 오버라이드하여 결과 표시 애니메이션 추가 가능
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "Vote|UI")
	void OnVoteEnded(EVoteType VoteType, bool bPassed, const FString& Reason);

	// ========================================================================
	// UI 요소 (BP에서 바인딩)
	// ========================================================================

	// ========================================================================
	// UI 요소 (BP에서 바인딩) [Vote Info] (투표 정보)
	// ========================================================================

	/** [Title] 투표 제목 (예: "맵 이동 투표") - 필수 */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidget, DisplayName = "Vote Title Text (투표 제목)"))
	TObjectPtr<UTextBlock> Text_VoteTitle;

	/** [Target] 투표 대상 (예: "→ TestMap") - 필수 */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidget, DisplayName = "Vote Target Text (투표 대상)"))
	TObjectPtr<UTextBlock> Text_VoteTarget;

	/** [Count] 찬성 카운트 (예: "찬성: 2 / 4") - 필수 */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidget, DisplayName = "Agree Count Text (찬성 수)"))
	TObjectPtr<UTextBlock> Text_AgreeCount;

	/** [Count] 반대 카운트 (예: "반대: 1 / 4") - 필수 */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidget, DisplayName = "Disagree Count Text (반대 수)"))
	TObjectPtr<UTextBlock> Text_DisagreeCount;

	/** [Time] 남은 시간 (예: "남은 시간: 25초") - 필수 */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidget, DisplayName = "Remaining Time Text (남은 시간)"))
	TObjectPtr<UTextBlock> Text_RemainingTime;

	/** [Time] 시간 프로그레스 바 - 선택 */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidgetOptional, DisplayName = "Time Progress Bar (시간 프로그레스 바)"))
	TObjectPtr<UProgressBar> ProgressBar_Time;

	// ========================================================================
	// 결과 위젯 설정 [Result] (투표 결과)
	// ========================================================================

	/**
	 * @brief   [Result] 투표 결과 위젯 클래스 (BP에서 WBP_VoteResultWidget 지정)
	 * @note    None이면 결과 메시지를 표시하지 않고 즉시 숨김
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Vote|UI", meta = (DisplayName = "Vote Result Widget Class (투표 결과 위젯 클래스)"))
	TSubclassOf<UVoteResultWidget> VoteResultWidgetClass;

	// ========================================================================
	// 헬퍼 함수 [Helper] (도우미)
	// ========================================================================

	/**
	 * @brief   [Helper] 투표 종류에 따른 제목 문자열 반환
	 * @param   VoteType - 투표 종류
	 * @return  "맵 이동 투표", "강퇴 투표" 등
	 */
	UFUNCTION(BlueprintPure, Category = "Vote|UI")
	FString GetVoteTitleText(EVoteType VoteType) const;

	/**
	 * @brief   [Helper] 투표 대상 문자열 반환
	 * @param   Request - 투표 요청 정보
	 * @return  "→ MapName", "대상: PlayerName" 등
	 */
	UFUNCTION(BlueprintPure, Category = "Vote|UI")
	FString GetVoteTargetText(const FVoteRequest& Request) const;

private:
	/** [Internal] VoteManager 참조 (WeakPtr) */
	UPROPERTY()
	TWeakObjectPtr<UVoteManagerComponent> CachedVoteManager;

	/** [Internal] 현재 투표 요청 정보 캐시 */
	FVoteRequest CurrentRequest;

	/** [Internal] 타임아웃 값 캐시 (프로그레스 바용) */
	float CachedTimeout = 0.0f;
};
