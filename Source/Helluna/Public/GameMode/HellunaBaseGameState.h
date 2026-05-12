// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameState.h"
#include "Utils/Vote/VoteManagerComponent.h"
#include "HellunaTypes.h"
#include "HellunaBaseGameState.generated.h"

// =========================================================================================
// 📌 HellunaBaseGameState
// =========================================================================================
//
// 📌 역할: 모든 게임모드에서 공유하는 공통 시스템을 제공하는 Base GameState
//
// 📌 포함 시스템:
//    🗳️ 투표 시스템: VoteManagerComponent (맵 이동, 강퇴 등)
//    🎭 캐릭터 선택: UsedCharacters 배열 + 델리게이트
//    🗺️ 맵 이동: Server_SaveAndMoveLevel() + OnPreMapTransition() 훅
//
// 📌 상속 구조:
//    AGameState → AHellunaBaseGameState → AHellunaDefenseGameState (Defense 전용)
//
// 📌 작성자: Gihyeon
// 📌 작성일: 2026-02-06
// =========================================================================================

class UVoteManagerComponent;

UCLASS()
class HELLUNA_API AHellunaBaseGameState : public AGameState
{
	GENERATED_BODY()

public:
	/** 생성자 */
	AHellunaBaseGameState();

	// =========================================================================================
	// [투표 시스템] VoteManagerComponent (김기현)
	// =========================================================================================
	//
	// 📌 역할: 멀티플레이어 투표 시스템 관리 (맵 이동, 강퇴, 난이도 변경 등)
	// 📌 사용: MoveMapActor 등에서 StartVote() 호출하여 투표 시작
	// 📌 복제: 컴포넌트 자체가 복제되어 클라이언트에서도 상태 확인 가능
	//
	// =========================================================================================

	/** 투표 관리 컴포넌트 (맵 이동, 강퇴 등 투표 처리) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Vote")
	TObjectPtr<UVoteManagerComponent> VoteManagerComponent;

	// ═══════════════════════════════════════════════════════════════════════════════
	// 🎭 캐릭터 선택 시스템 - 실시간 UI 갱신용 (김기현)
	// ═══════════════════════════════════════════════════════════════════════════════
	//
	// 📌 목적: 다른 플레이어가 캐릭터 선택 시 모든 클라이언트 UI 자동 갱신
	// 📌 구조: UsedCharacters 배열이 변경되면 OnRep → 델리게이트 브로드캐스트
	//
	// ═══════════════════════════════════════════════════════════════════════════════

	/** 캐릭터 사용 상태 변경 델리게이트 (UI 바인딩용) */
	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnUsedCharactersChanged);

	UPROPERTY(BlueprintAssignable, Category = "Character Select")
	FOnUsedCharactersChanged OnUsedCharactersChanged;

	/** 특정 캐릭터가 사용 중인지 확인 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Character Select")
	bool IsCharacterUsed(EHellunaHeroType HeroType) const;

	/** 사용 중인 캐릭터 목록 반환 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Character Select")
	TArray<EHellunaHeroType> GetUsedCharacters() const { return UsedCharacters; }

	/** [서버 전용] 캐릭터 사용 등록 */
	void AddUsedCharacter(EHellunaHeroType HeroType);

	/** [서버 전용] 캐릭터 사용 해제 */
	void RemoveUsedCharacter(EHellunaHeroType HeroType);

	// =========================================================================================
	// 🗺️ 맵 이동 공통 로직
	// =========================================================================================

	/**
	 * [서버 전용] 현재 상태를 저장하고, 다음 레벨로 이동합니다.
	 *
	 * ⚠️ [Fix:gamestate-rpc-warning 2026-05-02] DO NOT add Server RPC macro to this function.
	 * GameState에서 Server RPC는 라우팅 실패 (silent failure). 반드시 PlayerController에 두어야 함.
	 * 현재는 일반 함수로 HasAuthority 가드 적용 — 클라가 호출해도 early return.
	 */
	UFUNCTION(BlueprintCallable, Category = "Helluna|System")
	void Server_SaveAndMoveLevel(FName NextLevelName);

protected:
	/**
	 * 맵 이동 전 자식 클래스 전용 처리 훅 (가상함수)
	 * Defense: WriteDataToDisk() 호출
	 * 탐험/보스전: 각자 필요한 저장 로직 구현
	 */
	virtual void OnPreMapTransition();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** 현재 사용 중인 캐릭터 목록 (Replicated) */
	UPROPERTY(ReplicatedUsing = OnRep_UsedCharacters)
	TArray<EHellunaHeroType> UsedCharacters;

	/** 캐릭터 목록 변경 시 클라이언트에서 호출 */
	UFUNCTION()
	void OnRep_UsedCharacters();
};
