// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Player/Inv_PlayerController.h"
#include "GenericTeamAgentInterface.h"
#include "HellunaHeroController.generated.h"

class UVoteWidget;
class UVoteManagerComponent;
class UHellunaGameResultWidget;
class UHellunaChatWidget;
class UInputAction;
class UInputMappingContext;

/**
 * @brief   Helluna 영웅 전용 PlayerController
 * @details AInv_PlayerController를 상속받아 인벤토리 기능을 유지하면서
 *          팀 시스템(IGenericTeamAgentInterface)을 제공합니다.
 *          GAS(HellunaHeroGameplayAbility)에서 Cast 대상으로 사용됩니다.
 *
 *          상속 구조:
 *          APlayerController
 *            └── AInv_PlayerController (인벤토리/장비)
 *                  └── AHellunaHeroController (팀ID, GAS, 투표 시스템)
 *                        └── BP_HellunaHeroController (에디터 BP)
 */
UCLASS()
class HELLUNA_API AHellunaHeroController : public AInv_PlayerController, public IGenericTeamAgentInterface
{
	GENERATED_BODY()

public:
	AHellunaHeroController();

	//~ Begin IGenericTeamAgentInterface Interface.
	virtual FGenericTeamId GetGenericTeamId() const override;
	//~ End IGenericTeamAgentInterface Interface

	// =========================================================================================
	// [투표 시스템] Server RPC (김기현)
	// =========================================================================================

	/**
	 * @brief   투표 제출 Server RPC (클라이언트 → 서버)
	 *
	 * @details PlayerController는 클라이언트의 NetConnection을 소유하므로
	 *          Server RPC가 정상 작동합니다.
	 *          내부에서 VoteManagerComponent::ReceiveVote()를 호출합니다.
	 *
	 * @param   bAgree - true: 찬성, false: 반대
	 */
	UFUNCTION(Server, Reliable)
	void Server_SubmitVote(bool bAgree);

	// =========================================================================================
	// [Phase 10] 채팅 시스템 Server RPC
	// =========================================================================================

	/**
	 * 채팅 메시지 전송 Server RPC (클라이언트 → 서버)
	 * @param Message  전송할 메시지 (1~200자)
	 */
	UFUNCTION(Server, Reliable, WithValidation)
	void Server_SendChatMessage(const FString& Message);

	/**
	 * 채팅 입력 토글 (BP에서도 호출 가능)
	 * Enter 키 입력 시 호출 → 위젯의 입력창 활성/비활성 전환
	 */
	UFUNCTION(BlueprintCallable, Category = "Chat (채팅)")
	void ToggleChatInput();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override; // C5: 타이머/델리게이트 정리

	// =========================================================================================
	// [투표 시스템] 위젯 자동 생성 (김기현)
	// =========================================================================================

	/**
	 * @brief   투표 UI 위젯 클래스 (BP에서 WBP_VoteWidget 지정)
	 * @note    None이면 투표 위젯을 생성하지 않음
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Vote|UI", meta = (DisplayName = "Vote Widget Class (투표 위젯 클래스)"))
	TSubclassOf<UVoteWidget> VoteWidgetClass;

	// =========================================================================================
	// [Phase 10] 채팅 위젯 설정
	// =========================================================================================

	/** 채팅 위젯 클래스 (BP에서 WBP_HellunaChatWidget 지정) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Chat|UI (채팅|UI)",
		meta = (DisplayName = "Chat Widget Class (채팅 위젯 클래스)"))
	TSubclassOf<UHellunaChatWidget> ChatWidgetClass;

	/** 채팅 토글 입력 액션 (Enter 키에 매핑된 IA 에셋) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Chat|Input (채팅|입력)",
		meta = (DisplayName = "Chat Toggle Action (채팅 토글 액션)"))
	TObjectPtr<UInputAction> ChatToggleAction;

	/** 채팅 입력 매핑 컨텍스트 (Enter 키 → ChatToggleAction) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Chat|Input (채팅|입력)",
		meta = (DisplayName = "Chat Mapping Context (채팅 입력 매핑)"))
	TObjectPtr<UInputMappingContext> ChatMappingContext;

private:
	FGenericTeamId HeroTeamID;

	// =========================================================================================
	// [투표 시스템] 위젯 초기화 내부 함수 (김기현)
	// =========================================================================================

	/** 투표 위젯 생성 및 VoteManager 바인딩 */
	void InitializeVoteWidget();

	/** 타이머 핸들 (GameState 복제 대기용) */
	FTimerHandle VoteWidgetInitTimerHandle;

	/** 생성된 투표 위젯 인스턴스 */
	UPROPERTY()
	TObjectPtr<UVoteWidget> VoteWidgetInstance;

	// =========================================================================================
	// [Phase 10] 채팅 시스템 내부 함수
	// =========================================================================================

	/** 채팅 위젯 초기화 (GameState 복제 대기 + 재시도) */
	void InitializeChatWidget();

	/** 채팅 위젯 초기화 타이머 핸들 */
	FTimerHandle ChatWidgetInitTimerHandle;

	/** 생성된 채팅 위젯 인스턴스 */
	UPROPERTY()
	TObjectPtr<UHellunaChatWidget> ChatWidgetInstance;

	/** 채팅 입력 핸들러 (Enhanced Input에서 호출) */
	void OnChatToggleInput(const struct FInputActionValue& Value);

	/** 위젯에서 메시지 제출 시 콜백 */
	UFUNCTION()
	void OnChatMessageSubmitted(const FString& Message);

	/** 스팸 방지: 마지막 메시지 시간 (서버 전용) */
	double LastChatMessageTime = 0.0;
	static constexpr double ChatCooldownSeconds = 0.5;

	// =========================================================================================
	// [디버그] 서버 치트 RPC — 클라이언트에서 서버 GameMode 함수 호출 (김기현)
	// =========================================================================================
public:
	/**
	 * [디버그] 클라이언트 → 서버: 강제 게임 종료
	 * BP에서 키보드 입력(F9 등)에 바인딩하여 사용
	 * @param ReasonIndex  0=Escaped, 1=AllDead, 2=ServerShutdown
	 */
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Debug(디버그)",
		meta = (DisplayName = "Cheat End Game (치트 게임 종료)"))
	void Server_CheatEndGame(uint8 ReasonIndex);

	// =========================================================================================
	// [Phase 7] 게임 결과 UI (김기현)
	// =========================================================================================
	/**
	 * [서버 → 클라이언트] 게임 결과 UI 표시 RPC
	 *
	 * @param ResultItems  보존된 아이템 목록 (사망자는 빈 배열)
	 * @param bSurvived    생존 여부
	 * @param Reason       종료 사유 문자열
	 * @param LobbyURL     로비 서버 접속 URL
	 */
	UFUNCTION(Client, Reliable)
	void Client_ShowGameResult(const TArray<FInv_SavedItemData>& ResultItems, bool bSurvived,
		const FString& Reason, const FString& LobbyURL);

protected:
	/** 결과 위젯 클래스 (BP에서 설정) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "GameResult|UI",
		meta = (DisplayName = "Game Result Widget Class (결과 위젯 클래스)"))
	TSubclassOf<UHellunaGameResultWidget> GameResultWidgetClass;

private:
	/** 생성된 결과 위젯 인스턴스 */
	UPROPERTY()
	TObjectPtr<UHellunaGameResultWidget> GameResultWidgetInstance;
};
