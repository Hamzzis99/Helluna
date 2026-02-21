// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Player/Inv_PlayerController.h"
#include "GenericTeamAgentInterface.h"
#include "HellunaHeroController.generated.h"

class UVoteWidget;
class UVoteManagerComponent;

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

protected:
	virtual void BeginPlay() override;

	// =========================================================================================
	// [투표 시스템] 위젯 자동 생성 (김기현)
	// =========================================================================================

	/**
	 * @brief   투표 UI 위젯 클래스 (BP에서 WBP_VoteWidget 지정)
	 * @note    None이면 투표 위젯을 생성하지 않음
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Vote|UI", meta = (DisplayName = "Vote Widget Class (투표 위젯 클래스)"))
	TSubclassOf<UVoteWidget> VoteWidgetClass;

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
};
