// Fill out your copyright notice in the Description page of Project Settings.

/**
 * @file    Utils/Vote/VoteInputComponent.cpp
 * @brief   VoteInputComponent 구현
 *
 * @details 투표 입력 처리 컴포넌트의 구현 파일입니다.
 *          Enhanced Input System을 사용하여 F1(찬성)/F2(반대) 입력을 처리합니다.
 *
 * @author  [작성자]
 * @date    2026-02-05
 */

#include "Utils/Vote/VoteInputComponent.h"
#include "Utils/Vote/VoteManagerComponent.h"
#include "Utils/Vote/VoteTypes.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/GameStateBase.h"
#include "Engine/World.h"

// ============================================================================
// 생성자
// ============================================================================

UVoteInputComponent::UVoteInputComponent()
{
	// 자동 활성화
	bAutoActivate = true;

	// Tick 불필요
	PrimaryComponentTick.bCanEverTick = false;

	UE_LOG(LogHellunaVote, Log, TEXT("[VoteInput] 생성자 호출 - 컴포넌트 생성됨"));
}

// ============================================================================
// 라이프사이클
// ============================================================================

void UVoteInputComponent::BeginPlay()
{
	Super::BeginPlay();

	UE_LOG(LogHellunaVote, Log, TEXT("[VoteInput] BeginPlay 진입"));

	// PlayerController에 부착되었는지 확인
	APlayerController* PC = Cast<APlayerController>(GetOwner());
	if (!PC)
	{
		UE_LOG(LogHellunaVote, Warning, TEXT("[VoteInput] BeginPlay - Owner가 PlayerController가 아님"));
		return;
	}

	// 로컬 플레이어인지 확인
	if (!PC->IsLocalController())
	{
		UE_LOG(LogHellunaVote, Log, TEXT("[VoteInput] BeginPlay - 로컬 컨트롤러 아님, 스킵"));
		return;
	}

	// VoteManager 델리게이트 바인딩
	if (UVoteManagerComponent* VoteManager = GetVoteManager())
	{
		VoteManager->OnVoteStarted.AddDynamic(this, &UVoteInputComponent::OnVoteStarted);
		VoteManager->OnVoteEnded.AddDynamic(this, &UVoteInputComponent::OnVoteEnded);
		UE_LOG(LogHellunaVote, Log, TEXT("[VoteInput] VoteManager 델리게이트 바인딩 완료"));
	}
	else
	{
		UE_LOG(LogHellunaVote, Warning, TEXT("[VoteInput] BeginPlay - VoteManager를 찾을 수 없음"));
	}

	// Enhanced Input 컴포넌트에 액션 바인딩
	if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PC->InputComponent))
	{
		if (VoteAgreeAction)
		{
			EIC->BindAction(VoteAgreeAction, ETriggerEvent::Started, this, &UVoteInputComponent::OnVoteAgreeInput);
			UE_LOG(LogHellunaVote, Log, TEXT("[VoteInput] VoteAgreeAction 바인딩 완료"));
		}
		else
		{
			UE_LOG(LogHellunaVote, Warning, TEXT("[VoteInput] VoteAgreeAction이 설정되지 않음"));
		}

		if (VoteDisagreeAction)
		{
			EIC->BindAction(VoteDisagreeAction, ETriggerEvent::Started, this, &UVoteInputComponent::OnVoteDisagreeInput);
			UE_LOG(LogHellunaVote, Log, TEXT("[VoteInput] VoteDisagreeAction 바인딩 완료"));
		}
		else
		{
			UE_LOG(LogHellunaVote, Warning, TEXT("[VoteInput] VoteDisagreeAction이 설정되지 않음"));
		}
	}
	else
	{
		UE_LOG(LogHellunaVote, Warning, TEXT("[VoteInput] EnhancedInputComponent를 찾을 수 없음"));
	}

	UE_LOG(LogHellunaVote, Log, TEXT("[VoteInput] BeginPlay 완료"));
}

void UVoteInputComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UE_LOG(LogHellunaVote, Log, TEXT("[VoteInput] EndPlay 진입 - Reason: %d"), static_cast<int32>(EndPlayReason));

	// 입력 비활성화
	DisableVoteInput();

	// VoteManager 델리게이트 언바인드
	if (UVoteManagerComponent* VoteManager = GetVoteManager())
	{
		VoteManager->OnVoteStarted.RemoveDynamic(this, &UVoteInputComponent::OnVoteStarted);
		VoteManager->OnVoteEnded.RemoveDynamic(this, &UVoteInputComponent::OnVoteEnded);
		UE_LOG(LogHellunaVote, Log, TEXT("[VoteInput] VoteManager 델리게이트 언바인드 완료"));
	}

	Super::EndPlay(EndPlayReason);

	UE_LOG(LogHellunaVote, Log, TEXT("[VoteInput] EndPlay 완료"));
}

// ============================================================================
// Public 함수 - 입력 활성화/비활성화
// ============================================================================

void UVoteInputComponent::EnableVoteInput()
{
	UE_LOG(LogHellunaVote, Log, TEXT("[VoteInput] EnableVoteInput 진입"));

	// 이미 활성화 상태면 리턴
	if (bVoteInputEnabled)
	{
		UE_LOG(LogHellunaVote, Log, TEXT("[VoteInput] EnableVoteInput - 이미 활성화됨"));
		return;
	}

	// IMC 체크
	if (!VoteMappingContext)
	{
		UE_LOG(LogHellunaVote, Warning, TEXT("[VoteInput] EnableVoteInput 실패 - VoteMappingContext가 설정되지 않음"));
		return;
	}

	// Enhanced Input Subsystem 가져오기
	UEnhancedInputLocalPlayerSubsystem* Subsystem = GetEnhancedInputSubsystem();
	if (!Subsystem)
	{
		UE_LOG(LogHellunaVote, Warning, TEXT("[VoteInput] EnableVoteInput 실패 - EnhancedInputSubsystem을 찾을 수 없음"));
		return;
	}

	// IMC 추가
	Subsystem->AddMappingContext(VoteMappingContext, MappingContextPriority);
	bVoteInputEnabled = true;

	UE_LOG(LogHellunaVote, Log, TEXT("[VoteInput] EnableVoteInput 완료 - IMC 추가됨, Priority: %d"), MappingContextPriority);
}

void UVoteInputComponent::DisableVoteInput()
{
	UE_LOG(LogHellunaVote, Log, TEXT("[VoteInput] DisableVoteInput 진입"));

	// 이미 비활성화 상태면 리턴
	if (!bVoteInputEnabled)
	{
		UE_LOG(LogHellunaVote, Log, TEXT("[VoteInput] DisableVoteInput - 이미 비활성화됨"));
		return;
	}

	// IMC 체크
	if (!VoteMappingContext)
	{
		bVoteInputEnabled = false;
		return;
	}

	// Enhanced Input Subsystem 가져오기
	UEnhancedInputLocalPlayerSubsystem* Subsystem = GetEnhancedInputSubsystem();
	if (!Subsystem)
	{
		bVoteInputEnabled = false;
		return;
	}

	// IMC 제거
	Subsystem->RemoveMappingContext(VoteMappingContext);
	bVoteInputEnabled = false;

	UE_LOG(LogHellunaVote, Log, TEXT("[VoteInput] DisableVoteInput 완료 - IMC 제거됨"));
}

// ============================================================================
// 입력 핸들러
// ============================================================================

void UVoteInputComponent::OnVoteAgreeInput(const FInputActionValue& Value)
{
	UE_LOG(LogHellunaVote, Log, TEXT("[VoteInput] OnVoteAgreeInput - 찬성 입력 감지"));
	SubmitVote(true);
}

void UVoteInputComponent::OnVoteDisagreeInput(const FInputActionValue& Value)
{
	UE_LOG(LogHellunaVote, Log, TEXT("[VoteInput] OnVoteDisagreeInput - 반대 입력 감지"));
	SubmitVote(false);
}

void UVoteInputComponent::SubmitVote(bool bAgree)
{
	UE_LOG(LogHellunaVote, Log, TEXT("[VoteInput] SubmitVote 진입 - bAgree: %s"),
		bAgree ? TEXT("찬성") : TEXT("반대"));

	// 입력 활성화 체크
	if (!bVoteInputEnabled)
	{
		UE_LOG(LogHellunaVote, Warning, TEXT("[VoteInput] SubmitVote 실패 - 입력이 활성화되지 않음"));
		return;
	}

	// VoteManager 가져오기
	UVoteManagerComponent* VoteManager = GetVoteManager();
	if (!VoteManager)
	{
		UE_LOG(LogHellunaVote, Warning, TEXT("[VoteInput] SubmitVote 실패 - VoteManager를 찾을 수 없음"));
		return;
	}

	// 투표 진행 중 체크
	if (!VoteManager->IsVoteInProgress())
	{
		UE_LOG(LogHellunaVote, Warning, TEXT("[VoteInput] SubmitVote 실패 - 진행 중인 투표 없음"));
		return;
	}

	// PlayerState 가져오기
	APlayerState* MyPS = GetLocalPlayerState();
	if (!MyPS)
	{
		UE_LOG(LogHellunaVote, Warning, TEXT("[VoteInput] SubmitVote 실패 - PlayerState를 찾을 수 없음"));
		return;
	}

	// Server RPC 호출
	VoteManager->Server_SubmitVote(MyPS, bAgree);

	UE_LOG(LogHellunaVote, Log, TEXT("[VoteInput] SubmitVote 완료 - %s가 %s 투표 제출"),
		*MyPS->GetPlayerName(),
		bAgree ? TEXT("찬성") : TEXT("반대"));
}

// ============================================================================
// 델리게이트 핸들러
// ============================================================================

void UVoteInputComponent::OnVoteStarted(const FVoteRequest& Request)
{
	UE_LOG(LogHellunaVote, Log, TEXT("[VoteInput] OnVoteStarted 수신 - %s"), *Request.GetVoteTypeName());
	EnableVoteInput();
}

void UVoteInputComponent::OnVoteEnded(EVoteType VoteType, bool bPassed, const FString& Reason)
{
	UE_LOG(LogHellunaVote, Log, TEXT("[VoteInput] OnVoteEnded 수신 - Type: %d, Passed: %s"),
		static_cast<int32>(VoteType),
		bPassed ? TEXT("true") : TEXT("false"));
	DisableVoteInput();
}

// ============================================================================
// 헬퍼 함수
// ============================================================================

UVoteManagerComponent* UVoteInputComponent::GetVoteManager() const
{
	// 캐시된 값이 유효하면 반환
	if (CachedVoteManager.IsValid())
	{
		return CachedVoteManager.Get();
	}

	// GameState에서 VoteManagerComponent 검색
	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogHellunaVote, Warning, TEXT("[VoteInput] GetVoteManager - World가 null"));
		return nullptr;
	}

	AGameStateBase* GameState = World->GetGameState();
	if (!GameState)
	{
		UE_LOG(LogHellunaVote, Warning, TEXT("[VoteInput] GetVoteManager - GameState가 null"));
		return nullptr;
	}

	UVoteManagerComponent* VoteManager = GameState->FindComponentByClass<UVoteManagerComponent>();
	if (VoteManager)
	{
		// 캐시 저장
		CachedVoteManager = VoteManager;
		UE_LOG(LogHellunaVote, Log, TEXT("[VoteInput] GetVoteManager - VoteManager 캐시됨"));
	}
	else
	{
		UE_LOG(LogHellunaVote, Warning, TEXT("[VoteInput] GetVoteManager - GameState에 VoteManagerComponent 없음"));
	}

	return VoteManager;
}

APlayerState* UVoteInputComponent::GetLocalPlayerState() const
{
	APlayerController* PC = Cast<APlayerController>(GetOwner());
	if (!PC)
	{
		UE_LOG(LogHellunaVote, Warning, TEXT("[VoteInput] GetLocalPlayerState - Owner가 PlayerController가 아님"));
		return nullptr;
	}

	APlayerState* PS = PC->GetPlayerState<APlayerState>();
	if (!PS)
	{
		UE_LOG(LogHellunaVote, Warning, TEXT("[VoteInput] GetLocalPlayerState - PlayerState가 null"));
	}

	return PS;
}

UEnhancedInputLocalPlayerSubsystem* UVoteInputComponent::GetEnhancedInputSubsystem() const
{
	APlayerController* PC = Cast<APlayerController>(GetOwner());
	if (!PC)
	{
		UE_LOG(LogHellunaVote, Warning, TEXT("[VoteInput] GetEnhancedInputSubsystem - Owner가 PlayerController가 아님"));
		return nullptr;
	}

	ULocalPlayer* LocalPlayer = PC->GetLocalPlayer();
	if (!LocalPlayer)
	{
		UE_LOG(LogHellunaVote, Warning, TEXT("[VoteInput] GetEnhancedInputSubsystem - LocalPlayer가 null"));
		return nullptr;
	}

	UEnhancedInputLocalPlayerSubsystem* Subsystem = LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
	if (!Subsystem)
	{
		UE_LOG(LogHellunaVote, Warning, TEXT("[VoteInput] GetEnhancedInputSubsystem - EnhancedInputSubsystem을 찾을 수 없음"));
	}

	return Subsystem;
}
