// Fill out your copyright notice in the Description page of Project Settings.

// ════════════════════════════════════════════════════════════════════════════════
// HellunaBaseGameState.cpp
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 이 파일의 역할:
//    공통 시스템(투표, 캐릭터 선택, 맵 이동)을 구현하는 Base GameState
//
// 📌 작성자: Gihyeon
// ════════════════════════════════════════════════════════════════════════════════

#include "GameMode/HellunaBaseGameState.h"
#include "GameMode/HellunaBaseGameMode.h"
#include "Helluna.h"
#include "MDF_Function/MDF_Instance/MDF_GameInstance.h"
#include "Net/UnrealNetwork.h"

// =========================================================================================
// 생성자
// =========================================================================================

AHellunaBaseGameState::AHellunaBaseGameState()
{
	VoteManagerComponent = CreateDefaultSubobject<UVoteManagerComponent>(TEXT("VoteManagerComponent"));

	UE_LOG(LogHellunaVote, Log, TEXT("[HellunaBaseGameState] 생성자 - VoteManagerComponent 생성됨"));
}

// =========================================================================================
// 복제 프로퍼티
// =========================================================================================

void AHellunaBaseGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AHellunaBaseGameState, UsedCharacters);
}

// ═══════════════════════════════════════════════════════════════════════════════
// 🎭 캐릭터 선택 시스템 - 실시간 UI 갱신 (김기현)
// ═══════════════════════════════════════════════════════════════════════════════

void AHellunaBaseGameState::OnRep_UsedCharacters()
{
#if HELLUNA_DEBUG_GAMEMODE
	UE_LOG(LogTemp, Warning, TEXT("[BaseGameState] OnRep_UsedCharacters - 사용 중인 캐릭터: %d명"), UsedCharacters.Num());
#endif

	// 델리게이트 브로드캐스트 → 모든 바인딩된 UI 갱신!
	OnUsedCharactersChanged.Broadcast();
}

bool AHellunaBaseGameState::IsCharacterUsed(EHellunaHeroType HeroType) const
{
	return UsedCharacters.Contains(HeroType);
}

void AHellunaBaseGameState::AddUsedCharacter(EHellunaHeroType HeroType)
{
	if (!HasAuthority()) return;

	if (HeroType == EHellunaHeroType::None) return;

	if (!UsedCharacters.Contains(HeroType))
	{
		UsedCharacters.Add(HeroType);
#if HELLUNA_DEBUG_GAMEMODE
		UE_LOG(LogTemp, Warning, TEXT("[BaseGameState] 캐릭터 사용 등록: %s (총 %d명)"),
			*UEnum::GetValueAsString(HeroType), UsedCharacters.Num());
#endif

		// 서버에서도 델리게이트 호출 (Listen Server용)
		OnUsedCharactersChanged.Broadcast();
	}
}

void AHellunaBaseGameState::RemoveUsedCharacter(EHellunaHeroType HeroType)
{
	if (!HasAuthority()) return;

	if (UsedCharacters.Contains(HeroType))
	{
		UsedCharacters.Remove(HeroType);
#if HELLUNA_DEBUG_GAMEMODE
		UE_LOG(LogTemp, Warning, TEXT("[BaseGameState] 캐릭터 사용 해제: %s (총 %d명)"),
			*UEnum::GetValueAsString(HeroType), UsedCharacters.Num());
#endif

		// 서버에서도 델리게이트 호출 (Listen Server용)
		OnUsedCharactersChanged.Broadcast();
	}
}

// =========================================================================================
// 🗺️ 맵 이동 공통 로직
// =========================================================================================

void AHellunaBaseGameState::Server_SaveAndMoveLevel(FName NextLevelName)
{
	if (!HasAuthority()) return;

	if (NextLevelName.IsNone())
	{
#if HELLUNA_DEBUG_GAMEMODE
		UE_LOG(LogTemp, Error, TEXT("[BaseGameState] 이동할 맵 이름이 없습니다!"));
#endif
		return;
	}

#if HELLUNA_DEBUG_GAMEMODE
	UE_LOG(LogTemp, Warning, TEXT("[BaseGameState] 맵 이동 요청(%s). 저장 및 플래그 설정..."), *NextLevelName.ToString());
#endif

	// ============================================
	// 0. 모든 플레이어 인벤토리 저장 (맵 이동 전!)
	// ============================================
	// [Fix46-M8] GetWorld() null 체크
	UWorld* StateWorld = GetWorld();
	if (!StateWorld) { return; }
	if (AHellunaBaseGameMode* GM = StateWorld->GetAuthGameMode<AHellunaBaseGameMode>())
	{
		GM->SaveAllPlayersInventory();
	}
	else
	{
#if HELLUNA_DEBUG_GAMEMODE
		UE_LOG(LogTemp, Warning, TEXT("[BaseGameState] ⚠️ GameMode 없음 - 인벤토리 저장 생략"));
#endif
	}

	// 1. 자식 클래스 전용 저장 (가상함수 훅)
	OnPreMapTransition();

	// 2. GameInstance에 "나 이사 간다!" 플래그 설정
	UMDF_GameInstance* GI = Cast<UMDF_GameInstance>(GetGameInstance());
	if (GI)
	{
		GI->bIsMapTransitioning = true;
#if HELLUNA_DEBUG_GAMEMODE
		UE_LOG(LogTemp, Warning, TEXT("[BaseGameState] 이사 확인증 발급 완료 (bIsMapTransitioning = true)"));
#endif
	}
	else
	{
#if HELLUNA_DEBUG_GAMEMODE
		UE_LOG(LogTemp, Error, TEXT("[BaseGameState] GameInstance 형변환 실패! 프로젝트 설정을 확인하세요."));
#endif
	}

	// 3. ServerTravel 실행
	UWorld* World = GetWorld();
	if (World)
	{
		FString TravelURL = FString::Printf(TEXT("%s?listen"), *NextLevelName.ToString());
		World->ServerTravel(TravelURL, true, false);
	}
}

void AHellunaBaseGameState::OnPreMapTransition()
{
	// 빈 구현 - 자식 클래스에서 override하여 사용
}
