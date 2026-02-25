// ════════════════════════════════════════════════════════════════════════════════
// HellunaLobbyGameMode.h
// ════════════════════════════════════════════════════════════════════════════════
//
// 로비 전용 GameMode — Stash/Loadout 듀얼 Grid UI 관리
//
// 📌 상속 구조:
//    AGameMode → AInv_SaveGameMode → AHellunaBaseGameMode → AHellunaLobbyGameMode
//
// 📌 역할:
//    - PostLogin: 크래시 복구 → SQLite Stash 로드 → StashComp에 RestoreFromSaveData
//    - Logout: 현재 Stash/Loadout 상태를 SQLite에 저장
//    - 인게임 캐릭터 스폰/전투 로직은 전혀 없음 (로비 전용!)
//
// 📌 사용법:
//    BP_HellunaLobbyGameMode에서 이 클래스를 부모로 지정
//    로비 맵(L_Lobby)의 WorldSettings에서 GameMode Override로 설정
//
// 작성자: Gihyeon (Claude Code 보조)
// ════════════════════════════════════════════════════════════════════════════════

#pragma once

#include "CoreMinimal.h"
#include "GameMode/HellunaBaseGameMode.h"
#include "HellunaLobbyGameMode.generated.h"

// 전방 선언
class AHellunaLobbyController;
class UHellunaSQLiteSubsystem;
class UInv_InventoryComponent;

UCLASS()
class HELLUNA_API AHellunaLobbyGameMode : public AHellunaBaseGameMode
{
	GENERATED_BODY()

public:
	AHellunaLobbyGameMode();

	// ════════════════════════════════════════════════════════════════
	// GameMode 오버라이드
	// ════════════════════════════════════════════════════════════════

	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void Logout(AController* Exiting) override;

	/** PlayerId 획득 (public 래퍼 — Controller에서 호출용) */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "로비",
		meta = (DisplayName = "플레이어 ID 가져오기"))
	FString GetLobbyPlayerId(APlayerController* PC) const { return GetPlayerSaveId(PC); }

protected:
	// ════════════════════════════════════════════════════════════════
	// Stash 로드/저장 헬퍼
	// ════════════════════════════════════════════════════════════════

	/**
	 * SQLite에서 Stash 로드 → StashComp에 RestoreFromSaveData
	 *
	 * @param LobbyPC  대상 로비 컨트롤러
	 * @param PlayerId 플레이어 고유 ID
	 */
	void LoadStashToComponent(AHellunaLobbyController* LobbyPC, const FString& PlayerId);

	/**
	 * 현재 StashComp + LoadoutComp 상태를 SQLite에 저장
	 *
	 * @param LobbyPC  대상 로비 컨트롤러
	 * @param PlayerId 플레이어 고유 ID
	 */
	void SaveComponentsToDatabase(AHellunaLobbyController* LobbyPC, const FString& PlayerId);

	/**
	 * ItemType → UInv_ItemComponent* 리졸버 (RestoreFromSaveData에 전달)
	 * 기존 HellunaBaseGameMode::ResolveItemClass()를 활용
	 */
	UInv_ItemComponent* ResolveItemTemplate(const FGameplayTag& ItemType);

	// ════════════════════════════════════════════════════════════════
	// SQLite 서브시스템 캐시
	// ════════════════════════════════════════════════════════════════

	/** SQLite 서브시스템 참조 (BeginPlay에서 캐시) */
	UPROPERTY()
	TObjectPtr<UHellunaSQLiteSubsystem> SQLiteSubsystem;
};
